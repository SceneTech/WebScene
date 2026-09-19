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

const webscene_semantic_typed_value_v2* find_semantic_typed_value(
    const webscene_semantic_snapshot_view_v2& snapshot,
    std::string_view role,
    std::string_view name)
{
    const auto* node = find_semantic_node(snapshot.base, role, name);
    if (node == nullptr || snapshot.typed_value_count != snapshot.base.node_count)
        return nullptr;
    const auto index = static_cast<uint32_t>(node - snapshot.base.nodes);
    return index < snapshot.typed_value_count
        ? &snapshot.typed_values[index] : nullptr;
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
          <input id="zoom" type="range" min="25" max="200" step="5" value="125" aria-label="Zoom">
          <div role="spinbutton" aria-label="Columns" aria-valuemin="1" aria-valuemax="12" aria-valuenow="3"></div>
          <div role="slider" aria-label="Broken range" aria-valuemin="10" aria-valuemax="2" aria-valuenow="nan"></div>
          <input id="unicode" value="a😀bc" aria-label="Unicode editor">
          <span id="query-help">Filters the file tree</span>
          <button id="toggle" aria-expanded="false" aria-controls="panel">Open panel</button>
          <section id="panel" role="region" aria-label="Results"></section>
          <iframe id="preview" src="about:blank"></iframe>
        </main>`;
      const query = document.getElementById('query');
      query.focus();
      query.setSelectionRange(1, 4, 'backward');
      document.getElementById('unicode').setSelectionRange(1, 3);
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
    const auto* typed_snapshot =
        webscene_engine_acquire_semantic_snapshot_v2(engine);
    require(typed_snapshot != nullptr
            && typed_snapshot->base.version == 2U
            && typed_snapshot->typed_value_count
                == typed_snapshot->base.node_count,
        "v2 semantic companion table did not match the v1 publication");
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
    const auto* query_typed = find_semantic_typed_value(
        *typed_snapshot, "textbox", "Search files");
    require(query_typed != nullptr
            && (query_typed->flags
                & (WEBSCENE_SEMANTIC_TYPED_TEXT_CARET_V2
                    | WEBSCENE_SEMANTIC_TYPED_TEXT_SELECTION_V2))
            == (WEBSCENE_SEMANTIC_TYPED_TEXT_CARET_V2
                | WEBSCENE_SEMANTIC_TYPED_TEXT_SELECTION_V2)
            && query_typed->text_selection_start_utf16 == 1U
            && query_typed->text_selection_end_utf16 == 4U
            && query_typed->text_caret_offset_utf16 == 1U,
        "semantic text control lost its UTF-16 selection or caret direction");
    const auto* zoom = find_semantic_typed_value(
        *typed_snapshot, "slider", "Zoom");
    require(zoom != nullptr
            && (zoom->flags
                & (WEBSCENE_SEMANTIC_TYPED_NUMERIC_VALUE_V2
                    | WEBSCENE_SEMANTIC_TYPED_NUMERIC_MINIMUM_V2
                    | WEBSCENE_SEMANTIC_TYPED_NUMERIC_MAXIMUM_V2
                    | WEBSCENE_SEMANTIC_TYPED_NUMERIC_INCREMENT_V2))
                == (WEBSCENE_SEMANTIC_TYPED_NUMERIC_VALUE_V2
                    | WEBSCENE_SEMANTIC_TYPED_NUMERIC_MINIMUM_V2
                    | WEBSCENE_SEMANTIC_TYPED_NUMERIC_MAXIMUM_V2
                    | WEBSCENE_SEMANTIC_TYPED_NUMERIC_INCREMENT_V2)
            && zoom->numeric_value == 125.0
            && zoom->numeric_minimum == 25.0
            && zoom->numeric_maximum == 200.0
            && zoom->numeric_increment == 5.0,
        "native range typed semantics were incomplete");
    const auto* columns = find_semantic_typed_value(
        *typed_snapshot, "spinbutton", "Columns");
    require(columns != nullptr && columns->numeric_value == 3.0
            && columns->numeric_minimum == 1.0
            && columns->numeric_maximum == 12.0,
        "ARIA numeric typed semantics were incomplete");
    const auto* broken = find_semantic_typed_value(
        *typed_snapshot, "slider", "Broken range");
    require(broken != nullptr
            && (broken->flags
                & (WEBSCENE_SEMANTIC_TYPED_NUMERIC_VALUE_V2
                    | WEBSCENE_SEMANTIC_TYPED_NUMERIC_MINIMUM_V2
                    | WEBSCENE_SEMANTIC_TYPED_NUMERIC_MAXIMUM_V2)) == 0U,
        "malformed or inverted numeric semantics were published");
    const auto* unicode = find_semantic_typed_value(
        *typed_snapshot, "textbox", "Unicode editor");
    require(unicode != nullptr
            && unicode->text_selection_start_utf16 == 1U
            && unicode->text_selection_end_utf16 == 3U,
        "semantic selection offsets were not published in DOM UTF-16 units");
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
    webscene_semantic_snapshot_release_v2(typed_snapshot);
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
