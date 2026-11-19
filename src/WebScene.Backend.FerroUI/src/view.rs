use crate::{
    composition::{draw_frame, SceneVisual, SceneVisualMessage},
    input,
    render::Frame,
    Engine, Error, RuntimeOptions,
};
use ferroui_base::input::*;
use ferroui_base::interactivity::InteractiveImpl;
use ferroui_base::layout::LayoutableImpl;
use ferroui_base::media::{DrawingContext, ImmediateDrawingContext};
use ferroui_base::rendering::composition::{
    CompositionCustomVisual, ElementComposition, ICompositionCustomVisualHandler,
};
use ferroui_base::rendering::scene_graph::ICustomDrawOperation;
use ferroui_base::threading::{DispatcherPriority, DispatcherTimer};
use ferroui_base::{
    ferro_class, ferro_class_info, ferro_impl_classes, instantiate, FerroObjectImpl, Point, Rect,
    Ref, StyledElementImpl, Vector, VisualImpl, VisualImplExt, VisualTreeAttachmentEventArgs,
};
use ferroui_controls::{Control, ControlImpl, ControlImplExt, SizeChangedEventArgs, TopLevel};
use std::{
    cell::{Cell, RefCell},
    rc::Rc,
    sync::Arc,
    time::Duration,
};

type MessageHandler = Rc<dyn Fn(&str)>;
type ErrorHandler = Rc<dyn Fn(&Error)>;

