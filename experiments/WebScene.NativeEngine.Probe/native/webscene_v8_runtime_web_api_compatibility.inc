    void install_web_api_compatibility_script(
        v8::Local<v8::Context> local_context,
        std::string_view name,
        std::string_view source)
    {
        v8::TryCatch try_catch(isolate);
        v8::Local<v8::Script> script;
        if (!v8::Script::Compile(
                local_context,
                js_string(isolate, source.data())).ToLocal(&script)
            || script->Run(local_context).IsEmpty()) {
            throw std::runtime_error(
                std::string("Unable to install WebScene ") + std::string(name)
                + " compatibility: "
                + describe_reported_exception(try_catch, local_context));
        }
    }

    void install_performance_timeline(v8::Local<v8::Context> local_context)
    {
        install_web_api_compatibility_script(
            local_context,
            "Performance Timeline",
            browserCompatibilityScript);
    }

    void install_web_api_compatibility(v8::Local<v8::Context> local_context)
    {
        constexpr std::array scripts{
            std::pair{"FileReader", fileReaderCompatibilityScript},
            std::pair{"stylesheet CSSOM", cssCompatibilityScript},
        };
        for (const auto& [name, source] : scripts) {
            install_web_api_compatibility_script(local_context, name, source);
        }
    }
