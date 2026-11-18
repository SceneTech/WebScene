use crate::{
    ffi,
    render::{Frame, Renderer},
    resources::Resources,
    FileHttpLoader, ResourceLoader, ResourceRequest, Scene,
};
use std::{
    cell::Cell,
    ffi::{c_char, c_void},
    path::{Path, PathBuf},
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc, Mutex, OnceLock,
    },
    time::Instant,
};

#[derive(Debug, Clone)]
pub struct Error(pub String);
impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        self.0.fmt(f)
    }
}
impl std::error::Error for Error {}

/// The native library and its optional V8 compilation cache. The library must implement ABI 3.
#[derive(Clone)]
pub struct RuntimeOptions {
    pub library_path: PathBuf,
    pub cache_directory: Option<PathBuf>,
    pub resource_loader: Arc<dyn ResourceLoader>,
    pub capture_console: bool,
}
impl RuntimeOptions {
    pub fn new(library_path: impl Into<PathBuf>) -> Self {
        Self {
            library_path: library_path.into(),
            cache_directory: None,
            resource_loader: Arc::new(FileHttpLoader),
            capture_console: false,
        }
    }
}
struct Callbacks {
    resources: Resources,
    changed: AtomicBool,
}

/// Owns the worker and all callback state. Use on one host thread; native callbacks never
/// reference FerroUI objects. Dropping the engine joins its worker before freeing callbacks.
pub struct Engine {
    api: Arc<ffi::NativeApi>,
    handle: *mut ffi::webscene_engine,
    callbacks: Box<Callbacks>,
    renderer: Renderer,
    sequence: u64,
    clock: Instant,
    scene: Option<Scene>,
    options: RuntimeOptions,
    loaded: bool,
    viewport: Option<(f64, f64, f64)>,
    visible: Cell<bool>,
    dark: Cell<bool>,
}
impl Engine {
    pub fn new(options: RuntimeOptions) -> Result<Self, Error> {
        // SAFETY: caller explicitly chooses a native runtime to load. All symbols and ABI
        // are validated before constructing the engine; NativeApi keeps its library alive.
        let api = runtime_api(&options.library_path)?;
        if unsafe { api.webscene_engine_get_abi_version() } != 3 {
            return Err(Error("WebScene ABI 3 is required".into()));
        }
        if unsafe { api.webscene_engine_prewarm() } == 0 {
            return Err(Error("Native runtime prewarm failed".into()));
        }
        let cache = options
            .cache_directory
            .as_ref()
            .map(|p| p.to_string_lossy().into_owned())
            .unwrap_or_default();
        if let Some(path) = &options.cache_directory {
            std::fs::create_dir_all(path).map_err(|e| Error(e.to_string()))?;
        }
        let mut callbacks = Box::new(Callbacks {
            resources: Resources::new(options.resource_loader.clone()),
            changed: AtomicBool::new(true),
        });
        let user = (&mut *callbacks as *mut Callbacks).cast();
        let native = ffi::webscene_engine_options {
            struct_size: size_of::<ffi::webscene_engine_options>() as u32,
            compilation_cache_directory: cache.as_ptr().cast(),
            compilation_cache_directory_length: cache.len(),
            resource_load_callback: Some(load),
            resource_load_user_data: user,
            resource_load_callback_v3: Some(load_v3),
            resource_load_v3_user_data: user,
            text_measure_callback: Some(measure),
            text_measure_user_data: user,
            scene_published_callback: Some(published),
            scene_published_user_data: user,
            animation_frame_requested_callback: Some(wake),
            animation_frame_requested_user_data: user,
            host_request_available_callback: Some(wake),
            host_request_available_user_data: user,
            ..Default::default()
        };
        let handle = unsafe { api.webscene_engine_create_with_options(&native) };
        if handle.is_null() {
            return Err(Error("Native engine creation failed".into()));
        }
        unsafe {
            api.webscene_engine_configure_diagnostics(
                handle,
                1 | 8 | if options.capture_console { 2 } else { 0 },
                Some(wake),
                user,
            );
        }
        Ok(Self {
            api,
            handle,
            callbacks,
            renderer: Renderer::default(),
            sequence: 0,
            clock: Instant::now(),
            scene: None,
            options,
            loaded: false,
            viewport: None,
            visible: Cell::new(true),
            dark: Cell::new(false),
        })
    }
    pub fn load_url(&mut self, url: &str) -> Result<(), Error> {
        url::Url::parse(url).map_err(|e| Error(e.to_string()))?;
        if self.loaded {
            // Match NativeWebSceneView navigation: each document gets a fresh native
            // engine/realm, so top-level lexical bindings and event handlers cannot leak.
            let mut next = Self::new(self.options.clone())?;
            if let Some((width, height, scale)) = self.viewport {
                next.resize(width, height, scale)?;
            }
            next.set_visible(self.visible.get())?;
            next.set_dark_mode(self.dark.get())?;
            next.load_url(url)?;
            *self = next;
            return Ok(());
        }

        let result = unsafe {
            self.api
                .webscene_engine_load_url(self.handle, url.as_ptr().cast(), url.len())
        };
        self.check(result)?;
        self.loaded = true;
        self.renderer = Renderer::default();
        self.scene = None;
        self.callbacks.changed.store(true, Ordering::Release);
        Ok(())
    }
    pub fn load_file(&mut self, path: impl AsRef<Path>) -> Result<(), Error> {
        let path = path
            .as_ref()
            .canonicalize()
            .map_err(|e| Error(e.to_string()))?;
        let url =
            url::Url::from_file_path(path).map_err(|_| Error("Invalid document path".into()))?;
        self.load_url(url.as_str())
    }
    pub fn execute_script(&self, script: &str) -> Result<(), Error> {
        self.check(unsafe {
            self.api.webscene_engine_execute_script(
                self.handle,
                script.as_ptr().cast(),
                script.len(),
                b"ferroui-host".as_ptr().cast(),
                12,
            )
        })
    }
    pub fn resize(&mut self, width: f64, height: f64, scale: f64) -> Result<(), Error> {
        if !width.is_finite()
            || !height.is_finite()
            || !scale.is_finite()
            || width <= 0.0
            || height <= 0.0
            || scale <= 0.0
        {
            return Err(Error(
                "Viewport and device scale must be finite and positive".into(),
            ));
        }
        let resize = self.event(6, 0, width, height, scale, 0.0);
        let frame = self.event(
            5,
            0,
            self.clock.elapsed().as_secs_f64() * 1000.0,
            0.0,
            0.0,
            0.0,
        );
        self.check(unsafe {
            self.api
                .webscene_engine_enqueue_resize_frame(self.handle, &resize, &frame)
        })?;
        self.viewport = Some((width, height, scale));
        Ok(())
    }
    pub fn pointer(
        &mut self,
        kind: u32,
        flags: u32,
        x: f64,
        y: f64,
        dx: f64,
        dy: f64,
    ) -> Result<(), Error> {
        if !matches!(kind, 1..=4 | 10) {
            return Err(Error("Invalid pointer event kind".into()));
        }
        self.enqueue(kind, flags, x, y, dx, dy)
    }
    pub fn key(&mut self, down: bool, key_code: u32, modifiers: u32) -> Result<(), Error> {
        self.enqueue(
            if down { 7 } else { 8 },
            modifiers,
            key_code as f64,
            0.0,
            0.0,
            0.0,
        )
    }
    pub fn text(&mut self, text: &str) -> Result<(), Error> {
        for ch in text.chars() {
            self.enqueue(9, 0, ch as u32 as f64, 0.0, 0.0, 0.0)?;
        }
        Ok(())
    }
    fn event(
        &mut self,
        kind: u32,
        flags: u32,
        x: f64,
        y: f64,
        dx: f64,
        dy: f64,
    ) -> ffi::webscene_input_event {
        self.sequence += 1;
        ffi::webscene_input_event {
            kind,
            flags,
            sequence: self.sequence,
            x,
            y,
            delta_x: dx,
            delta_y: dy,
        }
    }
    fn enqueue(
        &mut self,
        kind: u32,
        flags: u32,
        x: f64,
        y: f64,
        dx: f64,
        dy: f64,
    ) -> Result<(), Error> {
        let event = self.event(kind, flags, x, y, dx, dy);
        self.check(unsafe { self.api.webscene_engine_enqueue(self.handle, &event) })
    }
    /// Drives animation demand and applies queued immutable revisions in order. Returns
    /// true when there is a new frame. Call from the host's presentation cadence.
    pub fn update(&mut self) -> Result<bool, Error> {
        if unsafe {
            self.api
                .webscene_engine_requires_animation_frame(self.handle)
        } != 0
        {
            self.enqueue(
                5,
                0,
                self.clock.elapsed().as_secs_f64() * 1000.0,
                0.0,
                0.0,
                0.0,
            )?;
        }
        let failure = self.runtime_failure();
        if !failure.is_empty() {
            return Err(Error(failure));
        }
        if !self.callbacks.changed.swap(false, Ordering::AcqRel) {
            return Ok(false);
        }
        let mut changed = false;
        // Bound work per host tick; keep the edge set when more work may be queued.
        for i in 0..16 {
            let ptr = unsafe { self.api.webscene_engine_acquire_next_scene(self.handle) };
            if ptr.is_null() {
                break;
            }
            struct Lease<'a>(&'a ffi::NativeApi, *const ffi::webscene_scene_view);
            impl Drop for Lease<'_> {
                fn drop(&mut self) {
                    unsafe { self.0.webscene_scene_release(self.1) }
                }
            }
            let lease = Lease(&self.api, ptr);
            let scene = unsafe { Scene::copy(&*ptr) }?;
            if !self.renderer.accepts(&scene) {
                unsafe {
                    self.api
                        .webscene_engine_request_scene_checkpoint(self.handle);
                }
                break;
            }
            self.renderer.apply(&scene)?;
            self.check(unsafe { self.api.webscene_scene_acknowledge(ptr) })?;
            self.scene = Some(scene);
            changed = true;
            drop(lease);
            if i == 15 {
                self.callbacks.changed.store(true, Ordering::Release);
            }
        }
        Ok(changed)
    }
    pub fn scene(&self) -> Option<&Scene> {
        self.scene.as_ref()
    }
    pub fn draw(&self, canvas: &skia_safe::Canvas) {
        self.renderer.frame.draw(canvas);
    }
    pub(crate) fn frame(&self) -> Arc<Frame> {
        self.renderer.frame.clone()
    }
    pub fn cursor(&self) -> u32 {
        unsafe { self.api.webscene_engine_get_cursor(self.handle) }
    }
    pub fn set_visible(&self, visible: bool) -> Result<(), Error> {
        self.check(unsafe {
            self.api
                .webscene_engine_set_visible(self.handle, visible as u8)
        })?;
        self.visible.set(visible);
        Ok(())
    }
    pub fn set_dark_mode(&self, dark: bool) -> Result<(), Error> {
        self.check(unsafe {
            self.api
                .webscene_engine_set_preferred_color_scheme(self.handle, dark as u32)
        })?;
        self.dark.set(dark);
        Ok(())
    }
    pub fn request_low_memory(&self) -> Result<(), Error> {
        self.check(unsafe { self.api.webscene_engine_request_low_memory(self.handle) })
    }
    pub fn take_diagnostic(&self) -> Option<String> {
        let s = self.copy(|h, p, n| unsafe { (self.api.webscene_engine_take_diagnostic)(h, p, n) });
        (!s.is_empty()).then_some(s)
    }
    pub fn take_host_request(&self) -> Option<String> {
        let s =
            self.copy(|h, p, n| unsafe { (self.api.webscene_engine_take_host_request)(h, p, n) });
        (!s.is_empty()).then_some(s)
    }
    pub fn runtime_failure(&self) -> String {
        self.copy(|h, p, n| unsafe { (self.api.webscene_engine_copy_runtime_failure)(h, p, n) })
    }
    fn copy(&self, f: impl Fn(*mut ffi::webscene_engine, *mut c_char, usize) -> usize) -> String {
        let length = f(self.handle, std::ptr::null_mut(), 0);
        if length == 0 {
            return String::new();
        }
        let mut bytes = vec![0u8; length];
        let written = f(self.handle, bytes.as_mut_ptr().cast(), length);
        bytes.truncate(written.min(length));
        if bytes.last() == Some(&0) {
            bytes.pop();
        }
        String::from_utf8_lossy(&bytes).into_owned()
    }
    fn check(&self, result: u8) -> Result<(), Error> {
        if result != 0 {
            Ok(())
        } else {
            let message =
                self.copy(|h, p, n| unsafe { (self.api.webscene_engine_copy_last_error)(h, p, n) });
            Err(Error(if message.is_empty() {
                "Native operation failed".into()
            } else {
                message
            }))
        }
    }
}
impl Drop for Engine {
    fn drop(&mut self) {
        unsafe {
            self.api.webscene_engine_destroy(self.handle);
        }
    }
}