/// Focusable FerroUI control presenting a WebScene native document through a Skia lease.
///
/// `open` creates the runtime; `load_url` navigates; `close` joins the worker. Detaching
/// suspends frame driving, and reattaching resumes the retained document. UI callbacks
/// run after native calls and after the engine borrow has been released.
#[repr(C)]
pub struct WebSceneView {
    base: Control,
    engine: RefCell<Option<Engine>>,
    composition: RefCell<Option<CompositionCustomVisual>>,
    timer: RefCell<Option<Rc<DispatcherTimer>>>,
    viewport: Cell<(f64, f64, f64)>,
    visible: Cell<bool>,
    failed: Cell<bool>,
    cursor_kind: Cell<u32>,
    buttons: Cell<u32>,
    pointer_position: Cell<Point>,
    last_error: RefCell<Option<Error>>,
    error: RefCell<Option<ErrorHandler>>,
    diagnostic: RefCell<Option<MessageHandler>>,
    host_request: RefCell<Option<MessageHandler>>,
}
ferro_class!(WebSceneView:Control);
ferro_class_info!(WebSceneView {
    new: WebSceneView::new
});
ferro_impl_classes!(WebSceneView:FerroObjectImpl,StyledElementImpl,LayoutableImpl,InteractiveImpl);
impl WebSceneView {
    pub fn construct() -> Self {
        Self {
            base: Control::construct(),
            engine: RefCell::new(None),
            composition: RefCell::new(None),
            timer: RefCell::new(None),
            viewport: Cell::new((0.0, 0.0, 0.0)),
            visible: Cell::new(false),
            failed: Cell::new(false),
            cursor_kind: Cell::new(u32::MAX),
            buttons: Cell::new(0),
            pointer_position: Cell::new(Point::default()),
            last_error: RefCell::new(None),
            error: RefCell::new(None),
            diagnostic: RefCell::new(None),
            host_request: RefCell::new(None),
        }
    }
    pub fn new() -> Ref<Self> {
        let this = instantiate(Self::construct());
        this.set_focusable(true);
        this.set_clip_to_bounds(true);
        this
    }
    pub fn open(&self, options: RuntimeOptions) -> Result<(), Error> {
        let engine = Engine::new(options)?;
        self.close();
        *self.engine.borrow_mut() = Some(engine);
        self.failed.set(false);
        self.last_error.borrow_mut().take();
        self.viewport.set((0.0, 0.0, 0.0));
        if self.is_attached_to_visual_tree() {
            self.start();
        }
        Ok(())
    }
    pub fn close(&self) {
        if let Some(timer) = self.timer.borrow_mut().take() {
            timer.stop();
        }
        self.detach_composition();
        self.engine.borrow_mut().take();
        self.invalidate_visual();
    }
    pub fn load_url(&self, url: &str) -> Result<(), Error> {
        self.with_engine(|e| e.load_url(url))?;
        // Navigation may replace the native runtime and revoke its mailbox.
        self.detach_composition();
        self.attach_composition();
        self.failed.set(false);
        self.last_error.borrow_mut().take();
        self.invalidate_visual();
        Ok(())
    }
    pub fn execute_script(&self, script: &str) -> Result<(), Error> {
        self.with_engine(|e| e.execute_script(script))
    }
    pub fn set_dark_mode(&self, dark: bool) -> Result<(), Error> {
        self.with_engine(|e| e.set_dark_mode(dark))
    }
    pub fn last_error(&self) -> Option<Error> {
        self.last_error.borrow().clone()
    }
    pub fn on_error(&self, handler: impl Fn(&Error) + 'static) {
        *self.error.borrow_mut() = Some(Rc::new(handler));
    }
    pub fn on_diagnostic(&self, handler: impl Fn(&str) + 'static) {
        *self.diagnostic.borrow_mut() = Some(Rc::new(handler));
    }
    pub fn on_host_request(&self, handler: impl Fn(&str) + 'static) {
        *self.host_request.borrow_mut() = Some(Rc::new(handler));
    }
    fn with_engine<T>(&self, f: impl FnOnce(&mut Engine) -> Result<T, Error>) -> Result<T, Error> {
        let mut slot = self.engine.borrow_mut();
        let engine = slot
            .as_mut()
            .ok_or_else(|| Error("Open the WebScene runtime first".into()))?;
        f(engine)
    }
    fn report(&self, error: Error) {
        self.failed.set(true);
        if let Some(visual) = self.composition.borrow().as_ref() {
            visual.send_handler_message(Arc::new(SceneVisualMessage::SetRunning(false)));
        }
        *self.last_error.borrow_mut() = Some(error.clone());
        let handler = self.error.borrow().clone();
        if let Some(handler) = handler {
            handler(&error);
        }
    }
    fn start(&self) {
        if self.timer.borrow().is_some() || self.engine.borrow().is_none() {
            return;
        }
        self.attach_composition();
        let weak = self.to_ref().downgrade();
        let timer = DispatcherTimer::with_callback(
            Duration::from_millis(16),
            DispatcherPriority::RENDER,
            move |_| {
                if let Some(view) = weak.upgrade() {
                    view.tick();
                }
            },
        );
        *self.timer.borrow_mut() = Some(timer);
        self.tick();
    }
    fn tick(&self) {
        if self.failed.get() {
            return;
        }
        self.attach_composition();
        let result = self.with_engine(|engine| {
            let visible = self.is_effectively_visible();
            if visible != self.visible.replace(visible) {
                engine.set_visible(visible)?;
                if let Some(visual) = self.composition.borrow().as_ref() {
                    visual.send_handler_message(Arc::new(SceneVisualMessage::SetRunning(visible)));
                }
            }
            let bounds = self.bounds();
            self.update_viewport(engine, bounds.width, bounds.height)?;
            let changed = if self.composition.borrow().is_some() {
                if let Some(error) = engine.mailbox().take_error() {
                    return Err(error);
                }
                let failure = engine.runtime_failure();
                if !failure.is_empty() {
                    return Err(Error(failure));
                }
                false
            } else if visible {
                engine.update()?
            } else {
                false
            };
            let cursor = engine.cursor();
            let mut diagnostics = vec![];
            let mut requests = vec![];
            // Bounded draining prevents a console-heavy document monopolizing the UI.
            for _ in 0..64 {
                if let Some(s) = engine.take_diagnostic() {
                    diagnostics.push(s);
                } else {
                    break;
                }
            }
            for _ in 0..64 {
                if let Some(s) = engine.take_host_request() {
                    requests.push(s);
                } else {
                    break;
                }
            }
            Ok((changed, cursor, diagnostics, requests))
        });
        match result {
            Err(error) => self.report(error),
            Ok((changed, cursor, diagnostics, requests)) => {
                let kind = match cursor {
                    1 => StandardCursorType::Hand,
                    2 => StandardCursorType::Ibeam,
                    3 => StandardCursorType::Cross,
                    4 => StandardCursorType::Wait,
                    5 => StandardCursorType::SizeAll,
                    6 => StandardCursorType::No,
                    7 => StandardCursorType::Help,
                    8 => StandardCursorType::SizeWestEast,
                    9 => StandardCursorType::SizeNorthSouth,
                    _ => StandardCursorType::Arrow,
                };
                if self.cursor_kind.replace(cursor) != cursor {
                    self.set_cursor(Some(Cursor::new(kind)));
                }
                if changed {
                    self.invalidate_visual();
                }
                let diagnostic = self.diagnostic.borrow().clone();
                if let Some(handler) = diagnostic {
                    for s in diagnostics {
                        handler(&s);
                    }
                }
                let request = self.host_request.borrow().clone();
                if let Some(handler) = request {
                    for s in requests {
                        handler(&s);
                    }
                }
            }
        }
    }

