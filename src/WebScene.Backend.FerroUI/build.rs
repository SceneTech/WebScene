fn main() {
    println!("cargo:rerun-if-changed=include/webscene_native_engine.h");
    bindgen::Builder::default()
        .header("include/webscene_native_engine.h")
        .clang_args(["-x", "c++", "-std=c++17"])
        .allowlist_function("webscene_engine_(get_abi_version|prewarm|create_with_options|destroy|load_url|execute_script|enqueue|enqueue_resize_frame|acquire_next_scene|request_scene_checkpoint|requires_animation_frame|set_visible|get_cursor|set_preferred_color_scheme|request_low_memory|copy_last_error|configure_diagnostics|take_diagnostic|copy_runtime_failure|take_host_request)")
        .allowlist_function("webscene_scene_(release|acknowledge)")
        .allowlist_type("webscene_(scene_view|engine_options|input_event|text_metrics)")
        .allowlist_var("WEBSCENE_(INPUT_.*|DIAGNOSTIC_.*)")
        .dynamic_library_name("NativeApi")
        .dynamic_link_require_all(true)
        .derive_default(true)
        .generate()
        .expect("generate bindings to WebScene ABI 3")
        .write_to_file(std::path::PathBuf::from(std::env::var_os("OUT_DIR").unwrap()).join("bindings.rs"))
        .unwrap();
}
