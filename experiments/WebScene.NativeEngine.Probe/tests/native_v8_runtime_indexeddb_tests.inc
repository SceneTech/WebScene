size_t load_indexeddb_test_document(
    void*,
    uint32_t kind,
    const char* url,
    size_t url_length,
    const char*,
    size_t,
    int64_t,
    char* destination,
    size_t destination_capacity)
{
    constexpr std::string_view content{
        "<!doctype html><title>IndexedDB origin fixture</title>"};
    constexpr size_t header_size = 2U + sizeof(uint32_t) + sizeof(int64_t) * 2U;
    const auto address = std::string_view(url, url_length);
    if (kind != WEBSCENE_RESOURCE_DOCUMENT
        || !address.starts_with("http://127.0.0.1:")
        || !address.ends_with('/')) {
        return 0U;
    }
    const auto required = header_size + content.size();
    if (destination == nullptr || destination_capacity < required) return required;
    destination[0] = 1;
    destination[1] = 0;
    const uint32_t entity_tag_length = 0U;
    const int64_t timestamp = 0;
    std::memcpy(destination + 2U, &entity_tag_length, sizeof(entity_tag_length));
    std::memcpy(destination + 2U + sizeof(entity_tag_length),
        &timestamp, sizeof(timestamp));
    std::memcpy(destination + 2U + sizeof(entity_tag_length) + sizeof(timestamp),
        &timestamp, sizeof(timestamp));
    std::copy(content.begin(), content.end(), destination + header_size);
    return required;
}

webscene_engine* create_indexeddb_engine(
    const std::filesystem::path& root,
    std::string_view partition,
    uint16_t port,
    uint64_t quota = 4U * 1024U * 1024U)
{
    const auto root_text = root.string();
    webscene_engine_options options{};
    options.struct_size = sizeof(options);
    options.storage_directory = root_text.data();
    options.storage_directory_length = root_text.size();
    options.storage_partition_key = partition.data();
    options.storage_partition_key_length = partition.size();
    options.storage_quota_bytes = quota;
    options.resource_load_callback = load_indexeddb_test_document;
    auto* engine = webscene_engine_create_with_options(&options);
    if (engine == nullptr) return nullptr;
    const auto address = std::string("http://127.0.0.1:")
        + std::to_string(port) + '/';
    if (webscene_engine_load_url(engine, address.data(), address.size()) == 0U
        || evaluate_until_equals(engine, "location.href",
               "indexeddb-initial-origin.js", '"' + address + '"', 2500)
            != '"' + address + '"'
        || take_typed_host_request(engine)) {
        webscene_engine_destroy(engine);
        return nullptr;
    }
    return engine;
}

void wait_for_indexeddb_flag(webscene_engine* engine, std::string_view expression)
{
    require(
        evaluate_until_equals(
            engine, expression, "indexeddb-completion-probe.js", "true", 2500)
            == "true",
        "IndexedDB operation did not settle: " + std::string(expression));
}

