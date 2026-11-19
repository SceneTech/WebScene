use ferroui_base::threading::{DispatcherPriority, DispatcherTimer};
use ferroui_controls::{AppBuilder, Application, Control, Window};
use ferroui_desktop::AppBuilderDesktopExtensions;
use std::{path::PathBuf, rc::Rc};
use webscene::{RuntimeOptions, WebSceneView};

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    if args.is_empty() {
        eprintln!("Usage: showcase /absolute/path/to/native/library [document URL]");
        std::process::exit(2);
    }
    AppBuilder::configure::<Application>()
        .use_platform_detect()
        .start(
            |app, args| {
                app.styles()
                    .add(ferroui_themes_simple::SimpleTheme::new().as_style());
                let view = WebSceneView::new();
                view.on_error(|e| eprintln!("WebScene: {e}"));
                view.on_diagnostic(|message| eprintln!("WebScene diagnostic: {message}"));
                view.on_host_request(|message| eprintln!("WebScene host request: {message}"));
                let mut options = RuntimeOptions::new(&args[0]);
                options.cache_directory = Some(cache_directory());
                options.capture_console = true;
                view.open(options).expect("open native runtime");
                let default_url = url::Url::from_file_path(
                    std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("examples/showcase.html"),
                )
                .unwrap();
                view.load_url(
                    args.get(1)
                        .map(String::as_str)
                        .unwrap_or(default_url.as_str()),
                )
                .expect("load document");
                let window = Window::new();
                window.set_title(Some("WebScene · FerroUI".into()));
                window.set_width(960.0);
                window.set_height(680.0);
                window.set_content(Some(Rc::new(view.clone().upcast::<Control>())));
                let _timer = std::env::var("WEBSCENE_SMOKE_EXIT_MS").ok().map(|ms| {
                    let window = window.clone();
                    let view = view.clone();
                    DispatcherTimer::run_once(
                        move || {
                            view.close();
                            window.close();
                        },
                        std::time::Duration::from_millis(ms.parse().unwrap()),
                        DispatcherPriority::NORMAL,
                    )
                });
                app.run_window(window);
                view.close();
            },
            &args,
        );
}

fn cache_directory() -> PathBuf {
    if let Some(directory) = std::env::var_os("WEBSCENE_CACHE_DIR") {
        return PathBuf::from(directory);
    }
    #[cfg(target_os = "windows")]
    let base = std::env::var_os("LOCALAPPDATA").map(PathBuf::from);
    #[cfg(target_os = "macos")]
    let base = std::env::var_os("HOME").map(|home| PathBuf::from(home).join("Library/Caches"));
    #[cfg(not(any(target_os = "windows", target_os = "macos")))]
    let base = std::env::var_os("XDG_CACHE_HOME")
        .map(PathBuf::from)
        .or_else(|| std::env::var_os("HOME").map(|home| PathBuf::from(home).join(".cache")));
    base.unwrap_or_else(std::env::temp_dir)
        .join("WebScene")
        .join("FerroUI")
        .join("showcase")
}
