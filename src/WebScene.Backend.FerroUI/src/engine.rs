use crate::{
    ffi,
    render::{Frame, Renderer},
    resources::Resources,
    CacheValidators, FileHttpLoader, ResourceLoader, ResourceRequest, Scene,
};
use ferroui_base::rendering::IRenderLoop;
use std::{
    cell::Cell,
    ffi::{c_char, c_void},
    ops::Deref,
    path::{Path, PathBuf},
    sync::{
        atomic::{AtomicBool, AtomicU64, Ordering},
        Arc, Mutex, OnceLock, Weak,
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
            resource_loader: Arc::new(FileHttpLoader::pooled()),
            capture_console: false,
        }
    }
}
struct Callbacks {
    resources: Resources,
    publication: Arc<ScenePublication>,
}

struct ScenePublication {
    changed: AtomicBool,
    resize: Mutex<ResizePublication>,
}

#[derive(Default)]
struct ResizePublication {
    published_sequence: u64,
    pending_sequence: Option<u64>,
    render_loop: Option<Weak<dyn IRenderLoop>>,
}

impl ScenePublication {
    fn notify_resize(&self, update: impl FnOnce(&mut ResizePublication)) {
        let render_loop = {
            let mut resize = self.resize.lock().unwrap();
            update(&mut resize);
            if !resize
                .pending_sequence
                .is_some_and(|pending| pending <= resize.published_sequence)
            {
                return;
            }
            resize.pending_sequence = None;
            resize.render_loop.as_ref().and_then(Weak::upgrade)
        };
        if let Some(render_loop) = render_loop {
            // The matching scene may arrive in the OS's nested resize loop.
            // Wake FerroUI directly, without posting through the UI queue.
            render_loop.wakeup();
            render_loop.request_frame_out_of_turn();
        }
    }
}

type ObserveCompositorFrame = unsafe extern "C" fn(*mut ffi::webscene_engine, f64);

struct RuntimeApi {
    bindings: ffi::NativeApi,
    observe_compositor_frame: Option<ObserveCompositorFrame>,
}
impl Deref for RuntimeApi {
    type Target = ffi::NativeApi;

    fn deref(&self) -> &Self::Target {
        &self.bindings
    }
}

/// The native endpoint lives in this mailbox until the UI owner revokes it.
/// Ordinary publications set an atomic edge for the compositor's render clock.
/// A completed resize can also wake an out-of-turn paint.
pub(crate) struct SceneMailbox {
    endpoint: Mutex<Option<SceneEndpoint>>,
    publication: Arc<ScenePublication>,
    error: Mutex<Option<Error>>,
    resize_frame_deadline_us: AtomicU64,
}

struct SceneEndpoint {
    api: Arc<RuntimeApi>,
    handle: *mut ffi::webscene_engine,
    clock: Instant,
    consumer: u64,
}

// SAFETY: acquisition, acknowledgement and frame enqueue are native
// compositor-thread APIs. The mailbox gate excludes close from these calls.
unsafe impl Send for SceneEndpoint {}

impl SceneMailbox {
    pub fn attach(&self, render_loop: &Arc<dyn IRenderLoop>) -> Option<u64> {
        let mut guard = self.endpoint.lock().unwrap();
        let endpoint = guard.as_mut()?;
        endpoint.consumer += 1;
        self.publication.resize.lock().unwrap().render_loop = Some(Arc::downgrade(render_loop));
        unsafe {
            endpoint
                .api
                .webscene_engine_request_scene_checkpoint(endpoint.handle)
        };
        Some(endpoint.consumer)
    }

    pub fn detach(&self) {
        let mut resize = self.publication.resize.lock().unwrap();
        resize.render_loop = None;
        resize.pending_sequence = None;
    }

