#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
uint64_t runtime_counter_diagnostic(webscene_engine*, std::string_view);
void execute_and_wait_for_wrapper_retention_scene(webscene_engine*, std::string_view, std::string_view);
#endif

void test_media_query_matching_scales_linearly()
{
    for (const auto count : {8, 128}) {
        auto* engine = webscene_engine_create(0);
        require(engine != nullptr, "media matching scaling engine creation failed");
        resize(engine, 800, 600, 1);
        wait_for_consumed_inputs(engine, 1, "media matching initial resize failed");
        auto setup = std::string{R"JS((count => {
          document.body.innerHTML='<section class="media-list">'+
            Array.from({length:count},(_,i)=>'<b class="media-item">'+i+'</b>').join('')+'</section>';
          const style=document.createElement('style');
          style.textContent=`.media-item {display:block;width:11px;height:13px}
            .media-item:nth-child(odd) {height:17px}
            .media-item:first-child ~ .media-item {width:19px}
            @media(max-width:600px) {.media-list {--size:23px} .media-item {width:var(--size)}}`;
          document.head.appendChild(style);
          globalThis.mediaSnapshot=()=>Array.from(document.querySelectorAll('.media-item')).every((n,i)=>
            getComputedStyle(n).width === (matchMedia('(max-width:600px)').matches ? (i?'19px':'23px') : (i?'19px':'11px')) &&
            getComputedStyle(n).height === (i%2?'13px':'17px'));
          return mediaSnapshot();
        })(COUNT))JS"};
        setup.replace(setup.find("COUNT"), 5, std::to_string(count));
        const auto initial=evaluate(engine, setup, "media-match-setup.js");
        require(initial == "true", "media matching initial styles differ: " + initial + " " +
            (initial == "true" ? std::string{} : evaluate(engine,
                "JSON.stringify({width:innerWidth,items:Array.from(document.querySelectorAll('.media-item')).map((n,i)=>[i,getComputedStyle(n).width,getComputedStyle(n).height]).filter(x=>x[1]!== (x[0]?'19px':'11px') || x[2]!== (x[0]%2?'13px':'17px')).slice(0,5)})",
                "media-match-initial-failure.js")));
        for (int transition=0; transition<4; ++transition) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            std::this_thread::sleep_for(std::chrono::milliseconds(260));
            execute_and_wait_for_wrapper_retention_scene(engine,
                "document.body.style.backgroundColor='rgb(1,2,3)'", "media-match-before.js");
            const auto before=runtime_counter_diagnostic(engine,"css-positional-sibling-visits");
#endif
            resize(engine, transition%2 ? 800 : 480, 600, transition+2);
            wait_for_consumed_inputs(engine, transition+2, "media matching transition failed");
            require(evaluate(engine, "mediaSnapshot()", "media-match-snapshot.js") == "true",
                "media matching activation/deactivation styles differ");
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            std::this_thread::sleep_for(std::chrono::milliseconds(260));
            execute_and_wait_for_wrapper_retention_scene(engine,
                "document.body.style.backgroundColor='rgb(3,2,1)'", "media-match-after.js");
            const auto visits=runtime_counter_diagnostic(engine,"css-positional-sibling-visits")-before;
            std::cout << "media-matching nodes=" << count << " transition=" << transition
                << " positional-visits=" << visits << '\n';
            require(visits>0 && visits<=static_cast<uint64_t>(count*3),
                "media recascade repeated positional sibling scans");
#endif
        }
        webscene_engine_destroy(engine);
    }
}

void test_media_query_callback_can_create_more_queries()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "reentrant media query engine creation failed");
    resize(engine, 800, 600, 1);
    wait_for_consumed_inputs(engine, 1, "initial media viewport not applied");
    evaluate(engine, R"JS(
        globalThis.mediaChanges = [];
        const first = matchMedia('(max-width:500px)');
        const second = matchMedia('(max-width:500px)');
        first.addEventListener('change', () => {
            mediaChanges.push('first');
            // Force growth of the runtime's query owner collection during dispatch.
            for (let index = 0; index < 4096; index++) matchMedia('(min-width:1px)');
        });
        second.addEventListener('change', () => mediaChanges.push('second'));
        true
    )JS", "media-query-reentrancy.js");
    resize(engine, 400, 600, 2);
    wait_for_consumed_inputs(engine, 2, "compact media viewport not applied");
    require(evaluate(engine, "mediaChanges", "media-query-reentrancy-result.js") == R"(["first","second"])",
        "creating queries during change dispatch invalidated existing query delivery");
    webscene_engine_destroy(engine);
}

