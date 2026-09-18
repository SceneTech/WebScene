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
        __pickerState = {
          length: handles.length,
          kinds: handles.map(handle => handle.kind),
          names: handles.map(handle => handle.name),
          instance: handles.every(handle => handle instanceof FileSystemFileHandle),
          base: handles.every(handle => handle instanceof FileSystemHandle),
          tag: Object.prototype.toString.call(handles[0]),
          read: null
        };
        return handles[0].queryPermission().then(value => {
          __pickerState.read = value;
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
            && opened_state.find("\"read\":\"granted\"") != std::string::npos,
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
        __saveState = `${handle.kind}:${handle.name}:${handle instanceof FileSystemFileHandle}`;
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
    require(evaluate(engine.get(), "__saveState", "file-picker-save-result.js")
            == "\"file:drawing.json:true\"",
        "save picker handle contract changed");
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
            if (__stableHandle !== handle) throw new Error('unstable grant identity');
            return __stableHandle.isSameEntry(handle).then(async same => {
              if (!same) throw new Error('stable native entry identity changed');
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
    require(webscene_engine_take_file_grant_read_request_v2(engine.get()) == nullptr,
        "file read request queue did not drain after 100 cycles");
    require(webscene_engine_take_file_grant_write_request_v2(engine.get()) == nullptr,
        "file write request queue did not drain after 100 cycles");

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
