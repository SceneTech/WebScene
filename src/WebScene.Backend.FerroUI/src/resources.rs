use std::{
    collections::HashMap,
    sync::{Arc, Mutex},
    thread::{self, ThreadId},
    time::{Duration, SystemTime, UNIX_EPOCH},
};

/// A request on the native worker. Implementations must not call the engine or UI.
#[derive(Clone, Debug, PartialEq, Eq)]
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

/// Metadata from a previously cached response.
#[derive(Default, PartialEq, Eq)]
pub struct CacheValidators {
    pub entity_tag: String,
    pub last_modified_unix_seconds: i64,
}

/// A response with optional cache metadata. A missing body represents HTTP 304.
#[derive(Default)]
pub struct CachedResource {
    pub content: Option<Vec<u8>>,
    pub validators: CacheValidators,
    pub fresh_until_unix_seconds: i64,
    pub cacheable: bool,
}

pub trait ResourceLoader: Send + Sync + 'static {
    fn load(&self, request: &ResourceRequest) -> Result<ResourceResponse, String>;

    fn load_cached(
        &self,
        request: &ResourceRequest,
        _validators: &CacheValidators,
    ) -> Result<CachedResource, String> {
        self.load(request).map(|response| CachedResource {
            content: Some(response.content),
            ..Default::default()
        })
    }
}

/// File and HTTP(S) loading for trusted application content. Requests have a 30s timeout.
/// Supply a custom loader to restrict origins or serve packaged assets.
pub struct FileHttpLoader;
impl ResourceLoader for FileHttpLoader {
    fn load(&self, request: &ResourceRequest) -> Result<ResourceResponse, String> {
        Self::content(self.load_cached(request, &CacheValidators::default())?)
    }
    fn load_cached(
        &self,
        request: &ResourceRequest,
        validators: &CacheValidators,
    ) -> Result<CachedResource, String> {
        Self::load_with(request, validators, Self::http_agent)
    }
}

struct PooledFileHttpLoader(ureq::Agent);

impl ResourceLoader for PooledFileHttpLoader {
    fn load(&self, request: &ResourceRequest) -> Result<ResourceResponse, String> {
        FileHttpLoader::content(self.load_cached(request, &CacheValidators::default())?)
    }
    fn load_cached(
        &self,
        request: &ResourceRequest,
        validators: &CacheValidators,
    ) -> Result<CachedResource, String> {
        FileHttpLoader::load_with(request, validators, || self.0.clone())
    }
}

impl FileHttpLoader {
    pub(crate) fn pooled() -> impl ResourceLoader {
        PooledFileHttpLoader(Self::http_agent())
    }

    fn http_agent() -> ureq::Agent {
        ureq::Agent::config_builder()
            .http_status_as_error(false)
            .timeout_global(Some(Duration::from_secs(30)))
            .build()
            .into()
    }