const webscene_file_grant_durable_request_v2*
take_indexeddb_durable_request(webscene_engine* engine)
{
    const webscene_file_grant_durable_request_v2* request = nullptr;
    for (int attempt = 0; attempt < 500 && request == nullptr; ++attempt) {
        request = webscene_engine_take_file_grant_durable_request_v2(engine);
        if (request == nullptr) {
            (void)evaluate(engine, "true", "indexeddb-durable-pump.js");
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(request != nullptr, "durable file grant request was not published");
    return request;
}

bool complete_indexeddb_durable_request(
    webscene_engine* engine,
    std::uint64_t request_id,
    std::uint32_t action,
    std::uint32_t status,
    const std::array<std::uint8_t,
        WEBSCENE_FILE_GRANT_DURABLE_LOCATOR_BYTES_V2>& locator,
    const std::vector<std::uint8_t>& grant = {},
    std::string_view name = {})
{
    const bool success = status == WEBSCENE_FILE_GRANT_DURABLE_SUCCESS_V2;
    const bool revoke = action == WEBSCENE_FILE_GRANT_DURABLE_REVOKE_V2;
    const webscene_file_grant_durable_completion_v2 completion{
        sizeof(completion), 2U, request_id, status, action,
        {success ? locator.data() : nullptr, success ? locator.size() : 0U},
        {success && !grant.empty() ? grant.data() : nullptr,
            success ? grant.size() : 0U},
        success && !revoke ? WEBSCENE_FILE_PANEL_ENTRY_FILE_V2 : 0U,
        success && !revoke ? WEBSCENE_FILE_PANEL_GRANT_READ_V2 : 0U,
        {success && !revoke ? name.data() : nullptr,
            success && !revoke ? name.size() : 0U}, {}};
    return webscene_engine_complete_file_grant_durable_request_v2(
        engine, &completion) != 0;
}

void test_indexeddb_file_system_handle_persistence()
{
    const auto root = std::filesystem::temp_directory_path()
        / ("webscene-indexeddb-file-handles-"
            + std::to_string(std::chrono::steady_clock::now()
                .time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const auto cleanup = [&] {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    };
    constexpr std::string_view partition{"code-oss-durable-profile"};
    constexpr std::string_view expected_origin{"http://loopback"};
    const std::vector<std::uint8_t> live_grant{1U, 0U, 2U, 255U};
    const std::vector<std::uint8_t> restored_grant{9U, 0U, 8U, 7U};
    std::array<std::uint8_t, WEBSCENE_FILE_GRANT_DURABLE_LOCATOR_BYTES_V2>
        locator{};
    for (std::size_t index = 0; index < locator.size(); ++index)
        locator[index] = static_cast<std::uint8_t>(index + 11U);

    auto* engine = create_indexeddb_engine(root, partition, 43001U);
    require(engine != nullptr, "durable IndexedDB engine creation failed");
    const webscene_input_event activation{
        WEBSCENE_INPUT_POINTER_DOWN, 1U | (1U << 8U), 43001U,
        1.0, 1.0, 0.0, 0.0};
    require(webscene_engine_enqueue(engine, &activation) != 0,
        "durable picker activation was rejected");
    require(evaluate(engine, "true", "indexeddb-picker-activation.js") == "true",
        "durable picker activation did not drain");
    execute(engine, R"JS(
      globalThis.__durablePicker='pending';
      showOpenFilePicker().then(handles=>{
        globalThis.__durableHandle=handles[0];
        __durablePicker='ready';
      }, error=>{__durablePicker=error.name});
    )JS", "indexeddb-durable-picker.js");
    const webscene_file_panel_request_v2* panel = nullptr;
    for (int attempt = 0; attempt < 250 && panel == nullptr; ++attempt) {
        panel = webscene_engine_take_file_panel_request_v2(engine);
        if (panel == nullptr)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(panel != nullptr, "durable picker request was not published");
    const auto panel_id = panel->request_id;
    webscene_file_panel_request_release_v2(panel);
    constexpr std::string_view file_name{"durable🙂.txt"};
    const webscene_file_panel_entry_v2 entry{
        sizeof(entry), 2U, WEBSCENE_FILE_PANEL_ENTRY_FILE_V2,
        WEBSCENE_FILE_PANEL_GRANT_READ_V2,
        {file_name.data(), file_name.size()},
        {live_grant.data(), live_grant.size()}};
    const webscene_file_panel_completion_v2 opened{
        sizeof(opened), 2U, panel_id, WEBSCENE_FILE_PANEL_SUCCESS_V2, 0U,
        &entry, 1U, {}, {}};
    require(webscene_engine_complete_file_panel_request_v2(engine, &opened) != 0,
        "durable picker completion was rejected");
    require(evaluate_until_equals(engine, "__durablePicker",
                "indexeddb-durable-picker-result.js", "\"ready\"", 2500)
            == "\"ready\"",
        "durable picker did not produce a handle");

    execute_and_wait(engine, R"JS(
      globalThis.__durableStore={done:false};
      (async()=>{
        const opened=await new Promise((resolve,reject)=>{
          const request=indexedDB.open('handles',1);
          request.onupgradeneeded=()=>request.result.createObjectStore('store');
          request.onsuccess=()=>resolve(request.result);
          request.onerror=()=>reject(request.error);
        });
        const tx=opened.transaction('store','readwrite');
        tx.objectStore('store').put(
          {handle:__durableHandle,bytes:new Uint8Array([7,8])},'key');
        await new Promise((resolve,reject)=>{
          tx.oncomplete=resolve; tx.onabort=tx.onerror=()=>reject(tx.error);
        });
        opened.close();
        __durableStore={done:true};
      })().catch(error=>{__durableStore={done:true,error:error.name}});
    )JS", "indexeddb-durable-store.js");
    auto* durable = take_indexeddb_durable_request(engine);
    require(durable->action == WEBSCENE_FILE_GRANT_DURABLE_EXPORT_V2
            && durable->grant_id.byte_count == live_grant.size()
            && std::memcmp(durable->grant_id.data,
                live_grant.data(), live_grant.size()) == 0
            && std::string_view(durable->storage_partition.data,
                durable->storage_partition.byte_count) == partition
            && std::string_view(durable->serialized_origin.data,
                durable->serialized_origin.byte_count) == expected_origin,
        "durable export lost its opaque grant or storage binding");
    auto durable_id = durable->request_id;
    webscene_file_grant_durable_request_release_v2(durable);
    require(complete_indexeddb_durable_request(engine, durable_id,
            WEBSCENE_FILE_GRANT_DURABLE_EXPORT_V2,
            WEBSCENE_FILE_GRANT_DURABLE_SUCCESS_V2, locator, {}, file_name),
        "durable export completion was rejected");
    wait_for_indexeddb_flag(engine, "__durableStore.done===true");
    require(evaluate(engine, "!__durableStore.error",
                "indexeddb-durable-store-result.js") == "true",
        "IndexedDB durable store failed");
    webscene_engine_destroy(engine);

    engine = create_indexeddb_engine(root, partition, 43002U);
    require(engine != nullptr, "durable IndexedDB restart engine failed");
    execute_and_wait(engine, R"JS(
      globalThis.__durableLoad={done:false};
      (async()=>{
        const opened=await new Promise((resolve,reject)=>{
          const request=indexedDB.open('handles');
          request.onsuccess=()=>resolve(request.result);
          request.onerror=()=>reject(request.error);
        });
        const tx=opened.transaction('store');
        const value=await new Promise((resolve,reject)=>{
          const request=tx.objectStore('store').get('key');
          request.onsuccess=()=>resolve(request.result);
          request.onerror=()=>reject(request.error);
        });
        globalThis.__restoredHandle=value.handle;
        const permissions=await Promise.all([
          value.handle.queryPermission(),
          value.handle.queryPermission({mode:'readwrite'}),
          value.handle.requestPermission(),
          value.handle.requestPermission({mode:'readwrite'})
        ]);
        __durableLoad={done:true,file:value.handle instanceof FileSystemFileHandle,
          name:value.handle.name,bytes:Array.from(value.bytes),permissions};
        opened.close();
      })().catch(error=>{__durableLoad={done:true,error:error.name}});
    )JS", "indexeddb-durable-load.js");
    durable = take_indexeddb_durable_request(engine);
    require(durable->action == WEBSCENE_FILE_GRANT_DURABLE_RESTORE_V2
            && durable->locator.byte_count == locator.size()
            && std::memcmp(durable->locator.data,
                locator.data(), locator.size()) == 0
            && std::string_view(durable->storage_partition.data,
                durable->storage_partition.byte_count) == partition
            && std::string_view(durable->serialized_origin.data,
                durable->serialized_origin.byte_count) == expected_origin,
        "durable restore lost its locator or storage binding");
    durable_id = durable->request_id;
    webscene_file_grant_durable_request_release_v2(durable);
    require(complete_indexeddb_durable_request(engine, durable_id,
            WEBSCENE_FILE_GRANT_DURABLE_RESTORE_V2,
            WEBSCENE_FILE_GRANT_DURABLE_SUCCESS_V2, locator,
            restored_grant, file_name),
        "durable restore completion was rejected");
    wait_for_indexeddb_flag(engine, "__durableLoad.done===true");
    const auto durable_load_result = evaluate(engine,
        "__durableLoad.file===true&&__durableLoad.name==='durable🙂.txt'"
        "&&JSON.stringify(__durableLoad.bytes)==='[7,8]'"
        "&&JSON.stringify(__durableLoad.permissions)==="
        "'[\"granted\",\"denied\",\"granted\",\"denied\"]'",
        "indexeddb-durable-load-result.js");
    require(durable_load_result == "true",
        "IndexedDB did not rehydrate the file handle after restart: "
            + durable_load_result);

    execute_and_wait(engine, R"JS(
      globalThis.__durableDelete={done:false};
      const request=indexedDB.deleteDatabase('handles');
      request.onsuccess=()=>{__durableDelete={done:true}};
      request.onerror=()=>{__durableDelete={done:true,error:request.error?.name}};
    )JS", "indexeddb-durable-delete.js");
    durable = take_indexeddb_durable_request(engine);
    if (durable->action == WEBSCENE_FILE_GRANT_DURABLE_RESTORE_V2) {
        durable_id = durable->request_id;
        webscene_file_grant_durable_request_release_v2(durable);
        require(complete_indexeddb_durable_request(engine, durable_id,
                WEBSCENE_FILE_GRANT_DURABLE_RESTORE_V2,
                WEBSCENE_FILE_GRANT_DURABLE_SUCCESS_V2, locator,
                restored_grant, file_name),
            "durable delete preflight restore was rejected");
        durable = take_indexeddb_durable_request(engine);
    }
    require(durable->action == WEBSCENE_FILE_GRANT_DURABLE_REVOKE_V2
            && durable->locator.byte_count == locator.size(),
        "IndexedDB deletion did not revoke its durable locator: action="
            + std::to_string(durable->action) + " locator="
            + std::to_string(durable->locator.byte_count));
    durable_id = durable->request_id;
    webscene_file_grant_durable_request_release_v2(durable);
    require(complete_indexeddb_durable_request(engine, durable_id,
            WEBSCENE_FILE_GRANT_DURABLE_REVOKE_V2,
            WEBSCENE_FILE_GRANT_DURABLE_SUCCESS_V2, locator),
        "durable revoke completion was rejected");
    wait_for_indexeddb_flag(engine, "__durableDelete.done===true");
    require(evaluate(engine, "!__durableDelete.error",
                "indexeddb-durable-delete-result.js") == "true",
        "IndexedDB durable deletion failed");

    execute_and_wait(engine, R"JS(
      globalThis.__staleDurable={done:false};
      (async()=>{
        const opened=await new Promise((resolve,reject)=>{
          const request=indexedDB.open('stale-handles',1);
          request.onupgradeneeded=()=>request.result.createObjectStore('store');
          request.onsuccess=()=>resolve(request.result);
          request.onerror=()=>reject(request.error);
        });
        const tx=opened.transaction('store','readwrite');
        tx.objectStore('store').put(__restoredHandle,'key');
        tx.oncomplete=()=>{__staleDurable={done:true};opened.close()};
        tx.onabort=tx.onerror=()=>{__staleDurable={done:true,error:tx.error?.name}};
      })().catch(error=>{__staleDurable={done:true,error:error.name}});
    )JS", "indexeddb-durable-stale-store.js");
    durable = take_indexeddb_durable_request(engine);
    require(durable->action == WEBSCENE_FILE_GRANT_DURABLE_EXPORT_V2,
        "stale-generation fixture did not export its handle");
    durable_id = durable->request_id;
    webscene_file_grant_durable_request_release_v2(durable);
    constexpr std::string_view navigated{"http://127.0.0.1:43003/"};
    require(webscene_engine_load_url(
                engine, navigated.data(), navigated.size()) != 0U
            && evaluate_until_equals(engine, "location.href",
                   "indexeddb-durable-stale-navigation.js",
                   "\"http://127.0.0.1:43003/\"", 2500)
                == "\"http://127.0.0.1:43003/\"",
        "stale-generation fixture did not navigate");
    require(!complete_indexeddb_durable_request(engine, durable_id,
            WEBSCENE_FILE_GRANT_DURABLE_EXPORT_V2,
            WEBSCENE_FILE_GRANT_DURABLE_SUCCESS_V2, locator, {}, file_name),
        "stale export completion crossed the retired generation");
    webscene_engine_destroy(engine);
    cleanup();
}

void test_indexeddb_persistence_and_transaction_contract()
{
    const auto root = std::filesystem::temp_directory_path()
        / ("webscene-indexeddb-runtime-"
            + std::to_string(std::chrono::steady_clock::now()
                .time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const auto cleanup = [&] {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    };

    auto* engine = create_indexeddb_engine(root, "code-oss-profile", 41001U);
    require(engine != nullptr, "IndexedDB engine creation failed");
    execute_and_wait(engine, R"JS(
      globalThis.indexedDbProbe = { done: false };
      let stage = 'open-v1';
      (async () => {
        const open = (version, upgrade) => new Promise((resolve, reject) => {
          const request = indexedDB.open('vscode-web-state-db-global', version);
          request.onupgradeneeded = event => upgrade?.(request.result, event);
          request.onerror = () => reject(request.error);
          request.onsuccess = () => resolve(request.result);
        });
        const transactionDone = transaction => new Promise((resolve, reject) => {
          transaction.oncomplete = resolve;
          transaction.onabort = transaction.onerror = () => reject(transaction.error);
        });
        let upgradeOld = -1;
        const database = await open(1, (db, event) => {
          upgradeOld = event.oldVersion;
          db.createObjectStore('ItemTable');
        });
        stage = 'write-v1';
        const write = database.transaction('ItemTable', 'readwrite');
        const source = {
          nested: { enabled: true },
          list: [1, 2, 3],
          bytes: new Uint8Array([0, 7, 255]),
          date: new Date(456),
          map: new Map([['theme', 'dark']])
        };
        write.objectStore('ItemTable').put(source, 'alpha');
        source.nested.enabled = false;
        source.bytes[1] = 99;
        source.map.set('theme', 'mutated');
        write.objectStore('ItemTable').put('second', 'beta');
        await transactionDone(write);

        stage = 'concurrent-writes';
        const concurrentWrites = [];
        for (let index = 0; index < 12; ++index) {
          const transaction = database.transaction('ItemTable', 'readwrite');
          transaction.objectStore('ItemTable').put(
            `value-${index}`, `parallel-${index}`);
          concurrentWrites.push(transactionDone(transaction));
        }
        await Promise.all(concurrentWrites);

        stage = 'abort-v1';
        const rolledBack = database.transaction('ItemTable', 'readwrite');
        rolledBack.objectStore('ItemTable').put('must-not-commit', 'alpha');
        let abortName = '';
        rolledBack.onabort = () => { abortName = rolledBack.error?.name || ''; };
        rolledBack.abort();
        await new Promise(resolve => setTimeout(resolve, 5));

        stage = 'cursor-v1';
        const cursorValues = [];
        const read = database.transaction('ItemTable', 'readonly');
        await new Promise((resolve, reject) => {
          const cursor = read.objectStore('ItemTable').openCursor();
          cursor.onerror = () => reject(cursor.error);
          cursor.onsuccess = () => {
            if (!cursor.result) { resolve(); return; }
            cursorValues.push([cursor.result.key, cursor.result.value]);
            cursor.result.continue();
          };
        });
        let versionChange = '';
        database.onversionchange = event => {
          versionChange = `${event.oldVersion}->${event.newVersion}`;
          database.close();
        };
        stage = 'open-v2';
        const upgraded = await open(2, db => db.createObjectStore('SecondTable'));
        globalThis.indexedDbProbe = {
          done: true,
          upgradeOld,
          abortName,
          versionChange,
          version: upgraded.version,
          stores: Array.from(upgraded.objectStoreNames),
          cursorValues
        };
        upgraded.close();
      })().catch(error => {
        globalThis.indexedDbProbe = {
          done: true, error: `${error?.name}:${error?.message}`,
          stage: typeof stage === 'string' ? stage : 'unknown'
        };
      });
    )JS", "indexeddb-runtime-contract.js");
    wait_for_indexeddb_flag(engine, "indexedDbProbe.done===true");
    const auto runtime_probe = evaluate(
        engine, "JSON.stringify(indexedDbProbe)", "indexeddb-probe-details.js");
    require(evaluate(engine, "!indexedDbProbe.error", "indexeddb-no-error.js") == "true",
        "IndexedDB runtime contract failed: " + runtime_probe);
    require(evaluate(engine,
        "indexedDbProbe.upgradeOld===0&&indexedDbProbe.abortName==='AbortError'"
        "&&indexedDbProbe.versionChange==='1->2'&&indexedDbProbe.version===2"
        "&&indexedDbProbe.stores.join(',')==='ItemTable,SecondTable'"
        "&&indexedDbProbe.cursorValues.length===14"
        "&&indexedDbProbe.cursorValues.filter(item=>"
        "item[0].startsWith('parallel-')).length===12"
        "&&indexedDbProbe.cursorValues[0][0]==='alpha'"
        "&&indexedDbProbe.cursorValues[0][1].nested.enabled"
        "&&indexedDbProbe.cursorValues[0][1].bytes[2]===255"
        "&&indexedDbProbe.cursorValues[0][1].bytes[1]===7"
        "&&indexedDbProbe.cursorValues[0][1].date instanceof Date"
        "&&indexedDbProbe.cursorValues[0][1].date.getTime()===456"
        "&&indexedDbProbe.cursorValues[0][1].map instanceof Map"
        "&&indexedDbProbe.cursorValues[0][1].map.get('theme')==='dark'",
        "indexeddb-runtime-result.js") == "true",
        "upgrade, rollback, structured clone, or cursor behavior changed");
    webscene_engine_destroy(engine);

    engine = create_indexeddb_engine(root, "code-oss-profile", 41999U);
    require(engine != nullptr, "IndexedDB restart engine creation failed");
    // A different ephemeral loopback port retains the explicit app partition.
    execute_and_wait(engine, R"JS(
      globalThis.indexedDbRestartProbe = { done: false };
      (() => new Promise((resolve, reject) => {
        const request = indexedDB.open('vscode-web-state-db-global');
        request.onerror = () => reject(request.error);
        request.onsuccess = () => resolve(request.result);
      }))().then(database => new Promise((resolve, reject) => {
        const transaction = database.transaction('ItemTable', 'readonly');
        const request = transaction.objectStore('ItemTable').get('alpha');
        transaction.onerror = transaction.onabort = () => reject(transaction.error);
        transaction.oncomplete = () => {
          globalThis.indexedDbRestartProbe = {
            done: true,
            restored: request.result.nested.enabled
              && request.result.list.join(',') === '1,2,3'
              && request.result.bytes[1] === 7
              && request.result.date instanceof Date
              && request.result.date.getTime() === 456
              && request.result.map instanceof Map
              && request.result.map.get('theme') === 'dark'
          };
          database.close();
          resolve();
        };
      })).catch(error => {
        globalThis.indexedDbRestartProbe = {
          done: true, error: `${error?.name}:${error?.message}`
        };
      });
    )JS", "indexeddb-restart-contract.js");
    wait_for_indexeddb_flag(engine, "indexedDbRestartProbe.done===true");
    require(evaluate(engine,
        "indexedDbRestartProbe.restored===true&&!indexedDbRestartProbe.error",
        "indexeddb-restart-result.js") == "true",
        "IndexedDB state did not survive an engine restart and port change");
    webscene_engine_destroy(engine);
    cleanup();
}

void test_indexeddb_quota_and_corruption_failures()
{
    const auto root = std::filesystem::temp_directory_path()
        / ("webscene-indexeddb-failure-"
            + std::to_string(std::chrono::steady_clock::now()
                .time_since_epoch().count()));
    std::filesystem::create_directories(root);
    auto* engine = create_indexeddb_engine(
        root, "failure-profile", 42001U, 1024U * 1024U);
    require(engine != nullptr, "IndexedDB failure engine creation failed");
    execute_and_wait(engine, R"JS(
      globalThis.indexedDbQuotaProbe = { done: false };
      new Promise((resolve, reject) => {
        const open = indexedDB.open('quota-db', 1);
        open.onupgradeneeded = () => open.result.createObjectStore('items');
        open.onerror = () => reject(open.error);
        open.onsuccess = () => resolve(open.result);
      }).then(database => new Promise(resolve => {
        const transaction = database.transaction('items', 'readwrite');
        transaction.objectStore('items').put('x'.repeat(2 * 1024 * 1024), 'large');
        transaction.onabort = transaction.onerror = () => {
          globalThis.indexedDbQuotaProbe = {
            done: true, name: transaction.error?.name
          };
          database.close();
          resolve();
        };
        transaction.oncomplete = () => {
          globalThis.indexedDbQuotaProbe = { done: true, name: 'committed' };
          database.close();
          resolve();
        };
      })).catch(error => {
        globalThis.indexedDbQuotaProbe = { done: true, name: error?.name };
      });
    )JS", "indexeddb-quota-contract.js");
    wait_for_indexeddb_flag(engine, "indexedDbQuotaProbe.done===true");
    require(evaluate(engine,
        "indexedDbQuotaProbe.name==='QuotaExceededError'",
        "indexeddb-quota-result.js") == "true",
        "quota failure did not abort with QuotaExceededError");
    webscene_engine_destroy(engine);

    std::filesystem::path database_file;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.path().extension() == ".wsidb") {
            database_file = entry.path();
            break;
        }
    }
    require(!database_file.empty(), "quota database file was not created");
    std::fstream corrupt(database_file, std::ios::binary | std::ios::in | std::ios::out);
    corrupt.seekg(-1, std::ios::end);
    const auto prior = corrupt.get();
    require(prior != std::char_traits<char>::eof(), "quota database file was empty");
    corrupt.seekp(-1, std::ios::end);
    corrupt.put(static_cast<char>(prior ^ 0xff));
    corrupt.close();

    engine = create_indexeddb_engine(
        root, "failure-profile", 42002U, 1024U * 1024U);
    require(engine != nullptr, "IndexedDB corruption engine creation failed");
    execute_and_wait(engine, R"JS(
      globalThis.indexedDbCorruptionProbe = { done: false };
      const request = indexedDB.open('quota-db');
      request.onsuccess = () => {
        globalThis.indexedDbCorruptionProbe = { done: true, name: 'opened' };
      };
      request.onerror = () => {
        globalThis.indexedDbCorruptionProbe = {
          done: true, name: request.error?.name
        };
      };
    )JS", "indexeddb-corruption-contract.js");
    wait_for_indexeddb_flag(engine, "indexedDbCorruptionProbe.done===true");
    require(evaluate(engine,
        "indexedDbCorruptionProbe.name==='DataError'",
        "indexeddb-corruption-result.js") == "true",
        "corrupt data did not surface as DataError");
    execute_and_wait(engine, R"JS(
      globalThis.indexedDbRecoveryProbe = { done: false };
      const removed = indexedDB.deleteDatabase('quota-db');
      removed.onerror = () => {
        globalThis.indexedDbRecoveryProbe = {
          done: true, error: removed.error?.name
        };
      };
      removed.onsuccess = () => {
        const reopened = indexedDB.open('quota-db', 1);
        reopened.onupgradeneeded = () => reopened.result.createObjectStore('items');
        reopened.onerror = () => {
          globalThis.indexedDbRecoveryProbe = {
            done: true, error: reopened.error?.name
          };
        };
        reopened.onsuccess = () => {
          globalThis.indexedDbRecoveryProbe = {
            done: true, recovered: reopened.result.version === 1
          };
          reopened.result.close();
        };
      };
    )JS", "indexeddb-corruption-recovery.js");
    wait_for_indexeddb_flag(engine, "indexedDbRecoveryProbe.done===true");
    require(evaluate(engine,
        "indexedDbRecoveryProbe.recovered===true&&!indexedDbRecoveryProbe.error",
        "indexeddb-corruption-recovery-result.js") == "true",
        "deleteDatabase did not recover corrupt durable state");
    webscene_engine_destroy(engine);
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void test_indexeddb_runtime_contract()
{
    test_indexeddb_persistence_and_transaction_contract();
    test_indexeddb_quota_and_corruption_failures();
    test_indexeddb_file_system_handle_persistence();
}
