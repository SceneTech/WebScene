webscene_engine* create_indexeddb_engine(
    const std::filesystem::path& root,
    std::string_view partition,
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
    return webscene_engine_create_with_options(&options);
}

void configure_indexeddb_test_origin(webscene_engine* engine, uint16_t port)
{
    const auto source = std::string(
        "location.protocol='http:';location.hostname='127.0.0.1';"
        "location.origin='http://127.0.0.1:") + std::to_string(port)
        + "';location.href=location.origin+'/';";
    execute_and_wait(engine, source, "indexeddb-test-origin.js");
}

void wait_for_indexeddb_flag(webscene_engine* engine, std::string_view expression)
{
    require(
        evaluate_until_equals(
            engine, expression, "indexeddb-completion-probe.js", "true", 2500)
            == "true",
        "IndexedDB operation did not settle: " + std::string(expression));
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

    auto* engine = create_indexeddb_engine(root, "code-oss-profile");
    require(engine != nullptr, "IndexedDB engine creation failed");
    configure_indexeddb_test_origin(engine, 41001U);
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
        write.objectStore('ItemTable').put(
          { nested: { enabled: true }, list: [1, 2, 3], bytes: new Uint8Array([0, 7, 255]) },
          'alpha');
        write.objectStore('ItemTable').put('second', 'beta');
        await transactionDone(write);

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
        "&&indexedDbProbe.cursorValues.length===2"
        "&&indexedDbProbe.cursorValues[0][0]==='alpha'"
        "&&indexedDbProbe.cursorValues[0][1].nested.enabled"
        "&&indexedDbProbe.cursorValues[0][1].bytes[2]===255",
        "indexeddb-runtime-result.js") == "true",
        "upgrade, rollback, structured clone, or cursor behavior changed");
    webscene_engine_destroy(engine);

    engine = create_indexeddb_engine(root, "code-oss-profile");
    require(engine != nullptr, "IndexedDB restart engine creation failed");
    // A different ephemeral loopback port retains the explicit app partition.
    configure_indexeddb_test_origin(engine, 41999U);
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
    auto* engine = create_indexeddb_engine(root, "failure-profile", 1024U * 1024U);
    require(engine != nullptr, "IndexedDB failure engine creation failed");
    configure_indexeddb_test_origin(engine, 42001U);
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

    engine = create_indexeddb_engine(root, "failure-profile", 1024U * 1024U);
    require(engine != nullptr, "IndexedDB corruption engine creation failed");
    configure_indexeddb_test_origin(engine, 42002U);
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
}