    fn load_with(
        request: &ResourceRequest,
        validators: &CacheValidators,
        http_agent: impl FnOnce() -> ureq::Agent,
    ) -> Result<CachedResource, String> {
        let url = url::Url::parse(&request.url).map_err(|e| e.to_string())?;
        let mut metadata = CachedResource::default();
        let mut content = match url.scheme() {
            "file" if request.method == "GET" => {
                std::fs::read(url.to_file_path().map_err(|_| "Invalid file URL")?)
                    .map_err(|e| e.to_string())?
            }
            "http" | "https" => {
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
                let asset = request.method == "GET" && request.kind <= 3;
                if asset && !validators.entity_tag.is_empty() {
                    builder = builder.header("If-None-Match", &validators.entity_tag);
                }
                if asset && (1..253_402_300_800).contains(&validators.last_modified_unix_seconds) {
                    let time = UNIX_EPOCH
                        + Duration::from_secs(validators.last_modified_unix_seconds as u64);
                    builder = builder.header("If-Modified-Since", httpdate::fmt_http_date(time));
                }
                let req = builder
                    .body(request.body.as_slice())
                    .map_err(|e| e.to_string())?;
                let mut response = http_agent().run(req).map_err(|e| e.to_string())?;
                if asset {
                    let cached = (response.status() == 304).then_some(validators);
                    metadata = http_cache_metadata(response.headers(), SystemTime::now(), cached);
                }
                if response.status() == 304 && asset {
                    return Ok(metadata);
                }
                if !response.status().is_success() {
                    return Err(format!("HTTP {}", response.status()));
                }
                response
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
        metadata.content = Some(content);
        Ok(metadata)
    }

    fn content(response: CachedResource) -> Result<ResourceResponse, String> {
        response
            .content
            .map(|content| ResourceResponse { content })
            .ok_or_else(|| "HTTP 304 without a conditional request".into())
    }
}

fn http_cache_metadata(
    headers: &ureq::http::HeaderMap,
    now: SystemTime,
    cached: Option<&CacheValidators>,
) -> CachedResource {
    let header = |name| headers.get(name).and_then(|value| value.to_str().ok());
    let timestamp = |name| header(name).and_then(|value| httpdate::parse_http_date(value).ok());
    let unix_seconds = |time: SystemTime| {
        time.duration_since(UNIX_EPOCH)
            .map_or(0, |value| value.as_secs().min(i64::MAX as u64) as i64)
    };
    let mut max_age = None;
    let mut revalidate = false;
    let mut cacheable = !headers.contains_key("set-cookie")
        && !headers
            .get_all("vary")
            .iter()
            .any(|value| value.as_bytes() == b"*");
    for directive in headers
        .get_all("cache-control")
        .iter()
        .filter_map(|value| value.to_str().ok())
        .flat_map(|value| value.split(','))
    {
        let (name, value) = directive
            .trim()
            .split_once('=')
            .unwrap_or((directive.trim(), ""));
        match name.to_ascii_lowercase().as_str() {
            "no-store" | "private" => cacheable = false,
            "no-cache" => revalidate = true,
            "max-age" => max_age = value.trim().trim_matches('"').parse::<u64>().ok(),
            _ => {}
        }
    }
    let date = timestamp("date").unwrap_or(now);
    let age = header("age")
        .and_then(|value| value.parse::<u64>().ok())
        .unwrap_or(0)
        .max(now.duration_since(date).map_or(0, |value| value.as_secs()));
    let fresh_until_unix_seconds = max_age
        .or_else(|| {
            timestamp("expires").map(|expires| {
                expires
                    .duration_since(date)
                    .map_or(0, |time| time.as_secs())
            })
        })
        .filter(|seconds| *seconds > age && !revalidate)
        .and_then(|seconds| now.checked_add(Duration::from_secs(seconds - age)))
        .map_or(0, unix_seconds);
    let validators = CacheValidators {
        entity_tag: header("etag")
            .unwrap_or_else(|| cached.map_or("", |response| &response.entity_tag))
            .to_owned(),
        last_modified_unix_seconds: timestamp("last-modified").map_or_else(
            || cached.map_or(0, |response| response.last_modified_unix_seconds),
            unix_seconds,
        ),
    };
    CachedResource {
        content: None,
        cacheable: cacheable
            && (fresh_until_unix_seconds != 0
                || !validators.entity_tag.is_empty()
                || validators.last_modified_unix_seconds != 0),
        validators,
        fresh_until_unix_seconds,
    }
}

pub(crate) struct Resources {
    pub loader: Arc<dyn ResourceLoader>,
    // Native makes the size probe and copy in the same call stack. Keep one
    // retry per caller so concurrent fetches can run without sharing responses.
    pending: Mutex<HashMap<ThreadId, (ResourceRequest, CacheValidators, Vec<u8>)>>,
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
        validators: CacheValidators,
        destination: *mut std::ffi::c_char,
        capacity: usize,
    ) -> usize {
        let caller = thread::current().id();
        let retained = self.pending.lock().unwrap().remove(&caller);
        let bytes = match retained {
            Some((previous, cached, bytes)) if previous == request && cached == validators => bytes,
            _ => {
                // Do not hold the retry map lock during file or network I/O.
                let mut bytes = vec![0; 22];
                match self.loader.load_cached(&request, &validators) {
                    Ok(response) => {
                        let tag = response.validators.entity_tag.as_bytes();
                        bytes[0] = if response.content.is_some() { 1 } else { 2 };
                        bytes[1] = u8::from(response.cacheable);
                        bytes[2..6].copy_from_slice(&(tag.len() as u32).to_le_bytes());
                        bytes[6..14].copy_from_slice(
                            &response.validators.last_modified_unix_seconds.to_le_bytes(),
                        );
                        bytes[14..22]
                            .copy_from_slice(&response.fresh_until_unix_seconds.to_le_bytes());
                        bytes.extend_from_slice(tag);
                        if let Some(content) = response.content {
                            bytes.extend(content);
                        }
                    }
                    Err(_) => {
                        bytes[0] = 3;
                        bytes[2..6].copy_from_slice(&6u32.to_le_bytes());
                        bytes.extend_from_slice(b"loader");
                    }
                }
                bytes
            }
        };
        let length = bytes.len();
        if !destination.is_null() && capacity >= length {
            // SAFETY: the native copy callback supplies a writable buffer of capacity bytes.
            unsafe {
                std::ptr::copy_nonoverlapping(bytes.as_ptr(), destination.cast(), length);
            }
        } else {
            self.pending
                .lock()
                .unwrap()
                .insert(caller, (request, validators, bytes));
        }
        length
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::{
        io::{BufRead, BufReader, Write},
        net::TcpListener,
        sync::atomic::{AtomicUsize, Ordering},
    };
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
    fn pooled_loader_reuses_connection_for_separate_resources() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap();
        let server = std::thread::spawn(move || {
            let (stream, _) = listener.accept().unwrap();
            drop(listener);
            stream
                .set_read_timeout(Some(Duration::from_secs(2)))
                .unwrap();
            let mut stream = BufReader::new(stream);
            for path in ["/first", "/second"] {
                let mut line = String::new();
                stream.read_line(&mut line).unwrap();
                assert_eq!(line, format!("GET {path} HTTP/1.1\r\n"));
                while line != "\r\n" {
                    line.clear();
                    assert_ne!(stream.read_line(&mut line).unwrap(), 0);
                }
                stream
                    .get_mut()
                    .write_all(b"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK")
                    .unwrap();
            }
        });
        let loader = crate::RuntimeOptions::new("").resource_loader;
        for path in ["/first", "/second"] {
            let request = ResourceRequest {
                url: format!("http://{address}{path}"),
                kind: 0,
                method: "GET".into(),
                body: vec![],
                content_type: String::new(),
            };
            assert_eq!(loader.load(&request).unwrap().content, b"OK");
        }
        server.join().unwrap();
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
        assert_eq!(
            resources.copy(
                request.clone(),
                CacheValidators::default(),
                std::ptr::null_mut(),
                0
            ),
            27
        );
        let mut bytes = [0u8; 27];
        assert_eq!(
            resources.copy(
                request.clone(),
                CacheValidators::default(),
                bytes.as_mut_ptr().cast(),
                10
            ),
            27
        );
        assert_eq!(
            resources.copy(
                request,
                CacheValidators::default(),
                bytes.as_mut_ptr().cast(),
                27
            ),
            27
        );
        assert_eq!(loader.0.load(Ordering::Relaxed), 1);
        assert_eq!(bytes[0], 1);
        assert_eq!(&bytes[22..], b"hello");
    }

    #[test]
    fn a_slow_resource_leaves_other_native_loaders_free() {
        use std::sync::mpsc;

        struct BlockingLoader {
            started: mpsc::Sender<()>,
            release: Mutex<mpsc::Receiver<()>>,
        }
        impl ResourceLoader for BlockingLoader {
            fn load(&self, request: &ResourceRequest) -> Result<ResourceResponse, String> {
                if request.url.ends_with("/slow") {
                    self.started.send(()).unwrap();
                    self.release.lock().unwrap().recv().unwrap();
                }
                Ok(ResourceResponse {
                    content: b"ok".to_vec(),
                })
            }
        }

        let (started, waiting) = mpsc::channel();
        let (release, blocked) = mpsc::channel();
        let (done, completed) = mpsc::channel();
        let resources = Resources::new(Arc::new(BlockingLoader {
            started,
            release: Mutex::new(blocked),
        }));
        let load = |path| {
            let request = ResourceRequest {
                url: format!("https://app.test/{path}"),
                kind: 0,
                method: "GET".into(),
                body: vec![],
                content_type: String::new(),
            };
            let mut bytes = [0u8; 24];
            assert_eq!(
                resources.copy(
                    request,
                    CacheValidators::default(),
                    bytes.as_mut_ptr().cast(),
                    bytes.len()
                ),
                bytes.len()
            );
            assert_eq!(&bytes[22..], b"ok");
        };

        thread::scope(|scope| {
            scope.spawn(|| load("slow"));
            waiting.recv().unwrap();
            scope.spawn(|| {
                load("fast");
                done.send(()).unwrap();
            });
            let finished_while_blocked = completed.recv_timeout(Duration::from_secs(1)).is_ok();
            release.send(()).unwrap();
            assert!(finished_while_blocked);
        });
    }

    #[test]
    fn http_asset_cache_revalidates_and_preserves_validators() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap();
        let server = thread::spawn(move || {
            for revalidation in [false, true] {
                let (stream, _) = listener.accept().unwrap();
                stream
                    .set_read_timeout(Some(Duration::from_secs(2)))
                    .unwrap();
                let mut stream = BufReader::new(stream);
                let mut request = String::new();
                loop {
                    let mut line = String::new();
                    assert_ne!(stream.read_line(&mut line).unwrap(), 0);
                    if line == "\r\n" {
                        break;
                    }
                    request.push_str(&line);
                }
                let request = request.to_ascii_lowercase();
                assert_eq!(request.contains("if-none-match: \"v1\""), revalidation);
                assert_eq!(request.contains("if-modified-since:"), revalidation);
                stream.get_mut().write_all(if revalidation {
                    b"HTTP/1.1 304 Not Modified\r\nConnection: close\r\nCache-Control: max-age=120\r\n\r\n"
                } else {
                    b"HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: no-cache\r\nETag: \"v1\"\r\nLast-Modified: Wed, 21 Oct 2015 07:28:00 GMT\r\nContent-Length: 2\r\n\r\njs"
                }).unwrap();
            }
        });
        let request = ResourceRequest {
            url: format!("http://{address}/app.js"),
            kind: 1,
            method: "GET".into(),
            body: vec![],
            content_type: String::new(),
        };
        let loader = FileHttpLoader::pooled();
        let first = loader
            .load_cached(&request, &CacheValidators::default())
            .unwrap();
        assert_eq!(first.content.as_deref(), Some(b"js".as_slice()));
        assert!(first.cacheable);
        assert_eq!(first.fresh_until_unix_seconds, 0);
        let second = loader.load_cached(&request, &first.validators).unwrap();
        assert!(second.content.is_none());
        assert_eq!(second.validators.entity_tag, first.validators.entity_tag);
        assert!(second.cacheable);
        assert!(second.fresh_until_unix_seconds > 0);
        server.join().unwrap();

        let mut headers = ureq::http::HeaderMap::new();
        headers.insert("cache-control", "no-store, max-age=120".parse().unwrap());
        assert!(!http_cache_metadata(&headers, SystemTime::now(), None).cacheable);
    }
}
