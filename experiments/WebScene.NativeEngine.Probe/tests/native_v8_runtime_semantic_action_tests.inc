uint32_t request_semantic_action(
    webscene_engine* engine,
    const webscene_semantic_snapshot_view_v1& snapshot,
    const webscene_semantic_node_v1& node,
    uint32_t action,
    std::string_view value = {},
    uint32_t selection_start = 0U,
    uint32_t selection_end = 0U)
{
    const webscene_semantic_action_request_v1 request{
        sizeof(webscene_semantic_action_request_v1),
        1U,
        snapshot.snapshot_generation,
        node.semantic_id,
        action,
        0U,
        value.empty() ? nullptr : value.data(),
        value.size(),
        selection_start,
        selection_end};
    return webscene_engine_request_semantic_action_v1(engine, &request);
}

template <typename Predicate>
const webscene_semantic_snapshot_view_v1* wait_for_semantic_action_snapshot(
    webscene_engine* engine,
    uint64_t generation_after,
    Predicate&& predicate)
{
    for (auto attempt = 0; attempt < 500; ++attempt) {
        const auto* snapshot = webscene_engine_acquire_semantic_snapshot_v1(engine);
        if (snapshot != nullptr
            && snapshot->snapshot_generation > generation_after
            && predicate(*snapshot)) {
            return snapshot;
        }
        webscene_semantic_snapshot_release_v1(snapshot);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    fail("semantic action publication timed out");
}

void test_semantic_action_dom_contract_and_retirement()
{
    auto* engine = webscene_engine_create(0U);
    require(engine != nullptr, "semantic action engine creation failed");
    evaluate(engine, R"JS(
      document.body.innerHTML = `
        <label for="editor">Editor</label><input id="editor" value="seed">
        <label for="readonly">Readonly</label><input id="readonly" value="fixed" readonly>
        <label for="check">Enabled</label><input id="check" type="checkbox">
        <label for="level">Level</label><input id="level" type="range" min="0" max="10" step="2" value="4">
        <button id="press">Press</button>`;
      globalThis.semanticPresses = 0;
      document.getElementById('press').addEventListener('click', event => {
        ++semanticPresses;
        event.currentTarget.setAttribute('aria-pressed', 'true');
      });
      globalThis.semanticInputs = 0;
      document.getElementById('editor').addEventListener('input', () => ++semanticInputs);
      globalThis.semanticNumericInputs = 0;
      globalThis.semanticNumericChanges = 0;
      document.getElementById('level').addEventListener('input', () => ++semanticNumericInputs);
      document.getElementById('level').addEventListener('change', () => ++semanticNumericChanges);
      true;
    )JS", "semantic-action-setup.js");

    const auto* snapshot = wait_for_semantic_snapshot(engine);
    const auto* editor = find_semantic_node(*snapshot, "textbox", "Editor");
    const auto* readonly = find_semantic_node(*snapshot, "textbox", "Readonly");
    const auto* check = find_semantic_node(*snapshot, "checkbox", "Enabled");
    const auto* level = find_semantic_node(*snapshot, "slider", "Level");
    const auto* press = find_semantic_node(*snapshot, "button", "Press");
    require(editor != nullptr && readonly != nullptr && check != nullptr
            && level != nullptr && press != nullptr,
        "semantic action fixtures were absent");
    require((editor->supported_actions
                & (WEBSCENE_SEMANTIC_ACTION_SUPPORT_FOCUS_V1
                    | WEBSCENE_SEMANTIC_ACTION_SUPPORT_SET_VALUE_V1
                    | WEBSCENE_SEMANTIC_ACTION_SUPPORT_SET_SELECTION_V1))
            == (WEBSCENE_SEMANTIC_ACTION_SUPPORT_FOCUS_V1
                | WEBSCENE_SEMANTIC_ACTION_SUPPORT_SET_VALUE_V1
                | WEBSCENE_SEMANTIC_ACTION_SUPPORT_SET_SELECTION_V1),
        "editable text actions were not advertised");
    require((readonly->supported_actions
                & WEBSCENE_SEMANTIC_ACTION_SUPPORT_SET_VALUE_V1) == 0U
            && (readonly->supported_actions
                & WEBSCENE_SEMANTIC_ACTION_SUPPORT_SET_SELECTION_V1) != 0U,
        "readonly text action capabilities were incorrect");
    require((check->supported_actions
                & WEBSCENE_SEMANTIC_ACTION_SUPPORT_TOGGLE_V1) != 0U
            && (level->supported_actions
                & WEBSCENE_SEMANTIC_ACTION_SUPPORT_INCREMENT_V1) != 0U
            && (level->supported_actions
                & WEBSCENE_SEMANTIC_ACTION_SUPPORT_SET_VALUE_V1) != 0U
            && (press->supported_actions
                & WEBSCENE_SEMANTIC_ACTION_SUPPORT_PRESS_V1) != 0U,
        "control actions were not advertised");

    const auto* republished = wait_for_semantic_snapshot(
        engine, snapshot->snapshot_generation);
    auto previous_generation = republished->snapshot_generation;
    require(request_semantic_action(
                engine, *snapshot, *editor, WEBSCENE_SEMANTIC_ACTION_FOCUS_V1)
            == WEBSCENE_SEMANTIC_ACTION_QUEUED_V1,
        "live identity from an older semantic publication was not admitted");
    auto* next = wait_for_semantic_action_snapshot(
        engine, previous_generation, [](const auto& candidate) {
            const auto* node = find_semantic_node(candidate, "textbox", "Editor");
            return node != nullptr
                && (node->states & WEBSCENE_SEMANTIC_NODE_FOCUSED_V1) != 0U;
        });
    auto* updated_editor = find_semantic_node(*next, "textbox", "Editor");
    require(updated_editor != nullptr
            && (updated_editor->states & WEBSCENE_SEMANTIC_NODE_FOCUSED_V1) != 0U,
        "semantic focus did not reach the DOM worker");
    webscene_semantic_snapshot_release_v1(republished);
    webscene_semantic_snapshot_release_v1(snapshot);

    previous_generation = next->snapshot_generation;
    require(request_semantic_action(
                engine, *next, *updated_editor,
                WEBSCENE_SEMANTIC_ACTION_SET_VALUE_V1, "updated")
            == WEBSCENE_SEMANTIC_ACTION_QUEUED_V1,
        "semantic value edit was not admitted");
    auto* after_value = wait_for_semantic_action_snapshot(
        engine, previous_generation, [](const auto& candidate) {
            const auto* node = find_semantic_node(candidate, "textbox", "Editor");
            return node != nullptr
                && semantic_string(candidate, node->value) == "updated";
        });
    updated_editor = find_semantic_node(*after_value, "textbox", "Editor");
    require(updated_editor != nullptr
            && semantic_string(*after_value, updated_editor->value) == "updated",
        "semantic value edit did not update the live control");
    webscene_semantic_snapshot_release_v1(next);

    previous_generation = after_value->snapshot_generation;
    evaluate(engine, R"JS(
      globalThis.observeSemanticSelection = () => {
        const editor = document.getElementById('editor');
        if (editor.selectionStart === 1 && editor.selectionEnd === 5) {
          editor.setAttribute('aria-label', 'Editor selection applied');
        } else {
          setTimeout(observeSemanticSelection, 1);
        }
      };
      setTimeout(observeSemanticSelection, 1);
      true;
    )JS", "semantic-action-selection-observer.js");
    require(request_semantic_action(
                engine, *after_value, *updated_editor,
                WEBSCENE_SEMANTIC_ACTION_SET_SELECTION_V1, {}, 1U, 5U)
            == WEBSCENE_SEMANTIC_ACTION_QUEUED_V1,
        "semantic selection edit was not admitted");
    auto* after_selection = wait_for_semantic_action_snapshot(
        engine, previous_generation, [](const auto& candidate) {
            return find_semantic_node(
                candidate, "textbox", "Editor selection applied") != nullptr;
        });
    evaluate(engine, R"JS(
      if (document.getElementById('editor').selectionStart !== 1
          || document.getElementById('editor').selectionEnd !== 5
          || semanticInputs !== 1) throw new Error('semantic edit event/selection mismatch');
      true;
    )JS", "semantic-action-selection-assert.js");
    webscene_semantic_snapshot_release_v1(after_value);

    press = find_semantic_node(*after_selection, "button", "Press");
    previous_generation = after_selection->snapshot_generation;
    require(request_semantic_action(
                engine, *after_selection, *press, WEBSCENE_SEMANTIC_ACTION_PRESS_V1)
            == WEBSCENE_SEMANTIC_ACTION_QUEUED_V1,
        "semantic press was not admitted");
    auto* after_press = wait_for_semantic_action_snapshot(
        engine, previous_generation, [](const auto& candidate) {
            const auto* node = find_semantic_node(candidate, "button", "Press");
            return node != nullptr
                && (node->states & WEBSCENE_SEMANTIC_NODE_PRESSED_V1) != 0U;
        });
    evaluate(engine, R"JS(
      if (semanticPresses !== 1) throw new Error('semantic press did not dispatch click');
      true;
    )JS", "semantic-action-press-assert.js");
    webscene_semantic_snapshot_release_v1(after_selection);

    check = find_semantic_node(*after_press, "checkbox", "Enabled");
    previous_generation = after_press->snapshot_generation;
    require(request_semantic_action(
                engine, *after_press, *check, WEBSCENE_SEMANTIC_ACTION_TOGGLE_V1)
            == WEBSCENE_SEMANTIC_ACTION_QUEUED_V1,
        "semantic toggle was not admitted");
    auto* after_toggle = wait_for_semantic_action_snapshot(
        engine, previous_generation, [](const auto& candidate) {
            const auto* node = find_semantic_node(candidate, "checkbox", "Enabled");
            return node != nullptr
                && (node->states & WEBSCENE_SEMANTIC_NODE_CHECKED_V1) != 0U;
        });
    check = find_semantic_node(*after_toggle, "checkbox", "Enabled");
    require(check != nullptr
            && (check->states & WEBSCENE_SEMANTIC_NODE_CHECKED_V1) != 0U,
        "semantic toggle did not update checkedness");
    webscene_semantic_snapshot_release_v1(after_press);

    level = find_semantic_node(*after_toggle, "slider", "Level");
    previous_generation = after_toggle->snapshot_generation;
    require(request_semantic_action(
                engine, *after_toggle, *level,
                WEBSCENE_SEMANTIC_ACTION_INCREMENT_V1)
            == WEBSCENE_SEMANTIC_ACTION_QUEUED_V1,
        "semantic increment was not admitted");
    auto* after_increment = wait_for_semantic_action_snapshot(
        engine, previous_generation, [](const auto& candidate) {
            const auto* node = find_semantic_node(candidate, "slider", "Level");
            return node != nullptr
                && semantic_string(candidate, node->value) == "6";
        });
    level = find_semantic_node(*after_increment, "slider", "Level");
    require(level != nullptr && semantic_string(*after_increment, level->value) == "6",
        "semantic increment did not apply range step");
    webscene_semantic_snapshot_release_v1(after_toggle);

    previous_generation = after_increment->snapshot_generation;
    require(request_semantic_action(
                engine, *after_increment, *level,
                WEBSCENE_SEMANTIC_ACTION_DECREMENT_V1)
            == WEBSCENE_SEMANTIC_ACTION_QUEUED_V1,
        "semantic decrement was not admitted");
    auto* after_decrement = wait_for_semantic_action_snapshot(
        engine, previous_generation, [](const auto& candidate) {
            const auto* node = find_semantic_node(candidate, "slider", "Level");
            return node != nullptr
                && semantic_string(candidate, node->value) == "4";
        });
    level = find_semantic_node(*after_decrement, "slider", "Level");
    require(level != nullptr && semantic_string(*after_decrement, level->value) == "4",
        "semantic decrement did not apply range step");
    webscene_semantic_snapshot_release_v1(after_increment);

    previous_generation = after_decrement->snapshot_generation;
    require(request_semantic_action(
                engine, *after_decrement, *level,
                WEBSCENE_SEMANTIC_ACTION_SET_VALUE_V1, "8")
            == WEBSCENE_SEMANTIC_ACTION_QUEUED_V1,
        "semantic absolute numeric value was not admitted");
    auto* after_numeric_value = wait_for_semantic_action_snapshot(
        engine, previous_generation, [](const auto& candidate) {
            const auto* node = find_semantic_node(candidate, "slider", "Level");
            return node != nullptr
                && semantic_string(candidate, node->value) == "8";
        });
    evaluate(engine, R"JS(
      if (semanticNumericInputs !== 3 || semanticNumericChanges !== 3)
        throw new Error('semantic numeric input/change ordering mismatch');
      true;
    )JS", "semantic-action-numeric-value-assert.js");
    webscene_semantic_snapshot_release_v1(after_decrement);
    after_decrement = after_numeric_value;
    level = find_semantic_node(*after_decrement, "slider", "Level");

    readonly = find_semantic_node(*after_decrement, "textbox", "Readonly");
    require(request_semantic_action(
                engine, *after_decrement, *readonly,
                WEBSCENE_SEMANTIC_ACTION_SET_VALUE_V1, "rejected")
            == WEBSCENE_SEMANTIC_ACTION_UNSUPPORTED_V1,
        "readonly semantic value edit was admitted");
    std::string oversized(
        WEBSCENE_SEMANTIC_ACTION_MAXIMUM_VALUE_BYTES_V1 + 1U, 'x');
    updated_editor = find_semantic_node(
        *after_decrement, "textbox", "Editor selection applied");
    require(request_semantic_action(
                engine, *after_decrement, *updated_editor,
                WEBSCENE_SEMANTIC_ACTION_SET_VALUE_V1, oversized)
            == WEBSCENE_SEMANTIC_ACTION_PAYLOAD_TOO_LARGE_V1,
        "oversized semantic value payload was admitted");

    const auto retired_generation = after_decrement->snapshot_generation;
    const auto retired_id = updated_editor->semantic_id;
    evaluate(engine, "document.getElementById('editor').remove(); true;",
        "semantic-action-retire-node.js");
    const auto* after_retirement = wait_for_semantic_action_snapshot(
        engine, retired_generation, [](const auto& candidate) {
            return find_semantic_node(
                candidate, "textbox", "Editor selection applied") == nullptr;
        });
    webscene_semantic_action_request_v1 retired_request{
        sizeof(webscene_semantic_action_request_v1), 1U,
        retired_generation, retired_id,
        WEBSCENE_SEMANTIC_ACTION_FOCUS_V1, 0U, nullptr, 0U, 0U, 0U};
    require(webscene_engine_request_semantic_action_v1(engine, &retired_request)
            == WEBSCENE_SEMANTIC_ACTION_STALE_V1,
        "retired semantic node generation remained actionable");
    webscene_semantic_snapshot_release_v1(after_retirement);

    constexpr std::string_view replacement_url{"about:blank"};
    require(webscene_engine_load_url(
                engine, replacement_url.data(), replacement_url.size()) != 0U,
        "semantic action navigation replacement was rejected");
    require(webscene_engine_request_semantic_action_v1(engine, &retired_request)
            == WEBSCENE_SEMANTIC_ACTION_STALE_V1,
        "navigation did not synchronously retire semantic actions");
    webscene_semantic_snapshot_release_v1(after_decrement);
    webscene_engine_destroy(engine);
}

void test_semantic_action_queue_caps_and_worker_budget()
{
    auto* engine = webscene_engine_create(0U);
    require(engine != nullptr, "semantic action cap engine creation failed");
    evaluate(engine, R"JS(
      document.body.innerHTML = `<button id="block">Block</button><input aria-label="Target">`;
      document.getElementById('block').addEventListener('click', () => {
        const deadline = performance.now() + 750;
        while (performance.now() < deadline) {}
      });
      true;
    )JS", "semantic-action-cap-setup.js");
    const auto* snapshot = wait_for_semantic_snapshot(engine);
    const auto* blocker = find_semantic_node(*snapshot, "button", "Block");
    const auto* target = find_semantic_node(*snapshot, "textbox", "Target");
    require(blocker != nullptr && target != nullptr,
        "semantic action cap fixtures were absent");
    require(request_semantic_action(
                engine, *snapshot, *blocker, WEBSCENE_SEMANTIC_ACTION_PRESS_V1)
            == WEBSCENE_SEMANTIC_ACTION_QUEUED_V1,
        "semantic blocker action was not admitted");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto started = std::chrono::steady_clock::now();
    const std::string maximum_value(
        WEBSCENE_SEMANTIC_ACTION_MAXIMUM_VALUE_BYTES_V1, 'v');
    for (uint32_t index = 0U; index < 16U; ++index) {
        require(request_semantic_action(
                    engine, *snapshot, *target,
                    WEBSCENE_SEMANTIC_ACTION_SET_VALUE_V1, maximum_value)
                == WEBSCENE_SEMANTIC_ACTION_QUEUED_V1,
            "semantic aggregate value budget filled early");
    }
    require(request_semantic_action(
                engine, *snapshot, *target,
                WEBSCENE_SEMANTIC_ACTION_SET_VALUE_V1, maximum_value)
            == WEBSCENE_SEMANTIC_ACTION_QUEUE_FULL_V1,
        "semantic aggregate value budget was not enforced");
    uint32_t queued = 0U;
    uint32_t full = 0U;
    for (uint32_t index = 0U; index < 241U; ++index) {
        const auto admission = request_semantic_action(
            engine, *snapshot, *target, WEBSCENE_SEMANTIC_ACTION_FOCUS_V1);
        queued += admission == WEBSCENE_SEMANTIC_ACTION_QUEUED_V1 ? 1U : 0U;
        full += admission == WEBSCENE_SEMANTIC_ACTION_QUEUE_FULL_V1 ? 1U : 0U;
    }
    require(queued <= 240U && queued + 1U >= 240U
            && full >= 1U,
        "semantic action queue exceeded or failed to expose its cap");
    const auto* completed = wait_for_semantic_action_snapshot(
        engine, snapshot->snapshot_generation, [](const auto& candidate) {
            const auto* node = find_semantic_node(candidate, "textbox", "Target");
            return node != nullptr
                && (node->states & WEBSCENE_SEMANTIC_NODE_FOCUSED_V1) != 0U;
        });
    require(std::chrono::steady_clock::now() - started < std::chrono::seconds(2),
        "bounded semantic action worker batch exceeded its regression budget");
    webscene_semantic_snapshot_release_v1(completed);
    webscene_semantic_snapshot_release_v1(snapshot);
    webscene_engine_destroy(engine);
}