    pub fn update(
        &self,
        consumer: u64,
        renderer: &mut Renderer,
        drive_animation: bool,
    ) -> Result<Option<bool>, Error> {
        let guard = self.endpoint.lock().unwrap();
        let Some(endpoint) = guard
            .as_ref()
            .filter(|endpoint| endpoint.consumer == consumer)
        else {
            return Ok(None);
        };
        if drive_animation {
            let elapsed = endpoint.clock.elapsed();
            let timestamp = elapsed.as_secs_f64() * 1000.0;
            // SAFETY: the mailbox gate keeps the native endpoint alive.
            unsafe {
                if let Some(observe) = endpoint.api.observe_compositor_frame {
                    observe(endpoint.handle, timestamp);
                }
                if elapsed.as_micros()
                    >= self.resize_frame_deadline_us.load(Ordering::Acquire) as u128
                    && endpoint
                        .api
                        .webscene_engine_requires_animation_frame(endpoint.handle)
                        != 0
                {
                    // Animation frames carry a timestamp only. The UI keeps its
                    // own sequence for input and resize pairs.
                    let frame = ffi::webscene_input_event {
                        kind: 5,
                        x: timestamp,
                        ..Default::default()
                    };
                    // If the native queue is full, the next display opportunity
                    // can supply a timestamp without shutting down presentation.
                    endpoint
                        .api
                        .webscene_engine_enqueue(endpoint.handle, &frame);
                }
            }
        }
        let previous = renderer.frame.clone();
        consume_scenes(
            &endpoint.api,
            endpoint.handle,
            &self.publication.changed,
            renderer,
            None,
            2,
        )?;
        Ok(Some(!Arc::ptr_eq(&previous, &renderer.frame)))
    }

    pub fn report(&self, error: Error) {
        *self.error.lock().unwrap() = Some(error);
    }

    pub fn take_error(&self) -> Option<Error> {
        self.error.lock().unwrap().take()
    }
}

/// Owns the worker and all callback state. Use on one host thread; native callbacks never
/// reference FerroUI objects. Dropping the engine joins its worker before freeing callbacks.
pub struct Engine {
    api: Arc<RuntimeApi>,
    handle: *mut ffi::webscene_engine,
    callbacks: Box<Callbacks>,
    renderer: Renderer,
    sequence: u64,
    clock: Instant,
    mailbox: Arc<SceneMailbox>,
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
            publication: Arc::new(ScenePublication {
                changed: AtomicBool::new(true),
                resize: Mutex::new(ResizePublication::default()),
            }),
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
        let clock = Instant::now();
        let mailbox = Arc::new(SceneMailbox {
            publication: callbacks.publication.clone(),
            endpoint: Mutex::new(Some(SceneEndpoint {
                api: api.clone(),
                handle,
                clock,
                consumer: 0,
            })),
            error: Mutex::new(None),
            resize_frame_deadline_us: AtomicU64::new(0),
        });
        Ok(Self {
            api,
            handle,
            callbacks,
            renderer: Renderer::default(),
            sequence: 0,
            clock,
            mailbox,
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
        self.callbacks
            .publication
            .changed
            .store(true, Ordering::Release);
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
        // Each live resize already includes a frame. Give the paired input a
        // short window to publish before resuming standalone animation frames.
        self.mailbox.resize_frame_deadline_us.store(
            (self.clock.elapsed().as_micros() as u64).saturating_add(50_000),
            Ordering::Release,
        );
        self.check(unsafe {
            self.api
                .webscene_engine_enqueue_resize_frame(self.handle, &resize, &frame)
        })?;
        // Recheck the last publication after enqueue. A fast worker can finish
        // the resize before enqueue returns and its sequence becomes visible here.
        self.callbacks.publication.notify_resize(|publication| {
            publication.pending_sequence = Some(resize.sequence);
        });
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
        consume_scenes(
            &self.api,
            self.handle,
            &self.callbacks.publication.changed,
            &mut self.renderer,
            Some(&mut self.scene),
            16,
        )
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
    pub(crate) fn mailbox(&self) -> Arc<SceneMailbox> {
        self.mailbox.clone()
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
        copy_native(self.handle, f)
    }
    fn check(&self, result: u8) -> Result<(), Error> {
        check_native(&self.api, self.handle, result)
    }
}

fn consume_scenes(
    api: &ffi::NativeApi,
    handle: *mut ffi::webscene_engine,
    available: &AtomicBool,
    renderer: &mut Renderer,
    mut last_scene: Option<&mut Option<Scene>>,
    limit: usize,
) -> Result<bool, Error> {
    if !available.swap(false, Ordering::AcqRel) {
        return Ok(false);
    }
    let mut changed = false;
    for i in 0..limit {
        let ptr = unsafe { api.webscene_engine_acquire_next_scene(handle) };
        if ptr.is_null() {
            break;
        }
        struct Lease<'a>(&'a ffi::NativeApi, *const ffi::webscene_scene_view);
        impl Drop for Lease<'_> {
            fn drop(&mut self) {
                unsafe { self.0.webscene_scene_release(self.1) }
            }
        }
        let _lease = Lease(api, ptr);
        let scene = unsafe { Scene::copy(&*ptr) }?;
        if !renderer.accepts(&scene) {
            unsafe { api.webscene_engine_request_scene_checkpoint(handle) };
            break;
        }
        renderer.apply(&scene)?;
        check_native(api, handle, unsafe { api.webscene_scene_acknowledge(ptr) })?;
        if let Some(slot) = last_scene.as_deref_mut() {
            *slot = Some(scene);
        }
        changed = true;
        if i + 1 == limit {
            // A publication may race the last acknowledgement.
            available.store(true, Ordering::Release);
        }
    }
    Ok(changed)
}

