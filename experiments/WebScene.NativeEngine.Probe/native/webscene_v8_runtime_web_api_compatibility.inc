    void install_web_api_compatibility(v8::Local<v8::Context> local_context)
    {
        constexpr std::array scripts{
            std::pair{"Performance Timeline", browserCompatibilityScript},
            std::pair{"FileReader", fileReaderCompatibilityScript},
            std::pair{"stylesheet CSSOM", cssCompatibilityScript},
        };
        for (const auto& [name, source] : scripts) {
            v8::TryCatch try_catch(isolate);
            v8::Local<v8::Script> script;
            if (!v8::Script::Compile(
                    local_context,
                    js_string(isolate, source.data())).ToLocal(&script)
                || script->Run(local_context).IsEmpty()) {
                throw std::runtime_error(
                    std::string("Unable to install WebScene ") + name
                    + " compatibility: "
                    + describe_reported_exception(try_catch, local_context));
            }
        }
    }
