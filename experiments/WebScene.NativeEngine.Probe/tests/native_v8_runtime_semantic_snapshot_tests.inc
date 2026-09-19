std::string_view semantic_string(
    const webscene_semantic_snapshot_view_v1& snapshot,
    webscene_semantic_string_v1 value)
{
    require(
        value.offset <= snapshot.string_byte_count
            && value.length <= snapshot.string_byte_count - value.offset,
        "semantic snapshot contained an invalid string slice");
    if (value.length == 0U) return {};
    require(snapshot.string_bytes != nullptr,
        "semantic snapshot omitted a non-empty string arena");
    return {snapshot.string_bytes + value.offset, value.length};
}

const webscene_semantic_snapshot_view_v1* wait_for_semantic_snapshot(
    webscene_engine* engine,
    uint64_t generation_after = 0U,
    uint32_t minimum_documents = 1U)
{
    for (auto attempt = 0; attempt < 500; ++attempt) {
        const auto* snapshot =
            webscene_engine_acquire_semantic_snapshot_v1(engine);
        if (snapshot != nullptr
            && snapshot->snapshot_generation > generation_after
            && snapshot->document_count >= minimum_documents) return snapshot;
        webscene_semantic_snapshot_release_v1(snapshot);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    fail("semantic snapshot publication timed out");
}

const webscene_semantic_node_v1* find_semantic_node(
    const webscene_semantic_snapshot_view_v1& snapshot,
    std::string_view role,
    std::string_view name)
{
    for (uint32_t index = 0U; index < snapshot.node_count; ++index) {
        const auto& node = snapshot.nodes[index];
        if (semantic_string(snapshot, node.role) == role
            && semantic_string(snapshot, node.name) == name) return &node;
    }
    return nullptr;
}

void test_semantic_snapshot_nested_state_lifecycle_and_bounds()
{
    auto* engine = webscene_engine_create(0U);
    require(engine != nullptr, "semantic regression engine creation failed");
    evaluate(engine, R"JS(
      document.body.innerHTML = `
        <main role="main" aria-label="Workbench">
          <label id="query-label" for="query">Search files</label>
          <input id="query" value="needle" aria-describedby="query-help">
          <span id="query-help">Filters the file tree</span>
          <button id="toggle" aria-expanded="false" aria-controls="panel">Open panel</button>
          <section id="panel" role="region" aria-label="Results"></section>
          <iframe id="preview" src="about:blank"></iframe>
        </main>`;
      const query = document.getElementById('query');
      query.focus();
      const nested = document.getElementById('preview').contentDocument;
      nested.open();
      nested.write(`<article aria-label="Preview"><h2>Nested heading</h2><a href="#target">Jump</a></article>`);
      nested.close();
      true;
    )JS", "semantic-snapshot-setup.js");

    const auto* first = wait_for_semantic_snapshot(engine, 0U, 2U);
    require(first->version == 1U && first->struct_size >= sizeof(*first),
        "semantic snapshot ABI header was invalid");
    require(first->snapshot_generation != 0U && first->layout_generation != 0U,
        "semantic snapshot lacked publication/layout generations");
    require(first->flags == 0U,
        "small semantic snapshot was unexpectedly truncated");
    require(first->documents[0].frame_owner_dom_node_id == 0U
            && first->documents[1].frame_owner_dom_node_id != 0U
            && first->documents[1].frame_generation != 0U,
        "nested semantic document lacked owner/generation identity");
    const auto* query = find_semantic_node(*first, "textbox", "Search files");
    require(query != nullptr && semantic_string(*first, query->value) == "needle",
        "semantic text control lost computed name or live value");
    require((query->states & WEBSCENE_SEMANTIC_NODE_FOCUSED_V1) != 0U,
        "semantic focused node state was not published");
    require(query->width > 0.0F && query->height > 0.0F,
        "semantic text control lacked layout bounds");
    const auto* toggle = find_semantic_node(*first, "button", "Open panel");
    require(toggle != nullptr
            && (toggle->states & WEBSCENE_SEMANTIC_NODE_COLLAPSED_V1) != 0U,
        "semantic expanded state was not published");
    require(find_semantic_node(*first, "heading", "Nested heading") != nullptr,
        "nested document semantics were absent");
    require(first->relationship_count >= 2U,
        "semantic IDREF relationships were not resolved");
    const auto first_generation = first->snapshot_generation;
    const auto first_value = std::string(semantic_string(*first, query->value));

    evaluate(engine, R"JS(
      const query = document.getElementById('query');
      query.value = 'updated';
      document.getElementById('toggle').setAttribute('aria-expanded', 'true');
      document.getElementById('panel').remove();
      true;
    )JS", "semantic-snapshot-update.js");
    const auto* second = wait_for_semantic_snapshot(engine, first_generation, 2U);
    const auto* updated_query = find_semantic_node(*second, "textbox", "Search files");
    const auto* updated_toggle = find_semantic_node(*second, "button", "Open panel");
    require(updated_query != nullptr
            && semantic_string(*second, updated_query->value) == "updated",
        "semantic live value did not advance with the DOM generation");
    require(updated_toggle != nullptr
            && (updated_toggle->states & WEBSCENE_SEMANTIC_NODE_EXPANDED_V1) != 0U,
        "semantic expanded state did not advance");
    require(first_value == "needle" && semantic_string(*first, query->value) == "needle",
        "an acquired semantic lease mutated after a later publication");
    webscene_semantic_snapshot_release_v1(second);
    webscene_semantic_snapshot_release_v1(first);
    webscene_engine_destroy(engine);
}

void test_semantic_snapshot_caps_performance_and_engine_retirement()
{
    auto* engine = webscene_engine_create(0U);
    require(engine != nullptr, "semantic lifecycle engine creation failed");
    evaluate(engine, R"JS(
      const root = document.createElement('div');
      root.setAttribute('role', 'list');
      const fragment = document.createDocumentFragment();
      for (let index = 0; index < 17000; ++index) {
        const item = document.createElement('div');
        item.setAttribute('role', 'listitem');
        item.setAttribute('aria-label', `Item ${index}`);
        fragment.appendChild(item);
      }
      root.appendChild(fragment);
      document.body.appendChild(root);
      true;
    )JS", "semantic-snapshot-cap.js");
    const auto started = std::chrono::steady_clock::now();
    const auto* snapshot = wait_for_semantic_snapshot(engine);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    require(snapshot->node_count == 16384U
            && (snapshot->flags
                & WEBSCENE_SEMANTIC_SNAPSHOT_TRUNCATED_NODES_V1) != 0U,
        "semantic snapshot node cap was not explicit and deterministic");
    require(snapshot->string_byte_count <= 4U * 1024U * 1024U,
        "semantic snapshot exceeded its UTF-8 budget");
    require(elapsed < std::chrono::seconds(2),
        "bounded semantic snapshot construction exceeded its regression budget");
    const auto generation = snapshot->snapshot_generation;
    constexpr std::string_view replacement_url{"about:blank"};
    require(webscene_engine_load_url(
                engine, replacement_url.data(), replacement_url.size()) != 0U,
        "semantic lifecycle replacement navigation was rejected");
    const auto* retired = webscene_engine_acquire_semantic_snapshot_v1(engine);
    require(retired == nullptr || retired->snapshot_generation != generation,
        "replacement navigation exposed the retired semantic publication");
    webscene_semantic_snapshot_release_v1(retired);
    webscene_engine_destroy(engine);
    require(snapshot->snapshot_generation == generation
            && snapshot->node_count == 16384U,
        "semantic lease did not remain immutable across engine retirement");
    webscene_semantic_snapshot_release_v1(snapshot);
}
