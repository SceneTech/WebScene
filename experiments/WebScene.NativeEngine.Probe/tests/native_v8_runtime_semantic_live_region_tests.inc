std::string_view semantic_live_text(
    const webscene_semantic_live_batch_view_v1& batch,
    const webscene_semantic_live_event_v1& event)
{
    require(event.text.offset <= batch.string_byte_count
            && event.text.length <= batch.string_byte_count - event.text.offset,
        "semantic live text slice exceeded its lease");
    return {batch.string_bytes + event.text.offset, event.text.length};
}

const webscene_semantic_live_batch_view_v1* wait_for_semantic_live_batch(
    webscene_engine* engine)
{
    for (auto attempt = 0; attempt < 500; ++attempt) {
        const auto* batch = webscene_engine_take_semantic_live_events_v1(engine);
        if (batch != nullptr) return batch;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    fail("semantic live-region publication timed out");
}

void require_no_semantic_live_batch(webscene_engine* engine)
{
    for (auto attempt = 0; attempt < 20; ++attempt) {
        const auto* batch = webscene_engine_take_semantic_live_events_v1(engine);
        if (batch != nullptr) {
            const auto count = batch->event_count;
            webscene_semantic_live_batch_release_v1(batch);
            require(count == 0U, "unexpected semantic live-region event");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void test_semantic_live_regions_dom_order_busy_and_lifecycle()
{
    auto* engine = webscene_engine_create(0U);
    require(engine != nullptr, "semantic live-region engine creation failed");
    evaluate(engine, R"JS(
      document.body.innerHTML = `
        <section id="terminal" role="log"></section>
        <section id="problems" role="status" aria-atomic="true"><b>Problems</b><span>0</span></section>
        <section id="tasks" aria-live="assertive" aria-relevant="text removals">
          <span id="task-state">Task queued</span><span id="stale-task">Task stale</span>
        </section>
        <section id="chat" role="log" aria-busy="true"></section>
        <section id="hidden-live" aria-live="assertive" hidden></section>`;
      true;
    )JS", "semantic-live-setup.js");
    const auto* baseline = wait_for_semantic_snapshot(engine);
    webscene_semantic_snapshot_release_v1(baseline);
    require_no_semantic_live_batch(engine);

    evaluate(engine, R"JS(
      terminal.append(Object.assign(document.createElement('div'), { textContent: 'Terminal ready' }));
      terminal.append(Object.assign(document.createElement('div'), { textContent: 'Process complete' }));
      problems.querySelector('span').textContent = '2';
      document.getElementById('task-state').textContent = 'Task running';
      document.getElementById('stale-task').remove();
      const alert = document.createElement('section');
      alert.id = 'alert'; alert.setAttribute('role', 'alert'); alert.textContent = 'Build failed';
      document.body.append(alert);
      hiddenLive = document.getElementById('hidden-live');
      hiddenLive.textContent = 'must remain silent';
      true;
    )JS", "semantic-live-workbench-mutations.js");
    const auto* batch = wait_for_semantic_live_batch(engine);
    require(batch->event_count == 4U,
        "terminal/problems/tasks/alert changes did not coalesce per region");
    require(batch->events[0].role == WEBSCENE_SEMANTIC_LIVE_REGION_LOG_V1
            && semantic_live_text(*batch, batch->events[0])
                == "Terminal ready Process complete",
        "terminal log additions lost DOM order");
    require(batch->events[1].role == WEBSCENE_SEMANTIC_LIVE_REGION_STATUS_V1
            && (batch->events[1].flags & WEBSCENE_SEMANTIC_LIVE_ATOMIC_V1) != 0U
            && semantic_live_text(*batch, batch->events[1]) == "Problems 2",
        "atomic Problems status did not publish its whole region");
    require(batch->events[2].politeness == WEBSCENE_SEMANTIC_LIVE_ASSERTIVE_V1
            && (batch->events[2].flags
                & (WEBSCENE_SEMANTIC_LIVE_RELEVANT_TEXT_V1
                    | WEBSCENE_SEMANTIC_LIVE_RELEVANT_REMOVALS_V1))
                == (WEBSCENE_SEMANTIC_LIVE_RELEVANT_TEXT_V1
                    | WEBSCENE_SEMANTIC_LIVE_RELEVANT_REMOVALS_V1)
            && semantic_live_text(*batch, batch->events[2])
                == "Task running Task stale",
        "Tasks text/removal relevance changed");
    require(batch->events[3].role == WEBSCENE_SEMANTIC_LIVE_REGION_ALERT_V1
            && (batch->events[3].flags
                & WEBSCENE_SEMANTIC_LIVE_INITIAL_ALERT_V1) != 0U,
        "initial authored alert was not assertive and explicit");
    const auto first_sequence = batch->events[0].sequence;
    for (uint32_t index = 1U; index < batch->event_count; ++index) {
        require(batch->events[index].sequence == first_sequence + index,
            "semantic live-region sequence was not contiguous");
    }
    webscene_semantic_live_batch_release_v1(batch);

    evaluate(engine, R"JS(
      chat.append(Object.assign(document.createElement('div'), { textContent: 'Chat one' }));
      problems.querySelector('span').textContent = '2';
      true;
    )JS", "semantic-live-busy-and-duplicate.js");
    require_no_semantic_live_batch(engine);
    evaluate(engine, R"JS(
      chat.append(Object.assign(document.createElement('div'), { textContent: 'Chat two' }));
      chat.setAttribute('aria-busy', 'false');
      true;
    )JS", "semantic-live-busy-clear.js");
    batch = wait_for_semantic_live_batch(engine);
    require(batch->event_count == 1U
            && (batch->events[0].flags
                & WEBSCENE_SEMANTIC_LIVE_BUSY_COALESCED_V1) != 0U
            && semantic_live_text(*batch, batch->events[0]) == "Chat one Chat two",
        "busy Chat changes did not publish once after clearing");
    webscene_semantic_live_batch_release_v1(batch);

    evaluate(engine, "chat.remove(); chat.textContent = 'detached'; true;",
        "semantic-live-detached.js");
    require_no_semantic_live_batch(engine);

    evaluate(engine, "terminal.textContent = 'retire on low memory'; true;",
        "semantic-live-low-memory-pending.js");
    require(webscene_engine_request_low_memory(engine) != 0U,
        "semantic live low-memory request failed");
    batch = webscene_engine_take_semantic_live_events_v1(engine);
    if (batch != nullptr) {
        require(batch->event_count == 0U,
            "low memory retained semantic live-region events");
        webscene_semantic_live_batch_release_v1(batch);
    }

    evaluate(engine, "terminal.textContent = 'retire on navigation'; true;",
        "semantic-live-navigation-pending.js");
    constexpr std::string_view replacement{"about:blank"};
    require(webscene_engine_load_url(
                engine, replacement.data(), replacement.size()) != 0U,
        "semantic live navigation replacement failed");
    batch = webscene_engine_take_semantic_live_events_v1(engine);
    if (batch != nullptr) {
        require(batch->event_count == 0U,
            "navigation retained stale semantic live-region events");
        webscene_semantic_live_batch_release_v1(batch);
    }
    require_no_semantic_live_batch(engine);
    evaluate(engine, R"JS(
      const finalAlert = document.createElement('section');
      finalAlert.setAttribute('role', 'alert');
      finalAlert.textContent = 'Lease survives teardown';
      document.body.append(finalAlert);
      true;
    )JS", "semantic-live-teardown-lease.js");
    batch = wait_for_semantic_live_batch(engine);
    require(batch->event_count == 1U,
        "teardown lease fixture was not published");
    webscene_engine_destroy(engine);
    require(semantic_live_text(*batch, batch->events[0])
            == "Lease survives teardown",
        "semantic live lease did not outlive its engine");
    webscene_semantic_live_batch_release_v1(batch);
}

void test_semantic_live_region_caps_and_worker_budget()
{
    auto* engine = webscene_engine_create(0U);
    require(engine != nullptr, "semantic live cap engine creation failed");
    evaluate(engine, R"JS(
      document.body.innerHTML = Array.from({ length: 256 }, (_, index) =>
        `<section role="log" id="log-${index}"></section>`).join('');
      true;
    )JS", "semantic-live-cap-setup.js");
    const auto* baseline = wait_for_semantic_snapshot(engine);
    const auto baseline_generation = baseline->snapshot_generation;
    webscene_semantic_snapshot_release_v1(baseline);

    const auto started = std::chrono::steady_clock::now();
    evaluate(engine, R"JS(
      for (let index = 0; index < 256; ++index) {
        document.getElementById(`log-${index}`).textContent = `entry-${index}`;
      }
      true;
    )JS", "semantic-live-cap-fill.js");
    const auto* checkpoint = wait_for_semantic_action_snapshot(
        engine, baseline_generation, [](const auto&) { return true; });
    const auto checkpoint_generation = checkpoint->snapshot_generation;
    webscene_semantic_snapshot_release_v1(checkpoint);
    evaluate(engine, R"JS(
      document.getElementById('log-0').textContent = 'replacement-entry';
      true;
    )JS", "semantic-live-cap-overflow.js");
    checkpoint = wait_for_semantic_action_snapshot(
        engine, checkpoint_generation, [](const auto&) { return true; });
    webscene_semantic_snapshot_release_v1(checkpoint);

    const auto* batch = wait_for_semantic_live_batch(engine);
    require(batch->event_count == WEBSCENE_SEMANTIC_LIVE_MAXIMUM_EVENTS_PER_LEASE_V1,
        "semantic live lease exceeded or failed to reach its cap");
    require((batch->flags & WEBSCENE_SEMANTIC_LIVE_BATCH_DROPPED_EVENTS_V1) != 0U
            && batch->dropped_event_count == 1U,
        "semantic live oldest-first overflow was not explicit");
    require(batch->string_byte_count
            <= WEBSCENE_SEMANTIC_LIVE_MAXIMUM_QUEUED_TEXT_BYTES_V1,
        "semantic live lease exceeded its aggregate text cap");
    require(std::chrono::steady_clock::now() - started < std::chrono::seconds(2),
        "semantic live bounded worker publication exceeded its regression budget");
    webscene_semantic_live_batch_release_v1(batch);
    webscene_engine_destroy(engine);

    engine = webscene_engine_create(0U);
    require(engine != nullptr, "semantic live text-cap engine creation failed");
    evaluate(engine, R"JS(
      document.body.innerHTML = Array.from({ length: 17 }, (_, index) =>
        `<section role="log" id="large-${index}"></section>`).join('');
      true;
    )JS", "semantic-live-text-cap-setup.js");
    baseline = wait_for_semantic_snapshot(engine);
    webscene_semantic_snapshot_release_v1(baseline);
    evaluate(engine, R"JS(
      for (let index = 0; index < 17; ++index) {
        document.getElementById(`large-${index}`).textContent = 'x'.repeat(70000);
      }
      true;
    )JS", "semantic-live-text-cap-fill.js");
    batch = wait_for_semantic_live_batch(engine);
    require(batch->event_count == 16U
            && batch->string_byte_count
                == WEBSCENE_SEMANTIC_LIVE_MAXIMUM_QUEUED_TEXT_BYTES_V1,
        "semantic live aggregate UTF-8 queue cap changed");
    require(batch->dropped_event_count == 1U,
        "semantic live aggregate cap loss was not explicit");
    for (uint32_t index = 0U; index < batch->event_count; ++index) {
        require(batch->events[index].text.length
                    == WEBSCENE_SEMANTIC_LIVE_MAXIMUM_TEXT_BYTES_V1
                && (batch->events[index].flags
                    & WEBSCENE_SEMANTIC_LIVE_TEXT_TRUNCATED_V1) != 0U,
            "semantic live per-event text cap was not explicit");
    }
    webscene_semantic_live_batch_release_v1(batch);
    webscene_engine_destroy(engine);
}
