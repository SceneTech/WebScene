uint64_t file_system_access_test_peak_rss_bytes();
uint64_t file_system_access_test_open_fd_count();

const webscene_file_panel_request_v2* take_file_panel_request(
    webscene_engine* engine)
{
    const webscene_file_panel_request_v2* request = nullptr;
    for (int attempt = 0; attempt < 250 && request == nullptr; ++attempt) {
        request = webscene_engine_take_file_panel_request_v2(engine);
        if (request == nullptr)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(request != nullptr, "native file panel request was not published");
    return request;
}

const webscene_file_grant_same_entry_request_v2*
take_file_grant_same_entry_request(webscene_engine* engine)
{
    const webscene_file_grant_same_entry_request_v2* request = nullptr;
    for (int attempt = 0; attempt < 250 && request == nullptr; ++attempt) {
        request = webscene_engine_take_file_grant_same_entry_request_v2(engine);
        if (request == nullptr)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(request != nullptr,
        "native file grant same-entry request was not published");
    return request;
}

bool complete_file_grant_same_entry(
    webscene_engine* engine,
    uint64_t request_id,
    bool admitted,
    bool same_entry)
{
    const webscene_file_grant_same_entry_completion_v2 completion{
        sizeof(completion), 2U, request_id,
        static_cast<uint8_t>(admitted), static_cast<uint8_t>(same_entry), {}};
    return webscene_engine_complete_file_grant_same_entry_request_v2(
        engine, &completion) != 0;
}

const webscene_file_grant_ancestry_request_v2*
take_file_grant_ancestry_request(webscene_engine* engine)
{
    const webscene_file_grant_ancestry_request_v2* request = nullptr;
    for (int attempt = 0; attempt < 250 && request == nullptr; ++attempt) {
        request = webscene_engine_take_file_grant_ancestry_request_v2(engine);
        if (request == nullptr)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(request != nullptr,
        "native file grant ancestry request was not published");
    return request;
}

bool complete_file_grant_ancestry(
    webscene_engine* engine,
    uint64_t request_id,
    uint32_t status,
    const std::vector<std::string>& names = {},
    std::string_view error_code = {})
{
    std::vector<webscene_file_grant_ancestry_component_v2> components;
    components.reserve(names.size());
    for (const auto& name : names) components.push_back({
        sizeof(webscene_file_grant_ancestry_component_v2), 2U,
        {name.data(), name.size()}});
    const webscene_file_grant_ancestry_completion_v2 completion{
        sizeof(completion), 2U, request_id, status, 0U,
        components.empty() ? nullptr : components.data(), components.size(),
        {error_code.empty() ? nullptr : error_code.data(), error_code.size()}};
    return webscene_engine_complete_file_grant_ancestry_request_v2(
        engine, &completion) != 0;
}

const webscene_file_grant_read_request_v2*
take_file_grant_read_request(webscene_engine* engine)
{
    const webscene_file_grant_read_request_v2* request = nullptr;
    for (int attempt = 0; attempt < 250 && request == nullptr; ++attempt) {
        request = webscene_engine_take_file_grant_read_request_v2(engine);
        if (request == nullptr)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(request != nullptr, "native file grant read request was not published");
    return request;
}

bool complete_file_grant_read(
    webscene_engine* engine,
    uint64_t request_id,
    uint32_t status,
    uint64_t total_bytes,
    int64_t modification_time_ns,
    uint64_t offset,
    const std::vector<uint8_t>& bytes,
    bool eof)
{
    const bool success = status == WEBSCENE_FILE_GRANT_READ_SUCCESS_V2;
    const webscene_file_grant_metadata_v2 metadata = success
        ? webscene_file_grant_metadata_v2{sizeof(metadata), 2U, total_bytes,
            modification_time_ns, WEBSCENE_FILE_PANEL_ENTRY_FILE_V2, 0U}
        : webscene_file_grant_metadata_v2{};
    const webscene_file_grant_read_completion_v2 completion{
        sizeof(completion), 2U, request_id, status, 0U, metadata, offset,
        success && !bytes.empty() ? bytes.data() : nullptr,
        success ? bytes.size() : 0U,
        static_cast<uint8_t>(success && eof), {}};
    return webscene_engine_complete_file_grant_read_request_v2(
        engine, &completion) != 0;
}

const webscene_file_grant_write_request_v2*
take_file_grant_write_request(webscene_engine* engine)
{
    const webscene_file_grant_write_request_v2* request = nullptr;
    for (int attempt = 0; attempt < 250 && request == nullptr; ++attempt) {
        request = webscene_engine_take_file_grant_write_request_v2(engine);
        if (request == nullptr)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(request != nullptr, "native file grant write request was not published");
    return request;
}

bool complete_file_grant_write(
    webscene_engine* engine,
    uint64_t request_id,
    uint32_t action,
    uint32_t status,
    const std::vector<uint8_t>& transaction = {},
    uint64_t offset = 0,
    size_t byte_count = 0,
    uint64_t final_byte_count = 0)
{
    const bool committed = status == WEBSCENE_FILE_GRANT_WRITE_SUCCESS_V2
        && action == WEBSCENE_FILE_GRANT_WRITE_COMMIT_V2;
    const webscene_file_grant_metadata_v2 metadata = committed
        ? webscene_file_grant_metadata_v2{sizeof(metadata), 2U,
            final_byte_count, 1700000000999000000LL,
            WEBSCENE_FILE_PANEL_ENTRY_FILE_V2, 0U}
        : webscene_file_grant_metadata_v2{};
    const webscene_file_grant_write_completion_v2 completion{
        sizeof(completion), 2U, request_id, status, action,
        {transaction.empty() ? nullptr : transaction.data(), transaction.size()},
        metadata, offset, byte_count};
    return webscene_engine_complete_file_grant_write_request_v2(
        engine, &completion) != 0;
}

const webscene_file_grant_directory_request_v2*
take_file_grant_directory_request(webscene_engine* engine)
{
    const webscene_file_grant_directory_request_v2* request = nullptr;
    for (int attempt = 0; attempt < 250 && request == nullptr; ++attempt) {
        request = webscene_engine_take_file_grant_directory_request_v2(engine);
        if (request == nullptr)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(request != nullptr,
        "native file grant directory request was not published");
    return request;
}

const webscene_file_grant_release_request_v2*
take_file_grant_release_request(webscene_engine* engine)
{
    const webscene_file_grant_release_request_v2* request = nullptr;
    for (int attempt = 0; attempt < 250 && request == nullptr; ++attempt) {
        request = webscene_engine_take_file_grant_release_request_v2(engine);
        if (request == nullptr)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(request != nullptr, "native file grant release was not published");
    return request;
}

const webscene_file_grant_create_file_request_v2*
take_file_grant_create_file_request(webscene_engine* engine)
{
    const webscene_file_grant_create_file_request_v2* request = nullptr;
    for (int attempt = 0; attempt < 250 && request == nullptr; ++attempt) {
        request = webscene_engine_take_file_grant_create_file_request_v2(engine);
        if (request == nullptr)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(request != nullptr,
        "native file grant create-file request was not published");
    return request;
}

const webscene_file_grant_create_directory_request_v2*
take_file_grant_create_directory_request(webscene_engine* engine)
{
    const webscene_file_grant_create_directory_request_v2* request = nullptr;
    for (int attempt = 0; attempt < 250 && request == nullptr; ++attempt) {
        request = webscene_engine_take_file_grant_create_directory_request_v2(engine);
        if (request == nullptr)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(request != nullptr,
        "native file grant create-directory request was not published");
    return request;
}

const webscene_file_grant_remove_request_v2*
take_file_grant_remove_request(webscene_engine* engine)
{
    const webscene_file_grant_remove_request_v2* request = nullptr;
    for (int attempt = 0; attempt < 250 && request == nullptr; ++attempt) {
        request = webscene_engine_take_file_grant_remove_request_v2(engine);
        if (request == nullptr)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(request != nullptr,
        "native file grant remove request was not published");
    return request;
}

struct directory_completion_entry final {
    std::string name;
    std::vector<uint8_t> grant;
    uint32_t kind{WEBSCENE_FILE_PANEL_ENTRY_FILE_V2};
    uint32_t capabilities{WEBSCENE_FILE_PANEL_GRANT_READ_V2};
};

bool complete_file_grant_directory(
    webscene_engine* engine,
    uint64_t request_id,
    uint32_t action,
    uint32_t status,
    const std::vector<directory_completion_entry>& source_entries = {},
    const std::vector<uint8_t>& cursor = {},
    uint64_t skipped_symlinks = 0)
{
    std::vector<webscene_file_grant_directory_entry_v2> entries;
    entries.reserve(source_entries.size());
    for (const auto& source : source_entries) {
        entries.push_back({sizeof(webscene_file_grant_directory_entry_v2), 2U,
            {sizeof(webscene_file_grant_metadata_v2), 2U,
                source.kind == WEBSCENE_FILE_PANEL_ENTRY_FILE_V2 ? 3U : 0U,
                1700000000999000000LL, source.kind, 0U},
            source.capabilities, 0U,
            {source.name.data(), source.name.size()},
            {source.grant.data(), source.grant.size()}});
    }
    const webscene_file_grant_directory_completion_v2 completion{
        sizeof(completion), 2U, request_id, status, action,
        entries.empty() ? nullptr : entries.data(), entries.size(),
        {cursor.empty() ? nullptr : cursor.data(), cursor.size()},
        skipped_symlinks};
    return webscene_engine_complete_file_grant_directory_request_v2(
        engine, &completion) != 0;
}

bool complete_file_grant_create_file(
    webscene_engine* engine, uint64_t request_id, uint32_t status,
    std::string_view name = {}, const std::vector<uint8_t>& grant = {},
    uint64_t byte_count = 0)
{
    webscene_file_grant_create_file_completion_v2 completion{};
    completion.struct_size = sizeof(completion);
    completion.version = 2U;
    completion.request_id = request_id;
    completion.status = status;
    if (status == WEBSCENE_FILE_GRANT_CREATE_FILE_SUCCESS_V2) {
        completion.capabilities = WEBSCENE_FILE_PANEL_GRANT_READ_V2
            | WEBSCENE_FILE_PANEL_GRANT_WRITE_V2;
        completion.metadata = {sizeof(webscene_file_grant_metadata_v2), 2U,
            byte_count, 1700000000999000000LL,
            WEBSCENE_FILE_PANEL_ENTRY_FILE_V2, 0U};
        completion.display_name = {name.data(), name.size()};
        completion.grant_id = {grant.data(), grant.size()};
    }
    return webscene_engine_complete_file_grant_create_file_request_v2(
        engine, &completion) != 0;
}

bool complete_file_grant_create_directory(
    webscene_engine* engine, uint64_t request_id, uint32_t status,
    std::string_view name = {}, const std::vector<uint8_t>& grant = {})
{
    webscene_file_grant_create_directory_completion_v2 completion{};
    completion.struct_size = sizeof(completion);
    completion.version = 2U;
    completion.request_id = request_id;
    completion.status = status;
    if (status == WEBSCENE_FILE_GRANT_CREATE_DIRECTORY_SUCCESS_V2) {
        completion.capabilities = WEBSCENE_FILE_PANEL_GRANT_READ_V2
            | WEBSCENE_FILE_PANEL_GRANT_WRITE_V2
            | WEBSCENE_FILE_PANEL_GRANT_ENUMERATE_V2
            | WEBSCENE_FILE_PANEL_GRANT_CREATE_V2;
        completion.metadata = {sizeof(webscene_file_grant_metadata_v2), 2U,
            0U, 1700000000999000000LL,
            WEBSCENE_FILE_PANEL_ENTRY_DIRECTORY_V2, 0U};
        completion.display_name = {name.data(), name.size()};
        completion.grant_id = {grant.data(), grant.size()};
    }
    return webscene_engine_complete_file_grant_create_directory_request_v2(
        engine, &completion) != 0;
}

bool complete_file_grant_remove(
    webscene_engine* engine, uint64_t request_id, uint32_t status)
{
    const webscene_file_grant_remove_completion_v2 completion{
        sizeof(completion), 2U, request_id, status, 0U};
    return webscene_engine_complete_file_grant_remove_request_v2(
        engine, &completion) != 0;
}


void activate_file_picker(webscene_engine* engine, uint64_t sequence)
{
    pointer_button(
        engine, WEBSCENE_INPUT_POINTER_DOWN, 1, 1, sequence, true);
    require(evaluate(engine, "true", "file-picker-activation-barrier.js") == "true",
        "file picker activation barrier did not drain");
}

bool complete_file_panel_success(
    webscene_engine* engine,
    uint64_t request_id,
    uint32_t kind,
    uint32_t capabilities,
    std::string_view name,
    const std::vector<uint8_t>& grant)
{
    const webscene_file_panel_entry_v2 entry{
        sizeof(entry), 2U, kind, capabilities,
        {name.data(), name.size()}, {grant.data(), grant.size()}};
    const webscene_file_panel_completion_v2 completion{
        sizeof(completion), 2U, request_id,
        WEBSCENE_FILE_PANEL_SUCCESS_V2, 0U,
        &entry, 1U, {}, {}};
    return webscene_engine_complete_file_panel_request_v2(engine, &completion) != 0;
}

bool complete_file_panel_status(
    webscene_engine* engine,
    uint64_t request_id,
    uint32_t status,
    std::string_view message = {})
{
    const std::string_view code = status == WEBSCENE_FILE_PANEL_CANCELLED_V2
        ? std::string_view{} : std::string_view{"native-error"};
    if (status == WEBSCENE_FILE_PANEL_CANCELLED_V2) message = {};
    const webscene_file_panel_completion_v2 completion{
        sizeof(completion), 2U, request_id, status, 0U, nullptr, 0U,
        {code.data(), code.size()}, {message.data(), message.size()}};
    return webscene_engine_complete_file_panel_request_v2(engine, &completion) != 0;
}

void test_native_file_system_access_picker_and_handle_contract()
{
    std::unique_ptr<webscene_engine, decltype(&webscene_engine_destroy)> engine(
        webscene_engine_create(0), webscene_engine_destroy);
    require(engine != nullptr, "file system access engine creation failed");
    execute(engine.get(), R"JS(
      globalThis.__pickerState = 'pending';
      showOpenFilePicker().then(
        () => { __pickerState = 'unexpected'; },
        error => { __pickerState = error.name; });
    )JS", "file-picker-activation-required.js");
    const auto activation_state = evaluate(engine.get(), "__pickerState",
        "file-picker-activation-required-result.js");
    require(activation_state == "\"SecurityError\"",
        "file picker did not require transient user activation: "
            + activation_state);
    require(webscene_engine_take_file_panel_request_v2(engine.get()) == nullptr,
        "file picker published a request without activation");

    activate_file_picker(engine.get(), 50001U);
    execute(engine.get(), R"JS(
      globalThis.__pickerState = 'pending';
      globalThis.__pickerReplay = 'pending';
      showOpenFilePicker({
        multiple: true,
        startIn: 'documents',
        excludeAcceptAllOption: true,
        types: [{
          description: 'Drawings',
          accept: {
            'application/json': ['.json'],
            'image/*': ['.png', '.jpg']
          }
        }]
      }).then(handles => {
        globalThis.__openHandles = handles;
        let modeReads = 0;
        __pickerState = {
          length: handles.length,
          kinds: handles.map(handle => handle.kind),
          names: handles.map(handle => handle.name),
          instance: handles.every(handle => handle instanceof FileSystemFileHandle),
          base: handles.every(handle => handle instanceof FileSystemHandle),
          tag: Object.prototype.toString.call(handles[0]),
          permissions: null
        };
        return Promise.all([
          handles[0].queryPermission(),
          handles[0].queryPermission(undefined),
          handles[0].queryPermission(null),
          handles[0].queryPermission({get mode() {
            ++modeReads;
            return 'read';
          }}).then(value => `${value}:${modeReads}`),
          handles[0].queryPermission({mode:'readwrite'}),
          handles[0].requestPermission(),
          handles[0].requestPermission({mode:'readwrite'}),
          handles[0].queryPermission({mode:''}).then(
            () => 'unexpected', error => error.name),
          handles[0].queryPermission({mode:null}).then(
            () => 'unexpected', error => error.name),
          handles[0].queryPermission({mode:0}).then(
            () => 'unexpected', error => error.name),
          FileSystemHandle.prototype.queryPermission.call({}).then(
            () => 'unexpected', error => error.name)
        ]).then(values => {
          __pickerState.permissions = values;
        });
      }, error => { __pickerState = error.name; });
      showOpenFilePicker().then(
        () => { __pickerReplay = 'unexpected'; },
        error => { __pickerReplay = error.name; });
    )JS", "file-picker-open.js");
    auto* request = take_file_panel_request(engine.get());
    require(request->kind == WEBSCENE_FILE_PANEL_OPEN_FILE_V2
            && (request->flags & WEBSCENE_FILE_PANEL_ALLOW_MULTIPLE_V2) != 0U
            && (request->flags & WEBSCENE_FILE_PANEL_EXCLUDE_ACCEPT_ALL_V2) != 0U
            && request->maximum_selection_count == 64U
            && request->filter_count == 1U
            && request->filters[0].mime_type_count == 2U
            && request->filters[0].extension_count == 3U
            && std::string_view(
                reinterpret_cast<const char*>(request->initial_location_token.data),
                request->initial_location_token.byte_count)
                == "well-known:documents",
        "open picker options changed in the native request");
    const auto open_id = request->request_id;
    webscene_file_panel_request_release_v2(request);
    const std::vector<uint8_t> first_grant{1U, 0U, 2U, 255U};
    const std::vector<uint8_t> second_grant{3U, 4U, 0U, 5U};
    const std::string first_name{"café.json"};
    const std::string second_name{"empty.json"};
    const webscene_file_panel_entry_v2 entries[]{
        {sizeof(webscene_file_panel_entry_v2), 2U,
            WEBSCENE_FILE_PANEL_ENTRY_FILE_V2,
            WEBSCENE_FILE_PANEL_GRANT_READ_V2,
            {first_name.data(), first_name.size()},
            {first_grant.data(), first_grant.size()}},
        {sizeof(webscene_file_panel_entry_v2), 2U,
            WEBSCENE_FILE_PANEL_ENTRY_FILE_V2,
            WEBSCENE_FILE_PANEL_GRANT_READ_V2,
            {second_name.data(), second_name.size()},
            {second_grant.data(), second_grant.size()}}};
    const webscene_file_panel_completion_v2 opened{
        sizeof(opened), 2U, open_id, WEBSCENE_FILE_PANEL_SUCCESS_V2, 0U,
        entries, 2U, {}, {}};
    require(webscene_engine_complete_file_panel_request_v2(engine.get(), &opened) != 0,
        "open picker completion was rejected");
    std::string opened_state;
    for (int attempt = 0; attempt < 100; ++attempt) {
        opened_state = evaluate(engine.get(), "__pickerState",
            "file-picker-open-result.js");
        if (opened_state.find("café.json") != std::string::npos) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(opened_state.find("\"length\":2") != std::string::npos
            && opened_state.find("\"instance\":true") != std::string::npos
            && opened_state.find("\"base\":true") != std::string::npos
            && opened_state.find("[object FileSystemFileHandle]") != std::string::npos
            && opened_state.find("\"permissions\":[\"granted\",\"granted\","
                "\"granted\",\"granted:1\",\"denied\",\"granted\",\"denied\","
                "\"TypeError\",\"TypeError\",\"TypeError\",\"TypeError\"]")
                != std::string::npos,
        "open picker handle identity or permission contract changed: " + opened_state);
    require(evaluate(engine.get(), "__pickerReplay",
                "file-picker-replay-result.js") == "\"SecurityError\"",
        "file picker user activation was replayed");

    activate_file_picker(engine.get(), 50002U);
    execute(engine.get(), R"JS(
      globalThis.__repeatedHandle = null;
      showOpenFilePicker().then(handles => { __repeatedHandle = handles[0]; });
    )JS", "file-picker-repeated-entry.js");
    request = take_file_panel_request(engine.get());
    const auto repeated_id = request->request_id;
    webscene_file_panel_request_release_v2(request);
    const std::vector<uint8_t> repeated_grant{12U, 0U, 13U, 255U};
    require(complete_file_panel_success(
        engine.get(), repeated_id, WEBSCENE_FILE_PANEL_ENTRY_FILE_V2,
        WEBSCENE_FILE_PANEL_GRANT_READ_V2, first_name, repeated_grant),
        "repeated-entry picker completion was rejected");
    execute(engine.get(), R"JS(
      globalThis.__sameEntryState = 'pending';
      Promise.all([
        __openHandles[0].isSameEntry(__repeatedHandle),
        __openHandles[0].isSameEntry(__openHandles[1]),
        __openHandles[0].isSameEntry(__openHandles[0]),
        __openHandles[0].isSameEntry({}).then(
          () => 'unexpected', error => error.name)
      ]).then(values => { __sameEntryState = values; });
    )JS", "file-picker-same-entry.js");
    for (int comparison_index = 0; comparison_index < 3; ++comparison_index) {
        const auto* comparison = take_file_grant_same_entry_request(engine.get());
        require(comparison->first_grant_id.byte_count == first_grant.size()
                && std::memcmp(comparison->first_grant_id.data,
                    first_grant.data(), first_grant.size()) == 0,
            "same-entry comparison exposed the wrong first grant");
        const auto token_equals = [&](const std::vector<uint8_t>& expected) {
            return comparison->second_grant_id.byte_count == expected.size()
                && std::memcmp(comparison->second_grant_id.data,
                    expected.data(), expected.size()) == 0;
        };
        const bool same_entry =
            token_equals(repeated_grant) || token_equals(first_grant);
        require(same_entry || token_equals(second_grant),
            "same-entry comparison exposed an unknown opaque grant");
        const auto comparison_id = comparison->request_id;
        webscene_file_grant_same_entry_request_release_v2(comparison);
        require(complete_file_grant_same_entry(
            engine.get(), comparison_id, true, same_entry),
            "same-entry native authority result was rejected");
    }
    require(evaluate(engine.get(), "__sameEntryState",
                "file-picker-same-entry-result.js")
            == "[true,false,true,\"TypeError\"]",
        "same-entry did not use opaque native identity");

    constexpr int64_t file_modified_ns = 1700000000123000000LL;
    const std::vector<uint8_t> file_bytes{0U, 1U, 2U, 255U, 65U, 66U};
    execute(engine.get(), R"JS(
      globalThis.__getFileState='pending';
      __openHandles[0].getFile().then(file => {
        globalThis.__readFile=file;
        __getFileState={name:file.name,size:file.size,
          modified:file.lastModified,instance:file instanceof File};
      }, error => { __getFileState=error.name; });
    )JS", "file-handle-get-file.js");
    auto* read_request = take_file_grant_read_request(engine.get());
    require(read_request->offset == 0U && read_request->maximum_bytes == 1U
            && read_request->grant_id.byte_count == first_grant.size()
            && std::memcmp(read_request->grant_id.data,
                first_grant.data(), first_grant.size()) == 0,
        "getFile metadata read lost its bounded opaque grant request");
    const auto metadata_read_id = read_request->request_id;
    webscene_file_grant_read_request_release_v2(read_request);
    require(complete_file_grant_read(engine.get(), metadata_read_id,
            WEBSCENE_FILE_GRANT_READ_SUCCESS_V2, file_bytes.size(),
            file_modified_ns, 0U, {file_bytes[0]}, false),
        "getFile metadata completion was rejected");
    const auto get_file_state = evaluate(engine.get(), "__getFileState",
        "file-handle-get-file-result.js");
    require(get_file_state.find("\"name\":\"café.json\"") != std::string::npos
            && get_file_state.find("\"size\":6") != std::string::npos
            && get_file_state.find("\"modified\":1700000000123") != std::string::npos
            && get_file_state.find("\"instance\":true") != std::string::npos,
        "getFile did not expose immutable File metadata: " + get_file_state);

    execute(engine.get(), R"JS(
      globalThis.__fileBytes='pending';
      __readFile.arrayBuffer().then(buffer => {
        __fileBytes=Array.from(new Uint8Array(buffer));
      }, error => { __fileBytes=error.name; });
    )JS", "file-handle-get-file-bytes.js");
    read_request = take_file_grant_read_request(engine.get());
    require(read_request->offset == 0U
            && read_request->maximum_bytes == file_bytes.size(),
        "File.arrayBuffer did not request an exact bounded range");
    const auto bytes_read_id = read_request->request_id;
    webscene_file_grant_read_request_release_v2(read_request);
    require(complete_file_grant_read(engine.get(), bytes_read_id,
            WEBSCENE_FILE_GRANT_READ_SUCCESS_V2, file_bytes.size(),
            file_modified_ns, 0U, file_bytes, true),
        "File.arrayBuffer completion was rejected");
    require(evaluate(engine.get(), "__fileBytes",
                "file-handle-get-file-bytes-result.js")
            == "[0,1,2,255,65,66]",
        "File.arrayBuffer did not preserve binary bytes");

    execute(engine.get(), R"JS(
      globalThis.__fileSlice='pending';
      __readFile.slice(1,5).arrayBuffer().then(buffer => {
        __fileSlice=Array.from(new Uint8Array(buffer));
      }, error => { __fileSlice=error.name; });
    )JS", "file-handle-get-file-slice.js");
    read_request = take_file_grant_read_request(engine.get());
    require(read_request->offset == 1U && read_request->maximum_bytes == 4U,
        "File.slice did not preserve the native range offset");
    const auto slice_read_id = read_request->request_id;
    webscene_file_grant_read_request_release_v2(read_request);
    require(complete_file_grant_read(engine.get(), slice_read_id,
            WEBSCENE_FILE_GRANT_READ_SUCCESS_V2, file_bytes.size(),
            file_modified_ns, 1U, {1U, 2U, 255U, 65U}, false),
        "File.slice completion was rejected");
    require(evaluate(engine.get(), "__fileSlice",
                "file-handle-get-file-slice-result.js") == "[1,2,255,65]",
        "File.slice did not preserve binary bytes");

    constexpr std::size_t streamed_size = 64U * 1024U + 1U;
    std::vector<uint8_t> streamed_bytes(streamed_size);
    uint64_t streamed_sum{};
    for (std::size_t index = 0; index < streamed_bytes.size(); ++index) {
        streamed_bytes[index] = static_cast<uint8_t>(index % 251U);
        streamed_sum += streamed_bytes[index];
    }
    execute(engine.get(), R"JS(
      globalThis.__streamedFile='pending';
      __openHandles[0].getFile().then(async file => {
        const reader=file.stream().getReader();
        let length=0,sum=0;
        for (;;) {
          const {done,value}=await reader.read();
          if (done) break;
          length += value.byteLength;
          for (const byte of value) sum += byte;
        }
        __streamedFile={length,sum};
      }, error => { __streamedFile=error.name; });
    )JS", "file-handle-get-file-stream.js");
    read_request = take_file_grant_read_request(engine.get());
    const auto stream_metadata_id = read_request->request_id;
    webscene_file_grant_read_request_release_v2(read_request);
    require(complete_file_grant_read(engine.get(), stream_metadata_id,
            WEBSCENE_FILE_GRANT_READ_SUCCESS_V2, streamed_bytes.size(),
            file_modified_ns, 0U, {streamed_bytes[0]}, false),
        "stream metadata completion was rejected");
    read_request = take_file_grant_read_request(engine.get());
    require(read_request->offset == 0U && read_request->maximum_bytes == 64U * 1024U,
        "File.stream did not enforce the 64 KiB backpressure chunk");
    const auto first_stream_read_id = read_request->request_id;
    webscene_file_grant_read_request_release_v2(read_request);
    const std::vector<uint8_t> first_stream_chunk(
        streamed_bytes.begin(), streamed_bytes.begin() + 64U * 1024U);
    require(complete_file_grant_read(engine.get(), first_stream_read_id,
            WEBSCENE_FILE_GRANT_READ_SUCCESS_V2, streamed_bytes.size(),
            file_modified_ns, 0U, first_stream_chunk, false),
        "first stream chunk completion was rejected");
    read_request = take_file_grant_read_request(engine.get());
    require(read_request->offset == 64U * 1024U
            && read_request->maximum_bytes == 1U,
        "File.stream did not advance its bounded native range");
    const auto final_stream_read_id = read_request->request_id;
    webscene_file_grant_read_request_release_v2(read_request);
    require(complete_file_grant_read(engine.get(), final_stream_read_id,
            WEBSCENE_FILE_GRANT_READ_SUCCESS_V2, streamed_bytes.size(),
            file_modified_ns, 64U * 1024U, {streamed_bytes.back()}, true),
        "final stream chunk completion was rejected");
    require(evaluate(engine.get(), "__streamedFile.length",
                "file-handle-get-file-stream-length.js")
                == std::to_string(streamed_size)
            && evaluate(engine.get(), "__streamedFile.sum",
                "file-handle-get-file-stream-sum.js")
                == std::to_string(streamed_sum),
        "File.stream did not preserve ordered bytes");

    execute(engine.get(), R"JS(
      globalThis.__mutatedFile='pending';
      __openHandles[0].getFile().then(file => {
        globalThis.__mutationSnapshot=file;
        __mutatedFile='acquired';
      });
    )JS", "file-handle-get-file-mutation-acquire.js");
    read_request = take_file_grant_read_request(engine.get());
    const auto mutation_metadata_id = read_request->request_id;
    webscene_file_grant_read_request_release_v2(read_request);
    require(complete_file_grant_read(engine.get(), mutation_metadata_id,
            WEBSCENE_FILE_GRANT_READ_SUCCESS_V2, file_bytes.size(),
            file_modified_ns, 0U, {file_bytes[0]}, false),
        "mutation snapshot metadata completion was rejected");
    require(evaluate(engine.get(), "__mutatedFile",
                "file-handle-get-file-mutation-acquired.js") == "\"acquired\"",
        "mutation snapshot was not acquired");
    execute(engine.get(), R"JS(
      __mutatedFile='pending';
      __mutationSnapshot.text().then(
        () => { __mutatedFile='unexpected'; },
        error => { __mutatedFile=error.name; });
    )JS", "file-handle-get-file-mutation-read.js");
    read_request = take_file_grant_read_request(engine.get());
    const auto mutation_read_id = read_request->request_id;
    webscene_file_grant_read_request_release_v2(read_request);
    require(complete_file_grant_read(engine.get(), mutation_read_id,
            WEBSCENE_FILE_GRANT_READ_SUCCESS_V2, file_bytes.size(),
            file_modified_ns + 1, 0U, file_bytes, true),
        "mutation read completion was rejected");
    require(evaluate(engine.get(), "__mutatedFile",
                "file-handle-get-file-mutation-result.js")
            == "\"NotReadableError\"",
        "a mutated native file did not invalidate its File snapshot");

    struct read_status_case { uint32_t status; const char* expected_name; };
    constexpr read_status_case read_status_cases[] = {
        {WEBSCENE_FILE_GRANT_READ_CANCELLED_V2, "AbortError"},
        {WEBSCENE_FILE_GRANT_READ_DENIED_V2, "NotAllowedError"},
        {WEBSCENE_FILE_GRANT_READ_NOT_FOUND_V2, "NotFoundError"},
        {WEBSCENE_FILE_GRANT_READ_IO_ERROR_V2, "NotReadableError"}};
    for (const auto& status_case : read_status_cases) {
        execute(engine.get(), R"JS(
          globalThis.__readFailure='pending';
          __openHandles[0].getFile().then(
            () => { __readFailure='unexpected'; },
            error => { __readFailure=error.name; });
        )JS", "file-handle-get-file-status.js");
        read_request = take_file_grant_read_request(engine.get());
        const auto read_id = read_request->request_id;
        webscene_file_grant_read_request_release_v2(read_request);
        require(complete_file_grant_read(engine.get(), read_id,
                status_case.status, 0U, 0, 0U, {}, false),
            "native file read error completion was rejected");
        require(evaluate(engine.get(), "__readFailure",
                    "file-handle-get-file-status-result.js")
                == std::string("\"") + status_case.expected_name + "\"",
            "native file read status mapped to the wrong DOMException");
        require(!complete_file_grant_read(engine.get(), read_id,
                status_case.status, 0U, 0, 0U, {}, false),
            "native file read completed more than once");
    }

    activate_file_picker(engine.get(), 50003U);
    execute(engine.get(), R"JS(
      globalThis.__directoryState = 'pending';
      showDirectoryPicker({ mode: 'readwrite', startIn: __openHandles[0] }).then(
        handle => {
          globalThis.__directoryHandle = handle;
          return Promise.all([
            handle.queryPermission(),
            handle.queryPermission({mode:'readwrite'}),
            handle.isSameEntry(handle)
          ]).then(values => { __directoryState = {
            kind: handle.kind, name: handle.name,
            instance: handle instanceof FileSystemDirectoryHandle,
            values
          }; });
        }, error => { __directoryState = error.name; });
    )JS", "file-picker-directory.js");
    request = take_file_panel_request(engine.get());
    require(request->kind == WEBSCENE_FILE_PANEL_OPEN_DIRECTORY_V2
            && (request->flags & WEBSCENE_FILE_PANEL_REQUEST_WRITE_V2) != 0U
            && request->initial_location_token.byte_count == first_grant.size()
            && std::memcmp(request->initial_location_token.data,
                first_grant.data(), first_grant.size()) == 0,
        "directory picker mode or opaque start handle changed");
    const auto directory_id = request->request_id;
    webscene_file_panel_request_release_v2(request);
    const std::vector<uint8_t> directory_grant{7U, 0U, 8U, 9U};
    require(complete_file_panel_success(
        engine.get(), directory_id, WEBSCENE_FILE_PANEL_ENTRY_DIRECTORY_V2,
        WEBSCENE_FILE_PANEL_GRANT_READ_V2
            | WEBSCENE_FILE_PANEL_GRANT_WRITE_V2
            | WEBSCENE_FILE_PANEL_GRANT_ENUMERATE_V2,
        "workspace", directory_grant),
        "directory picker completion was rejected");
    const auto* directory_comparison =
        take_file_grant_same_entry_request(engine.get());
    require(directory_comparison->first_grant_id.byte_count
                == directory_grant.size()
            && directory_comparison->second_grant_id.byte_count
                == directory_grant.size(),
        "directory same-entry query lost its opaque native grant");
    const auto directory_comparison_id = directory_comparison->request_id;
    webscene_file_grant_same_entry_request_release_v2(directory_comparison);
    require(complete_file_grant_same_entry(
        engine.get(), directory_comparison_id, true, true),
        "directory same-entry native authority result was rejected");
    std::string directory_state;
    for (int attempt = 0; attempt < 100; ++attempt) {
        directory_state = evaluate(engine.get(), "__directoryState",
            "file-picker-directory-result.js");
        if (directory_state.find("workspace") != std::string::npos) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(directory_state.find("\"instance\":true") != std::string::npos
            && directory_state.find("[\"granted\",\"granted\",true]")
                != std::string::npos,
        "directory picker handle contract changed: " + directory_state);

    execute(engine.get(), R"JS(
      globalThis.__directoryIteration='pending';
      (async()=>{
        const values=[];
        for await (const [name, handle] of __directoryHandle) {
          values.push(`${name}:${handle.kind}:${handle.name}`);
        }
        __directoryIteration=values.join('|');
      })().catch(error=>{__directoryIteration=error.name});
    )JS", "file-directory-iteration.js");
    auto* directory_request = take_file_grant_directory_request(engine.get());
    require(directory_request->action
                == WEBSCENE_FILE_GRANT_DIRECTORY_ENUMERATE_V2
            && directory_request->directory_grant_id.byte_count
                == directory_grant.size()
            && directory_request->cursor.byte_count == 0
            && directory_request->maximum_entries == 32,
        "initial directory page lost grant or bounds");
    auto directory_request_id = directory_request->request_id;
    webscene_file_grant_directory_request_release_v2(directory_request);
    const std::vector<uint8_t> directory_cursor{31U, 32U};
    require(complete_file_grant_directory(engine.get(), directory_request_id,
            WEBSCENE_FILE_GRANT_DIRECTORY_ENUMERATE_V2,
            WEBSCENE_FILE_GRANT_DIRECTORY_SUCCESS_V2,
            {{"alpha.txt", {41U}, WEBSCENE_FILE_PANEL_ENTRY_FILE_V2,
                 WEBSCENE_FILE_PANEL_GRANT_READ_V2},
                {"資料", {42U}, WEBSCENE_FILE_PANEL_ENTRY_DIRECTORY_V2,
                 WEBSCENE_FILE_PANEL_GRANT_ENUMERATE_V2}},
            directory_cursor, 1U),
        "first directory page completion was rejected");
    directory_request = take_file_grant_directory_request(engine.get());
    require(directory_request->action
                == WEBSCENE_FILE_GRANT_DIRECTORY_ENUMERATE_V2
            && directory_request->cursor.byte_count == directory_cursor.size()
            && std::memcmp(directory_request->cursor.data,
                directory_cursor.data(), directory_cursor.size()) == 0,
        "directory continuation lost its opaque cursor");
    directory_request_id = directory_request->request_id;
    webscene_file_grant_directory_request_release_v2(directory_request);
    require(complete_file_grant_directory(engine.get(), directory_request_id,
            WEBSCENE_FILE_GRANT_DIRECTORY_ENUMERATE_V2,
            WEBSCENE_FILE_GRANT_DIRECTORY_SUCCESS_V2,
            {{"zeta.bin", {43U}, WEBSCENE_FILE_PANEL_ENTRY_FILE_V2,
                 WEBSCENE_FILE_PANEL_GRANT_READ_V2}}),
        "final directory page completion was rejected");
    const auto iteration_state = evaluate(engine.get(), "__directoryIteration",
        "file-directory-iteration-result.js");
    require(iteration_state
            == "\"alpha.txt:file:alpha.txt|資料:directory:資料|zeta.bin:file:zeta.bin\"",
        "async directory iteration changed: " + iteration_state);

    const auto large_directory_rss_before = file_system_access_test_peak_rss_bytes();
    const auto large_directory_started = std::chrono::steady_clock::now();
    execute(engine.get(), R"JS(
      globalThis.__largeDirectory='pending';
      (async()=>{
        let count=0;
        for await (const handle of __directoryHandle.values()) {
          if (!(handle instanceof FileSystemFileHandle)) throw new Error('kind');
          ++count;
        }
        __largeDirectory=count;
      })().catch(error=>{__largeDirectory=error.name});
    )JS", "file-directory-10000.js");
    for (uint32_t first = 0; first < 10000U; first += 32U) {
        directory_request = take_file_grant_directory_request(engine.get());
        require(directory_request->action
                == WEBSCENE_FILE_GRANT_DIRECTORY_ENUMERATE_V2,
            "large directory requested the wrong action");
        directory_request_id = directory_request->request_id;
        webscene_file_grant_directory_request_release_v2(directory_request);
        const auto count = std::min(32U, 10000U - first);
        std::vector<directory_completion_entry> page_entries;
        page_entries.reserve(count);
        for (uint32_t offset = 0; offset < count; ++offset) {
            const auto index = first + offset;
            page_entries.push_back({"entry-" + std::to_string(index),
                {50U, static_cast<uint8_t>(index & 0xffU),
                    static_cast<uint8_t>((index >> 8U) & 0xffU)},
                WEBSCENE_FILE_PANEL_ENTRY_FILE_V2,
                WEBSCENE_FILE_PANEL_GRANT_READ_V2});
        }
        const auto next = first + count < 10000U
            ? std::vector<uint8_t>{51U,
                static_cast<uint8_t>(first & 0xffU),
                static_cast<uint8_t>((first >> 8U) & 0xffU)}
            : std::vector<uint8_t>{};
        require(complete_file_grant_directory(engine.get(), directory_request_id,
                WEBSCENE_FILE_GRANT_DIRECTORY_ENUMERATE_V2,
                WEBSCENE_FILE_GRANT_DIRECTORY_SUCCESS_V2,
                page_entries, next),
            "large directory page completion was rejected");
    }
    require(evaluate(engine.get(), "__largeDirectory",
                "file-directory-10000-result.js") == "10000",
        "10,000-entry directory iteration did not complete");
    const auto large_directory_elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - large_directory_started).count();
    const auto large_directory_rss_after = file_system_access_test_peak_rss_bytes();
    require(large_directory_elapsed < 10.0,
        "10,000-entry directory iteration exceeded 10 seconds");
    require(large_directory_rss_before == 0 || large_directory_rss_after
            <= large_directory_rss_before + 64U * 1024U * 1024U,
        "10,000-entry directory iteration exceeded 64 MiB peak RSS growth");
    std::cout << "Directory paging gate: entries=10000 elapsedSeconds="
              << large_directory_elapsed << " rssDelta="
              << (large_directory_rss_after >= large_directory_rss_before
                    ? large_directory_rss_after - large_directory_rss_before : 0)
              << '\n';

    execute(engine.get(), R"JS(
      globalThis.__directoryLookup='pending';
      __directoryHandle.getFileHandle('alpha.txt').then(
        value=>{__directoryLookup=`${value.kind}:${value.name}`},
        error=>{__directoryLookup=error.name});
    )JS", "file-directory-lookup.js");
    directory_request = take_file_grant_directory_request(engine.get());
    directory_request_id = directory_request->request_id;
    webscene_file_grant_directory_request_release_v2(directory_request);
    const std::vector<uint8_t> lookup_cursor{44U};
    require(complete_file_grant_directory(engine.get(), directory_request_id,
            WEBSCENE_FILE_GRANT_DIRECTORY_ENUMERATE_V2,
            WEBSCENE_FILE_GRANT_DIRECTORY_SUCCESS_V2,
            {{"alpha.txt", {41U}, WEBSCENE_FILE_PANEL_ENTRY_FILE_V2,
                 WEBSCENE_FILE_PANEL_GRANT_READ_V2}}, lookup_cursor),
        "directory lookup page completion was rejected");
    directory_request = take_file_grant_directory_request(engine.get());
    require(directory_request->action
                == WEBSCENE_FILE_GRANT_DIRECTORY_RELEASE_CURSOR_V2
            && directory_request->directory_grant_id.byte_count == 0
            && directory_request->cursor.byte_count == lookup_cursor.size(),
        "successful lookup did not release its unfinished cursor");
    directory_request_id = directory_request->request_id;
    webscene_file_grant_directory_request_release_v2(directory_request);
    require(complete_file_grant_directory(engine.get(), directory_request_id,
            WEBSCENE_FILE_GRANT_DIRECTORY_RELEASE_CURSOR_V2,
            WEBSCENE_FILE_GRANT_DIRECTORY_SUCCESS_V2),
        "lookup cursor release completion was rejected");
    require(evaluate(engine.get(), "__directoryLookup",
                "file-directory-lookup-result.js")
            == "\"file:alpha.txt\"",
        "existing file lookup changed");

    execute(engine.get(), R"JS(
      globalThis.__createdFile='pending';
      __directoryHandle.getFileHandle('created-🙂.txt', {create:true}).then(
        value=>{globalThis.__createdHandle=value;
          __createdFile=`${value.kind}:${value.name}`},
        error=>{__createdFile=error.name});
    )JS", "file-directory-create-file.js");
    auto* create_file_request =
        take_file_grant_create_file_request(engine.get());
    require(create_file_request->directory_grant_id.byte_count
                == directory_grant.size()
            && std::memcmp(create_file_request->directory_grant_id.data,
                directory_grant.data(), directory_grant.size()) == 0
            && std::string_view(create_file_request->display_name.data,
                   create_file_request->display_name.byte_count)
                == "created-🙂.txt",
        "create-file request lost its parent grant or UTF-8 name");
    auto create_file_request_id = create_file_request->request_id;
    webscene_file_grant_create_file_request_release_v2(create_file_request);
    const std::vector<uint8_t> created_grant{52U, 53U};
    require(complete_file_grant_create_file(engine.get(),
                create_file_request_id,
                WEBSCENE_FILE_GRANT_CREATE_FILE_SUCCESS_V2,
                "created-🙂.txt", created_grant),
        "create-file success completion was rejected");
    require(evaluate(engine.get(), "__createdFile",
                "file-directory-create-file-result.js")
            == "\"file:created-🙂.txt\"",
        "create=true did not produce a file handle");

    execute(engine.get(), R"JS(
      globalThis.__createdContents='pending';
      __createdHandle.getFile().then(
        file=>file.text()).then(value=>{__createdContents=value},
        error=>{__createdContents=error.name});
    )JS", "file-directory-created-read.js");
    auto* created_read = take_file_grant_read_request(engine.get());
    require(created_read->grant_id.byte_count == created_grant.size()
            && std::memcmp(created_read->grant_id.data, created_grant.data(),
                created_grant.size()) == 0,
        "created handle did not retain its derived opaque grant");
    const auto created_read_id = created_read->request_id;
    webscene_file_grant_read_request_release_v2(created_read);
    const std::vector<uint8_t> preserved_bytes{'p','r','e','s','e','r','v','e','d'};
    require(complete_file_grant_read(engine.get(), created_read_id,
                WEBSCENE_FILE_GRANT_READ_SUCCESS_V2, preserved_bytes.size(),
                1700000000999000000LL, 0,
                {preserved_bytes[0]}, false),
        "created handle metadata completion was rejected");
    created_read = take_file_grant_read_request(engine.get());
    require(created_read->offset == 0
            && created_read->maximum_bytes == preserved_bytes.size(),
        "created File text read did not request its exact bounded range");
    const auto created_contents_read_id = created_read->request_id;
    webscene_file_grant_read_request_release_v2(created_read);
    require(complete_file_grant_read(engine.get(), created_contents_read_id,
                WEBSCENE_FILE_GRANT_READ_SUCCESS_V2, preserved_bytes.size(),
                1700000000999000000LL, 0, preserved_bytes, true),
        "created handle contents completion was rejected");
    require(evaluate(engine.get(), "__createdContents",
                "file-directory-created-read-result.js") == "\"preserved\"",
        "get-or-create handle did not preserve native file contents");

    struct create_file_status_case {
        uint32_t status;
        const char* expected_name;
    };
    constexpr create_file_status_case create_file_status_cases[] = {
        {WEBSCENE_FILE_GRANT_CREATE_FILE_CANCELLED_V2, "AbortError"},
        {WEBSCENE_FILE_GRANT_CREATE_FILE_DENIED_V2, "NotAllowedError"},
        {WEBSCENE_FILE_GRANT_CREATE_FILE_NOT_FOUND_V2, "NotFoundError"},
        {WEBSCENE_FILE_GRANT_CREATE_FILE_TYPE_MISMATCH_V2,
            "TypeMismatchError"},
        {WEBSCENE_FILE_GRANT_CREATE_FILE_CHANGED_V2, "InvalidStateError"},
        {WEBSCENE_FILE_GRANT_CREATE_FILE_IO_ERROR_V2, "NotReadableError"},
        {WEBSCENE_FILE_GRANT_CREATE_FILE_LIMIT_V2, "QuotaExceededError"}};
    for (const auto& status_case : create_file_status_cases) {
        execute(engine.get(), R"JS(
          globalThis.__createFileFailure='pending';
          __directoryHandle.getFileHandle('failure.txt', {create:true}).then(
            ()=>{__createFileFailure='unexpected'},
            error=>{__createFileFailure=error.name});
        )JS", "file-directory-create-file-status.js");
        create_file_request = take_file_grant_create_file_request(engine.get());
        create_file_request_id = create_file_request->request_id;
        webscene_file_grant_create_file_request_release_v2(create_file_request);
        require(complete_file_grant_create_file(engine.get(),
                    create_file_request_id, status_case.status),
            "create-file error completion was rejected");
        require(evaluate(engine.get(), "__createFileFailure",
                    "file-directory-create-file-status-result.js")
                == std::string("\"") + status_case.expected_name + "\"",
            "create-file status mapped to the wrong DOMException");
        require(!complete_file_grant_create_file(engine.get(),
                    create_file_request_id, status_case.status),
            "create-file operation completed more than once");
    }
    execute(engine.get(), "globalThis.__createdHandle=null",
        "file-directory-created-release.js");
    for (int attempt = 0; attempt < 3; ++attempt) {
        require(webscene_engine_request_low_memory(engine.get()) != 0
                && evaluate(engine.get(), "true",
                    "file-directory-created-release-gc.js") == "true",
            "created file handle collection barrier failed");
    }
    std::size_t created_release_count = 0;
    while (const auto* released =
        webscene_engine_take_file_grant_release_request_v2(engine.get())) {
        if (released->grant_id.byte_count == created_grant.size()
            && std::memcmp(released->grant_id.data,
                created_grant.data(), created_grant.size()) == 0) {
            ++created_release_count;
        }
        webscene_file_grant_release_request_release_v2(released);
    }
    require(created_release_count == 1,
        "created wrapper release count changed: "
            + std::to_string(created_release_count));
    if (std::getenv("WEBSCENE_TEST_FILE_GRANT_CREATE_FILE_ONLY") != nullptr)
        return;

    execute(engine.get(), R"JS(
      globalThis.__createdDirectory='pending';
      __directoryHandle.getDirectoryHandle('created-🙂', {create:true}).then(
        value=>{globalThis.__createdDirectoryHandle=value;
          __createdDirectory=`${value.kind}:${value.name}`},
        error=>{__createdDirectory=error.name});
    )JS", "file-directory-create-directory.js");
    auto* create_directory_request =
        take_file_grant_create_directory_request(engine.get());
    require(create_directory_request->directory_grant_id.byte_count
                == directory_grant.size()
            && std::memcmp(create_directory_request->directory_grant_id.data,
                directory_grant.data(), directory_grant.size()) == 0
            && std::string_view(create_directory_request->display_name.data,
                   create_directory_request->display_name.byte_count)
                == "created-🙂",
        "create-directory request lost its parent grant or UTF-8 name");
    auto create_directory_request_id = create_directory_request->request_id;
    webscene_file_grant_create_directory_request_release_v2(
        create_directory_request);
    const std::vector<uint8_t> created_directory_grant{62U, 63U};
    require(complete_file_grant_create_directory(engine.get(),
                create_directory_request_id,
                WEBSCENE_FILE_GRANT_CREATE_DIRECTORY_SUCCESS_V2,
                "created-🙂", created_directory_grant),
        "create-directory success completion was rejected");
    require(evaluate(engine.get(), "__createdDirectory",
                "file-directory-create-directory-result.js")
            == "\"directory:created-🙂\"",
        "create=true did not produce a directory handle");

    execute(engine.get(), R"JS(
      globalThis.__createdDirectoryEntries='pending';
      (async()=>{const names=[];
        for await (const value of __createdDirectoryHandle.values())
          names.push(value.name);
        __createdDirectoryEntries=JSON.stringify(names)})().catch(
          error=>{__createdDirectoryEntries=error.name});
    )JS", "file-directory-created-directory-enumerate.js");
    directory_request = take_file_grant_directory_request(engine.get());
    require(directory_request->directory_grant_id.byte_count
                == created_directory_grant.size()
            && std::memcmp(directory_request->directory_grant_id.data,
                created_directory_grant.data(), created_directory_grant.size())
                == 0,
        "created directory handle did not retain its derived opaque grant");
    directory_request_id = directory_request->request_id;
    webscene_file_grant_directory_request_release_v2(directory_request);
    require(complete_file_grant_directory(engine.get(), directory_request_id,
                WEBSCENE_FILE_GRANT_DIRECTORY_ENUMERATE_V2,
                WEBSCENE_FILE_GRANT_DIRECTORY_SUCCESS_V2),
        "created directory enumeration completion was rejected");
    require(evaluate(engine.get(), "__createdDirectoryEntries",
                "file-directory-created-directory-enumerate-result.js")
            == "\"[]\"",
        "created directory did not enumerate as empty");

    struct create_directory_status_case {
        uint32_t status;
        const char* expected_name;
    };
    constexpr create_directory_status_case create_directory_status_cases[] = {
        {WEBSCENE_FILE_GRANT_CREATE_DIRECTORY_CANCELLED_V2, "AbortError"},
        {WEBSCENE_FILE_GRANT_CREATE_DIRECTORY_DENIED_V2, "NotAllowedError"},
        {WEBSCENE_FILE_GRANT_CREATE_DIRECTORY_NOT_FOUND_V2, "NotFoundError"},
        {WEBSCENE_FILE_GRANT_CREATE_DIRECTORY_TYPE_MISMATCH_V2,
            "TypeMismatchError"},
        {WEBSCENE_FILE_GRANT_CREATE_DIRECTORY_CHANGED_V2, "InvalidStateError"},
        {WEBSCENE_FILE_GRANT_CREATE_DIRECTORY_IO_ERROR_V2, "NotReadableError"},
        {WEBSCENE_FILE_GRANT_CREATE_DIRECTORY_LIMIT_V2,
            "QuotaExceededError"}};
    for (const auto& status_case : create_directory_status_cases) {
        execute(engine.get(), R"JS(
          globalThis.__createDirectoryFailure='pending';
          __directoryHandle.getDirectoryHandle('failure', {create:true}).then(
            ()=>{__createDirectoryFailure='unexpected'},
            error=>{__createDirectoryFailure=error.name});
        )JS", "file-directory-create-directory-status.js");
        create_directory_request =
            take_file_grant_create_directory_request(engine.get());
        create_directory_request_id = create_directory_request->request_id;
        webscene_file_grant_create_directory_request_release_v2(
            create_directory_request);
        require(complete_file_grant_create_directory(engine.get(),
                    create_directory_request_id, status_case.status),
            "create-directory error completion was rejected");
        require(evaluate(engine.get(), "__createDirectoryFailure",
                    "file-directory-create-directory-status-result.js")
                == std::string("\"") + status_case.expected_name + "\"",
            "create-directory status mapped to the wrong DOMException");
        require(!complete_file_grant_create_directory(engine.get(),
                    create_directory_request_id, status_case.status),
            "create-directory operation completed more than once");
    }
    if (std::getenv(
            "WEBSCENE_TEST_FILE_GRANT_CREATE_DIRECTORY_ONLY") != nullptr)
        return;

    execute(engine.get(), R"JS(
      globalThis.__removeResult='pending';
      __directoryHandle.removeEntry('removed-🙂', {recursive:true}).then(
        value=>{__removeResult=String(value)},
        error=>{__removeResult=error.name});
    )JS", "file-directory-remove.js");
    auto* remove_request = take_file_grant_remove_request(engine.get());
    require(remove_request->directory_grant_id.byte_count
                == directory_grant.size()
            && std::memcmp(remove_request->directory_grant_id.data,
                directory_grant.data(), directory_grant.size()) == 0
            && std::string_view(remove_request->display_name.data,
                   remove_request->display_name.byte_count) == "removed-🙂"
            && remove_request->recursive == 1U,
        "removeEntry lost its parent grant, Unicode name, or recursive flag");
    auto remove_request_id = remove_request->request_id;
    webscene_file_grant_remove_request_release_v2(remove_request);
    require(complete_file_grant_remove(engine.get(), remove_request_id,
                WEBSCENE_FILE_GRANT_REMOVE_SUCCESS_V2),
        "remove success completion was rejected");
    require(evaluate(engine.get(), "__removeResult",
                "file-directory-remove-result.js") == "\"undefined\"",
        "removeEntry did not resolve undefined");

    struct remove_status_case {
        uint32_t status;
        const char* expected_name;
    };
    constexpr remove_status_case remove_status_cases[] = {
        {WEBSCENE_FILE_GRANT_REMOVE_CANCELLED_V2, "AbortError"},
        {WEBSCENE_FILE_GRANT_REMOVE_DENIED_V2, "NotAllowedError"},
        {WEBSCENE_FILE_GRANT_REMOVE_NOT_FOUND_V2, "NotFoundError"},
        {WEBSCENE_FILE_GRANT_REMOVE_INVALID_MODIFICATION_V2,
            "InvalidModificationError"},
        {WEBSCENE_FILE_GRANT_REMOVE_CHANGED_V2, "InvalidStateError"},
        {WEBSCENE_FILE_GRANT_REMOVE_IO_ERROR_V2, "NotReadableError"},
        {WEBSCENE_FILE_GRANT_REMOVE_LIMIT_V2, "QuotaExceededError"}};
    for (const auto& status_case : remove_status_cases) {
        execute(engine.get(), R"JS(
          globalThis.__removeFailure='pending';
          __directoryHandle.removeEntry('failure').then(
            ()=>{__removeFailure='unexpected'},
            error=>{__removeFailure=error.name});
        )JS", "file-directory-remove-status.js");
        remove_request = take_file_grant_remove_request(engine.get());
        remove_request_id = remove_request->request_id;
        require(remove_request->recursive == 0U,
            "removeEntry default recursive option changed");
        webscene_file_grant_remove_request_release_v2(remove_request);
        require(complete_file_grant_remove(engine.get(), remove_request_id,
                    status_case.status),
            "remove error completion was rejected");
        require(evaluate(engine.get(), "__removeFailure",
                    "file-directory-remove-status-result.js")
                == std::string("\"") + status_case.expected_name + "\"",
            "remove status mapped to the wrong DOMException");
        require(!complete_file_grant_remove(engine.get(), remove_request_id,
                    status_case.status),
            "remove operation completed more than once");
    }
    execute(engine.get(), R"JS(
      globalThis.__removeValidation='pending';
      (async()=>{
      const values=[];
      for (const [name,options] of [['',{}],['.',{}],['..',{}],['a/b',{}],
        ['a\\b',{}],['ok',1]]) {
        try { await __directoryHandle.removeEntry(name,options);
          values.push('unexpected'); }
        catch(error) { values.push(error.name); }
      }
      __removeValidation=values.join(':');
    })().catch(error=>{__removeValidation=`unexpected:${error.name}`});
    )JS", "file-directory-remove-validation.js");
    const auto remove_validation = evaluate_until_equals(
        engine.get(),
        "__removeValidation",
        "file-directory-remove-validation-result.js",
        "\"TypeError:TypeError:TypeError:TypeError:TypeError:TypeError\"");
    const auto expected_remove_validation =
        "\"TypeError:TypeError:TypeError:TypeError:TypeError:TypeError\"";
    if (remove_validation != expected_remove_validation) {
        fail("removeEntry WebIDL name/options validation changed: "
            + remove_validation);
    }
    require(webscene_engine_take_file_grant_remove_request_v2(engine.get()) == nullptr,
        "invalid removeEntry call published a native removal request");
    if (std::getenv("WEBSCENE_TEST_FILE_GRANT_REMOVE_ONLY") != nullptr)
        return;

    execute(engine.get(), R"JS(
      globalThis.__resolveSame='pending';
      __directoryHandle.resolve(__directoryHandle).then(
        value=>{__resolveSame=JSON.stringify(value)},
        error=>{__resolveSame=error.name});
    )JS", "file-directory-resolve-same.js");
    auto* ancestry_request = take_file_grant_ancestry_request(engine.get());
    require(ancestry_request->base_directory_grant_id.byte_count
                == directory_grant.size()
            && ancestry_request->possible_descendant_grant_id.byte_count
                == directory_grant.size()
            && std::memcmp(ancestry_request->base_directory_grant_id.data,
                directory_grant.data(), directory_grant.size()) == 0,
        "same-directory resolve lost its opaque grants");
    auto ancestry_request_id = ancestry_request->request_id;
    webscene_file_grant_ancestry_request_release_v2(ancestry_request);
    require(complete_file_grant_ancestry(engine.get(), ancestry_request_id,
            WEBSCENE_FILE_GRANT_ANCESTRY_SUCCESS_V2),
        "same-directory ancestry completion was rejected");
    require(evaluate(engine.get(), "__resolveSame",
                "file-directory-resolve-same-result.js") == "\"[]\"",
        "same-directory resolve did not return an empty sequence");

    execute(engine.get(), R"JS(
      globalThis.__resolveNested='pending';
      __directoryHandle.resolve(__openHandles[0]).then(
        value=>{__resolveNested=value.join('/')},
        error=>{__resolveNested=error.name});
    )JS", "file-directory-resolve-nested.js");
    ancestry_request = take_file_grant_ancestry_request(engine.get());
    require(ancestry_request->base_directory_grant_id.byte_count
                == directory_grant.size()
            && ancestry_request->possible_descendant_grant_id.byte_count
                == first_grant.size()
            && std::memcmp(
                ancestry_request->possible_descendant_grant_id.data,
                first_grant.data(), first_grant.size()) == 0,
        "descendant resolve lost its independently selected opaque grant");
    ancestry_request_id = ancestry_request->request_id;
    webscene_file_grant_ancestry_request_release_v2(ancestry_request);
    require(complete_file_grant_ancestry(engine.get(), ancestry_request_id,
            WEBSCENE_FILE_GRANT_ANCESTRY_SUCCESS_V2,
            {"nested\xF0\x9F\x98\x8A", "alpha.txt"}),
        "nested ancestry completion was rejected");
    require(evaluate(engine.get(), "__resolveNested",
                "file-directory-resolve-nested-result.js")
            == "\"nested😊/alpha.txt\"",
        "resolve lost ordered Unicode components");

    execute(engine.get(), R"JS(
      globalThis.__resolveUnrelated='pending';
      __directoryHandle.resolve(__openHandles[0]).then(
        value=>{__resolveUnrelated=value===null?'null':'unexpected'},
        error=>{__resolveUnrelated=error.name});
    )JS", "file-directory-resolve-unrelated.js");
    ancestry_request = take_file_grant_ancestry_request(engine.get());
    ancestry_request_id = ancestry_request->request_id;
    webscene_file_grant_ancestry_request_release_v2(ancestry_request);
    require(complete_file_grant_ancestry(engine.get(), ancestry_request_id,
            WEBSCENE_FILE_GRANT_ANCESTRY_NOT_DESCENDANT_V2),
        "not-descendant ancestry completion was rejected");
    require(evaluate(engine.get(), "__resolveUnrelated",
                "file-directory-resolve-unrelated-result.js") == "\"null\"",
        "unrelated resolve did not return null");

    struct ancestry_status_case { uint32_t status; const char* expected_name; };
    constexpr ancestry_status_case ancestry_status_cases[] = {
        {WEBSCENE_FILE_GRANT_ANCESTRY_CANCELLED_V2, "AbortError"},
        {WEBSCENE_FILE_GRANT_ANCESTRY_DENIED_V2, "NotAllowedError"},
        {WEBSCENE_FILE_GRANT_ANCESTRY_NOT_FOUND_V2, "NotFoundError"},
        {WEBSCENE_FILE_GRANT_ANCESTRY_CHANGED_V2, "InvalidStateError"},
        {WEBSCENE_FILE_GRANT_ANCESTRY_IO_ERROR_V2, "NotReadableError"},
        {WEBSCENE_FILE_GRANT_ANCESTRY_LIMIT_V2, "QuotaExceededError"}};
    for (const auto& status_case : ancestry_status_cases) {
        execute(engine.get(), R"JS(
          globalThis.__resolveFailure='pending';
          __directoryHandle.resolve(__openHandles[0]).then(
            ()=>{__resolveFailure='unexpected'},
            error=>{__resolveFailure=error.name});
        )JS", "file-directory-resolve-status.js");
        ancestry_request = take_file_grant_ancestry_request(engine.get());
        ancestry_request_id = ancestry_request->request_id;
        webscene_file_grant_ancestry_request_release_v2(ancestry_request);
        require(complete_file_grant_ancestry(engine.get(), ancestry_request_id,
                status_case.status, {}, "native-error"),
            "native ancestry error completion was rejected");
        require(evaluate(engine.get(), "__resolveFailure",
                    "file-directory-resolve-status-result.js")
                == std::string("\"") + status_case.expected_name + "\"",
            "native ancestry status mapped to the wrong DOMException");
        require(!complete_file_grant_ancestry(engine.get(), ancestry_request_id,
                status_case.status, {}, "native-error"),
            "native ancestry request completed more than once");
    }

    execute(engine.get(), R"JS(
      globalThis.__directoryErrors='pending';
      Promise.all([
        __directoryHandle.getFileHandle('../bad').catch(e=>e.name),
        __directoryHandle.getDirectoryHandle('../bad', {create:true}).catch(e=>e.name),
        __directoryHandle.resolve({}).catch(e=>e.name)
      ]).then(values=>{__directoryErrors=values.join(':')});
    )JS", "file-directory-errors.js");
    require(evaluate(engine.get(), "__directoryErrors",
                "file-directory-errors-result.js")
            == "\"TypeError:TypeError:TypeError\"",
        "directory authority boundaries changed");

    execute(engine.get(), R"JS(
      globalThis.__directoryMismatch='pending';
      __directoryHandle.getDirectoryHandle('alpha.txt').then(
        ()=>{__directoryMismatch='unexpected'},
        error=>{__directoryMismatch=error.name});
    )JS", "file-directory-kind-mismatch.js");
    directory_request = take_file_grant_directory_request(engine.get());
    directory_request_id = directory_request->request_id;
    webscene_file_grant_directory_request_release_v2(directory_request);
    require(complete_file_grant_directory(engine.get(), directory_request_id,
            WEBSCENE_FILE_GRANT_DIRECTORY_ENUMERATE_V2,
            WEBSCENE_FILE_GRANT_DIRECTORY_SUCCESS_V2,
            {{"alpha.txt", {41U}, WEBSCENE_FILE_PANEL_ENTRY_FILE_V2,
                 WEBSCENE_FILE_PANEL_GRANT_READ_V2}}),
        "wrong-kind directory page was rejected");
    require(evaluate(engine.get(), "__directoryMismatch",
                "file-directory-kind-mismatch-result.js")
            == "\"TypeMismatchError\"",
        "wrong-kind lookup did not reject TypeMismatchError");

    execute(engine.get(), R"JS(
      globalThis.__directoryMissing='pending';
      __directoryHandle.getFileHandle('missing.txt').then(
        ()=>{__directoryMissing='unexpected'},
        error=>{__directoryMissing=error.name});
    )JS", "file-directory-missing.js");
    directory_request = take_file_grant_directory_request(engine.get());
    directory_request_id = directory_request->request_id;
    webscene_file_grant_directory_request_release_v2(directory_request);
    require(complete_file_grant_directory(engine.get(), directory_request_id,
            WEBSCENE_FILE_GRANT_DIRECTORY_ENUMERATE_V2,
            WEBSCENE_FILE_GRANT_DIRECTORY_SUCCESS_V2),
        "empty lookup page was rejected");
    require(evaluate(engine.get(), "__directoryMissing",
                "file-directory-missing-result.js") == "\"NotFoundError\"",
        "missing lookup did not reject NotFoundError");

    struct directory_status_case { uint32_t status; const char* expected_name; };
    constexpr directory_status_case directory_status_cases[] = {
        {WEBSCENE_FILE_GRANT_DIRECTORY_CANCELLED_V2, "AbortError"},
        {WEBSCENE_FILE_GRANT_DIRECTORY_DENIED_V2, "NotAllowedError"},
        {WEBSCENE_FILE_GRANT_DIRECTORY_NOT_FOUND_V2, "NotFoundError"},
        {WEBSCENE_FILE_GRANT_DIRECTORY_CHANGED_V2, "InvalidStateError"},
        {WEBSCENE_FILE_GRANT_DIRECTORY_IO_ERROR_V2, "NotReadableError"},
        {WEBSCENE_FILE_GRANT_DIRECTORY_LIMIT_V2, "QuotaExceededError"}};
    for (const auto& status_case : directory_status_cases) {
        execute(engine.get(), R"JS(
          globalThis.__directoryFailure='pending';
          __directoryHandle.entries().next().then(
            ()=>{__directoryFailure='unexpected'},
            error=>{__directoryFailure=error.name});
        )JS", "file-directory-status.js");
        directory_request = take_file_grant_directory_request(engine.get());
        directory_request_id = directory_request->request_id;
        webscene_file_grant_directory_request_release_v2(directory_request);
        require(complete_file_grant_directory(engine.get(), directory_request_id,
                WEBSCENE_FILE_GRANT_DIRECTORY_ENUMERATE_V2,
                status_case.status),
            "directory error completion was rejected");
        require(evaluate(engine.get(), "__directoryFailure",
                    "file-directory-status-result.js")
                == std::string("\"") + status_case.expected_name + "\"",
            "directory status mapped to the wrong DOMException");
        require(!complete_file_grant_directory(engine.get(), directory_request_id,
                WEBSCENE_FILE_GRANT_DIRECTORY_ENUMERATE_V2,
                status_case.status),
            "directory operation completed more than once");
    }

    execute(engine.get(), R"JS(
      globalThis.__directoryEarly='pending';
      (async()=>{
        for await (const value of __directoryHandle.values()) {
          __directoryEarly=value.name;
          break;
        }
      })().catch(error=>{__directoryEarly=error.name});
    )JS", "file-directory-early-return.js");
    directory_request = take_file_grant_directory_request(engine.get());
    directory_request_id = directory_request->request_id;
    webscene_file_grant_directory_request_release_v2(directory_request);
    const std::vector<uint8_t> early_cursor{45U};
    require(complete_file_grant_directory(engine.get(), directory_request_id,
            WEBSCENE_FILE_GRANT_DIRECTORY_ENUMERATE_V2,
            WEBSCENE_FILE_GRANT_DIRECTORY_SUCCESS_V2,
            {{"early.txt", {46U}, WEBSCENE_FILE_PANEL_ENTRY_FILE_V2,
                 WEBSCENE_FILE_PANEL_GRANT_READ_V2}}, early_cursor),
        "early-return directory page was rejected");
    directory_request = take_file_grant_directory_request(engine.get());
    require(directory_request->action
            == WEBSCENE_FILE_GRANT_DIRECTORY_RELEASE_CURSOR_V2,
        "early iterator return did not release its cursor");
    directory_request_id = directory_request->request_id;
    webscene_file_grant_directory_request_release_v2(directory_request);
    require(complete_file_grant_directory(engine.get(), directory_request_id,
            WEBSCENE_FILE_GRANT_DIRECTORY_RELEASE_CURSOR_V2,
            WEBSCENE_FILE_GRANT_DIRECTORY_SUCCESS_V2),
        "early iterator cursor release was rejected");
    require(evaluate(engine.get(), "__directoryEarly",
                "file-directory-early-return-result.js") == "\"early.txt\"",
        "early iterator result changed");

    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        execute(engine.get(),
            "globalThis.__directoryCycle='pending';"
            "__directoryHandle.entries().next().then(value=>{"
            "__directoryCycle=value.done?'done':'unexpected'},"
            "error=>{__directoryCycle=error.name});",
            "file-directory-lifecycle-cycle.js");
        directory_request = take_file_grant_directory_request(engine.get());
        directory_request_id = directory_request->request_id;
        webscene_file_grant_directory_request_release_v2(directory_request);
        require(complete_file_grant_directory(engine.get(), directory_request_id,
                WEBSCENE_FILE_GRANT_DIRECTORY_ENUMERATE_V2,
                WEBSCENE_FILE_GRANT_DIRECTORY_SUCCESS_V2),
            "directory lifecycle completion was rejected");
        require(evaluate(engine.get(), "__directoryCycle",
                    "file-directory-lifecycle-result.js") == "\"done\"",
            "directory lifecycle iteration did not drain");
    }
    if (const auto* filter = std::getenv("WEBSCENE_NATIVE_ENGINE_TEST_FILTER");
        filter != nullptr
        && std::string_view(filter) == "file-system-directory-resolve") {
        require(webscene_engine_take_file_grant_ancestry_request_v2(engine.get())
                    == nullptr,
            "focused ancestry request queue did not drain");
        return;
    }

    activate_file_picker(engine.get(), 50004U);
    execute(engine.get(), R"JS(
      globalThis.__saveState = 'pending';
      showSaveFilePicker({
        suggestedName: 'drawing.json',
        startIn: __directoryHandle,
        excludeAcceptAllOption: true,
        types: [{description:'JSON', accept:{'application/json':['.json']}}]
      }).then(handle => {
        globalThis.__saveHandle = handle;
        return Promise.all([
          handle.queryPermission(),
          handle.queryPermission({mode:'readwrite'}),
          handle.requestPermission(),
          handle.requestPermission({mode:'readwrite'})
        ]).then(permissions => {
          __saveState = {
            kind: handle.kind,
            name: handle.name,
            instance: handle instanceof FileSystemFileHandle,
            permissions
          };
        });
      }, error => { __saveState = error.name; });
    )JS", "file-picker-save.js");
    request = take_file_panel_request(engine.get());
    require(request->kind == WEBSCENE_FILE_PANEL_SAVE_FILE_V2
            && (request->flags & WEBSCENE_FILE_PANEL_CONFIRM_OVERWRITE_V2) != 0U
            && (request->flags & WEBSCENE_FILE_PANEL_CAN_CREATE_DIRECTORIES_V2) != 0U
            && (request->flags & WEBSCENE_FILE_PANEL_EXCLUDE_ACCEPT_ALL_V2) != 0U
            && std::string_view(request->suggested_name.data,
                request->suggested_name.byte_count) == "drawing.json"
            && request->initial_location_token.byte_count == directory_grant.size()
            && std::memcmp(request->initial_location_token.data,
                directory_grant.data(), directory_grant.size()) == 0,
        "save picker options changed in the native request");
    const auto save_id = request->request_id;
    webscene_file_panel_request_release_v2(request);
    const std::vector<uint8_t> save_grant{10U, 0U, 11U};
    require(complete_file_panel_success(
        engine.get(), save_id, WEBSCENE_FILE_PANEL_ENTRY_FILE_V2,
        WEBSCENE_FILE_PANEL_GRANT_WRITE_V2,
        "drawing.json", save_grant),
        "save picker completion was rejected");
    const auto save_state = evaluate(
        engine.get(), "__saveState", "file-picker-save-result.js");
    require(save_state.find("\"kind\":\"file\"") != std::string::npos
            && save_state.find("\"name\":\"drawing.json\"") != std::string::npos
            && save_state.find("\"instance\":true") != std::string::npos
            && save_state.find("\"permissions\":[\"denied\",\"denied\","
                "\"denied\",\"denied\"]") != std::string::npos,
        "write-only save handle permission contract changed: " + save_state);
    execute(engine.get(), R"JS(
      globalThis.__writeOnlyRead='pending';
      __saveHandle.getFile().then(
        () => { __writeOnlyRead='unexpected'; },
        error => { __writeOnlyRead=error.name; });
    )JS", "file-handle-write-only-read.js");
    require(evaluate(engine.get(), "__writeOnlyRead",
                "file-handle-write-only-read-result.js") == "\"NotAllowedError\"",
        "write-only native grant was allowed to read file content");
    require(webscene_engine_take_file_grant_read_request_v2(engine.get()) == nullptr,
        "write-only getFile reached the native read broker");

    if (const auto* filter = std::getenv("WEBSCENE_NATIVE_ENGINE_TEST_FILTER");
        filter != nullptr
        && std::string_view(filter) == "file-system-handle-permissions") {
        execute(engine.get(), R"JS(
          globalThis.__clonePermissions='pending';
          (async()=>{
            const clone=structuredClone(__openHandles[0]);
            __clonePermissions=await Promise.all([
              clone.queryPermission(),
              clone.queryPermission({mode:'readwrite'}),
              clone.requestPermission(),
              clone.requestPermission({mode:'readwrite'})
            ]);
          })().catch(error=>{__clonePermissions=error.name});
        )JS", "file-handle-permission-clone.js");
        const auto clone_permissions = evaluate(engine.get(),
            "__clonePermissions", "file-handle-permission-clone-result.js");
        require(clone_permissions
                == "[\"granted\",\"denied\",\"granted\",\"denied\"]",
            "structured clone lost exact permission capabilities: "
                + clone_permissions);
        webscene_engine_memory_metrics before{sizeof(before)};
        require(webscene_engine_get_memory_metrics(engine.get(), &before) != 0,
            "permission performance baseline metrics unavailable");
        const auto fd_before = file_system_access_test_open_fd_count();
        const auto rss_before = file_system_access_test_peak_rss_bytes();
        const auto started = std::chrono::steady_clock::now();
        execute(engine.get(), R"JS(
          globalThis.__permissionCycles='pending';
          (async()=>{
            for (let cycle=0;cycle<100;++cycle) {
              const checks=[];
              for (let index=0;index<100;++index) {
                checks.push(__openHandles[0].queryPermission());
              }
              const values=await Promise.all(checks);
              if (values.some(value=>value!=='granted'))
                throw new Error('permission state changed');
            }
            __permissionCycles=10000;
          })().catch(error=>{__permissionCycles=error.name});
        )JS", "file-handle-permission-cycles.js");
        std::string cycle_state;
        for (int attempt = 0; attempt < 500; ++attempt) {
            cycle_state = evaluate(engine.get(), "__permissionCycles",
                "file-handle-permission-cycles-result.js");
            if (cycle_state != "\"pending\"") break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        require(cycle_state == "10000",
            "permission lifecycle promises did not settle: " + cycle_state);
        const auto elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started).count();
        webscene_engine_memory_metrics after{sizeof(after)};
        require(webscene_engine_get_memory_metrics(engine.get(), &after) != 0,
            "permission performance final metrics unavailable");
        const auto fd_after = file_system_access_test_open_fd_count();
        const auto rss_after = file_system_access_test_peak_rss_bytes();
        require(elapsed < 5.0, "10k permission queries exceeded time budget");
        require(after.v8_used_heap_bytes <= before.v8_used_heap_bytes
                + 8U * 1024U * 1024U,
            "10k permission queries exceeded heap budget");
        require(rss_after <= rss_before + 64U * 1024U * 1024U,
            "10k permission queries exceeded RSS budget");
        require(fd_after <= fd_before + 2U,
            "permission queries leaked file descriptors");
        require(webscene_engine_take_file_panel_request_v2(engine.get()) == nullptr
                && webscene_engine_take_file_grant_read_request_v2(engine.get())
                    == nullptr
                && webscene_engine_take_file_grant_write_request_v2(engine.get())
                    == nullptr
                && webscene_engine_take_file_grant_directory_request_v2(engine.get())
                    == nullptr,
            "terminal permission checks reached a native broker");
        std::cout << "File System Access permission gate: queries=10000 cycles=100"
                  << " elapsedSeconds=" << elapsed
                  << " heapDelta="
                  << static_cast<int64_t>(after.v8_used_heap_bytes)
                        - static_cast<int64_t>(before.v8_used_heap_bytes)
                  << " rssBefore=" << rss_before << " rssAfter=" << rss_after
                  << " fdBefore=" << fd_before << " fdAfter=" << fd_after
                  << " queuedRequests=0" << '\n';
        return;
    }

    const std::vector<uint8_t> write_transaction(16U, 0x5aU);
    execute(engine.get(), R"JS(
      globalThis.__writeState='pending';
      __saveHandle.createWritable().then(async stream => {
        globalThis.__writeStream=stream;
        const tag=Object.prototype.toString.call(stream);
        const instance=stream instanceof FileSystemWritableFileStream
          && stream instanceof WritableStream;
        await stream.write('abc');
        await stream.seek(5);
        await stream.write(new Uint8Array([0,255]));
        await stream.truncate(8);
        await stream.close();
        __writeState={tag,instance,closed:true};
      }, error => { __writeState=error.name; });
    )JS", "file-handle-create-writable.js");
    auto* write_request = take_file_grant_write_request(engine.get());
    require(write_request->action == WEBSCENE_FILE_GRANT_WRITE_BEGIN_V2
            && write_request->grant_id.byte_count == save_grant.size()
            && std::memcmp(write_request->grant_id.data,
                save_grant.data(), save_grant.size()) == 0,
        "createWritable begin lost its opaque grant");
    auto write_id = write_request->request_id;
    webscene_file_grant_write_request_release_v2(write_request);
    require(complete_file_grant_write(engine.get(), write_id,
            WEBSCENE_FILE_GRANT_WRITE_BEGIN_V2,
            WEBSCENE_FILE_GRANT_WRITE_SUCCESS_V2, write_transaction),
        "createWritable begin completion was rejected");
    write_request = take_file_grant_write_request(engine.get());
    require(write_request->action == WEBSCENE_FILE_GRANT_WRITE_CHUNK_V2
            && write_request->offset == 0U && write_request->byte_count == 3U
            && std::string_view(reinterpret_cast<const char*>(write_request->data),
                write_request->byte_count) == "abc",
        "string write lost its encoded native range");
    write_id = write_request->request_id;
    webscene_file_grant_write_request_release_v2(write_request);
    require(complete_file_grant_write(engine.get(), write_id,
            WEBSCENE_FILE_GRANT_WRITE_CHUNK_V2,
            WEBSCENE_FILE_GRANT_WRITE_SUCCESS_V2, {}, 0U, 3U),
        "string write completion was rejected");
    write_request = take_file_grant_write_request(engine.get());
    require(write_request->action == WEBSCENE_FILE_GRANT_WRITE_CHUNK_V2
            && write_request->offset == 5U && write_request->byte_count == 2U
            && write_request->data[0] == 0U && write_request->data[1] == 255U,
        "seek/binary write lost its native offset or bytes");
    write_id = write_request->request_id;
    webscene_file_grant_write_request_release_v2(write_request);
    require(complete_file_grant_write(engine.get(), write_id,
            WEBSCENE_FILE_GRANT_WRITE_CHUNK_V2,
            WEBSCENE_FILE_GRANT_WRITE_SUCCESS_V2, {}, 5U, 2U),
        "binary write completion was rejected");
    write_request = take_file_grant_write_request(engine.get());
    require(write_request->action == WEBSCENE_FILE_GRANT_WRITE_COMMIT_V2
            && write_request->final_byte_count == 8U,
        "truncate/close lost the final atomic byte count");
    write_id = write_request->request_id;
    webscene_file_grant_write_request_release_v2(write_request);
    require(complete_file_grant_write(engine.get(), write_id,
            WEBSCENE_FILE_GRANT_WRITE_COMMIT_V2,
            WEBSCENE_FILE_GRANT_WRITE_SUCCESS_V2, {}, 0U, 0U, 8U),
        "atomic commit completion was rejected");
    const auto write_state = evaluate(engine.get(), "__writeState",
        "file-handle-create-writable-result.js");
    require(write_state.find("[object FileSystemWritableFileStream]")
                != std::string::npos
            && write_state.find("\"instance\":true") != std::string::npos
            && write_state.find("\"closed\":true") != std::string::npos,
        "createWritable stream identity or close changed: " + write_state);
    require(evaluate(engine.get(), R"JS((async()=>{
      try { await __writeStream.write('late'); return 'unexpected'; }
      catch (error) { return error.name; }
    })())JS", "file-handle-write-after-close.js") == "\"TypeError\"",
        "write after close did not reject TypeError");

    activate_file_picker(engine.get(), 50005U);
    execute(engine.get(), R"JS(
      globalThis.__readWriteHandle=null;
      showOpenFilePicker().then(handles => { __readWriteHandle=handles[0]; });
    )JS", "file-handle-readwrite-picker.js");
    request = take_file_panel_request(engine.get());
    const auto readwrite_picker_id = request->request_id;
    webscene_file_panel_request_release_v2(request);
    const std::vector<uint8_t> readwrite_grant{21U, 0U, 22U};
    require(complete_file_panel_success(engine.get(), readwrite_picker_id,
            WEBSCENE_FILE_PANEL_ENTRY_FILE_V2,
            WEBSCENE_FILE_PANEL_GRANT_READ_V2
                | WEBSCENE_FILE_PANEL_GRANT_WRITE_V2,
            "existing.txt", readwrite_grant),
        "read/write picker completion was rejected");
    execute(engine.get(), R"JS(
      globalThis.__keepExisting='pending';
      __readWriteHandle.createWritable({keepExistingData:true}).then(async stream => {
        await stream.seek(3);
        await stream.write('d');
        await stream.close();
        __keepExisting='closed';
      }, error => { __keepExisting=error.name; });
    )JS", "file-handle-create-writable-keep-existing.js");
    write_request = take_file_grant_write_request(engine.get());
    write_id = write_request->request_id;
    webscene_file_grant_write_request_release_v2(write_request);
    require(complete_file_grant_write(engine.get(), write_id,
            WEBSCENE_FILE_GRANT_WRITE_BEGIN_V2,
            WEBSCENE_FILE_GRANT_WRITE_SUCCESS_V2, write_transaction),
        "keepExistingData begin completion was rejected");
    read_request = take_file_grant_read_request(engine.get());
    require(read_request->offset == 0U
            && read_request->maximum_bytes == 1024U * 1024U,
        "keepExistingData did not request a bounded snapshot chunk");
    const auto keep_read_id = read_request->request_id;
    webscene_file_grant_read_request_release_v2(read_request);
    const std::vector<uint8_t> existing_bytes{'a','b','c'};
    require(complete_file_grant_read(engine.get(), keep_read_id,
            WEBSCENE_FILE_GRANT_READ_SUCCESS_V2, existing_bytes.size(),
            file_modified_ns, 0U, existing_bytes, true),
        "keepExistingData snapshot completion was rejected");
    write_request = take_file_grant_write_request(engine.get());
    require(write_request->action == WEBSCENE_FILE_GRANT_WRITE_CHUNK_V2
            && write_request->offset == 0U && write_request->byte_count == 3U,
        "keepExistingData did not stage the existing bytes");
    write_id = write_request->request_id;
    webscene_file_grant_write_request_release_v2(write_request);
    require(complete_file_grant_write(engine.get(), write_id,
            WEBSCENE_FILE_GRANT_WRITE_CHUNK_V2,
            WEBSCENE_FILE_GRANT_WRITE_SUCCESS_V2, {}, 0U, 3U),
        "keepExistingData staging completion was rejected");
    write_request = take_file_grant_write_request(engine.get());
    require(write_request->action == WEBSCENE_FILE_GRANT_WRITE_CHUNK_V2
            && write_request->offset == 3U && write_request->byte_count == 1U
            && write_request->data[0] == 'd',
        "keepExistingData append lost its cursor or byte");
    write_id = write_request->request_id;
    webscene_file_grant_write_request_release_v2(write_request);
    require(complete_file_grant_write(engine.get(), write_id,
            WEBSCENE_FILE_GRANT_WRITE_CHUNK_V2,
            WEBSCENE_FILE_GRANT_WRITE_SUCCESS_V2, {}, 3U, 1U),
        "keepExistingData append completion was rejected");
    write_request = take_file_grant_write_request(engine.get());
    require(write_request->action == WEBSCENE_FILE_GRANT_WRITE_COMMIT_V2
            && write_request->final_byte_count == 4U,
        "keepExistingData commit lost final length");
    write_id = write_request->request_id;
    webscene_file_grant_write_request_release_v2(write_request);
    require(complete_file_grant_write(engine.get(), write_id,
            WEBSCENE_FILE_GRANT_WRITE_COMMIT_V2,
            WEBSCENE_FILE_GRANT_WRITE_SUCCESS_V2, {}, 0U, 0U, 4U),
        "keepExistingData commit completion was rejected");
    require(evaluate(engine.get(), "__keepExisting",
                "file-handle-create-writable-keep-existing-result.js")
            == "\"closed\"",
        "keepExistingData writer did not close");

    execute(engine.get(), R"JS(
      globalThis.__abortedWrite='pending';
      __saveHandle.createWritable().then(async stream => {
        await stream.abort('discard');
        __abortedWrite='aborted';
      }, error => { __abortedWrite=error.name; });
    )JS", "file-handle-create-writable-abort.js");
    write_request = take_file_grant_write_request(engine.get());
    write_id = write_request->request_id;
    webscene_file_grant_write_request_release_v2(write_request);
    require(complete_file_grant_write(engine.get(), write_id,
            WEBSCENE_FILE_GRANT_WRITE_BEGIN_V2,
            WEBSCENE_FILE_GRANT_WRITE_SUCCESS_V2, write_transaction),
        "abort fixture begin completion was rejected");
    write_request = take_file_grant_write_request(engine.get());
    require(write_request->action == WEBSCENE_FILE_GRANT_WRITE_ABORT_V2
            && write_request->transaction_id.byte_count
                == write_transaction.size(),
        "stream abort did not carry only its opaque transaction");
    write_id = write_request->request_id;
    webscene_file_grant_write_request_release_v2(write_request);
    require(complete_file_grant_write(engine.get(), write_id,
            WEBSCENE_FILE_GRANT_WRITE_ABORT_V2,
            WEBSCENE_FILE_GRANT_WRITE_SUCCESS_V2),
        "stream abort completion was rejected");
    require(evaluate(engine.get(), "__abortedWrite",
                "file-handle-create-writable-abort-result.js") == "\"aborted\"",
        "stream abort did not preserve terminal state");

    execute(engine.get(), R"JS(
      globalThis.__writeFailure='pending';
      __saveHandle.createWritable().then(
        () => { __writeFailure='unexpected'; },
        error => { __writeFailure=error.name; });
    )JS", "file-handle-create-writable-denied.js");
    write_request = take_file_grant_write_request(engine.get());
    write_id = write_request->request_id;
    webscene_file_grant_write_request_release_v2(write_request);
    require(complete_file_grant_write(engine.get(), write_id,
            WEBSCENE_FILE_GRANT_WRITE_BEGIN_V2,
            WEBSCENE_FILE_GRANT_WRITE_DENIED_V2),
        "denied writer completion was rejected");
    require(evaluate(engine.get(), "__writeFailure",
                "file-handle-create-writable-denied-result.js")
            == "\"NotAllowedError\"",
        "native writer denial mapped to the wrong DOMException");
    require(!complete_file_grant_write(engine.get(), write_id,
            WEBSCENE_FILE_GRANT_WRITE_BEGIN_V2,
            WEBSCENE_FILE_GRANT_WRITE_DENIED_V2),
        "native writer completed more than once");

    const auto descriptor_state = evaluate(engine.get(), R"JS((()=>{
      const kind=Object.getOwnPropertyDescriptor(FileSystemHandle.prototype,'kind');
      const same=Object.getOwnPropertyDescriptor(FileSystemHandle.prototype,'isSameEntry');
      const getFile=Object.getOwnPropertyDescriptor(FileSystemFileHandle.prototype,'getFile');
      return {
        hiddenBridge: typeof __webSceneRequestFilePanel,
        hiddenIdentityBridge: typeof __webSceneCompareFileGrants,
        hiddenReadBridge: typeof __webSceneReadFileGrant,
        hiddenWriteBridge: typeof __webSceneWriteFileGrant,
        hiddenDirectoryBridge: typeof __webSceneDirectoryGrant,
        hiddenFileFactory: typeof __webSceneCreateNativeFile,
        opaqueHandle: Reflect.ownKeys(__openHandles[0]).length === 0,
        illegal: (()=>{try{new FileSystemHandle();return false}catch(e){return e.name==='TypeError'}})(),
        kindEnumerable: kind.enumerable,
        kindConfigurable: kind.configurable,
        sameEnumerable: same.enumerable,
        sameConfigurable: same.configurable,
        sameWritable: same.writable,
        getFileEnumerable: getFile.enumerable,
        getFileConfigurable: getFile.configurable,
        getFileWritable: getFile.writable,
        inheritance: Object.getPrototypeOf(FileSystemFileHandle.prototype)===FileSystemHandle.prototype
      };
    })())JS", "file-picker-idl-result.js");
    require(descriptor_state.find("\"hiddenBridge\":\"undefined\"")
                != std::string::npos
            && descriptor_state.find("\"hiddenIdentityBridge\":\"undefined\"")
                != std::string::npos
            && descriptor_state.find("\"hiddenReadBridge\":\"undefined\"")
                != std::string::npos
            && descriptor_state.find("\"hiddenWriteBridge\":\"undefined\"")
                != std::string::npos
            && descriptor_state.find("\"hiddenDirectoryBridge\":\"undefined\"")
                != std::string::npos
            && descriptor_state.find("\"hiddenFileFactory\":\"undefined\"")
                != std::string::npos
            && descriptor_state.find("\"opaqueHandle\":true") != std::string::npos
            && descriptor_state.find("\"illegal\":true") != std::string::npos
            && descriptor_state.find("\"kindEnumerable\":true") != std::string::npos
            && descriptor_state.find("\"kindConfigurable\":true") != std::string::npos
            && descriptor_state.find("\"sameEnumerable\":true") != std::string::npos
            && descriptor_state.find("\"sameConfigurable\":true") != std::string::npos
            && descriptor_state.find("\"sameWritable\":true") != std::string::npos
            && descriptor_state.find("\"getFileEnumerable\":true") != std::string::npos
            && descriptor_state.find("\"getFileConfigurable\":true") != std::string::npos
            && descriptor_state.find("\"getFileWritable\":true") != std::string::npos
            && descriptor_state.find("\"inheritance\":true") != std::string::npos,
        "File System Access IDL descriptors changed: " + descriptor_state);

    execute(engine.get(), R"JS(
      globalThis.__deniedSameEntry = 'pending';
      __openHandles[0].isSameEntry(__repeatedHandle).then(
        value => { __deniedSameEntry = value; },
        error => { __deniedSameEntry = error.name; });
    )JS", "file-picker-same-entry-fail-closed.js");
    const auto* denied_comparison =
        take_file_grant_same_entry_request(engine.get());
    const auto denied_comparison_id = denied_comparison->request_id;
    webscene_file_grant_same_entry_request_release_v2(denied_comparison);
    require(!complete_file_grant_same_entry(
                engine.get(), denied_comparison_id, false, true),
        "denied same-entry completion leaked a true result");
    require(complete_file_grant_same_entry(
                engine.get(), denied_comparison_id, false, false),
        "fail-closed same-entry completion was rejected");
    require(evaluate(engine.get(), "__deniedSameEntry",
                "file-picker-same-entry-fail-closed-result.js") == "false",
        "denied native identity did not resolve false");
    require(!complete_file_grant_same_entry(
                engine.get(), denied_comparison_id, false, false),
        "same-entry comparison completed more than once");

    execute(engine.get(), R"JS(
      globalThis.__malformedPicker = { synchronous: false, promise: false, name: 'pending' };
      try {
        const invalid = showOpenFilePicker({
          types: Array.from({length: 33}, () => ({
            accept: {'application/octet-stream': ['.bin']}
          }))
        });
        __malformedPicker.promise = invalid instanceof Promise;
        invalid.catch(error => { __malformedPicker.name = error.name; });
      } catch (error) {
        __malformedPicker.synchronous = true;
        __malformedPicker.name = error.name;
      }
    )JS", "file-picker-malformed-options.js");
    const auto malformed_state = evaluate(engine.get(), "__malformedPicker",
        "file-picker-malformed-options-result.js");
    require(malformed_state.find("\"synchronous\":false") != std::string::npos
            && malformed_state.find("\"promise\":true") != std::string::npos
            && malformed_state.find("\"name\":\"TypeError\"") != std::string::npos,
        "malformed picker options did not reject a Promise: " + malformed_state);
    require(webscene_engine_take_file_panel_request_v2(engine.get()) == nullptr,
        "malformed picker options reached the native broker");

    struct status_case {
        uint32_t status;
        const char* expected_name;
    };
    constexpr status_case status_cases[] = {
        {WEBSCENE_FILE_PANEL_CANCELLED_V2, "AbortError"},
        {WEBSCENE_FILE_PANEL_DENIED_V2, "NotAllowedError"},
        {WEBSCENE_FILE_PANEL_ERROR_V2, "OperationError"}};
    for (uint64_t index = 0; index < std::size(status_cases); ++index) {
        activate_file_picker(engine.get(), 50010U + index);
        execute(engine.get(), R"JS(
          globalThis.__pickerFailure='pending';
          showOpenFilePicker().then(
            () => { __pickerFailure='unexpected'; },
            error => { __pickerFailure=error.name; });
        )JS", "file-picker-status-mapping.js");
        request = take_file_panel_request(engine.get());
        const auto request_id = request->request_id;
        webscene_file_panel_request_release_v2(request);
        require(complete_file_panel_status(engine.get(), request_id,
                    status_cases[index].status, "bounded native failure"),
            "file picker status completion was rejected");
        require(evaluate(engine.get(), "__pickerFailure",
                    "file-picker-status-mapping-result.js")
                == std::string("\"") + status_cases[index].expected_name + "\"",
            "file picker completion status mapped to the wrong DOMException");
        require(!complete_file_panel_status(engine.get(), request_id,
                    status_cases[index].status),
            "file picker completion settled more than once");
    }
}

uint64_t file_system_access_test_peak_rss_bytes()
{
#if defined(__APPLE__) || defined(__linux__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0U;
#if defined(__APPLE__)
    return static_cast<uint64_t>(usage.ru_maxrss);
#else
    return static_cast<uint64_t>(usage.ru_maxrss) * 1024U;
#endif
#else
    return 0U;
#endif
}

uint64_t file_system_access_test_open_fd_count()
{
#if defined(__APPLE__) || defined(__linux__)
    std::error_code error;
    uint64_t count = 0U;
    for (std::filesystem::directory_iterator iterator("/dev/fd", error), end;
         !error && iterator != end; iterator.increment(error)) {
        ++count;
    }
    return error ? 0U : count;
#else
    return 0U;
#endif
}

void test_native_file_system_access_lifecycle_and_performance()
{
    std::unique_ptr<webscene_engine, decltype(&webscene_engine_destroy)> engine(
        webscene_engine_create(0), webscene_engine_destroy);
    require(engine != nullptr, "file picker lifecycle engine creation failed");
    execute(engine.get(), "globalThis.__pickerCycles=0;globalThis.__stableHandle=null",
        "file-picker-cycle-setup.js");
    webscene_engine_memory_metrics before{sizeof(before)};
    require(webscene_engine_get_memory_metrics(engine.get(), &before) != 0,
        "file picker baseline memory metrics unavailable");
    const auto rss_before = file_system_access_test_peak_rss_bytes();
    const std::vector<uint8_t> stable_grant{42U, 0U, 43U};
    const auto started = std::chrono::steady_clock::now();
    for (uint64_t cycle = 0; cycle < 100U; ++cycle) {
        activate_file_picker(engine.get(), 51000U + cycle);
        execute(engine.get(), R"JS(
          showOpenFilePicker().then(handles => {
            const handle=handles[0];
            if (__stableHandle === null) __stableHandle=handle;
            else if (__stableHandle === handle)
              throw new Error('a fresh picker result reused its wrapper');
            return __stableHandle.isSameEntry(handle).then(async same => {
              if (!same) throw new Error('stable native entry identity changed');
              const permissions=await Promise.all([
                handle.queryPermission(),
                handle.queryPermission({mode:'readwrite'}),
                handle.requestPermission(),
                handle.requestPermission({mode:'readwrite'})
              ]);
              if (permissions.join(',') !== 'granted,granted,granted,granted')
                throw new Error('stable native permissions changed');
              const file=await __stableHandle.getFile();
              if (file.size !== 1 || file.name !== 'stable.txt')
                throw new Error('stable native file metadata changed');
              const writer=await __stableHandle.createWritable();
              await writer.write(new Uint8Array([__pickerCycles & 255]));
              await writer.close();
              ++__pickerCycles;
            });
          });
        )JS", "file-picker-cycle.js");
        const auto* request = take_file_panel_request(engine.get());
        const auto request_id = request->request_id;
        webscene_file_panel_request_release_v2(request);
        require(complete_file_panel_success(
            engine.get(), request_id, WEBSCENE_FILE_PANEL_ENTRY_FILE_V2,
            WEBSCENE_FILE_PANEL_GRANT_READ_V2
                | WEBSCENE_FILE_PANEL_GRANT_WRITE_V2,
            "stable.txt", stable_grant),
            "file picker lifecycle completion was rejected");
        const auto* comparison =
            take_file_grant_same_entry_request(engine.get());
        require(comparison->first_grant_id.byte_count == stable_grant.size()
                && comparison->second_grant_id.byte_count == stable_grant.size(),
            "same-entry lifecycle query lost the stable grant");
        const auto comparison_id = comparison->request_id;
        webscene_file_grant_same_entry_request_release_v2(comparison);
        require(complete_file_grant_same_entry(
            engine.get(), comparison_id, true, true),
            "same-entry lifecycle completion was rejected");
        const auto* read = take_file_grant_read_request(engine.get());
        require(read->grant_id.byte_count == stable_grant.size()
                && read->offset == 0U && read->maximum_bytes == 1U,
            "getFile lifecycle query lost its bounded stable grant");
        const auto read_id = read->request_id;
        webscene_file_grant_read_request_release_v2(read);
        require(complete_file_grant_read(engine.get(), read_id,
                WEBSCENE_FILE_GRANT_READ_SUCCESS_V2, 1U,
                1700000000000000000LL, 0U, {42U}, true),
            "getFile lifecycle completion was rejected");
        const auto* write_begin = take_file_grant_write_request(engine.get());
        require(write_begin->action == WEBSCENE_FILE_GRANT_WRITE_BEGIN_V2
                && write_begin->grant_id.byte_count == stable_grant.size(),
            "writer lifecycle begin lost the stable grant");
        const auto write_begin_id = write_begin->request_id;
        webscene_file_grant_write_request_release_v2(write_begin);
        const std::vector<uint8_t> transaction(16U,
            static_cast<uint8_t>(cycle));
        require(complete_file_grant_write(engine.get(), write_begin_id,
                WEBSCENE_FILE_GRANT_WRITE_BEGIN_V2,
                WEBSCENE_FILE_GRANT_WRITE_SUCCESS_V2, transaction),
            "writer lifecycle begin completion was rejected");
        const auto* write_chunk = take_file_grant_write_request(engine.get());
        require(write_chunk->action == WEBSCENE_FILE_GRANT_WRITE_CHUNK_V2
                && write_chunk->offset == 0U && write_chunk->byte_count == 1U
                && write_chunk->data[0] == static_cast<uint8_t>(cycle),
            "writer lifecycle chunk changed");
        const auto write_chunk_id = write_chunk->request_id;
        webscene_file_grant_write_request_release_v2(write_chunk);
        require(complete_file_grant_write(engine.get(), write_chunk_id,
                WEBSCENE_FILE_GRANT_WRITE_CHUNK_V2,
                WEBSCENE_FILE_GRANT_WRITE_SUCCESS_V2, {}, 0U, 1U),
            "writer lifecycle chunk completion was rejected");
        const auto* write_commit = take_file_grant_write_request(engine.get());
        require(write_commit->action == WEBSCENE_FILE_GRANT_WRITE_COMMIT_V2
                && write_commit->final_byte_count == 1U,
            "writer lifecycle commit changed");
        const auto write_commit_id = write_commit->request_id;
        webscene_file_grant_write_request_release_v2(write_commit);
        require(complete_file_grant_write(engine.get(), write_commit_id,
                WEBSCENE_FILE_GRANT_WRITE_COMMIT_V2,
                WEBSCENE_FILE_GRANT_WRITE_SUCCESS_V2, {}, 0U, 0U, 1U),
            "writer lifecycle commit completion was rejected");
    }
    require(evaluate(engine.get(), "__pickerCycles",
                "file-picker-cycle-result.js") == "100",
        "file picker lifecycle promises did not settle exactly once");
    const auto clone_state = evaluate(engine.get(), R"JS((() => {
      globalThis.__lifetimeClone=structuredClone(__stableHandle);
      return {
        distinct:__lifetimeClone!==__stableHandle,
        instance:__lifetimeClone instanceof FileSystemFileHandle,
        base:__lifetimeClone instanceof FileSystemHandle,
        tag:Object.prototype.toString.call(__lifetimeClone),
        name:__lifetimeClone.name,
        kind:__lifetimeClone.kind,
        keys:Object.keys(__lifetimeClone).length,
        release:typeof __lifetimeClone.release
      };
    })())JS", "file-handle-structured-clone.js");
    require(clone_state.find("\"distinct\":true") != std::string::npos
            && clone_state.find("\"instance\":true") != std::string::npos
            && clone_state.find("\"base\":true") != std::string::npos
            && clone_state.find("[object FileSystemFileHandle]") != std::string::npos
            && clone_state.find("\"name\":\"stable.txt\"") != std::string::npos
            && clone_state.find("\"kind\":\"file\"") != std::string::npos
            && clone_state.find("\"keys\":0") != std::string::npos
            && clone_state.find("\"release\":\"undefined\"")
                != std::string::npos,
        "structured clone diverged from Chrome FileSystemHandle semantics");
    execute(engine.get(), R"JS(
      globalThis.__clonePermissions='pending';
      Promise.all([
        __lifetimeClone.queryPermission(),
        __lifetimeClone.queryPermission({mode:'readwrite'}),
        __lifetimeClone.requestPermission(),
        __lifetimeClone.requestPermission({mode:'readwrite'})
      ]).then(values=>{__clonePermissions=values},
        error=>{__clonePermissions=error.name});
    )JS", "file-handle-clone-permissions.js");
    require(evaluate(engine.get(), "__clonePermissions",
                "file-handle-clone-permissions-result.js")
            == "[\"granted\",\"granted\",\"granted\",\"granted\"]",
        "structured clone lost capability-exact permission state");
    execute(engine.get(), R"JS(
      globalThis.__cloneSame='pending';
      __lifetimeClone.isSameEntry(__stableHandle).then(
        value=>{__cloneSame=value}, error=>{__cloneSame=error.name});
    )JS", "file-handle-clone-same-entry.js");
    const auto* clone_comparison = take_file_grant_same_entry_request(engine.get());
    require(clone_comparison->first_grant_id.byte_count == stable_grant.size()
            && clone_comparison->second_grant_id.byte_count == stable_grant.size(),
        "structured clone lost its shared opaque authority");
    const auto clone_comparison_id = clone_comparison->request_id;
    webscene_file_grant_same_entry_request_release_v2(clone_comparison);
    require(complete_file_grant_same_entry(
            engine.get(), clone_comparison_id, true, true),
        "structured-clone identity completion was rejected");
    require(evaluate(engine.get(), "__cloneSame",
                "file-handle-clone-same-entry-result.js") == "true",
        "structured clone no longer identifies the same native entry");
    execute(engine.get(), "globalThis.__lifetimeClone=null",
        "file-handle-clone-release-reference.js");
    require(webscene_engine_request_low_memory(engine.get()) != 0,
        "file handle weak-owner collection request failed");
    require(evaluate(engine.get(), "true", "file-handle-gc-barrier.js") == "true",
        "file handle GC barrier failed");
    require(webscene_engine_take_file_grant_release_request_v2(engine.get()) == nullptr,
        "shared grant released while its stable wrapper was still alive");
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    require(elapsed < 5.0, "100 file picker cycles exceeded five seconds");
    webscene_engine_memory_metrics after{sizeof(after)};
    require(webscene_engine_get_memory_metrics(engine.get(), &after) != 0,
        "file picker final memory metrics unavailable");
    require(after.v8_used_heap_bytes <= before.v8_used_heap_bytes + 8U * 1024U * 1024U
            && after.v8_external_bytes <= before.v8_external_bytes + 2U * 1024U * 1024U,
        "100 stable-grant cycles exceeded the heap/external-memory budget");
    const auto rss_after = file_system_access_test_peak_rss_bytes();
    constexpr uint64_t maximum_rss_growth = 64U * 1024U * 1024U;
    require(rss_before == 0U || rss_after <= rss_before + maximum_rss_growth,
        "100 stable-grant cycles exceeded the 64 MiB peak RSS budget");
    require(webscene_engine_take_file_panel_request_v2(engine.get()) == nullptr,
        "file picker request queue did not drain after 100 cycles");
    require(webscene_engine_take_file_grant_same_entry_request_v2(engine.get())
                == nullptr,
        "same-entry request queue did not drain after 100 cycles");
    require(webscene_engine_take_file_grant_ancestry_request_v2(engine.get())
                == nullptr,
        "ancestry request queue did not drain after 100 cycles");
    require(webscene_engine_take_file_grant_read_request_v2(engine.get()) == nullptr,
        "file read request queue did not drain after 100 cycles");
    require(webscene_engine_take_file_grant_write_request_v2(engine.get()) == nullptr,
        "file write request queue did not drain after 100 cycles");
    require(webscene_engine_take_file_grant_create_file_request_v2(engine.get())
                == nullptr,
        "file create request queue did not drain after 100 cycles");

    activate_file_picker(engine.get(), 51999U);
    execute(engine.get(), R"JS(
      globalThis.__weakHandle=null;
      showOpenFilePicker().then(handles=>{__weakHandle=handles[0]});
    )JS", "file-handle-weak-owner-picker.js");
    const auto* weak_picker = take_file_panel_request(engine.get());
    const auto weak_picker_id = weak_picker->request_id;
    webscene_file_panel_request_release_v2(weak_picker);
    const std::vector<uint8_t> weak_grant{91U, 0U, 92U, 93U};
    require(complete_file_panel_success(
            engine.get(), weak_picker_id, WEBSCENE_FILE_PANEL_ENTRY_FILE_V2,
            WEBSCENE_FILE_PANEL_GRANT_READ_V2, "weak.txt", weak_grant),
        "weak-owner picker completion was rejected");
    require(evaluate(engine.get(), "__weakHandle.name",
                "file-handle-weak-owner-ready.js") == "\"weak.txt\"",
        "weak-owner handle did not settle");
    execute(engine.get(), "globalThis.__weakHandle=null",
        "file-handle-weak-owner-drop.js");
    for (int attempt = 0; attempt < 3; ++attempt) {
        require(webscene_engine_request_low_memory(engine.get()) != 0,
            "weak-owner low-memory collection request failed");
        require(evaluate(engine.get(), "true", "file-handle-weak-owner-gc.js")
                == "true",
            "weak-owner GC barrier failed");
    }
    const auto* weak_release = take_file_grant_release_request(engine.get());
    require(weak_release->grant_id.byte_count == weak_grant.size()
            && std::memcmp(weak_release->grant_id.data,
                weak_grant.data(), weak_grant.size()) == 0,
        "final weak wrapper released the wrong grant");
    webscene_file_grant_release_request_release_v2(weak_release);
    require(webscene_engine_take_file_grant_release_request_v2(engine.get()) == nullptr,
        "weak wrapper released its grant more than once");

    activate_file_picker(engine.get(), 51998U);
    execute(engine.get(), R"JS(
      globalThis.__directoryHandle=null;
      showDirectoryPicker().then(handle=>{__directoryHandle=handle});
    )JS", "file-handle-lifetime-directory-picker.js");
    const auto* directory_picker = take_file_panel_request(engine.get());
    require(directory_picker->kind == WEBSCENE_FILE_PANEL_OPEN_DIRECTORY_V2,
        "lifetime directory picker changed kind");
    const auto directory_picker_id = directory_picker->request_id;
    webscene_file_panel_request_release_v2(directory_picker);
    const std::vector<uint8_t> directory_grant{71U, 0U, 72U};
    require(complete_file_panel_success(
            engine.get(), directory_picker_id,
            WEBSCENE_FILE_PANEL_ENTRY_DIRECTORY_V2,
            WEBSCENE_FILE_PANEL_GRANT_ENUMERATE_V2
                | WEBSCENE_FILE_PANEL_GRANT_CREATE_V2
                | WEBSCENE_FILE_PANEL_GRANT_WRITE_V2,
            "lifetime", directory_grant),
        "lifetime directory picker completion was rejected");
    require(evaluate(engine.get(), "__directoryHandle.name",
                "file-handle-lifetime-directory-ready.js") == "\"lifetime\"",
        "lifetime directory handle did not settle");

    activate_file_picker(engine.get(), 52000U);
    execute(engine.get(), R"JS(
      globalThis.__retiredPicker='pending';
      showOpenFilePicker().then(
        ()=>{__retiredPicker='unexpected'},
        error=>{__retiredPicker=error.name});
    )JS", "file-picker-before-navigation.js");
    const auto* stale = take_file_panel_request(engine.get());
    const auto stale_id = stale->request_id;
    webscene_file_panel_request_release_v2(stale);
    execute(engine.get(), R"JS(
      globalThis.__retiredIdentity='pending';
      __stableHandle.isSameEntry(__stableHandle).then(
        ()=>{__retiredIdentity='unexpected'},
        error=>{__retiredIdentity=error.name});
    )JS", "file-picker-same-entry-before-navigation.js");
    const auto* stale_identity =
        take_file_grant_same_entry_request(engine.get());
    const auto stale_identity_id = stale_identity->request_id;
    webscene_file_grant_same_entry_request_release_v2(stale_identity);
    execute(engine.get(), R"JS(
      globalThis.__retiredRead='pending';
      __stableHandle.getFile().then(
        ()=>{__retiredRead='unexpected'},
        error=>{__retiredRead=error.name});
    )JS", "file-picker-read-before-navigation.js");
    const auto* stale_read = take_file_grant_read_request(engine.get());
    const auto stale_read_id = stale_read->request_id;
    webscene_file_grant_read_request_release_v2(stale_read);
    execute(engine.get(), R"JS(
      globalThis.__retiredWrite='pending';
      __stableHandle.createWritable().then(
        ()=>{__retiredWrite='unexpected'},
        error=>{__retiredWrite=error.name});
    )JS", "file-picker-write-before-navigation.js");
    const auto* stale_write = take_file_grant_write_request(engine.get());
    const auto stale_write_id = stale_write->request_id;
    webscene_file_grant_write_request_release_v2(stale_write);
    execute(engine.get(), R"JS(
      globalThis.__retiredDirectory='pending';
      __directoryHandle.entries().next().then(
        ()=>{__retiredDirectory='unexpected'},
        error=>{__retiredDirectory=error.name});
    )JS", "file-picker-directory-before-navigation.js");
    const auto* stale_directory =
        take_file_grant_directory_request(engine.get());
    const auto stale_directory_id = stale_directory->request_id;
    webscene_file_grant_directory_request_release_v2(stale_directory);
    execute(engine.get(), R"JS(
      globalThis.__retiredCreateFile='pending';
      __directoryHandle.getFileHandle('stale.txt', {create:true}).then(
        ()=>{__retiredCreateFile='unexpected'},
        error=>{__retiredCreateFile=error.name});
    )JS", "file-picker-create-file-before-navigation.js");
    const auto* stale_create_file =
        take_file_grant_create_file_request(engine.get());
    const auto stale_create_file_id = stale_create_file->request_id;
    webscene_file_grant_create_file_request_release_v2(stale_create_file);
    execute(engine.get(), R"JS(
      globalThis.__retiredCreateDirectory='pending';
      __directoryHandle.getDirectoryHandle('stale', {create:true}).then(
        ()=>{__retiredCreateDirectory='unexpected'},
        error=>{__retiredCreateDirectory=error.name});
    )JS", "file-picker-create-directory-before-navigation.js");
    const auto* stale_create_directory =
        take_file_grant_create_directory_request(engine.get());
    const auto stale_create_directory_id = stale_create_directory->request_id;
    webscene_file_grant_create_directory_request_release_v2(
        stale_create_directory);
    execute(engine.get(), R"JS(
      globalThis.__retiredRemove='pending';
      __directoryHandle.removeEntry('stale').then(
        ()=>{__retiredRemove='unexpected'},
        error=>{__retiredRemove=error.name});
    )JS", "file-picker-remove-before-navigation.js");
    const auto* stale_remove = take_file_grant_remove_request(engine.get());
    const auto stale_remove_id = stale_remove->request_id;
    webscene_file_grant_remove_request_release_v2(stale_remove);
    execute(engine.get(), R"JS(
      globalThis.__retiredAncestry='pending';
      __directoryHandle.resolve(__stableHandle).then(
        ()=>{__retiredAncestry='unexpected'},
        error=>{__retiredAncestry=error.name});
    )JS", "file-picker-ancestry-before-navigation.js");
    const auto* stale_ancestry =
        take_file_grant_ancestry_request(engine.get());
    const auto stale_ancestry_id = stale_ancestry->request_id;
    webscene_file_grant_ancestry_request_release_v2(stale_ancestry);
    const auto path = std::filesystem::temp_directory_path()
        / ("webscene-picker-navigation-"
            + std::to_string(std::chrono::steady_clock::now()
                .time_since_epoch().count()) + ".html");
    { std::ofstream file(path); file << "<!doctype html><script>globalThis.navigationReady=true</script>"; }
    const auto root = path.parent_path().string();
    webscene_engine_set_resource_root(engine.get(), root.data(), root.size());
    const auto url = "file://" + path.string();
    require(webscene_engine_load_url(engine.get(), url.data(), url.size()) != 0,
        "file picker lifecycle navigation failed");
    for (int attempt = 0; attempt < 100
            && evaluate(engine.get(), "globalThis.navigationReady===true",
                "file-picker-navigation-ready.js") != "true"; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(!complete_file_panel_success(
        engine.get(), stale_id, WEBSCENE_FILE_PANEL_ENTRY_FILE_V2,
        WEBSCENE_FILE_PANEL_GRANT_READ_V2, "stale.txt", stable_grant),
        "stale file picker completion survived navigation retirement");
    require(!complete_file_grant_same_entry(
                engine.get(), stale_identity_id, true, true),
        "stale same-entry completion survived navigation retirement");
    require(!complete_file_grant_read(engine.get(), stale_read_id,
                WEBSCENE_FILE_GRANT_READ_SUCCESS_V2, 1U,
                1700000000000000000LL, 0U, {42U}, true),
        "stale file read completion survived navigation retirement");
    require(!complete_file_grant_write(engine.get(), stale_write_id,
                WEBSCENE_FILE_GRANT_WRITE_BEGIN_V2,
                WEBSCENE_FILE_GRANT_WRITE_SUCCESS_V2,
                std::vector<uint8_t>(16U, 9U)),
        "stale file write completion survived navigation retirement");
    require(!complete_file_grant_directory(engine.get(), stale_directory_id,
                WEBSCENE_FILE_GRANT_DIRECTORY_ENUMERATE_V2,
                WEBSCENE_FILE_GRANT_DIRECTORY_SUCCESS_V2),
        "stale directory completion survived navigation retirement");
    require(!complete_file_grant_create_file(engine.get(),
                stale_create_file_id,
                WEBSCENE_FILE_GRANT_CREATE_FILE_SUCCESS_V2,
                "stale.txt", {99U}),
        "stale create-file completion survived navigation retirement");
    require(!complete_file_grant_create_directory(engine.get(),
                stale_create_directory_id,
                WEBSCENE_FILE_GRANT_CREATE_DIRECTORY_SUCCESS_V2,
                "stale", {100U}),
        "stale create-directory completion survived navigation retirement");
    require(!complete_file_grant_remove(engine.get(), stale_remove_id,
                WEBSCENE_FILE_GRANT_REMOVE_SUCCESS_V2),
        "stale remove completion survived navigation retirement");
    require(!complete_file_grant_ancestry(engine.get(), stale_ancestry_id,
                WEBSCENE_FILE_GRANT_ANCESTRY_SUCCESS_V2,
                {"stable.txt"}),
        "stale ancestry completion survived navigation retirement");
    bool released_stable = false;
    bool released_directory = false;
    for (int index = 0; index < 2; ++index) {
        const auto* released = take_file_grant_release_request(engine.get());
        const auto matches = [&](const std::vector<uint8_t>& expected) {
            return released->grant_id.byte_count == expected.size()
                && std::memcmp(released->grant_id.data,
                    expected.data(), expected.size()) == 0;
        };
        released_stable = released_stable || matches(stable_grant);
        released_directory = released_directory || matches(directory_grant);
        webscene_file_grant_release_request_release_v2(released);
    }
    require(released_stable && released_directory,
        "navigation did not release each live opaque grant exactly once");
    require(webscene_engine_take_file_grant_release_request_v2(engine.get()) == nullptr,
        "navigation released the same grant more than once");
    std::filesystem::remove(path);
    std::cout << "File System Access lifecycle gate: cycles=100 elapsedSeconds="
              << elapsed << " heapDelta="
              << static_cast<int64_t>(after.v8_used_heap_bytes)
                    - static_cast<int64_t>(before.v8_used_heap_bytes)
              << " externalDelta="
              << static_cast<int64_t>(after.v8_external_bytes)
                    - static_cast<int64_t>(before.v8_external_bytes)
              << " rssBefore=" << rss_before << " rssAfter=" << rss_after
              << " queuedRequests=0" << '\n';
}
