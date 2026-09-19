uint32_t request_semantic_delta(
    webscene_engine* engine,
    uint64_t base_snapshot_generation)
{
    const webscene_semantic_delta_request_v1 request{
        sizeof(webscene_semantic_delta_request_v1),
        1U,
        base_snapshot_generation,
        0U};
    return webscene_engine_request_semantic_delta_v1(engine, &request);
}

const webscene_semantic_delta_view_v1* wait_for_semantic_delta(
    webscene_engine* engine)
{
    for (auto attempt = 0; attempt < 1000; ++attempt) {
        const auto* delta = webscene_engine_take_semantic_delta_v1(engine);
        if (delta != nullptr) return delta;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    fail("semantic delta publication timed out");
}

std::string_view semantic_delta_string(
    const webscene_semantic_delta_view_v1& delta,
    webscene_semantic_string_v1 value)
{
    require(value.offset <= delta.string_byte_count
            && value.length <= delta.string_byte_count - value.offset,
        "semantic delta string exceeded its lease");
    return value.length == 0U ? std::string_view{} : std::string_view{
        delta.string_bytes + value.offset, value.length};
}

size_t semantic_delta_operation_count(
    const webscene_semantic_delta_view_v1& delta,
    uint32_t kind)
{
    return static_cast<size_t>(std::count_if(
        delta.operations,
        delta.operations + delta.operation_count,
        [&](const auto& operation) { return operation.kind == kind; }));
}

void test_semantic_delta_workbench_and_nested_generation_contract()
{
    auto* engine = webscene_engine_create(0U);
    require(engine != nullptr, "semantic delta engine creation failed");
    evaluate(engine, R"JS(
      document.body.innerHTML = `
        <main id="tree" role="tree" aria-label="Files">
          <div id="row-1" role="treeitem" aria-label="one"></div>
          <div id="row-2" role="treeitem" aria-label="two"></div>
        </main>
        <section id="terminal" role="log" aria-label="Terminal"></section>
        <label for="editor">Editor</label><input id="editor" value="seed">
        <button id="toggle" aria-controls="panel">Toggle panel</button>
        <section id="panel" role="region" aria-label="Panel"></section>
        <iframe id="preview" src="about:blank"></iframe>`;
      const nested = document.getElementById('preview').contentDocument;
      nested.open();
      nested.write('<article aria-label="Preview"><h2>Old nested view</h2></article>');
      nested.close();
      true;
    )JS", "semantic-delta-workbench-setup.js");
    const auto* base = wait_for_semantic_snapshot(engine, 0U, 2U);
    require(base->flags == 0U && base->document_count == 2U,
        "semantic delta base snapshot was incomplete");
    const auto base_generation = base->snapshot_generation;
    const auto base_top_generation = base->top_document_generation;
    const auto old_frame_generation = base->documents[1].frame_generation;

    evaluate(engine, R"JS(
      document.getElementById('row-1').remove();
      const inserted = document.createElement('div');
      inserted.id = 'row-3'; inserted.setAttribute('role', 'treeitem');
      inserted.setAttribute('aria-label', 'three');
      document.getElementById('tree').append(inserted);
      document.getElementById('panel').append(document.getElementById('row-2'));
      const editor = document.getElementById('editor');
      editor.value = 'updated'; editor.setAttribute('aria-label', 'Editor updated');
      editor.focus();
      document.getElementById('terminal').setAttribute('aria-label', 'Terminal active');
      document.getElementById('toggle').setAttribute('aria-controls', 'terminal');
      true;
    )JS", "semantic-delta-workbench-churn.js");
    require(request_semantic_delta(engine, base_generation)
            == WEBSCENE_SEMANTIC_DELTA_QUEUED_V1,
        "semantic delta workbench request was not admitted");
    require(request_semantic_delta(engine, base_generation)
            == WEBSCENE_SEMANTIC_DELTA_QUEUE_FULL_V1,
        "semantic delta admitted more than one pinned request");
    const auto* delta = wait_for_semantic_delta(engine);
    require(delta->flags == 0U
            && delta->base_snapshot_generation == base_generation
            && delta->new_snapshot_generation > base_generation
            && delta->base_top_document_generation == base_top_generation
            && delta->new_top_document_generation == base_top_generation,
        "semantic delta generation header was invalid");
    require(semantic_delta_operation_count(
                *delta, WEBSCENE_SEMANTIC_DELTA_REMOVE_V1) != 0U
            && semantic_delta_operation_count(
                *delta, WEBSCENE_SEMANTIC_DELTA_INSERT_V1) != 0U
            && semantic_delta_operation_count(
                *delta, WEBSCENE_SEMANTIC_DELTA_REPARENT_V1) != 0U
            && semantic_delta_operation_count(
                *delta, WEBSCENE_SEMANTIC_DELTA_UPDATE_V1) != 0U
            && semantic_delta_operation_count(
                *delta, WEBSCENE_SEMANTIC_DELTA_RELATIONSHIP_REMOVE_V1) != 0U
            && semantic_delta_operation_count(
                *delta, WEBSCENE_SEMANTIC_DELTA_RELATIONSHIP_ADD_V1) != 0U
            && semantic_delta_operation_count(
                *delta, WEBSCENE_SEMANTIC_DELTA_FOCUS_V1) == 1U,
        "semantic workbench churn did not expose every operation class");
    const auto updated_editor = std::find_if(
        delta->operations, delta->operations + delta->operation_count,
        [&](const auto& operation) {
            return operation.kind == WEBSCENE_SEMANTIC_DELTA_UPDATE_V1
                && semantic_delta_string(*delta, operation.name) == "Editor updated";
        });
    require(updated_editor != delta->operations + delta->operation_count
            && semantic_delta_string(*delta, updated_editor->value) == "updated",
        "semantic delta update did not carry a complete node value");
    const auto workbench_generation = delta->new_snapshot_generation;
    webscene_semantic_delta_release_v1(delta);
    require(request_semantic_delta(engine, base_generation)
            == WEBSCENE_SEMANTIC_DELTA_STALE_BASE_V1,
        "semantic delta admitted a retired base generation");
    webscene_semantic_snapshot_release_v1(base);

    evaluate(engine, R"JS(
      const nested = document.getElementById('preview').contentDocument;
      nested.open();
      nested.write('<article aria-label="Preview replacement"><h2>New nested view</h2></article>');
      nested.close();
      true;
    )JS", "semantic-delta-nested-replacement.js");
    require(request_semantic_delta(engine, workbench_generation)
            == WEBSCENE_SEMANTIC_DELTA_QUEUED_V1,
        "nested semantic delta request was not admitted");
    delta = wait_for_semantic_delta(engine);
    require(delta->flags == 0U, "nested replacement forced full recovery");
    const auto old_frame_remove = std::find_if(
        delta->operations, delta->operations + delta->operation_count,
        [&](const auto& operation) {
            return operation.kind == WEBSCENE_SEMANTIC_DELTA_REMOVE_V1
                && operation.frame_generation == old_frame_generation;
        });
    const auto new_frame_insert = std::find_if(
        delta->operations, delta->operations + delta->operation_count,
        [&](const auto& operation) {
            return operation.kind == WEBSCENE_SEMANTIC_DELTA_INSERT_V1
                && operation.frame_generation != 0U
                && operation.frame_generation != old_frame_generation;
        });
    require(old_frame_remove != delta->operations + delta->operation_count
            && new_frame_insert != delta->operations + delta->operation_count,
        "nested document replacement crossed its frame generation boundary");
    const auto nested_generation = delta->new_snapshot_generation;
    webscene_semantic_delta_release_v1(delta);

    evaluate(engine, "document.getElementById('editor').value = 'retire-low'; true;",
        "semantic-delta-low-memory-pending.js");
    require(request_semantic_delta(engine, nested_generation)
            == WEBSCENE_SEMANTIC_DELTA_QUEUED_V1,
        "semantic delta low-memory fixture was not admitted");
    require(webscene_engine_request_low_memory(engine) != 0U
            && webscene_engine_take_semantic_delta_v1(engine) == nullptr,
        "low memory retained a semantic delta");
    const auto* after_low_memory = wait_for_semantic_snapshot(engine);
    const auto after_low_memory_generation =
        after_low_memory->snapshot_generation;
    evaluate(engine, "document.getElementById('editor').value = 'retire-nav'; true;",
        "semantic-delta-navigation-pending.js");
    require(request_semantic_delta(engine, after_low_memory_generation)
            == WEBSCENE_SEMANTIC_DELTA_QUEUED_V1,
        "semantic delta navigation fixture was not admitted");
    constexpr std::string_view replacement_url{"about:blank"};
    require(webscene_engine_load_url(
                engine, replacement_url.data(), replacement_url.size()) != 0U
            && webscene_engine_take_semantic_delta_v1(engine) == nullptr,
        "navigation retained a semantic delta");
    webscene_semantic_snapshot_release_v1(after_low_memory);
    webscene_engine_destroy(engine);
}

void test_semantic_delta_overflow_full_snapshot_recovery()
{
    auto* engine = webscene_engine_create(0U);
    require(engine != nullptr, "semantic delta overflow engine creation failed");
    evaluate(engine, "document.body.innerHTML = '<main id=rows></main>'; true;",
        "semantic-delta-overflow-base.js");
    const auto* base = wait_for_semantic_snapshot(engine);
    const auto base_generation = base->snapshot_generation;
    evaluate(engine, R"JS(
      const rows = document.getElementById('rows');
      const fragment = document.createDocumentFragment();
      for (let index = 0; index < 9000; ++index) {
        const row = document.createElement('div');
        row.setAttribute('role', 'treeitem');
        row.setAttribute('aria-label', `virtual-${index}`);
        fragment.append(row);
      }
      rows.append(fragment);
      true;
    )JS", "semantic-delta-overflow-fill.js");
    require(request_semantic_delta(engine, base_generation)
            == WEBSCENE_SEMANTIC_DELTA_QUEUED_V1,
        "semantic delta overflow request was not admitted");
    const auto* delta = wait_for_semantic_delta(engine);
    require((delta->flags
                & (WEBSCENE_SEMANTIC_DELTA_FULL_SNAPSHOT_REQUIRED_V1
                    | WEBSCENE_SEMANTIC_DELTA_OVERFLOW_V1))
            == (WEBSCENE_SEMANTIC_DELTA_FULL_SNAPSHOT_REQUIRED_V1
                | WEBSCENE_SEMANTIC_DELTA_OVERFLOW_V1)
            && delta->operation_count == 0U
            && delta->string_byte_count == 0U,
        "semantic delta overflow exposed a partial patch");
    const auto recovery_generation = delta->new_snapshot_generation;
    const auto* recovery = webscene_engine_acquire_semantic_snapshot_v1(engine);
    require(recovery != nullptr
            && recovery->snapshot_generation == recovery_generation
            && recovery->flags == 0U
            && recovery->node_count > WEBSCENE_SEMANTIC_DELTA_MAXIMUM_OPERATIONS_V1,
        "semantic delta overflow did not publish its full-snapshot recovery");
    webscene_engine_destroy(engine);
    require(delta->new_snapshot_generation == recovery_generation
            && recovery->snapshot_generation == recovery_generation,
        "semantic delta or recovery lease did not survive engine teardown");
    webscene_semantic_snapshot_release_v1(recovery);
    webscene_semantic_delta_release_v1(delta);
    webscene_semantic_snapshot_release_v1(base);
}

void test_semantic_delta_virtualized_tree_100_cycle_memory_and_p95()
{
    auto* engine = webscene_engine_create(0U);
    require(engine != nullptr, "semantic delta cycle engine creation failed");
    evaluate(engine, R"JS(
      document.body.innerHTML = `
        <main id="virtual-tree" role="tree" aria-label="Explorer"></main>
        <section id="terminal" role="log" aria-label="Terminal 0"></section>
        <label for="editor">Editor</label><input id="editor" value="0">
        <section id="panel" role="region" aria-label="Panel"></section>`;
      const tree = document.getElementById('virtual-tree');
      const fragment = document.createDocumentFragment();
      for (let index = 0; index < 2048; ++index) {
        const row = document.createElement('div');
        row.id = `virtual-row-${index}`;
        row.setAttribute('role', 'treeitem');
        row.setAttribute('aria-label', `Row ${index}`);
        fragment.append(row);
      }
      tree.append(fragment);
      true;
    )JS", "semantic-delta-cycle-setup.js");
    const auto* base = wait_for_semantic_snapshot(engine);
    auto generation = base->snapshot_generation;
    webscene_semantic_snapshot_release_v1(base);
    webscene_engine_memory_metrics memory_before{sizeof(memory_before)};
    require(webscene_engine_get_memory_metrics(engine, &memory_before) != 0U,
        "semantic delta memory baseline was unavailable");
    std::vector<double> durations;
    durations.reserve(100U);
    for (uint32_t cycle = 0U; cycle < 100U; ++cycle) {
        evaluate(engine, std::string(R"JS((() => {
          const cycle = )JS") + std::to_string(cycle) + R"JS(;
          const row = document.getElementById(`virtual-row-${cycle % 2048}`);
          row.setAttribute('aria-label', `Row active ${cycle}`);
          const panel = document.getElementById('panel');
          const tree = document.getElementById('virtual-tree');
          const moved = document.getElementById(`virtual-row-${(cycle + 1) % 2048}`);
          (cycle & 1 ? tree : panel).append(moved);
          document.getElementById('terminal').setAttribute('aria-label', `Terminal ${cycle}`);
          document.getElementById('editor').value = String(cycle);
          return true;
        })())JS", "semantic-delta-cycle-mutation.js");
        const auto started = std::chrono::steady_clock::now();
        require(request_semantic_delta(engine, generation)
                == WEBSCENE_SEMANTIC_DELTA_QUEUED_V1,
            "semantic delta cycle request was not admitted");
        const auto* delta = wait_for_semantic_delta(engine);
        durations.push_back(std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count());
        require(delta->flags == 0U && delta->operation_count != 0U
                && delta->operation_count < 32U,
            "small workbench churn produced an unbounded semantic delta");
        generation = delta->new_snapshot_generation;
        webscene_semantic_delta_release_v1(delta);
    }
    std::sort(durations.begin(), durations.end());
    const auto p95 = durations[94U];
    require(p95 <= 50.0,
        "semantic delta request-to-publication p95 exceeded 50 ms");
    webscene_engine_memory_metrics memory_after{sizeof(memory_after)};
    require(webscene_engine_get_memory_metrics(engine, &memory_after) != 0U,
        "semantic delta final memory metrics were unavailable");
    require(memory_after.v8_used_heap_bytes
                <= memory_before.v8_used_heap_bytes + 16U * 1024U * 1024U
            && memory_after.native_dom_node_pool_reserved_bytes
                <= memory_before.native_dom_node_pool_reserved_bytes
                    + 2U * 1024U * 1024U,
        "semantic delta 100-cycle memory gate exceeded its retained budget");
    webscene_engine_destroy(engine);
}
