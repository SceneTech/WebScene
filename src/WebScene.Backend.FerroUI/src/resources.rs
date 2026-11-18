use std::{
    collections::HashMap,
    sync::{Arc, Mutex},
    time::Duration,
};

/// A request on the native worker. Implementations must not call the engine or UI.
#[derive(Clone, Debug)]
pub struct ResourceRequest {
    pub url: String,
    pub kind: u32,
    pub method: String,
    pub body: Vec<u8>,
    pub content_type: String,
}

pub struct ResourceResponse {
    pub content: Vec<u8>,
}

pub trait ResourceLoader: Send + Sync + 'static {
    fn load(&self, request: &ResourceRequest) -> Result<ResourceResponse, String>;
}

/// File and HTTP(S) loading for trusted application content. Requests have a 30s timeout.
/// Supply a custom loader to restrict origins or serve packaged assets.
pub struct FileHttpLoader;
impl ResourceLoader for FileHttpLoader {
    fn load(&self, request: &ResourceRequest) -> Result<ResourceResponse, String> {
        let url = url::Url::parse(&request.url).map_err(|e| e.to_string())?;
        let mut content = match url.scheme() {
            "file" if request.method == "GET" => {
                std::fs::read(url.to_file_path().map_err(|_| "Invalid file URL")?)
                    .map_err(|e| e.to_string())?
            }
            "http" | "https" => {
                let agent: ureq::Agent = ureq::Agent::config_builder()
                    .timeout_global(Some(Duration::from_secs(30)))
                    .build()
                    .into();
                let method = request
                    .method
                    .parse::<ureq::http::Method>()
                    .map_err(|e| e.to_string())?;
                let mut builder = ureq::http::Request::builder()
                    .method(method)
                    .uri(&request.url);
                if !request.content_type.is_empty() {
                    builder = builder.header("Content-Type", &request.content_type);
                }
                let req = builder
                    .body(request.body.as_slice())
                    .map_err(|e| e.to_string())?;
                agent
                    .run(req)
                    .map_err(|e| e.to_string())?
                    .body_mut()
                    .with_config()
                    .limit(64 * 1024 * 1024)
                    .read_to_vec()
                    .map_err(|e| e.to_string())?
            }
            _ => return Err("Unsupported resource scheme or method".into()),
        };
        if request.kind == 3 {
            if content.len() > 16 * 1024 * 1024 {
                return Err("Image exceeds 16 MiB".into());
            }
            if let Some(image) = skia_safe::Image::from_encoded(skia_safe::Data::new_copy(&content))
            {
                let (w, h) = (image.width(), image.height());
                if w <= 0 || h <= 0 || i64::from(w) * i64::from(h) > 16 * 1024 * 1024 {
                    return Err("Image exceeds 16 megapixels".into());
                }
                use base64::Engine;
                let mime = if content.starts_with(b"\x89PNG\r\n\x1a\n") {
                    "image/png"
                } else if content.starts_with(b"\xff\xd8") {
                    "image/jpeg"
                } else if content.starts_with(b"GIF8") {
                    "image/gif"
                } else if content.starts_with(b"RIFF") {
                    "image/webp"
                } else {
                    return Err("Unsupported encoded image format".into());
                };
                let encoded = base64::engine::general_purpose::STANDARD.encode(&content);
                content=format!("<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"{w}\" height=\"{h}\" viewBox=\"0 0 {w} {h}\"><image width=\"{w}\" height=\"{h}\" xlink:href=\"data:{mime};base64,{encoded}\"/></svg>").into_bytes();
            }
        }
        Ok(ResourceResponse { content })
    }
}

pub(crate) struct Resources {
    pub loader: Arc<dyn ResourceLoader>,
    // A response is fetched once, retained across size queries, then consumed by the copy.
    pending: Mutex<HashMap<String, Vec<u8>>>,
}
impl Resources {
    pub fn new(loader: Arc<dyn ResourceLoader>) -> Self {
        Self {
            loader,
            pending: Mutex::new(HashMap::new()),
        }
    }
    pub fn copy(
        &self,
        request: ResourceRequest,
        destination: *mut std::ffi::c_char,
        capacity: usize,
    ) -> usize {
        let key = format!(
            "{}\0{}\0{}\0{}\0{:?}",
            request.kind, request.method, request.url, request.content_type, request.body
        );
        let Ok(mut pending) = self.pending.lock() else {
            return 0;
        };
        let bytes = pending.entry(key.clone()).or_insert_with(|| {
            match self.loader.load(&request) {
                Ok(response) => {
                    let mut bytes = vec![0; 22];
                    bytes[0] = 1; // Content, no cache validators or freshness claim.
                    bytes.extend(response.content);
                    bytes
                }
                Err(_) => {
                    let mut bytes = vec![0; 22];
                    bytes[0] = 3;
                    bytes[2..6].copy_from_slice(&6u32.to_le_bytes());
                    bytes.extend_from_slice(b"loader");
                    bytes
                }
            }
        });
        let length = bytes.len();
        if !destination.is_null() && capacity >= length {
            // SAFETY: the native copy callback supplies a writable buffer of capacity bytes.
            unsafe {
                std::ptr::copy_nonoverlapping(bytes.as_ptr(), destination.cast(), length);
            }
            pending.remove(&key);
        }
        length
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicUsize, Ordering};
    struct Loader(AtomicUsize);
    impl ResourceLoader for Loader {
        fn load(&self, _: &ResourceRequest) -> Result<ResourceResponse, String> {
            self.0.fetch_add(1, Ordering::Relaxed);
            Ok(ResourceResponse {
                content: b"hello".to_vec(),
            })
        }
    }
    #[test]
    fn copy_protocol_fetches_once_and_preserves_response_for_short_buffers() {
        let loader = Arc::new(Loader(AtomicUsize::new(0)));
        let resources = Resources::new(loader.clone());
        let request = ResourceRequest {
            url: "https://app.test/".into(),
            kind: 0,
            method: "GET".into(),
            body: vec![],
            content_type: String::new(),
        };
        assert_eq!(resources.copy(request.clone(), std::ptr::null_mut(), 0), 27);
        let mut bytes = [0u8; 27];
        assert_eq!(
            resources.copy(request.clone(), bytes.as_mut_ptr().cast(), 10),
            27
        );
        assert_eq!(resources.copy(request, bytes.as_mut_ptr().cast(), 27), 27);
        assert_eq!(loader.0.load(Ordering::Relaxed), 1);
        assert_eq!(bytes[0], 1);
        assert_eq!(&bytes[22..], b"hello");
    }
}
