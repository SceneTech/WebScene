// Included after webscene_v8_runtime_support.inc, in its anonymous namespace.
// Reuses the runtime's SHA-256, V8 initialization, flags and snapshot loader.
struct precompiled_javascript_entry final {
    uint32_t tag{};
    std::string v8_version;
    std::string snapshot;
    std::vector<uint8_t> bytes;
};
struct precompiled_javascript_registry final {
    std::mutex mutex;
    std::unordered_map<std::string, std::shared_ptr<const precompiled_javascript_entry>> entries;
    std::atomic<uint64_t> count{0}, bytes{0}, hits{0}, rejections{0};
};
precompiled_javascript_registry& precompiled_javascript_store()
{
    // Remains usable by application static constructors and destructors, including
    // independently loaded clients of a shared WebScene runtime.
    static auto* store = new precompiled_javascript_registry;
    return *store;
}
std::string precompiled_javascript_snapshot_identity()
{
#if defined(WEBSCENE_V8_SNAPSHOT_FINGERPRINT)
    return WEBSCENE_V8_SNAPSHOT_FINGERPRINT;
#else
    return {};
#endif
}
std::string precompiled_javascript_key(const uint8_t* digest, bool module)
{
    auto key = std::string(reinterpret_cast<const char*>(digest), 32);
    key.push_back(module ? '\1' : '\0');
    return key;
}
std::shared_ptr<const precompiled_javascript_entry> find_precompiled_javascript(
    std::string_view source, bool module)
{
    auto& store = precompiled_javascript_store();
    if (store.count.load(std::memory_order_acquire) == 0) return {};
    const auto digest = sha256(reinterpret_cast<const uint8_t*>(source.data()), source.size());
    const auto key = precompiled_javascript_key(digest.data(), module);
    std::lock_guard lock(store.mutex);
    const auto found = store.entries.find(key);
    return found == store.entries.end() ? nullptr : found->second;
}
bool reject_precompiled_javascript(v8::Isolate* isolate, const char* reason)
{
    precompiled_javascript_store().rejections.fetch_add(1, std::memory_order_relaxed);
    isolate->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(
        isolate, reason).ToLocalChecked()));
    return false;
}
bool validate_precompiled_javascript(v8::Isolate* isolate,
    const precompiled_javascript_entry& entry)
{
    if (entry.tag != v8::ScriptCompiler::CachedDataVersionTag()
        || entry.v8_version != v8::V8::GetVersion()
        || entry.snapshot != precompiled_javascript_snapshot_identity()) {
        return reject_precompiled_javascript(isolate,
            "Precompiled JavaScript does not match this V8 build/flags/snapshot; rebuild the application with the matching SDK");
    }
    return true;
}
bool consume_precompiled_classic(v8::Isolate* isolate,
    v8::Local<v8::String> source, v8::Local<v8::String> name,
    const precompiled_javascript_entry& entry, v8::Local<v8::UnboundScript>& script)
{
    if (!validate_precompiled_javascript(isolate, entry)) return false;
    auto* cache = new v8::ScriptCompiler::CachedData(entry.bytes.data(),
        static_cast<int>(entry.bytes.size()), v8::ScriptCompiler::CachedData::BufferNotOwned);
    v8::ScriptOrigin origin(name);
    v8::ScriptCompiler::Source input(source, origin, cache);
    if (!v8::ScriptCompiler::CompileUnboundScript(isolate, &input,
        v8::ScriptCompiler::kConsumeCodeCache).ToLocal(&script)) return false;
    // V8 may compile a fallback internally before reporting rejection. Do not
    // execute that result or describe this feature as removing the compiler.
    if (cache->rejected) return reject_precompiled_javascript(isolate,
        "V8 rejected packaged JavaScript cache; rebuild the application (source fallback was not executed)");
    precompiled_javascript_store().hits.fetch_add(1, std::memory_order_relaxed);
    return true;
}
bool consume_precompiled_module(v8::Isolate* isolate,
    v8::Local<v8::String> source, v8::Local<v8::String> name,
    const precompiled_javascript_entry& entry, v8::Local<v8::Module>& module)
{
    if (!validate_precompiled_javascript(isolate, entry)) return false;
    auto* cache = new v8::ScriptCompiler::CachedData(entry.bytes.data(),
        static_cast<int>(entry.bytes.size()), v8::ScriptCompiler::CachedData::BufferNotOwned);
    v8::ScriptOrigin origin(name, 0, 0, false, -1, {}, false, false, true);
    v8::ScriptCompiler::Source input(source, origin, cache);
    if (!v8::ScriptCompiler::CompileModule(isolate, &input,
        v8::ScriptCompiler::kConsumeCodeCache).ToLocal(&module)) return false;
    if (cache->rejected) return reject_precompiled_javascript(isolate,
        "V8 rejected packaged module cache; rebuild the application (source fallback was not executed)");
    precompiled_javascript_store().hits.fetch_add(1, std::memory_order_relaxed);
    return true;
}

