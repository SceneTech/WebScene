use crate::{
    engine::SceneMailbox,
    render::{Frame, Renderer},
};
use ferroui_base::{
    media::ImmediateDrawingContext,
    rendering::composition::{CompositionCustomVisualHandler, ICompositionCustomVisualHandler},
    Rect,
};
use ferroui_skia::ISkiaApiLeaseFeature;
use std::{
    any::{Any, TypeId},
    cell::{Cell, RefCell},
    rc::Rc,
    sync::Arc,
};

pub(crate) struct SceneVisual {
    base: CompositionCustomVisualHandler,
    mailbox: Arc<SceneMailbox>,
    consumer: u64,
    renderer: RefCell<Renderer>,
    running: Cell<bool>,
    frame_scheduled: Cell<bool>,
}

pub(crate) enum SceneVisualMessage {
    SetRunning(bool),
    Resize,
}

impl SceneVisual {
    pub fn new(mailbox: Arc<SceneMailbox>, consumer: u64) -> Self {
        Self {
            base: CompositionCustomVisualHandler::new(),
            mailbox,
            consumer,
            renderer: RefCell::new(Renderer::default()),
            running: Cell::new(false),
            frame_scheduled: Cell::new(false),
        }
    }

    fn schedule_frame(&self) {
        if self.running.get() && !self.frame_scheduled.replace(true) {
            self.base.register_for_next_animation_frame_update();
        }
    }

    fn refresh(&self, drive_animation: bool) -> bool {
        match self.mailbox.update(
            self.consumer,
            &mut self.renderer.borrow_mut(),
            drive_animation,
        ) {
            Ok(Some(changed)) => changed,
            Ok(None) => {
                self.running.set(false);
                false
            }
            Err(error) => {
                self.running.set(false);
                self.mailbox.report(error);
                false
            }
        }
    }
}

impl ICompositionCustomVisualHandler for SceneVisual {
    fn handler_base(&self) -> &CompositionCustomVisualHandler {
        &self.base
    }

    fn on_message(&self, message: Arc<dyn Any + Send + Sync>) {
        match message.downcast_ref::<SceneVisualMessage>() {
            Some(SceneVisualMessage::SetRunning(running)) => {
                self.running.set(*running);
                self.schedule_frame();
            }
            Some(SceneVisualMessage::Resize) if self.running.get() => {
                // FerroUI can commit and paint inside the OS's resize loop,
                // before the compositor's next animation callback.
                if self.refresh(false) {
                    self.base.invalidate();
                }
            }
            _ => {}
        }
    }

    fn on_animation_frame_update(&self) {
        self.frame_scheduled.set(false);
        if !self.running.get() {
            return;
        }
        if self.refresh(true) {
            self.base.invalidate();
        }
        self.schedule_frame();
    }

    fn on_render(&self, context: &mut ImmediateDrawingContext<'_>) {
        let size = self.base.effective_size();
        let target = (size.x as f32, size.y as f32);
        let viewport = self.renderer.borrow().viewport;
        if self.running.get() && !viewports_match(target, viewport) {
            // A scene may have arrived after the resize commit acquired the
            // mailbox. A synchronous paint can consume it without driving RAF.
            self.refresh(false);
        }
        let renderer = self.renderer.borrow();
        draw_frame(
            context,
            Rect::new(0.0, 0.0, size.x, size.y),
            &renderer.frame,
            Some(renderer.viewport),
        );
    }
}

fn viewports_match(target: (f32, f32), viewport: (f32, f32)) -> bool {
    // Match Avalonia's tolerance for layout rounding.
    viewport.0 > 0.0
        && viewport.1 > 0.0
        && (target.0 - viewport.0).abs() <= 0.5
        && (target.1 - viewport.1).abs() <= 0.5
}

fn resize_scale(target: (f32, f32), viewport: (f32, f32)) -> (f32, f32) {
    if target.0 > 0.0 && target.1 > 0.0 && viewports_match(target, viewport) {
        (target.0 / viewport.0, target.1 / viewport.1)
    } else {
        (1.0, 1.0)
    }
}

pub(crate) fn draw_frame(
    context: &mut ImmediateDrawingContext<'_>,
    bounds: Rect,
    frame: &Frame,
    viewport: Option<(f32, f32)>,
) {
    if let Some(feature) = context
        .try_get_feature(TypeId::of::<dyn ISkiaApiLeaseFeature>())
        .and_then(|feature| {
            feature
                .downcast_ref::<Rc<dyn ISkiaApiLeaseFeature>>()
                .cloned()
        })
    {
        let lease = feature.lease();
        lease.with_sk_canvas(&mut |canvas| {
            let target = (bounds.width as f32, bounds.height as f32);
            let save = canvas.save();
            canvas.clip_rect(
                skia_safe::Rect::from_wh(target.0, target.1),
                skia_safe::ClipOp::Intersect,
                false,
            );
            // Keep the previous layout pixel-stable until the resized scene
            // arrives. Only correct subpixel rounding of matching viewports.
            if let Some(viewport) = viewport {
                canvas.scale(resize_scale(target, viewport));
            }
            frame.draw(canvas);
            canvas.restore_to_count(save);
        });
        lease.dispose();
    }
}

#[cfg(test)]
mod tests {
    use super::resize_scale;

    #[test]
    fn clips_stale_resize_frames_and_only_corrects_matching_viewport_rounding() {
        let viewport = (800.0, 600.0);
        assert_eq!(resize_scale((1000.0, 600.0), viewport), (1.0, 1.0));
        assert_eq!(resize_scale((800.0, 400.0), viewport), (1.0, 1.0));
        assert_eq!(resize_scale((800.0, 600.0), (0.0, 0.0)), (1.0, 1.0));
        assert_eq!(
            resize_scale((800.25, 599.75), viewport),
            (800.25 / 800.0, 599.75 / 600.0)
        );
    }
}