unsafe fn utf8<'a>(ptr: *const c_char, len: usize) -> &'a str {
    if len == 0 || ptr.is_null() {
        return "";
    }
    std::str::from_utf8(std::slice::from_raw_parts(ptr.cast(), len)).unwrap_or("")
}
unsafe extern "C" fn wake(user: *mut c_void) {
    if let Some(state) = (user as *const Callbacks).as_ref() {
        state.changed.store(true, Ordering::Release);
    }
}
unsafe extern "C" fn published(user: *mut c_void, _: u64, _: u64, _: f32, _: f32) {
    wake(user);
}
unsafe extern "C" fn load(
    user: *mut c_void,
    kind: u32,
    url: *const c_char,
    len: usize,
    tag: *const c_char,
    tag_len: usize,
    modified: i64,
    destination: *mut c_char,
    capacity: usize,
) -> usize {
    load_v3(
        user,
        kind,
        url,
        len,
        tag,
        tag_len,
        modified,
        std::ptr::null(),
        destination,
        capacity,
    )
}
unsafe extern "C" fn load_v3(
    user: *mut c_void,
    kind: u32,
    url: *const c_char,
    len: usize,
    _: *const c_char,
    _: usize,
    _: i64,
    context: *const ffi::webscene_resource_request_context_v3,
    destination: *mut c_char,
    capacity: usize,
) -> usize {
    // No Rust unwind may cross a native callback boundary, including a user loader panic.
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let state = &*(user as *const Callbacks);
        let mut request = ResourceRequest {
            url: utf8(url, len).into(),
            kind,
            method: "GET".into(),
            body: vec![],
            content_type: String::new(),
        };
        if let Some(context) = context.as_ref() {
            if context.struct_size as usize
                >= size_of::<ffi::webscene_resource_request_context_v3>()
            {
                let method = utf8(context.method, context.method_length);
                if !method.is_empty() {
                    request.method = method.into();
                }
                if context.body_length != 0 && !context.body.is_null() {
                    request.body =
                        std::slice::from_raw_parts(context.body.cast(), context.body_length)
                            .to_vec();
                }
                request.content_type =
                    utf8(context.content_type, context.content_type_length).into();
            }
        }
        state.resources.copy(request, destination, capacity)
    }))
    .unwrap_or(0)
}
unsafe extern "C" fn measure(
    _: *mut c_void,
    text: *const c_char,
    len: usize,
    family: *const c_char,
    family_len: usize,
    size: f32,
    weight: i32,
    letter: f32,
    word: f32,
    metrics: *mut ffi::webscene_text_metrics,
) -> u8 {
    std::panic::catch_unwind(|| {
        let Some(metrics) = metrics.as_mut() else {
            return 0;
        };
        if (metrics.struct_size as usize) < size_of::<ffi::webscene_text_metrics>() {
            return 0;
        }
        let p = crate::text::paragraph(
            utf8(text, len),
            utf8(family, family_len),
            size,
            weight,
            letter,
            word,
            skia_safe::Color::BLACK,
        );
        metrics.advance_width = p.max_intrinsic_width();
        metrics.ascent = p.alphabetic_baseline();
        metrics.descent = (p.height() - metrics.ascent).max(0.0);
        metrics.leading = 0.0;
        metrics.actual_bounding_box_left = 0.0;
        metrics.actual_bounding_box_right = metrics.advance_width;
        metrics.actual_bounding_box_ascent = metrics.ascent;
        metrics.actual_bounding_box_descent = metrics.descent;
        1
    })
    .unwrap_or(0)
}

// V8 has process-wide state and background workers. Keep its module loaded until process
// exit, even after the last view closes. Mixing two V8 runtime builds in one process is unsafe.
fn runtime_api(path: &Path) -> Result<Arc<ffi::NativeApi>, Error> {
    type LoadedRuntime = Option<(PathBuf, Arc<ffi::NativeApi>)>;
    static API: OnceLock<Mutex<LoadedRuntime>> = OnceLock::new();
    let path = path.canonicalize().map_err(|e| Error(e.to_string()))?;
    let mut slot = API
        .get_or_init(|| Mutex::new(None))
        .lock()
        .map_err(|_| Error("Runtime loader lock poisoned".into()))?;
    if let Some((loaded, api)) = &*slot {
        if loaded != &path {
            return Err(Error(
                "Only one WebScene native runtime may be loaded per process".into(),
            ));
        }
        return Ok(api.clone());
    }
    let api = Arc::new(unsafe { ffi::NativeApi::new(&path) }.map_err(|e| Error(e.to_string()))?);
    if unsafe { api.webscene_engine_get_abi_version() } != 3 {
        return Err(Error("WebScene ABI 3 is required".into()));
    }
    *slot = Some((path, api.clone()));
    Ok(api)
}