void test_media_query_resize_recascades_only_affected_subtrees()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "targeted media recascade engine creation failed");
    resize(engine, 800, 600, 1);
    wait_for_consumed_inputs(engine, 1, "initial targeted-media viewport not applied");
    execute(engine, R"JS(
        document.body.innerHTML = `
          <style>
            .media-shell { --leaf-width:17px; padding-left:3px; }
            .media-leaf { width:var(--leaf-width); height:11px; }
            .unaffected-sibling { width:23px; height:5px; }
            .media-direct-shell { padding-left:2px; }
            .media-direct-leaf { height:7px; }
            .media-inherit-shell { width:17px; }
            .media-inherit-leaf { width:inherit; }
            @media (max-width:600px) {
              .media-shell { --leaf-width:41px; padding-left:13px; }
              .media-leaf { height:29px; }
              .media-direct-shell { padding-left:19px; }
              .media-direct-leaf { height:31px; }
              .media-inherit-shell { width:43px; }
            }
          </style>
          <section class="media-shell"><div class="media-leaf"></div></section>
          <aside class="unaffected-sibling"></aside>
          <section class="media-direct-shell"><div class="media-direct-leaf"></div></section>
          <section class="media-inherit-shell"><div class="media-inherit-leaf"></div></section>`;
        globalThis.__mediaShell = document.querySelector('.media-shell');
        globalThis.__mediaLeaf = document.querySelector('.media-leaf');
        globalThis.__mediaUnaffected = document.querySelector('.unaffected-sibling');
    )JS", "targeted-media-recascade-setup.js");
    const auto snapshot = [&] {
        return evaluate(engine, R"JS((() => [
          getComputedStyle(__mediaShell).paddingLeft,
          getComputedStyle(__mediaLeaf).width,
          getComputedStyle(__mediaLeaf).height,
          getComputedStyle(__mediaUnaffected).width,
          getComputedStyle(document.querySelector('.media-direct-shell')).paddingLeft,
          getComputedStyle(document.querySelector('.media-direct-leaf')).height,
          getComputedStyle(document.querySelector('.media-inherit-leaf')).width
        ])())JS", "targeted-media-recascade-snapshot.js");
    };
    const auto wide = snapshot();
    require(wide == R"(["3px","17px","11px","23px","2px","7px","17px"])",
        "wide targeted-media cascade was incorrect: " + wide);

    resize(engine, 480, 600, 2);
    wait_for_consumed_inputs(engine, 2, "compact targeted-media viewport not applied");
    const auto compact = snapshot();
    require(compact == R"(["13px","41px","29px","23px","19px","31px","43px"])",
        "media activation did not recascade matched descendants: " + compact);

    resize(engine, 800, 600, 3);
    wait_for_consumed_inputs(engine, 3, "restored targeted-media viewport not applied");
    const auto restored = snapshot();
    require(restored == R"(["3px","17px","11px","23px","2px","7px","17px"])",
        "media deactivation did not restore matched subtree styles: " + restored);
    webscene_engine_destroy(engine);
}

void test_attribute_invalidation_scopes_subject_and_descendant_rules()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "attribute invalidation engine creation failed");
    resize(engine, 800, 600, 1);
    wait_for_consumed_inputs(engine, 1, "attribute invalidation viewport not applied");
    const auto result = evaluate(engine, R"JS((() => {
      document.body.innerHTML = `
        <style>
          [data-viewport-state="compact"] { width:41px; }
          [data-container-state="compact"] .attribute-child { height:29px; }
          [data-container-state="compact"] > .direct-child { width:31px; }
          [data-sibling-state="compact"] + .adjacent-target { width:23px; }
          [data-sibling-state="compact"] ~ .general-target { width:27px; }
          .pseudo-not-subject { width:11px; }
          .pseudo-not-subject:not([data-suppressed]) { width:43px; }
          [data-variable-state="compact"] { --consumer-height:37px; }
          .attribute-child { height:11px; }
          .variable-consumer { height:var(--consumer-height, 13px); }
        </style>
        <section id="attribute-subject"><div></div></section>
        <section id="attribute-container"><div class="attribute-child direct-child"></div></section>
        <section><div id="sibling-trigger"></div><div class="adjacent-target"></div><div class="general-target"></div></section>
        <div id="pseudo-not-subject" class="pseudo-not-subject"></div>
        <section id="variable-container"><div class="variable-consumer"></div></section>`;
      const subject = document.getElementById('attribute-subject');
      const container = document.getElementById('attribute-container');
      const child = container.firstElementChild;
      const variableContainer = document.getElementById('variable-container');
      const variableConsumer = variableContainer.firstElementChild;
      const siblingTrigger = document.getElementById('sibling-trigger');
      const pseudoNotSubject = document.getElementById('pseudo-not-subject');
      subject.setAttribute('data-viewport-state', 'compact');
      const subjectWidth = getComputedStyle(subject).width;
      container.setAttribute('data-container-state', 'compact');
      const descendantHeight = getComputedStyle(child).height;
      container.removeAttribute('data-container-state');
      const restoredHeight = getComputedStyle(child).height;
      variableContainer.setAttribute('data-variable-state', 'compact');
      const variableHeight = getComputedStyle(variableConsumer).height;
      container.setAttribute('data-container-state', 'compact');
      const directWidth = getComputedStyle(child).width;
      siblingTrigger.setAttribute('data-sibling-state', 'compact');
      const adjacentWidth = getComputedStyle(document.querySelector('.adjacent-target')).width;
      const generalWidth = getComputedStyle(document.querySelector('.general-target')).width;
      const pseudoBefore = getComputedStyle(pseudoNotSubject).width;
      pseudoNotSubject.setAttribute('data-suppressed', '');
      const pseudoSuppressed = getComputedStyle(pseudoNotSubject).width;
      pseudoNotSubject.removeAttribute('data-suppressed');
      const pseudoRestored = getComputedStyle(pseudoNotSubject).width;
      return [subjectWidth, descendantHeight, restoredHeight, variableHeight,
        directWidth, adjacentWidth, generalWidth,
        pseudoBefore, pseudoSuppressed, pseudoRestored];
    })())JS", "attribute-invalidation-scope.js");
    require(result == R"(["41px","29px","11px","37px","31px","23px","27px","43px","11px","43px"])",
        "attribute invalidation lost subject or descendant selector updates: " + result);
    webscene_engine_destroy(engine);
}
