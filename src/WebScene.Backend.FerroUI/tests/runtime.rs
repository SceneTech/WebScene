use std::{
    path::PathBuf,
    time::{Duration, Instant},
};
use webscene::{Engine, RuntimeOptions};

/// Runs against the same ABI-3 runtime shipped to NuGet consumers, not a simulated engine.
#[test]
#[ignore = "set WEBSCENE_NATIVE_ENGINE_LIBRARY to the 1.0.37 library and run --ignored"]
fn native_document_render_input_resize_reload_and_teardown() {
    let library = std::env::var("WEBSCENE_NATIVE_ENGINE_LIBRARY").expect("runtime library path");
    let mut options = RuntimeOptions::new(library);
    options.capture_console = true;
    let mut engine = Engine::new(options).unwrap();
    engine.resize(960.0, 680.0, 1.0).unwrap();
    let document = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("examples/showcase.html");
    engine.load_file(&document).unwrap();
    let wait = |engine: &mut Engine, after: u64| {
        let deadline = Instant::now() + Duration::from_secs(15);
        loop {
            engine.update().unwrap();
            while let Some(message) = engine.take_diagnostic() {
                eprintln!("{message}");
                assert!(!message.contains("\"kind\":\"exception\""), "{message}");
            }
            if engine
                .scene()
                .is_some_and(|s| s.is_ready() && s.revision() > after)
            {
                break;
            }
            assert!(
                Instant::now() < deadline,
                "document did not produce a ready scene"
            );
            std::thread::sleep(Duration::from_millis(10));
        }
    };
    wait(&mut engine, 0);
    let mut surface = skia_safe::surfaces::raster_n32_premul((960, 680)).unwrap();
    surface.canvas().clear(skia_safe::Color::TRANSPARENT);
    engine.draw(surface.canvas());
    let image = surface.image_snapshot();
    let png = image
        .encode(None, skia_safe::EncodedImageFormat::PNG, None)
        .unwrap();
    if let Ok(path) = std::env::var("WEBSCENE_SCREENSHOT") {
        std::fs::write(path, png.as_bytes()).unwrap();
    }
    assert!(
        png.size() > 5000,
        "expected text and chart pixels, not a blank frame"
    );
    let revision = engine.scene().unwrap().revision();
    engine
        .execute_script(
            "document.body.style.background='#ff0000'; console.log('FERROUI_SCRIPT_OK');",
        )
        .unwrap();
    wait(&mut engine, revision);
    surface.canvas().clear(skia_safe::Color::TRANSPARENT);
    engine.draw(surface.canvas());
    let pixel = surface.peek_pixels().unwrap().get_color((0, 0));
    assert_eq!(
        pixel,
        skia_safe::Color::RED,
        "script updates must reach the retained scene"
    );
    let revision = engine.scene().unwrap().revision();
    engine.resize(640.0, 480.0, 2.0).unwrap();
    wait(&mut engine, revision);
    assert_eq!(engine.scene().unwrap().viewport(), (640.0, 480.0));
    // Exercise actual native hit testing and event dispatch, then read an observable console result.
    engine.execute_script("document.addEventListener('pointerdown',()=>console.log('FERROUI_POINTER_OK')); document.addEventListener('keydown',()=>console.log('FERROUI_KEY_OK')); console.log('LISTENERS_READY');").unwrap();
    let deadline = Instant::now() + Duration::from_secs(5);
    let mut listeners_ready = false;
    while !listeners_ready {
        engine.update().unwrap();
        while let Some(s) = engine.take_diagnostic() {
            listeners_ready |= s.contains("LISTENERS_READY");
        }
        assert!(Instant::now() < deadline, "listener setup did not complete");
        std::thread::sleep(Duration::from_millis(10));
    }
    engine.pointer(2, 1, 60.0, 60.0, 0.0, 0.0).unwrap();
    engine.pointer(3, 0, 60.0, 60.0, 0.0, 0.0).unwrap();
    engine.key(true, 65, 0).unwrap();
    engine.text("é🙂").unwrap();
    engine.key(false, 65, 0).unwrap();
    let deadline = Instant::now() + Duration::from_secs(5);
    let (mut pointer, mut key) = (false, false);
    while !(pointer && key) {
        engine.update().unwrap();
        while let Some(s) = engine.take_diagnostic() {
            pointer |= s.contains("FERROUI_POINTER_OK");
            key |= s.contains("FERROUI_KEY_OK");
        }
        assert!(
            Instant::now() < deadline,
            "input not dispatched: pointer={pointer}, key={key}"
        );
        std::thread::sleep(Duration::from_millis(10));
    }
    engine.set_visible(false).unwrap();
    engine.set_visible(true).unwrap();
    engine.load_file(document).unwrap();
    wait(&mut engine, 0);
    drop(engine);
}