fn check_native(
    api: &ffi::NativeApi,
    handle: *mut ffi::webscene_engine,
    result: u8,
) -> Result<(), Error> {
    if result != 0 {
        return Ok(());
    }
    let message = copy_native(handle, |h, p, n| unsafe {
        api.webscene_engine_copy_last_error(h, p, n)
    });
    Err(Error(if message.is_empty() {
        "Native operation failed".into()
    } else {
        message
    }))
}

fn copy_native(
    handle: *mut ffi::webscene_engine,
    f: impl Fn(*mut ffi::webscene_engine, *mut c_char, usize) -> usize,
) -> String {
    let length = f(handle, std::ptr::null_mut(), 0);
    if length == 0 {
        return String::new();
    }
    let mut bytes = vec![0u8; length];
    let written = f(handle, bytes.as_mut_ptr().cast(), length);
    bytes.truncate(written.min(length));
    if bytes.last() == Some(&0) {
        bytes.pop();
    }
    String::from_utf8_lossy(&bytes).into_owned()
}

impl Drop for Engine {
    fn drop(&mut self) {
        self.mailbox.endpoint.lock().unwrap().take();
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
        state.publication.changed.store(true, Ordering::Release);
    }
}
unsafe extern "C" fn published(user: *mut c_void, _: u64, sequence: u64, _: f32, _: f32) {
    if let Some(state) = (user as *const Callbacks).as_ref() {
        state.publication.changed.store(true, Ordering::Release);
        state.publication.notify_resize(|publication| {
            publication.published_sequence = publication.published_sequence.max(sequence);
        });
    }
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
    tag: *const c_char,
    tag_len: usize,
    modified: i64,
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
        let validators = CacheValidators {
            entity_tag: utf8(tag, tag_len).into(),
            last_modified_unix_seconds: modified,
        };
        state
            .resources
            .copy(request, validators, destination, capacity)
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
fn runtime_api(path: &Path) -> Result<Arc<RuntimeApi>, Error> {
    type LoadedRuntime = Option<(PathBuf, Arc<RuntimeApi>)>;
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
    let library = unsafe { libloading::Library::new(&path) }.map_err(|e| Error(e.to_string()))?;
    // This export arrived after 1.0.37. Older engines can still accept the
    // corresponding animation frame input.
    let observe_compositor_frame = unsafe {
        library
            .get::<ObserveCompositorFrame>(b"webscene_engine_observe_compositor_frame\0")
            .ok()
            .map(|symbol| *symbol)
    };
    let bindings =
        unsafe { ffi::NativeApi::from_library(library) }.map_err(|e| Error(e.to_string()))?;
    let api = Arc::new(RuntimeApi {
        bindings,
        observe_compositor_frame,
    });
    if unsafe { api.webscene_engine_get_abi_version() } != 3 {
        return Err(Error("WebScene ABI 3 is required".into()));
    }
    *slot = Some((path, api.clone()));
    Ok(api)
}

#[cfg(test)]
mod tests {
    use super::*;
    use ferroui_base::rendering::testing::ManualRenderLoop;

    #[test]
    fn wakes_once_for_each_completed_resize_in_either_order() {
        let render_loop = ManualRenderLoop::new();
        let render_loop_ref: Arc<dyn IRenderLoop> = render_loop.clone();
        let publication = ScenePublication {
            changed: AtomicBool::new(false),
            resize: Mutex::new(ResizePublication {
                render_loop: Some(Arc::downgrade(&render_loop_ref)),
                ..ResizePublication::default()
            }),
        };

        publication.notify_resize(|state| state.pending_sequence = Some(4));
        publication.notify_resize(|state| state.published_sequence = 3);
        assert_eq!(render_loop.frames_requested_out_of_turn(), 0);
        publication.notify_resize(|state| state.published_sequence = 4);
        publication.notify_resize(|state| state.published_sequence = 5);
        assert_eq!(render_loop.frames_requested_out_of_turn(), 1);

        publication.notify_resize(|state| state.published_sequence = 8);
        assert_eq!(render_loop.frames_requested_out_of_turn(), 1);
        publication.notify_resize(|state| state.pending_sequence = Some(8));
        assert_eq!(render_loop.frames_requested_out_of_turn(), 2);
    }
}
