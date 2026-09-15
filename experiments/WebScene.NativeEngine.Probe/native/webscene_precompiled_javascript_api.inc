// Included in namespace webscene_native, outside the anonymous namespace.
extern "C" WEBSCENE_API int webscene_register_precompiled_javascript_v1(
    const webscene_precompiled_javascript_v1* input)
{
    try {
        constexpr size_t entry_limit = 32U * 1024U * 1024U;
        constexpr size_t total_limit = 256U * 1024U * 1024U;
        if (!input || input->struct_size < sizeof(*input) || input->version != 1
            || input->is_module > 1 || !input->v8_version || !input->snapshot_fingerprint
            || !input->data || input->data_length == 0 || input->data_length > entry_limit)
            return -1;
        auto entry = std::make_shared<precompiled_javascript_entry>();
        entry->tag = input->cached_data_version_tag;
        entry->v8_version = input->v8_version;
        entry->snapshot = input->snapshot_fingerprint;
        if (entry->v8_version.size() > 256 || entry->snapshot.size() > 4096) return -1;
        const auto digest = sha256(input->data, input->data_length);
        if (!std::equal(digest.begin(), digest.end(), input->payload_sha256)) return -1;
        auto& store = precompiled_javascript_store();
        const auto key = precompiled_javascript_key(input->source_sha256, input->is_module != 0);
        std::lock_guard lock(store.mutex);
        if (const auto known = store.entries.find(key); known != store.entries.end()) {
            const auto& prior = *known->second;
            return prior.tag == entry->tag && prior.v8_version == entry->v8_version
                && prior.snapshot == entry->snapshot ? 0 : -2;
        }
        if (store.entries.size() >= 4096 || store.bytes.load() > total_limit - input->data_length)
            return -1;
        entry->bytes.assign(input->data, input->data + input->data_length);
        store.entries.emplace(key, std::move(entry));
        store.bytes.fetch_add(input->data_length, std::memory_order_relaxed);
        store.count.fetch_add(1, std::memory_order_release);
        return 0;
    } catch (...) { return -1; }
}
extern "C" WEBSCENE_API int webscene_get_precompiled_javascript_stats_v1(
    webscene_precompiled_javascript_stats_v1* output)
{
    try {
        if (!output || output->struct_size < sizeof(*output) || output->version != 1) return -1;
        auto& store = precompiled_javascript_store();
        std::lock_guard lock(store.mutex);
        output->registered_scripts = store.count.load();
        output->registered_bytes = store.bytes.load();
        output->cache_hits = store.hits.load();
        output->cache_rejections = store.rejections.load();
        return 0;
    } catch (...) { return -1; }
}
extern "C" WEBSCENE_API int webscene_precompile_javascript_v1(
    const char* source, size_t length, const char* name, uint32_t module,
    webscene_precompiled_javascript_sink_v1 sink, void* user,
    char* error, size_t capacity)
{
    if (error && capacity) error[0] = '\0';
    const auto fail = [&](const std::string& message) {
        if (error && capacity) {
            const auto count = std::min(message.size(), capacity - 1);
            std::memcpy(error, message.data(), count);
            error[count] = '\0';
        }
        return -1;
    };
    try {
        if (!source || !name || !sink || module > 1 || length > 32U * 1024U * 1024U)
            return fail("Invalid JavaScript compiler input (maximum 32 MiB)");
        initialize_v8_process();
        std::unique_ptr<v8::ArrayBuffer::Allocator> allocator(
            v8::ArrayBuffer::Allocator::NewDefaultAllocator());
        v8::Isolate::CreateParams params;
        params.array_buffer_allocator = allocator.get();
        configure_startup_snapshot(params);
        struct dispose_isolate { void operator()(v8::Isolate* p) const { if (p) p->Dispose(); } };
        std::unique_ptr<v8::Isolate, dispose_isolate> owner(v8::Isolate::New(params));
        if (!owner) return fail("V8 isolate creation failed");
        auto* isolate = owner.get();
        return compile_precompiled_javascript_in_isolate(isolate, source, length,
            name, module, sink, user, error, capacity);
    } catch (const std::exception& e) { return fail(e.what()); }
    catch (...) { return fail("Unexpected failure compiling JavaScript"); }
}
