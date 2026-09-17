// These tests measure invalidation work, not wall-clock limits. Unrelated DOM
// and stylesheet growth must not amplify a fixed-size component mutation.
void test_variadic_child_vector_scaling()
{
    bool bounded = true;
    for (const auto* method : {"append", "prepend", "before", "after", "replaceChildren", "replaceWith"})
    for (const auto parent_count : {1, 2, 8})
    for (const auto count : {8, 128}) {
        auto* engine = webscene_engine_create(0);
        require(engine != nullptr, "variadic child-vector engine creation failed");
        resize(engine, 800, 600, 1);
        wait_for_consumed_inputs(engine, 1, "variadic child-vector viewport not applied");
        auto source = std::string{R"JS((() => {
          const sources=Array.from({length:PARENTS},()=>document.createElement('div'));
          const destination=document.createElement('div');
          const anchor=document.createElement('i'); destination.appendChild(anchor);
          const nodes=Array.from({length:COUNT},(_,i)=>{
            const n=document.createElement('b'); n.id='move-'+i; sources[i%sources.length].appendChild(n); return n;
          });
          document.body.append(...sources,destination);
          const style=document.createElement('style');
          style.textContent='b {display:block;width:11px;height:13px} b:first-child {width:41px} b:last-child {height:43px}';
          document.head.appendChild(style);
          globalThis.moveSiblings=()=>{
            const ordered=nodes.slice().reverse();
            if(['before','after','replaceWith'].includes('METHOD')) anchor.METHOD(...ordered);
            else destination.METHOD(...ordered);
            const expected=['append','after'].includes('METHOD')?[anchor,...ordered]:
              ['prepend','before'].includes('METHOD')?[...ordered,anchor]:ordered;
            return sources.every(source=>source.childNodes.length===0) && destination.childNodes.length===expected.length &&
              expected.every((n,i)=>destination.childNodes[i]===n && n.parentNode===destination) &&
              getComputedStyle(ordered[0]).width=== (expected[0]===anchor?'11px':'41px') &&
              getComputedStyle(ordered[ordered.length-1]).height===(expected[expected.length-1]===anchor?'13px':'43px');
          };
          return true;
        })())JS"};
        source.replace(source.find("COUNT"), 5, std::to_string(count));
        source.replace(source.find("PARENTS"), 7, std::to_string(parent_count));
        for (size_t at = source.find("METHOD"); at != std::string::npos; at = source.find("METHOD"))
            source.replace(at, 6, method);
        require(evaluate(engine, source, "variadic-vector-setup.js") == "true", "variadic vector setup failed");
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        constexpr auto counter = "dom-variadic-detach-child-visits";
        std::this_thread::sleep_for(std::chrono::milliseconds(260));
        execute_and_wait_for_wrapper_retention_scene(engine,
            "document.body.style.backgroundColor='rgb(1,2,3)'", "variadic-vector-before.js");
        const auto before = runtime_counter_diagnostic(engine, counter);
#endif
        require(evaluate(engine, "moveSiblings()", "variadic-vector-move.js") == "true",
            std::string("variadic child-vector move changed order, identity or styles: ") + method);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        std::this_thread::sleep_for(std::chrono::milliseconds(260));
        execute_and_wait_for_wrapper_retention_scene(engine,
            "document.body.style.backgroundColor='rgb(3,2,1)'", "variadic-vector-after.js");
        const auto delta = runtime_counter_diagnostic(engine, counter) - before;
        std::cout << "variadic-child-vector method=" << method << " parents=" << parent_count << " children=" << count
            << " detach-child-visits=" << delta << '\n';
        bounded &= delta > 0 && delta <= static_cast<uint64_t>(count + 1);
#endif
        webscene_engine_destroy(engine);
    }
    require(bounded, "variadic sibling removal rescanned the child vector instead of compacting once");
}