    fn update_viewport(&self, engine: &mut Engine, width: f64, height: f64) -> Result<(), Error> {
        let scale = TopLevel::get_top_level(Some(self))
            .map(|top_level| top_level.render_scaling())
            .unwrap_or(1.0);
        let viewport = (width, height, scale);
        if viewport != self.viewport.get() && width > 0.0 && height > 0.0 {
            engine.resize(width, height, scale)?;
            self.viewport.set(viewport);
            if let Some(visual) = self.composition.borrow().as_ref() {
                visual.set_size(Vector::new(width, height));
                visual.send_handler_message(Arc::new(SceneVisualMessage::Resize));
            }
        }
        Ok(())
    }

    fn attach_composition(&self) {
        if self.composition.borrow().is_some() {
            return;
        }
        let Some(parent) = ElementComposition::get_element_visual(self) else {
            return;
        };
        let Some(mailbox) = self.engine.borrow().as_ref().map(Engine::mailbox) else {
            return;
        };
        let compositor = parent.compositor();
        let Some(consumer) = mailbox.attach(compositor.render_loop()) else {
            return;
        };
        let visual = compositor.create_custom_visual(move || {
            Rc::new(SceneVisual::new(mailbox, consumer)) as Rc<dyn ICompositionCustomVisualHandler>
        });
        let bounds = self.bounds();
        visual.set_size(Vector::new(bounds.width, bounds.height));
        visual.set_clip_to_bounds(true);
        visual.set_is_hit_test_visible(false);
        ElementComposition::set_element_child_visual(self, Some((*visual).clone()));
        visual.send_handler_message(Arc::new(SceneVisualMessage::SetRunning(
            self.is_effectively_visible(),
        )));
        *self.composition.borrow_mut() = Some(visual);
        self.invalidate_visual();
    }

    fn detach_composition(&self) {
        if let Some(visual) = self.composition.borrow_mut().take() {
            if let Some(engine) = self.engine.borrow().as_ref() {
                engine.mailbox().detach();
            }
            visual.send_handler_message(Arc::new(SceneVisualMessage::SetRunning(false)));
            ElementComposition::set_element_child_visual(self, None);
            visual.dispose();
        }
    }

