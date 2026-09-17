void test_worker_starts_after_engine_configuration()
{
    const auto cache_root = std::filesystem::temp_directory_path()
        / ("webscene-worker-order-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(cache_root);
    const auto cache_path = cache_root.string();

    constexpr auto construction_count = 12;
    const auto started = std::chrono::steady_clock::now();
    for (auto iteration = 0; iteration < construction_count; ++iteration) {
        webscene_engine_options options{};
        options.struct_size = sizeof(options);
        options.compilation_cache_directory = cache_path.data();
        options.compilation_cache_directory_length = cache_path.size();
        auto* engine = webscene_engine_create_with_options(&options);
        require(engine != nullptr, "configured engine creation failed");
        execute_and_wait(engine, "void 0", "worker-configuration-order.js");
        require(last_error(engine).empty(),
            "worker observed partially configured engine state");
        webscene_engine_destroy(engine);
    }
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    require(elapsed < 2.0,
        "12 configured engine startup cycles exceeded two seconds");
    std::cout << "Configured worker startup gate: constructions="
              << construction_count << " elapsed=" << elapsed << "s\n";

    std::error_code error;
    std::filesystem::remove_all(cache_root, error);
}