void test_compiled_subject_index_scaling()
{
    for(const auto targets:{8,128}) {
        std::vector<uint64_t> observations;
        for(const auto unrelated:{32,1024}) {
            auto* engine=webscene_engine_create(0);
            require(engine!=nullptr,"subject index engine creation failed");
            resize(engine,800,600,1);
            wait_for_consumed_inputs(engine,1,"subject index initial viewport failed");
            auto source=std::string{R"JS((([count,noise]) => {
              document.body.innerHTML='<section class="subject-list">'+
                Array.from({length:count},()=>'<div class="target"></div>').join('')+'</section>';
              const style=document.createElement('style');
              style.textContent='.subject-list {--width:31px}.target {width:var(--width);height:1px}'+
                '@media(max-width:600px){.subject-list {--width:47px}}'+
                Array.from({length:noise},(_,i)=>':not([data-disabled]).unused-'+i+' {width:7px}').join('');
              document.head.appendChild(style);
              globalThis.subjectSnapshot=()=>Array.from(document.querySelectorAll('.target')).every(n=>
                getComputedStyle(n).width === (innerWidth<=600?'47px':'31px'));
              return subjectSnapshot();
            })(COUNTS))JS"};
            source.replace(source.find("COUNTS"),6,"["+std::to_string(targets)+","+std::to_string(unrelated)+"]");
            require(evaluate(engine,source,"subject-index-setup.js")=="true","subject index initial style mismatch");
            for(int transition=0;transition<2;++transition) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                std::this_thread::sleep_for(std::chrono::milliseconds(260));
                execute_and_wait_for_wrapper_retention_scene(engine,
                    "document.body.style.backgroundColor='rgb(1,2,3)'","subject-index-before.js");
                const auto before=runtime_counter_diagnostic(engine,"css-cascade-candidate-checks");
#endif
                resize(engine,transition?800:480,600,transition+2);
                wait_for_consumed_inputs(engine,transition+2,"subject index transition failed");
                require(evaluate(engine,"subjectSnapshot()","subject-index-after.js")=="true",
                    "subject index media activation/deactivation style mismatch");
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                std::this_thread::sleep_for(std::chrono::milliseconds(260));
                execute_and_wait_for_wrapper_retention_scene(engine,
                    "document.body.style.backgroundColor='rgb(3,2,1)'","subject-index-publication.js");
                const auto visits=runtime_counter_diagnostic(engine,"css-cascade-candidate-checks")-before;
                std::cout<<"subject-index-runtime targets="<<targets<<" unrelated="<<unrelated
                    <<" transition="<<transition<<" candidates="<<visits<<'\n';
                require(visits>0 && visits<=static_cast<uint64_t>(targets+4),
                    "unrelated pseudo-prefixed rules reached subject candidate matching");
                observations.push_back(visits);
#endif
            }
            webscene_engine_destroy(engine);
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        require(observations[0]==observations[2] && observations[1]==observations[3],
            "unrelated rule growth amplified subject candidate work");
#endif
    }
}