// Shared compiler body: no application execution or module instantiation.
int compile_precompiled_javascript_in_isolate(v8::Isolate* isolate,
    const char* source, size_t length, const char* name, uint32_t module,
    webscene_precompiled_javascript_sink_v1 sink, void* user,
    char* error, size_t capacity)
{
    const auto fail = [&](const std::string& message) {
        if (error && capacity) {
            const auto count = std::min(message.size(), capacity - 1);
            std::memcpy(error, message.data(), count); error[count] = '\0';
        }
        return -1;
    };
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handles(isolate);
        auto context = v8::Context::New(isolate);
        v8::Context::Scope context_scope(context);
        v8::TryCatch caught(isolate);
        v8::Local<v8::String> text, filename;
        if (!v8::String::NewFromUtf8(isolate, source, v8::NewStringType::kNormal,
                static_cast<int>(length)).ToLocal(&text)
            || !v8::String::NewFromUtf8(isolate, name).ToLocal(&filename))
            return fail("V8 source allocation failed");
        v8::ScriptOrigin origin(filename, 0, 0, false, -1, {}, false, false, module != 0);
        v8::ScriptCompiler::Source input(text, origin);
        std::unique_ptr<v8::ScriptCompiler::CachedData> cache;
        if (module) {
            v8::Local<v8::Module> compiled;
            // CompileModule on supported older V8 SDKs rejects kEagerCompile.
            // Module caches retain V8's normal lazy-function policy.
            if (v8::ScriptCompiler::CompileModule(isolate, &input,
                    v8::ScriptCompiler::kNoCompileOptions).ToLocal(&compiled))
                cache.reset(v8::ScriptCompiler::CreateCodeCache(compiled->GetUnboundModuleScript()));
        } else {
            v8::Local<v8::UnboundScript> compiled;
            if (v8::ScriptCompiler::CompileUnboundScript(isolate, &input,
                    v8::ScriptCompiler::kEagerCompile).ToLocal(&compiled))
                cache.reset(v8::ScriptCompiler::CreateCodeCache(compiled));
        }
        if (!cache || !cache->data || cache->length <= 0) {
            std::string message = std::string(name) + ": JavaScript compilation failed";
            if (caught.HasCaught()) {
                v8::String::Utf8Value detail(isolate, caught.Exception());
                if (*detail) message += ": " + std::string(*detail, detail.length());
                if (!caught.Message().IsEmpty())
                    message += " (line " + std::to_string(caught.Message()->GetLineNumber(context).FromMaybe(0)) + ")";
            }
            return fail(message);
        }
        if (cache->length > 32 * 1024 * 1024) return fail("Compiled cache exceeds the 32 MiB registration limit");
        webscene_precompiled_javascript_v1 result{};
        result.struct_size = sizeof(result); result.version = 1; result.is_module = module;
        result.cached_data_version_tag = v8::ScriptCompiler::CachedDataVersionTag();
        const auto source_hash = sha256(reinterpret_cast<const uint8_t*>(source), length);
        const auto cache_hash = sha256(cache->data, static_cast<size_t>(cache->length));
        std::copy(source_hash.begin(), source_hash.end(), result.source_sha256);
        std::copy(cache_hash.begin(), cache_hash.end(), result.payload_sha256);
        result.v8_version = v8::V8::GetVersion();
        const auto fingerprint = precompiled_javascript_snapshot_identity();
        result.snapshot_fingerprint = fingerprint.c_str();
        result.data = cache->data; result.data_length = static_cast<size_t>(cache->length);
        if (sink(&result, user) != 0) return fail("Unable to write compiled JavaScript output");
        return 0;
}