    fn pointer(&self, e: &PointerEventArgs, kind: u32, changed: MouseButton, dx: f64, dy: f64) {
        if self.engine.borrow().is_none() {
            return;
        }
        let point = e.get_current_point(Some(self));
        let flags = input::pointer_flags(point.properties, e.key_modifiers(), changed);
        self.buttons.set(flags & 31);
        self.pointer_position.set(point.position);
        let result = self.with_engine(|engine| {
            engine.pointer(kind, flags, point.position.x, point.position.y, dx, dy)
        });
        if let Err(error) = result {
            self.report(error);
        } else {
            e.set_handled(true);
        }
    }
    fn key(&self, e: &KeyEventArgs, down: bool) {
        let code = input::key(e.key);
        if code == 0 {
            return;
        }
        let result =
            self.with_engine(|engine| engine.key(down, code, input::modifiers(e.key_modifiers)));
        if let Err(error) = result {
            self.report(error);
        } else {
            e.set_handled(true);
        }
    }
}
impl ControlImpl for WebSceneView {
    fn on_size_changed(this: &Self, e: &SizeChangedEventArgs) {
        Self::parent_on_size_changed(this, e);
        if !this.is_attached_to_visual_tree() || this.failed.get() {
            return;
        }
        let size = e.new_size();
        let result = {
            let mut engine = this.engine.borrow_mut();
            engine
                .as_mut()
                .map(|engine| this.update_viewport(engine, size.width, size.height))
        };
        if let Some(Err(error)) = result {
            this.report(error);
        }
    }
}
impl VisualImpl for WebSceneView {
    fn on_attached_to_visual_tree(this: &Self, e: &VisualTreeAttachmentEventArgs) {
        Self::parent_on_attached_to_visual_tree(this, e);
        this.start();
    }
    fn on_detached_from_visual_tree(this: &Self, e: &VisualTreeAttachmentEventArgs) {
        if let Some(timer) = this.timer.borrow_mut().take() {
            timer.stop();
        }
        this.detach_composition();
        if let Some(engine) = this.engine.borrow().as_ref() {
            let _ = engine.set_visible(false);
        }
        this.visible.set(false);
        Self::parent_on_detached_from_visual_tree(this, e);
    }
    fn custom_hit_test(this: &Self, point: Point) -> Option<bool> {
        // The composition child draws the document. Hit testing still routes to
        // the owning control so its input handlers and pointer capture work.
        let bounds = this.bounds();
        Some(
            this.engine.borrow().is_some()
                && Rect::new(0.0, 0.0, bounds.width, bounds.height).contains(point),
        )
    }
    fn render(this: &Self, context: &mut DrawingContext) {
        if this.composition.borrow().is_some() {
            return;
        }
        let slot = this.engine.borrow();
        let Some(engine) = slot.as_ref() else {
            return;
        };
        let b = this.bounds();
        let op: Arc<dyn ICustomDrawOperation> = Arc::new(Draw {
            bounds: Rect::new(0.0, 0.0, b.width, b.height),
            frame: engine.frame(),
        });
        context.custom(&op);
    }
}
impl InputElementImpl for WebSceneView {
    fn on_pointer_moved(this: &Self, e: &PointerEventArgs) {
        this.pointer(e, 1, MouseButton::None, 0.0, 0.0);
    }
    fn on_pointer_exited(this: &Self, e: &PointerEventArgs) {
        this.pointer(e, 10, MouseButton::None, 0.0, 0.0);
    }
    fn on_pointer_pressed(this: &Self, e: &PointerPressedEventArgs) {
        this.focus();
        let properties = e.get_current_point(Some(this)).properties;
        let changed = properties.pointer_update_kind.get_mouse_button();
        e.pointer().capture(Some(&this.to_ref().upcast()));
        this.pointer(e, 2, changed, 0.0, 0.0);
    }
    fn on_pointer_released(this: &Self, e: &PointerReleasedEventArgs) {
        this.pointer(e, 3, e.initial_press_mouse_button(), 0.0, 0.0);
        e.pointer().capture(None);
    }
    fn on_pointer_capture_lost(this: &Self, _e: &PointerCaptureLostEventArgs) {
        let buttons = this.buttons.replace(0);
        let position = this.pointer_position.get();
        let result = this.with_engine(|engine| {
            for (mask, button) in [(1, 0), (2, 2), (4, 1), (8, 3), (16, 4)] {
                if buttons & mask != 0 {
                    engine.pointer(3, button << 8, position.x, position.y, 0.0, 0.0)?;
                }
            }
            Ok(())
        });
        if let Err(error) = result {
            this.report(error);
        }
    }
    fn on_pointer_wheel_changed(this: &Self, e: &PointerWheelEventArgs) {
        this.pointer(
            e,
            4,
            MouseButton::None,
            -e.delta().x * 50.0,
            -e.delta().y * 50.0,
        );
    }
    fn on_key_down(this: &Self, e: &KeyEventArgs) {
        this.key(e, true);
    }
    fn on_key_up(this: &Self, e: &KeyEventArgs) {
        this.key(e, false);
    }
    fn on_text_input(this: &Self, e: &TextInputEventArgs) {
        if let Some(text) = &e.text {
            let result = this.with_engine(|engine| engine.text(text));
            if let Err(error) = result {
                this.report(error);
            } else {
                e.set_handled(true);
            }
        }
    }
}
struct Draw {
    bounds: Rect,
    frame: Arc<Frame>,
}
impl ICustomDrawOperation for Draw {
    fn dispose(&self) {}
    fn bounds(&self) -> Rect {
        self.bounds
    }
    fn hit_test(&self, p: Point) -> bool {
        self.bounds.contains(p)
    }
    fn equals(&self, _: &dyn ICustomDrawOperation) -> bool {
        false
    }
    fn render(&self, context: &mut ImmediateDrawingContext<'_>) {
        draw_frame(context, self.bounds, &self.frame, None);
    }
}