void test_compiled_css_invalidation_scaling()
{
    std::vector<std::array<uint64_t, 7>> observations;
    for (const auto unrelated_count : {32, 1024}) {
        auto* engine = webscene_engine_create(0);
        require(engine != nullptr, "compiled invalidation engine creation failed");
        resize(engine, 800, 600, 1);
        wait_for_consumed_inputs(engine, 1, "compiled invalidation viewport not applied");
        auto source = std::string{R"JS((count => {
          const style = document.createElement('style');
          style.textContent = `
            .local > .target { width:11px; height:13px; }
            .local[data-a][data-b] > .target { width:41px; }
            .local.active[data-c] > .target { height:43px; }
            .card { width:17px; }
            .card:not(:has([data-ready])) { width:47px; }
            .local.active + .sibling { width:53px; }
            .sibling { width:19px; }
            [class~="raw"] > .target { height:59px; }
          ` + Array.from({length: count}, (_, i) =>
            '.unrelated-' + i + '[data-unrelated-' + i + '] { width:7px; }').join('');
          document.body.innerHTML = '<div id="local" class="local active" data-a data-b data-c><div class="target"></div></div><div class="sibling"></div><div class="card"><span id="ready"></span></div>'
            + Array.from({length: count}, (_, i) => '<div class="unrelated-' + i + '"></div>').join('');
          document.head.appendChild(style);
          globalThis.local = document.getElementById('local');
          globalThis.target = local.firstElementChild;
          globalThis.ready = document.getElementById('ready');
          globalThis.card = ready.parentElement;
          globalThis.sibling = document.querySelector('.sibling');
          return [getComputedStyle(target).width, getComputedStyle(target).height,
            getComputedStyle(card).width, getComputedStyle(sibling).width];
        })(NUMBER))JS"};
        source.replace(source.find("NUMBER"), 6, std::to_string(unrelated_count));
        const auto setup = evaluate(engine, source, "compiled-invalidation-setup.js");
        require(setup == R"(["41px","43px","47px","53px"])",
            "compiled invalidation initial styles differ: " + setup);
        const auto listener_ready = evaluate(engine, R"JS((() => {
          addEventListener('resize', () => {
            local.removeAttribute('data-a');
            local.removeAttribute('data-b');
            local.classList.remove('active');
            local.removeAttribute('data-c');
            const checkpoint = [getComputedStyle(target).width,
              getComputedStyle(target).height, getComputedStyle(sibling).width];
            for (let i = 0; i < 100; ++i) {
              local.className = i % 2 ? 'local raw' : 'local';
              ready.setAttribute('data-ready', String(i));
            }
            globalThis.checkpoint = checkpoint;
          }, {once:true});
          return true;
        })())JS", "compiled-invalidation-listener.js");
        require(listener_ready == "true", "compiled invalidation listener not installed");
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        // Scene diagnostics are sampled at most once per 250 ms. Publish a
        // fresh snapshot explicitly; input consumption is not that barrier.
        std::this_thread::sleep_for(std::chrono::milliseconds(260));
        execute_and_wait_for_wrapper_retention_scene(engine,
            "document.body.style.backgroundColor = 'rgb(1, 2, 3)'",
            "compiled-invalidation-baseline-publication.js");
        const std::array<std::string_view, 7> names{
            "selector-invalidation-plan-lookups",
            "selector-invalidation-candidate-visits",
            "selector-invalidation-fallback-visits",
            "css-compound-match-checks", "css-rule-match-checks",
            "css-cascade-applications", "css-cascade-candidate-checks"};
        std::array<uint64_t, 7> before{};
        for (size_t i = 0; i < names.size(); ++i)
            before[i] = runtime_counter_diagnostic(engine, names[i]);
#endif
        const auto started = std::chrono::steady_clock::now();
        resize(engine, 960, 600, 2);
        wait_for_consumed_inputs(engine, 2, "compiled invalidation resize not consumed");
        std::string result;
        for (size_t attempt = 0; attempt < 250; ++attempt) {
            result = evaluate(engine, R"JS(
              typeof checkpoint === 'undefined' ? null :
                [checkpoint, getComputedStyle(target).height, getComputedStyle(card).width]
            )JS", "compiled-invalidation-result.js");
            if (result != "null") break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        require(result == R"([["11px","13px","19px"],"59px","17px"])",
            "compiled invalidation lost batched transitions: " + result);
        const auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        std::cout << "css-invalidation-scaling unrelated=" << unrelated_count
            << " resize-and-check-ms=" << elapsed;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        std::this_thread::sleep_for(std::chrono::milliseconds(260));
        execute_and_wait_for_wrapper_retention_scene(engine,
            "document.body.style.backgroundColor = 'rgb(3, 2, 1)'",
            "compiled-invalidation-result-publication.js");
        std::array<uint64_t, 7> delta{};
        for (size_t i = 0; i < names.size(); ++i) {
            delta[i] = runtime_counter_diagnostic(engine, names[i]) - before[i];
            std::cout << ' ' << names[i] << '=' << delta[i];
        }
        require(delta[0] > 0 && delta[0] <= 40 && delta[1] <= 40 && delta[2] == 0,
            "local transitions performed excessive work or used document fallback");
        observations.push_back(delta);
#endif
        std::cout << '\n';
        webscene_engine_destroy(engine);
    }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    require(observations[0] == observations[1],
        "unrelated DOM/rule growth amplified local selector invalidation work");
#endif
}

