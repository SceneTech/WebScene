#[test]
fn packaged_header_matches_native_source() {
    let source = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../../experiments/WebScene.NativeEngine.Probe/native/webscene_native_engine.h");
    if source.exists() {
        assert_eq!(
            include_str!("../include/webscene_native_engine.h"),
            std::fs::read_to_string(source).unwrap(),
            "Regenerate/update the FerroUI ABI header when the engine ABI changes"
        );
    }
}