impl Drop for WebSceneView {
    fn drop(&mut self) {
        if let Some(timer) = self.timer.get_mut().take() {
            timer.stop();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use ferroui_base::threading::Dispatcher;
    use ferroui_base::{BoxedValue, FerroLocator};
    use ferroui_controls::{AppBuilder, Application, Window};
    use ferroui_headless::{
        FerroHeadlessPlatformExtensions, FerroHeadlessPlatformOptions, HeadlessWindowExtensions,
    };
    use ferroui_skia::SkiaApplicationExtensions;

    #[test]
    #[ignore = "requires WEBSCENE_NATIVE_ENGINE_LIBRARY; exercises the real FerroUI control"]
    fn control_renders_routes_input_and_releases_timer_on_detach() {
        let _dispatcher = Dispatcher::unit_test_scope();
        let locator = FerroLocator::enter_scope();
        AppBuilder::reset_setup_for_unit_tests();
        let builder = AppBuilder::configure::<Application>()
            .use_skia()
            .use_headless(FerroHeadlessPlatformOptions {
                use_headless_drawing: false,
                ..Default::default()
            })
            .setup_without_starting();
        builder
            .instance()
            .unwrap()
            .styles()
            .add(ferroui_themes_simple::SimpleTheme::new().as_style());
        let view = WebSceneView::new();
        let mut options =
            RuntimeOptions::new(std::env::var("WEBSCENE_NATIVE_ENGINE_LIBRARY").unwrap());
        options.capture_console = true;
        view.open(options).unwrap();
        let url = url::Url::from_file_path(
            std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("examples/showcase.html"),
        )
        .unwrap();
        view.load_url(url.as_str()).unwrap();
        let messages = Rc::new(RefCell::new(Vec::new()));
        let capture = messages.clone();
        view.on_diagnostic(move |s| capture.borrow_mut().push(s.to_owned()));
        let window = Window::new();
        window.set_width(960.0);
        window.set_height(680.0);
        window.set_content(Some(Rc::new(view.clone().upcast::<Control>()) as BoxedValue));
        window.show();
        assert!(view.composition.borrow().is_some());
        let deadline = std::time::Instant::now() + Duration::from_secs(15);
        let png = loop {
            let frame = window.capture_rendered_frame();
            view.tick();
            assert!(view.last_error().is_none(), "{:?}", view.last_error());
            if let Some(frame) = frame {
                let mut png = Vec::new();
                frame
                    .save(
                        &mut png,
                        &ferroui_base::media::imaging::BitmapEncoderOptions::Png(Default::default()),
                    )
                    .unwrap();
                if png.len() > 5000 {
                    break png;
                }
            }
            assert!(
                std::time::Instant::now() < deadline,
                "compositor should paint the document into the window"
            );
            std::thread::sleep(Duration::from_millis(16));
        };
        if let Ok(path) = std::env::var("WEBSCENE_CONTROL_SCREENSHOT") {
            std::fs::write(path, &png).unwrap();
        }
        view.execute_script(
            "document.elementFromPoint(60, 60).style.cursor = 'crosshair';
             document.addEventListener('pointerdown', () => console.log('CONTROL_POINTER_OK'));
             document.addEventListener('keydown', () => console.log('CONTROL_KEY_OK'));
             document.addEventListener('input', e => console.log('CONTROL_TEXT_OK', e.target.value));
             document.addEventListener('wheel', e => console.log('CONTROL_WHEEL', e.deltaX, e.deltaY));
             window.addEventListener('resize', () => console.log('CONTROL_RESIZE', innerWidth, innerHeight));
             console.log('CONTROL_LISTENERS_READY');",
        )
        .unwrap();
        let deadline = std::time::Instant::now() + Duration::from_secs(5);
        while !messages
            .borrow()
            .iter()
            .any(|s| s.contains("CONTROL_LISTENERS_READY"))
        {
            view.tick();
            assert!(std::time::Instant::now() < deadline);
            std::thread::sleep(Duration::from_millis(10));
        }
        window.mouse_down(
            Point::new(60.0, 60.0),
            MouseButton::Left,
            RawInputModifiers::empty(),
        );
        window.mouse_up(
            Point::new(60.0, 60.0),
            MouseButton::Left,
            RawInputModifiers::empty(),
        );
        window.key_press(
            Key::A,
            RawInputModifiers::empty(),
            PhysicalKey::A,
            Some("a"),
        );
        window.key_release(
            Key::A,
            RawInputModifiers::empty(),
            PhysicalKey::A,
            Some("a"),
        );
        let deadline = std::time::Instant::now() + Duration::from_secs(5);
        loop {
            view.tick();
            let m = messages.borrow();
            if m.iter().any(|s| s.contains("CONTROL_POINTER_OK"))
                && m.iter().any(|s| s.contains("CONTROL_KEY_OK"))
            {
                break;
            }
            assert!(std::time::Instant::now() < deadline, "{m:?}");
            drop(m);
            window.capture_rendered_frame();
            std::thread::sleep(Duration::from_millis(16));
        }
        window.mouse_down(
            Point::new(400.0, 210.0),
            MouseButton::Left,
            RawInputModifiers::empty(),
        );
        window.mouse_up(
            Point::new(400.0, 210.0),
            MouseButton::Left,
            RawInputModifiers::empty(),
        );
        window.key_text_input("é🙂");
        let deadline = std::time::Instant::now() + Duration::from_secs(5);
        loop {
            view.tick();
            let m = messages.borrow();
            if m.iter()
                .any(|s| s.contains("CONTROL_TEXT_OK") && s.contains("é🙂"))
            {
                break;
            }
            assert!(
                std::time::Instant::now() < deadline,
                "Unicode input not delivered: {m:?}"
            );
            drop(m);
            std::thread::sleep(Duration::from_millis(10));
        }

        let point = Point::new(60.0, 60.0);
        window.mouse_move(point, RawInputModifiers::empty());
        window.mouse_wheel(point, Vector::new(0.5, -1.0), RawInputModifiers::empty());
        window.set_width(800.0);
        window.set_height(560.0);
        let deadline = std::time::Instant::now() + Duration::from_secs(5);
        loop {
            window.capture_rendered_frame();
            view.tick();
            let m = messages.borrow();
            let cursor = window
                .input_root()
                .cursor_element()
                .and_then(|element| element.cursor())
                .map(|cursor| cursor.to_string());
            let wheel = m.iter().any(|s| s.contains("CONTROL_WHEEL | -25 | 50"));
            let resize = m.iter().any(|s| s.contains("CONTROL_RESIZE | 800 | 560"));
            if cursor.as_deref() == Some("Cross") && wheel && resize {
                break;
            }
            assert!(
                std::time::Instant::now() < deadline,
                "cursor={cursor:?}, bounds={:?}, error={:?}, diagnostics={m:?}",
                view.bounds(),
                view.last_error()
            );
            drop(m);
            std::thread::sleep(Duration::from_millis(16));
        }
        window.set_content(None);
        window.capture_rendered_frame();
        assert!(view.timer.borrow().is_none());
        assert!(view.composition.borrow().is_none());
        window.set_content(Some(Rc::new(view.clone().upcast::<Control>())));
        window.capture_rendered_frame();
        assert!(view.timer.borrow().is_some());
        assert!(view.composition.borrow().is_some());
        view.close();
        assert!(view.engine.borrow().is_none());
        assert!(view.timer.borrow().is_none());
        window.close();
        locator.dispose();
        AppBuilder::reset_setup_for_unit_tests();
    }
}