void test_character_data_stable_style_scaling()
{
    for (const auto affected : {8, 128}) for (const auto unrelated : {32, 1024}) {
        auto* engine = webscene_engine_create(0);
        require(engine != nullptr, "character-data scaling engine creation failed");
        resize(engine, 800, 600, 1);
        wait_for_consumed_inputs(engine, 1, "character-data viewport not applied");
        auto source = std::string{R"JS((() => {
          document.body.innerHTML='<main>'+('<div class="data-shell">seed</div><div class="data-target"></div>').repeat(AFFECTED)+
            '</main><aside>'+Array.from({length:UNRELATED},(_,i)=>'<div class="noise-'+i+'"></div>').join('')+'</aside>';
          const style=document.createElement('style');
          style.textContent='.data-target {display:block;width:11px}.data-shell:empty + .data-target {width:41px}'+
            Array.from({length:UNRELATED},(_,i)=>'.noise-'+i+':empty {height:1px}').join('');
          document.head.appendChild(style);
          const nodes=Array.from(document.querySelectorAll('.data-shell'),n=>n.firstChild);
          const targets=Array.from(document.querySelectorAll('.data-target'));
          globalThis.editStableText=()=>{
            const properties=['data','nodeValue','textContent'];
            for(let i=0;i<200;i++)for(const n of nodes)n[properties[i%3]]='edit-'+i;
            return nodes.every(n=>n.data==='edit-199' && n.parentNode.childNodes.length===1) &&
              targets.every(n=>getComputedStyle(n).width==='11px');
          };
          return targets.every(n=>getComputedStyle(n).width==='11px');
        })())JS"};
        source.replace(source.find("AFFECTED"), 8, std::to_string(affected));
        for (size_t at = source.find("UNRELATED"); at != std::string::npos; at = source.find("UNRELATED"))
            source.replace(at, 9, std::to_string(unrelated));
        require(evaluate(engine, source, "character-data-setup.js") == "true", "character-data setup mismatch");
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const std::array<std::string_view, 8> names{
            "selector-invalidation-plan-lookups", "selector-invalidation-candidate-visits",
            "selector-invalidation-fallback-visits", "css-compound-match-checks", "css-rule-match-checks",
            "css-cascade-applications", "css-cascade-candidate-checks", "css-positional-sibling-visits"};
        std::this_thread::sleep_for(std::chrono::milliseconds(260));
        execute_and_wait_for_wrapper_retention_scene(engine,
            "document.body.style.backgroundColor='rgb(1,2,3)'", "character-data-before.js");
        std::array<uint64_t, 8> before{};
        for (size_t i = 0; i < names.size(); ++i) before[i] = runtime_counter_diagnostic(engine, names[i]);
#endif
        const auto started = std::chrono::steady_clock::now();
        require(evaluate(engine, "editStableText()", "character-data-edit.js") == "true",
            "character-data stable editing lost text, identity or computed style");
        std::cout << "character-data-stable affected=" << affected << " unrelated=" << unrelated
            << " edits=" << 200 * affected << " edit-and-check-ms="
            << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        std::this_thread::sleep_for(std::chrono::milliseconds(260));
        execute_and_wait_for_wrapper_retention_scene(engine,
            "document.body.style.backgroundColor='rgb(3,2,1)'", "character-data-after.js");
        bool unchanged = true;
        for (size_t i = 0; i < names.size(); ++i) {
            const auto delta = runtime_counter_diagnostic(engine, names[i]) - before[i];
            std::cout << ' ' << names[i] << '=' << delta;
            unchanged &= delta == 0;
        }
        require(unchanged, "nonempty character-data edits unnecessarily recascaded structural selectors");
#endif
        std::cout << '\n';
        webscene_engine_destroy(engine);
    }
}

