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

    void install_performance_timeline_on_object(
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Object> target)
    {
        auto global = local_context->Global();
        v8::Local<v8::Value> raw_installer;
        if (!global->Get(
                local_context,
                js_string(isolate, "__webSceneInstallPerformanceTimeline"))
                .ToLocal(&raw_installer)
            || !raw_installer->IsFunction()) {
            throw std::runtime_error(
                "Performance Timeline object installer is unavailable");
        }
        v8::Local<v8::Value> arguments[]{target};
        v8::TryCatch try_catch(isolate);
        if (raw_installer.As<v8::Function>()
                ->Call(local_context, global, 1, arguments)
                .IsEmpty()) {
            throw std::runtime_error(
                std::string("Unable to install WebScene Performance Timeline object: ")
                + describe_reported_exception(try_catch, local_context));
        }
    }

    void install_web_api_compatibility(v8::Local<v8::Context> local_context)
    {
        install_web_api_compatibility_script(
            local_context,
            "FileReader",
            fileReaderCompatibilityScript);
    }