void test_compiled_css_route_scaling(bool structural_only = false, bool structural_noise = false)
{
    // Two independent axes: grow the affected component and grow unrelated
    // DOM/rules. Actual matching/cascade work must follow only the first axis.
    for (const auto* shape : {"descendants", "siblings", "disabled", "variables", "relational", "identity", "dataset",
            "tree-wide-position", "tree-position", "tree-siblings", "tree-relational", "tree-empty", "tree-reactions",
            "tree-prepend", "tree-before", "tree-after", "tree-replace-children", "tree-replace-with",
            "tree-data", "tree-node-value", "tree-text-content"}) {
        if (structural_only && !std::string_view(shape).starts_with("tree-")) continue;
        for (const auto affected_count : {8, 128}) {
            std::vector<std::array<uint64_t, 8>> observations;
            for (const auto unrelated_count : {32, 1024}) {
                auto* engine = webscene_engine_create(0);
                require(engine != nullptr, "CSS route scaling engine creation failed");
                resize(engine, 800, 600, 1);
                wait_for_consumed_inputs(engine, 1, "CSS route scaling viewport not applied");
                auto source = std::string{R"JS(((shape, count, unrelated) => {
                  const style = document.createElement('style');
                  const target = '<div class="matrix-target"></div>';
                  let markup = '';
                  let rule = '';
                  if (shape === 'descendants') {
                    markup = '<div id="trigger">' + ('<div>' + target + '</div>').repeat(count) + '</div>';
                    rule = '.matrix-target:is(.active .matrix-target) {width:41px}';
                  } else if (shape === 'siblings') {
                    markup = '<div><i id="trigger"></i>' + target.repeat(count) + '</div>';
                    rule = '.matrix-target:where(.active ~ .matrix-target) {width:41px}';
                  } else if (shape === 'disabled') {
                    markup = '<fieldset id="trigger">' + '<input class="matrix-target">'.repeat(count) + '</fieldset>';
                    rule = '.matrix-target:disabled {width:41px}';
                  } else if (shape === 'identity' || shape === 'dataset') {
                    markup = '<div><i id="trigger"></i>' + target.repeat(count) + '</div>';
                    rule = shape === 'identity' ? '#on ~ .matrix-target {width:41px}'
                      : '[data-mode="on"] ~ .matrix-target {width:41px}';
                  } else if (shape === 'variables') {
                    markup = '<div><i id="trigger"></i><div class="provider">' +
                      ('<div class="alias">' + target + '</div>').repeat(count) + '</div></div>';
                    rule = '.active + .provider {--base:41px} .alias {--alias:var(--base, 11px)} .matrix-target {width:var(--alias)}';
                  } else if (shape === 'tree-position') {
                    markup = ('<div class="tree-list">' + target + '</div>').repeat(count);
                    rule = '.matrix-target:nth-child(2) {width:41px}';
                  } else if (shape === 'tree-prepend' || shape === 'tree-before' || shape === 'tree-after'
                      || shape === 'tree-replace-children' || shape === 'tree-replace-with') {
                    markup = '<div class="tree-list"><b class="anchor"></b>' + target.repeat(count) + '</div>';
                    rule = '.marker ~ .matrix-target {width:41px}';
                  } else if (shape === 'tree-siblings' || shape === 'tree-wide-position') {
                    markup = '<div class="tree-list">' + target.repeat(count) + '</div>';
                    rule = shape === 'tree-wide-position'
                      ? '.marker ~ .matrix-target:nth-child(n+2) {width:41px}'
                      : '.marker ~ .matrix-target {width:41px}';
                  } else if (shape === 'tree-relational') {
                    markup = '<div class="tree-list"></div>' + target.repeat(count);
                    rule = '.tree-list:has(.marker) ~ .matrix-target {width:41px}';
                  } else if (shape === 'tree-data' || shape === 'tree-node-value' || shape === 'tree-text-content') {
                    markup = ('<div class="tree-list">seed</div>' + target).repeat(count);
                    rule = '.tree-list:empty + .matrix-target {width:41px}';
                  } else if (shape === 'tree-empty' || shape === 'tree-reactions') {
                    const marker = shape === 'tree-reactions'
                      ? '<matrix-checkpoint class="marker"></matrix-checkpoint>' : '<i class="marker"></i>';
                    markup = ('<div class="tree-list">' + marker + '</div>' + target).repeat(count);
                    rule = '.tree-list:empty + .matrix-target {width:41px}';
                  } else {
                    markup = ('<div class="matrix-target"><div class="branch"><i class="ready"></i></div></div>').repeat(count);
                    rule = '.matrix-target:has(.branch > [data-ready]) {width:41px}';
                  }
                  document.body.innerHTML = '<main>' + markup + '</main><aside>' +
                    Array.from({length:unrelated}, (_, i) => '<div class="noise-' + i + '"></div>').join('') + '</aside>';
                  style.textContent = '.matrix-target {display:block;width:11px}' + rule +
                    Array.from({length:unrelated}, (_, i) => STRUCTURAL_NOISE
                      ? '.noise-' + i + ':empty, .noise-' + i + ':has(.irrelevant), .noise-' + i + ':nth-child(2n) {height:1px}'
                      : '.noise-' + i + ' {height:1px}').join('');
                  document.head.appendChild(style);
                  const targets = Array.from(document.querySelectorAll('.matrix-target'));
                  const ready = Array.from(document.querySelectorAll('.ready'));
                  const trigger = document.getElementById('trigger');
                  const lists = Array.from(document.querySelectorAll('.tree-list'));
                  const reactions = [];
                  if (shape === 'tree-reactions') {
                    customElements.define('matrix-checkpoint', class extends HTMLElement {
                      connectedCallback() {
                        if (this.target) reactions.push('connected:' + getComputedStyle(this.target).width);
                      }
                      disconnectedCallback() {
                        reactions.push('disconnected:' + getComputedStyle(this.target).width);
                      }
                    });
                    lists.forEach(list => { list.firstChild.target = list.nextElementSibling; });
                  }
                  globalThis.matrixMutation = enabled => {
                    reactions.length = 0;
                    if (shape.startsWith('tree-')) lists.forEach(list => {
                      if (shape === 'tree-data' || shape === 'tree-node-value' || shape === 'tree-text-content') {
                        const property=shape==='tree-data'?'data':shape==='tree-node-value'?'nodeValue':'textContent';
                        list.firstChild[property]=enabled?'':'restored';
                        if(list.childNodes.length!==1 || list.firstChild.nodeType!==3 ||
                            list.firstChild.data!==(enabled?'':'restored'))throw new Error('Character-data update replaced or lost the text node');
                        return;
                      }
                      if (shape === 'tree-prepend' || shape === 'tree-before' || shape === 'tree-after'
                          || shape === 'tree-replace-children' || shape === 'tree-replace-with') {
                        if (enabled) {
                          const first=document.createElement('i'), second=document.createElement('i');
                          first.className=second.className='marker';
                          const text=document.createTextNode('spacer'), fragment=document.createDocumentFragment();
                          fragment.appendChild(second);
                          const anchor=list.firstChild;
                          if(shape==='tree-prepend')list.prepend(first,text,fragment);
                          else if(shape==='tree-before')anchor.before(first,text,fragment);
                          else if(shape==='tree-replace-children')list.replaceChildren(first,text,fragment,...targets);
                          else if(shape==='tree-replace-with')anchor.replaceWith(first,text,fragment);
                          else anchor.after(first,text,fragment);
                          const replacing=shape==='tree-replace-children'||shape==='tree-replace-with';
                          if(list.childNodes.length!==count+(replacing?3:4) || fragment.childNodes.length!==0 ||
                              first.parentNode!==list || text.parentNode!==list || second.parentNode!==list)
                            throw new Error('Variadic mutation dropped arguments or retained fragment children');
                          if(targets.some((target,i)=>list.childNodes[i+(replacing?3:4)]!==target))
                            throw new Error('Variadic mutation lost or reordered retained targets');
                          list.inserted=[first,text,second];
                          list.anchor=anchor;
                        } else if(shape==='tree-replace-children')list.replaceChildren(list.anchor,...targets);
                        else if(shape==='tree-replace-with'){
                          list.inserted[0].replaceWith(list.anchor);
                          list.inserted.slice(1).forEach(node=>node.remove());
                        } else list.inserted.forEach(node=>node.remove());
                        return;
                      }
                      if (enabled !== (shape === 'tree-empty' || shape === 'tree-reactions')) {
                        const marker = document.createElement(shape === 'tree-reactions' ? 'matrix-checkpoint' : 'i');
                        marker.className = 'marker';
                        if (shape === 'tree-reactions') marker.target = list.nextElementSibling;
                        list.insertBefore(marker, list.firstChild);
                      } else list.removeChild(list.firstChild);
                    });
                    else if (shape === 'disabled') trigger.toggleAttribute('disabled', enabled);
                    else if (shape === 'identity') trigger.id = enabled ? 'on' : 'off';
                    else if (shape === 'dataset') {
                      if (enabled) trigger.dataset.mode = 'on';
                      else delete trigger.dataset.mode;
                    }
                    else if (shape === 'relational') ready.forEach(node => node.toggleAttribute('data-ready', enabled));
                    else trigger.className = enabled ? 'active' : '';
                    // Assert outside callbacks: reported reaction exceptions
                    // must not masquerade as a passing native regression.
                    if (shape === 'tree-reactions' && (reactions.length !== count ||
                        reactions.some(value => value !== (enabled ? 'disconnected:41px' : 'connected:11px'))))
                      throw new Error('Structural reaction checkpoint mismatch: ' + reactions.join(','));
                    return targets.map(node => getComputedStyle(node).width).join(',');
                  };
                  return targets.map(node => getComputedStyle(node).width).join(',');
                })(SHAPE,COUNT,UNRELATED))JS"};
                source.replace(source.find("SHAPE"), 5, "'" + std::string(shape) + "'");
                source.replace(source.find("STRUCTURAL_NOISE"), 16, structural_noise ? "true" : "false");
                source.replace(source.find("COUNT"), 5, std::to_string(affected_count));
                source.replace(source.find("UNRELATED"), 9, std::to_string(unrelated_count));
                const auto expected = [&](const char* value) {
                    std::string result = "\"";
                    for (int i = 0; i < affected_count; ++i) {
                        if (i) result += ',';
                        result += value;
                    }
                    return result + '"';
                };
                const auto setup = evaluate(engine, source, "css-route-setup.js");
                require(setup == expected("11px"),
                    std::string("CSS route initial style mismatch: ") + shape + ": " + setup);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                const std::array<std::string_view, 8> names{
                    "selector-invalidation-plan-lookups", "selector-invalidation-candidate-visits",
                    "selector-invalidation-fallback-visits", "css-compound-match-checks",
                    "css-rule-match-checks", "css-cascade-applications", "css-cascade-candidate-checks",
                    "css-positional-sibling-visits"};
                std::this_thread::sleep_for(std::chrono::milliseconds(260));
                execute_and_wait_for_wrapper_retention_scene(engine,
                    "document.body.style.backgroundColor = 'rgb(1, 2, 3)'", "css-route-baseline.js");
                std::array<uint64_t, 8> before{};
                for (size_t i = 0; i < names.size(); ++i)
                    before[i] = runtime_counter_diagnostic(engine, names[i]);
#endif
                const auto started = std::chrono::steady_clock::now();
                require(evaluate(engine, "matrixMutation(true)", "css-route-add.js") == expected("41px"),
                    std::string("CSS route addition failed: ") + shape);
                require(evaluate(engine, "matrixMutation(false)", "css-route-remove.js") == expected("11px"),
                    std::string("CSS route removal failed: ") + shape);
                std::cout << "css-route-scaling shape=" << shape << " affected=" << affected_count
                    << " unrelated=" << unrelated_count << " structural-noise=" << structural_noise
                    << " mutation-and-check-ms="
                    << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                std::this_thread::sleep_for(std::chrono::milliseconds(260));
                execute_and_wait_for_wrapper_retention_scene(engine,
                    "document.body.style.backgroundColor = 'rgb(3, 2, 1)'", "css-route-result.js");
                std::array<uint64_t, 8> delta{};
                for (size_t i = 0; i < names.size(); ++i) {
                    delta[i] = runtime_counter_diagnostic(engine, names[i]) - before[i];
                    std::cout << ' ' << names[i] << '=' << delta[i];
                }
                require(delta[2] == 0, "compiled routes unexpectedly used document fallback");
                require(delta[5] >= static_cast<uint64_t>(2 * affected_count),
                    "route scaling failed to measure the affected cascades");
                // Generous linear ceilings catch accidental quadratic cascades
                // without binding the test to an incidental exact count.
                for (const auto value : delta)
                    require(value <= static_cast<uint64_t>(200 * affected_count + 256),
                        "compiled route work exceeded its linear component-size budget");
                if (std::string_view(shape) == "tree-wide-position")
                    require(delta[7] > 0 && delta[7] <= static_cast<uint64_t>(4 * (affected_count + 1)),
                        "positional selectors repeatedly scanned the same sibling list");
                if (std::string_view(shape) == "tree-replace-children")
                    require(delta[3] <= static_cast<uint64_t>(32 * affected_count + 128),
                        "replacement restarted sibling-prefix matching for every inserted child");
                observations.push_back(delta);
#endif
                std::cout << '\n';
                webscene_engine_destroy(engine);
            }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            require(observations[0] == observations[1],
                std::string("unrelated DOM/rules amplified compiled route work: ") + shape);
#endif
        }
    }
}
