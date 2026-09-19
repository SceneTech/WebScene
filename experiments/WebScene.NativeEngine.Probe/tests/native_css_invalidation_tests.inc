// These tests measure invalidation work, not wall-clock limits. Unrelated DOM
// and stylesheet growth must not amplify a fixed-size component mutation.
void test_live_form_state_selectors_and_scaling()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "live form-state engine creation failed");
    resize(engine, 800, 600, 1U);
    wait_for_consumed_inputs(engine, 1U, "live form-state viewport was not applied");

    const auto correctness = evaluate(engine, R"JS((() => {
      document.body.innerHTML = `<style>
        .probe {padding-left:19px;padding-top:17px;margin-left:5px;margin-top:7px}
        .probe:invalid {padding-left:47px}
        .probe:placeholder-shown {padding-top:83px}
        .probe:user-valid {margin-left:43px}
        .probe:user-invalid {margin-top:61px}
      </style><form id="form"></form>`;
      const form=document.getElementById('form');
      const textarea=document.createElement('textarea');
      const text=document.createTextNode('seed');
      textarea.className='probe'; textarea.setAttribute('required','');
      textarea.setAttribute('placeholder','enter'); textarea.appendChild(text);
      form.appendChild(textarea);
      const snapshot=()=>{const style=getComputedStyle(textarea);return [
        textarea.matches(':valid'),textarea.matches(':invalid'),
        textarea.matches(':placeholder-shown'),style.paddingLeft,style.paddingTop,
        document.querySelectorAll(':invalid').length,
        document.querySelectorAll(':placeholder-shown').length,textarea.value];};
      const states=[snapshot()];
      text.data=''; states.push(snapshot());
      text.nodeValue='restored'; states.push(snapshot());
      textarea.value='user'; text.textContent=''; states.push(snapshot());
      textarea.value=''; states.push(snapshot());
      textarea.removeAttribute('placeholder'); states.push(snapshot());
      textarea.setAttribute('placeholder','again'); textarea.removeAttribute('required'); states.push(snapshot());
      textarea.setAttribute('required',''); textarea.value='user'; form.reset(); states.push(snapshot());
      textarea.remove(); textarea.value='detached'; form.appendChild(textarea); states.push(snapshot());
      const input=document.createElement('input'); input.className='probe';
      input.setAttribute('required',''); input.setAttribute('placeholder','type');
      input.setAttribute('value','seed'); form.appendChild(input);
      const inputSnapshot=()=>[input.value,input.matches(':valid'),input.matches(':invalid'),
        input.matches(':placeholder-shown'),getComputedStyle(input).paddingLeft,
        getComputedStyle(input).paddingTop];
      const inputStates=[inputSnapshot()];
      input.setAttribute('value',''); inputStates.push(inputSnapshot());
      input.setAttribute('value','seed'); inputStates.push(inputSnapshot());
      input.value='user'; input.setAttribute('value',''); inputStates.push(inputSnapshot());
      form.reset(); inputStates.push(inputSnapshot());
      input.defaultValue='next'; inputStates.push(inputSnapshot());
      input.value='dirty'; input.defaultValue=''; inputStates.push(inputSnapshot());
      return [states,inputStates];
    })())JS", "live-form-state-correctness.js");
    require(correctness == R"JSON([[[true,false,false,"19px","17px",0,0,"seed"],[false,true,true,"47px","83px",1,1,""],[true,false,false,"19px","17px",0,0,"restored"],[true,false,false,"19px","17px",0,0,"user"],[false,true,true,"47px","83px",1,1,""],[false,true,false,"47px","17px",1,0,""],[true,false,true,"19px","83px",0,1,""],[false,true,true,"47px","83px",1,1,""],[true,false,false,"19px","17px",0,0,"detached"]],[["seed",true,false,false,"19px","17px"],["",false,true,true,"47px","83px"],["seed",true,false,false,"19px","17px"],["user",true,false,false,"19px","17px"],["",false,true,true,"47px","83px"],["next",true,false,false,"19px","17px"],["dirty",true,false,false,"19px","17px"]]])JSON",
        "live form-state selector/computed-style lifecycle diverged: " + correctness);

    const auto native_setup = evaluate(engine, R"JS((() => {
      const input=document.createElement('input'); input.id='native-value';
      input.className='probe'; input.setAttribute('required',''); input.setAttribute('placeholder','type');
      document.getElementById('form').appendChild(input); input.focus(); return [
        input.matches(':invalid'),input.matches(':placeholder-shown'),
        input.matches(':user-valid'),input.matches(':user-invalid'),
        getComputedStyle(input).paddingLeft,getComputedStyle(input).paddingTop,
        getComputedStyle(input).marginLeft,getComputedStyle(input).marginTop];
    })())JS", "live-form-state-native-setup.js");
    require(native_setup == R"([true,true,false,false,"47px","83px","5px","7px"])",
        "native form-state setup diverged: " + native_setup);
    keyboard_input(engine, WEBSCENE_INPUT_TEXT, 'x', 2U);
    wait_for_consumed_inputs(engine, 2U, "native form-state input was not consumed");
    const auto native_result = evaluate(engine, R"JS((() => {const input=document.getElementById('native-value');return [
      input.value,input.matches(':valid'),input.matches(':invalid'),
      input.matches(':placeholder-shown'),input.matches(':user-valid'),
      input.matches(':user-invalid'),getComputedStyle(input).paddingLeft,
      getComputedStyle(input).paddingTop,getComputedStyle(input).marginLeft,
      getComputedStyle(input).marginTop];})())JS", "live-form-state-native-result.js");
    require(native_result == R"(["x",true,false,false,true,false,"19px","17px","43px","7px"])",
        "native input did not invalidate live form state: " + native_result);
    const auto user_validity_lifecycle = evaluate(engine, R"JS((() => {
      const input=document.getElementById('native-value'),first=document.getElementById('form');
      const snapshot=()=>{const style=getComputedStyle(input);return [
        input.matches(':valid'),input.matches(':invalid'),input.matches(':user-valid'),
        input.matches(':user-invalid'),style.marginLeft,style.marginTop];};
      input.value='';const states=[snapshot()];
      input.removeAttribute('required');states.push(snapshot());
      input.setAttribute('required','');input.remove();states.push([
        input.matches(':valid'),input.matches(':invalid'),input.matches(':user-valid'),
        input.matches(':user-invalid')]);
      const second=document.createElement('form');document.body.appendChild(second);second.appendChild(input);
      first.reset();states.push(snapshot());second.reset();states.push(snapshot());
      return states;
    })())JS", "live-form-user-validity-lifecycle.js");
    require(user_validity_lifecycle == R"JSON([[false,true,false,true,"5px","61px"],[true,false,true,false,"43px","7px"],[false,true,false,true],[false,true,false,true,"5px","61px"],[false,true,false,false,"5px","7px"]])JSON",
        "live user-validity lifecycle diverged: " + user_validity_lifecycle);

    const auto checkbox_validity = evaluate(engine, R"JS((() => {
      const form=document.getElementById('form'),checkbox=document.createElement('input');
      checkbox.type='checkbox';checkbox.setAttribute('value','payload');checkbox.required=true;
      checkbox.className='probe';form.appendChild(checkbox);
      const snapshot=()=>[checkbox.checked,checkbox.hasAttribute('checked'),
        checkbox.matches(':valid'),checkbox.matches(':invalid'),
        getComputedStyle(checkbox).paddingLeft];
      const states=[snapshot()];
      checkbox.setAttribute('checked','');states.push(snapshot());
      checkbox.checked=false;states.push(snapshot());
      checkbox.removeAttribute('checked');checkbox.setAttribute('checked','');states.push(snapshot());
      form.reset();states.push(snapshot());
      checkbox.checked=false;form.removeAttribute('id');form.reset();states.push(snapshot());
      return states;
    })())JS", "required-checkbox-validity.js");
    require(checkbox_validity == R"JSON([[false,false,false,true,"47px"],[true,true,true,false,"19px"],[false,true,false,true,"47px"],[false,true,false,true,"47px"],[true,true,true,false,"19px"],[true,true,true,false,"19px"]])JSON",
        "required checkbox validity did not follow live checkedness: " + checkbox_validity);

    const auto radio_validity = evaluate(engine, R"JS((() => {
      const owner=document.createElement('form');owner.id='radio-owner';
      owner.innerHTML='<input id="radio-a" class="probe" type="radio" name="plan" required>'+
        '<input id="radio-b" class="probe" type="radio" name="plan">';
      const external=document.createElement('input');external.id='radio-external';
      external.className='probe';external.type='radio';external.name='plan';
      external.setAttribute('form','radio-owner');
      const other=document.createElement('form');
      other.innerHTML='<input id="radio-other" type="radio" name="plan" required checked>';
      document.body.append(owner,external,other);
      const a=document.getElementById('radio-a'),b=document.getElementById('radio-b');
      const isolated=document.getElementById('radio-other');
      const snapshot=()=>[a.checked,b.checked,external.checked,
        a.matches(':invalid'),b.matches(':invalid'),external.matches(':invalid'),
        [a,b,external].map(node=>getComputedStyle(node).paddingLeft).join(','),
        isolated.matches(':valid')];
      const states=[snapshot()];
      b.checked=true;states.push(snapshot());
      external.checked=true;states.push(snapshot());
      b.click();states.push(snapshot());
      a.addEventListener('click',event=>event.preventDefault(),{once:true});
      a.click();states.push(snapshot());

      const host=document.createElement('div');document.body.appendChild(host);
      const shadow=host.attachShadow({mode:'open'});
      const shadowRadio=document.createElement('input');shadowRadio.type='radio';
      shadowRadio.name='plan';shadowRadio.required=true;shadow.appendChild(shadowRadio);
      const shadowWasInvalid=shadowRadio.matches(':invalid');
      shadowRadio.checked=true;
      return [states,[shadowWasInvalid,shadowRadio.matches(':valid'),
        b.checked,b.matches(':valid')]];
    })())JS", "required-radio-group-validity.js");
    require(radio_validity == R"JSON([[[false,false,false,true,true,true,"47px,47px,47px",true],[false,true,false,false,false,false,"19px,19px,19px",true],[false,false,true,false,false,false,"19px,19px,19px",true],[false,true,false,false,false,false,"19px,19px,19px",true],[false,true,false,false,false,false,"19px,19px,19px",true]],[true,true,true,true]])JSON",
        "required radio validity did not follow group ownership and activation: "
            + radio_validity);

    const auto setup = evaluate(engine, R"JS((() => {
      document.body.innerHTML='<style>.scale:invalid{width:2px}.scale:placeholder-shown{height:3px}'+
        Array.from({length:1024},(_,i)=>'.noise-'+i+':hover{width:7px}').join('')+'</style>'+
        '<main id="controls"></main>';
      const root=document.getElementById('controls'),fragment=document.createDocumentFragment();
      for(let i=0;i<4096;i++){const node=document.createElement('textarea');node.className='scale';
        node.setAttribute('required','');node.setAttribute('placeholder','p');fragment.appendChild(node);} root.appendChild(fragment);
      globalThis.scaleTarget=root.firstElementChild;
      return [root.children.length,scaleTarget.matches(':invalid'),
        scaleTarget.matches(':placeholder-shown'),getComputedStyle(scaleTarget).width,
        getComputedStyle(scaleTarget).height];
    })())JS", "live-form-state-scale-setup.js");
    require(setup == R"([4096,true,true,"2px","3px"])",
        "4,096-control form-state setup diverged: " + setup);

#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    std::this_thread::sleep_for(std::chrono::milliseconds(260));
    execute_and_wait_for_wrapper_retention_scene(engine,
        "document.body.style.backgroundColor='rgb(1,2,3)'",
        "live-form-state-scale-baseline-scene.js");
#endif
    webscene_engine_memory_metrics memory_before{sizeof(memory_before)};
    require(webscene_engine_get_memory_metrics(engine, &memory_before) != 0,
        "live form-state memory baseline was unavailable");
    webscene_engine_metrics metrics_before{};
    webscene_engine_get_metrics(engine, &metrics_before);
    const auto rss_before = file_system_access_test_peak_rss_bytes();
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    const auto cascades_before = runtime_counter_diagnostic(
        engine, "css-cascade-applications");
    const auto candidates_before = runtime_counter_diagnostic(
        engine, "selector-invalidation-candidate-visits");
#endif
    const auto started = std::chrono::steady_clock::now();
    const auto scale_result = evaluate(engine, R"JS((() => {
      for(let i=0;i<1000;i++) scaleTarget.value=(i&1)?'':'x';
      return [scaleTarget.value,scaleTarget.matches(':invalid'),
        scaleTarget.matches(':placeholder-shown'),getComputedStyle(scaleTarget).width,
        getComputedStyle(scaleTarget).height,document.querySelectorAll('.scale').length];
    })())JS", "live-form-state-scale-mutation.js");
    const auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    require(scale_result == R"(["",true,true,"2px","3px",4096])",
        "4,096-control form-state mutation diverged: " + scale_result);
    require(elapsed < 2000.0, "1,000 bounded form-state transitions exceeded two seconds");
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    std::this_thread::sleep_for(std::chrono::milliseconds(260));
    execute_and_wait_for_wrapper_retention_scene(engine,
        "document.body.style.backgroundColor='rgb(3,2,1)'",
        "live-form-state-scale-result-scene.js");
#endif
    webscene_engine_metrics metrics_after{};
    webscene_engine_get_metrics(engine, &metrics_after);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    const auto cascades = runtime_counter_diagnostic(
        engine, "css-cascade-applications") - cascades_before;
    const auto candidates = runtime_counter_diagnostic(
        engine, "selector-invalidation-candidate-visits") - candidates_before;
    require(cascades <= 2004U && candidates <= 4008U,
        "form-state transitions scanned unrelated controls or rules");
#else
    const auto cascades = uint64_t{0};
    const auto candidates = uint64_t{0};
#endif
    require(metrics_after.layout_passes <= metrics_before.layout_passes + 2U,
        "batched form-state transitions forced repeated layout");

    require(evaluate(engine, R"JS((() => {document.getElementById('controls').remove();
      delete globalThis.scaleTarget;return document.querySelectorAll('.scale').length;})())JS",
        "live-form-state-scale-retire.js") == "0", "form-state controls did not detach");
    require(webscene_engine_request_low_memory(engine) != 0,
        "live form-state low-memory request was rejected");
    webscene_engine_memory_metrics memory_after{sizeof(memory_after)};
    for (auto attempt=0; attempt<250; ++attempt) {
        require(webscene_engine_get_memory_metrics(engine, &memory_after) != 0,
            "live form-state final memory metrics were unavailable");
        if (memory_after.low_memory_notifications > memory_before.low_memory_notifications) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto rss_after = file_system_access_test_peak_rss_bytes();
    require(memory_after.low_memory_notifications > memory_before.low_memory_notifications
            && memory_after.v8_used_heap_bytes <= memory_before.v8_used_heap_bytes + 8U*1024U*1024U
            && memory_after.native_dom_node_count <= memory_before.native_dom_node_count + 8U,
        "form-state detach/GC heap or DOM lifecycle exceeded its bound");
    require(rss_before == 0U || rss_after <= rss_before + 64U*1024U*1024U,
        "form-state 4,096-control RSS growth exceeded 64 MiB");
    std::cout << "Live form-state gate: controls=4096 transitions=1000 ms=" << elapsed
        << " cascades=" << cascades << " candidates=" << candidates
        << " layouts=" << (metrics_after.layout_passes-metrics_before.layout_passes)
        << " heapBefore=" << memory_before.v8_used_heap_bytes
        << " heapAfter=" << memory_after.v8_used_heap_bytes
        << " rssBefore=" << rss_before << " rssAfter=" << rss_after << '\n';
    webscene_engine_destroy(engine);
}

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

void test_nested_functional_selector_scaling()
{
    constexpr auto affected_count = 128;
    constexpr auto transition_count = 1000;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    std::vector<std::array<uint64_t, 7>> observations;
#endif
    for (const auto unrelated_count : {32, 1024}) {
        auto* engine = webscene_engine_create(0);
        require(engine != nullptr, "nested functional selector engine creation failed");
        resize(engine, 800, 600, 1U);
        wait_for_consumed_inputs(
            engine, 1U, "nested functional selector viewport was not applied");

        webscene_engine_memory_metrics loaded_memory{sizeof(loaded_memory)};
        const auto rss_before = file_system_access_test_peak_rss_bytes();
        auto setup_source = std::string{R"JS((([affected, unrelated]) => {
          const style = document.createElement('style');
          style.id = 'nested-functional-style';
          style.textContent = `
            .functional-leaf { display:block; width:11px; height:13px; }
            .functional-host:is(:not(.blocked, [data-off]), .fallback) > .functional-leaf {
              width:41px;
            }
            .functional-host:not(:is(.blocked, [data-off]), .excluded) > .functional-leaf {
              height:43px;
            }
            .functional-host:has(.functional-marker:is(.active, [data-token="a,b"])) > .functional-leaf {
              --nested-has-state: active;
            }
          ` + Array.from({length: unrelated}, (_, index) =>
            `.noise-${index}:has(.unused-a-${index}:is(.unused-b-${index}, [data-unused="${index},x"])) { color:rgb(7,8,9); }`
          ).join('');
          const root = document.createElement('main');
          root.id = 'nested-functional-root';
          root.innerHTML = `<section class="functional-host">${
            '<i class="functional-leaf"></i>'.repeat(affected)}<b class="functional-marker active"></b></section>` +
            Array.from({length: unrelated}, (_, index) =>
              `<aside class="noise-${index}"></aside>`).join('');
          document.head.appendChild(style);
          document.body.appendChild(root);
          const host = root.firstElementChild;
          const marker = host.lastElementChild;
          const leaves = Array.from(host.querySelectorAll('.functional-leaf'));
          const snapshot = enabled => leaves.every(node => {
            const computed = getComputedStyle(node);
            return computed.width === (enabled ? '11px' : '41px') &&
              computed.height === (enabled ? '13px' : '43px') &&
              computed.getPropertyValue('--nested-has-state').trim() ===
                (enabled ? '' : 'active');
          });
          globalThis.mutateNestedFunctional = () => {
            for (let index = 0; index < TRANSITIONS; ++index) {
              const enabled = (index & 1) === 0;
              host.toggleAttribute('data-off', enabled);
              marker.classList.toggle('active', !enabled);
              if (!snapshot(enabled)) return false;
            }
            return true;
          };
          const first = getComputedStyle(leaves[0]);
          return [leaves.length, snapshot(false), first.width, first.height,
            host.matches(':is(:not(.blocked, [data-off]), .fallback)'),
            host.matches(':not(:is(.blocked, [data-off]), .excluded)'),
            host.matches(':has(.functional-marker:is(.active, [data-token="a,b"]))'),
            leaves.filter(node => getComputedStyle(node).width === '41px').length,
            leaves.filter(node => getComputedStyle(node).height === '43px').length];
        })(COUNTS))JS"};
        setup_source.replace(
            setup_source.find("TRANSITIONS"), 11, std::to_string(transition_count));
        setup_source.replace(
            setup_source.find("COUNTS"), 6,
            "[" + std::to_string(affected_count) + ","
                + std::to_string(unrelated_count) + "]");
        const auto setup = evaluate(
            engine, setup_source, "nested-functional-scale-setup.js");
        require(setup == "[128,true,\"41px\",\"43px\",true,true,true,128,128]",
            "nested functional selector scale setup diverged: " + setup);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        std::this_thread::sleep_for(std::chrono::milliseconds(260));
        execute_and_wait_for_wrapper_retention_scene(
            engine,
            "document.body.style.backgroundColor='rgb(1,2,3)'",
            "nested-functional-scale-baseline.js");
        const std::array<std::string_view, 7> names{
            "selector-invalidation-plan-lookups",
            "selector-invalidation-candidate-visits",
            "selector-invalidation-fallback-visits",
            "css-compound-match-checks",
            "css-rule-match-checks",
            "css-cascade-applications",
            "css-cascade-candidate-checks"};
        std::array<uint64_t, 7> before{};
        for (size_t index = 0; index < names.size(); ++index) {
            before[index] = runtime_counter_diagnostic(engine, names[index]);
        }
#endif
        for (auto attempt = 0; attempt < 250; ++attempt) {
            require(webscene_engine_get_memory_metrics(engine, &loaded_memory) != 0,
                "nested functional selector loaded memory metrics were unavailable");
            if (loaded_memory.v8_used_heap_bytes > 0U
                && loaded_memory.native_dom_node_count >= affected_count) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        require(loaded_memory.v8_used_heap_bytes > 0U
                && loaded_memory.native_dom_node_count >= affected_count,
            "nested functional selector loaded memory metrics did not settle");
        const auto started = std::chrono::steady_clock::now();
        require(evaluate(
                engine,
                "mutateNestedFunctional()",
                "nested-functional-scale-mutations.js") == "true",
            "nested functional selector transitions produced stale computed style");
        const auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        require(elapsed < 2000.0,
            "1,000 nested functional selector transitions exceeded two seconds");

#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        std::this_thread::sleep_for(std::chrono::milliseconds(260));
        execute_and_wait_for_wrapper_retention_scene(
            engine,
            "document.body.style.backgroundColor='rgb(3,2,1)'",
            "nested-functional-scale-result.js");
        std::array<uint64_t, 7> delta{};
        for (size_t index = 0; index < names.size(); ++index) {
            delta[index] = runtime_counter_diagnostic(engine, names[index])
                - before[index];
        }
        std::cout << "nested-functional-counters unrelated=" << unrelated_count;
        for (size_t index = 0; index < names.size(); ++index) {
            std::cout << ' ' << names[index] << '=' << delta[index];
        }
        std::cout << '\n';
        require(delta[0] > 0U && delta[1] > 0U && delta[2] == 0U,
            "nested functional selector mutation used no plan or document fallback");
        const std::array<uint64_t, 7> upper_bounds{
            transition_count * 8U,
            transition_count * affected_count * 5U,
            0U,
            transition_count * affected_count * (affected_count * 3U + 64U),
            transition_count * affected_count * 10U,
            transition_count * affected_count * 3U,
            transition_count * affected_count * 10U};
        for (size_t index = 0; index < delta.size(); ++index) {
            require(delta[index] <= upper_bounds[index],
                "nested functional selector work exceeded its affected-tree bound");
        }
        observations.push_back(delta);
#endif

        require(evaluate(engine, R"JS((() => {
          document.getElementById('nested-functional-root').remove();
          document.getElementById('nested-functional-style').remove();
          delete globalThis.mutateNestedFunctional;
          return document.querySelectorAll('.functional-leaf').length;
        })())JS", "nested-functional-scale-retire.js") == "0",
            "nested functional selector fixture did not detach");
        require(webscene_engine_request_low_memory(engine) != 0,
            "nested functional selector low-memory request was rejected");
        webscene_engine_memory_metrics final_memory{sizeof(final_memory)};
        for (auto attempt = 0; attempt < 250; ++attempt) {
            require(webscene_engine_get_memory_metrics(engine, &final_memory) != 0,
                "nested functional selector final memory metrics were unavailable");
            if (final_memory.low_memory_notifications
                > loaded_memory.low_memory_notifications) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        const auto rss_after = file_system_access_test_peak_rss_bytes();
        require(final_memory.low_memory_notifications
                > loaded_memory.low_memory_notifications
                && final_memory.v8_used_heap_bytes
                    <= loaded_memory.v8_used_heap_bytes + 1024U * 1024U
                && final_memory.native_dom_node_count
                    <= loaded_memory.native_dom_node_count,
            "nested functional selector teardown retained heap or DOM nodes: heap "
                + std::to_string(loaded_memory.v8_used_heap_bytes) + " -> "
                + std::to_string(final_memory.v8_used_heap_bytes) + ", dom "
                + std::to_string(loaded_memory.native_dom_node_count) + " -> "
                + std::to_string(final_memory.native_dom_node_count) + ", low-memory "
                + std::to_string(loaded_memory.low_memory_notifications) + " -> "
                + std::to_string(final_memory.low_memory_notifications));
        require(rss_before == 0U || rss_after <= rss_before + 64U * 1024U * 1024U,
            "nested functional selector peak RSS grew by more than 64 MiB");
        std::cout << "nested-functional-selectors affected=" << affected_count
            << " unrelated=" << unrelated_count
            << " transitions=" << transition_count
            << " ms=" << elapsed
            << " heapBefore=" << loaded_memory.v8_used_heap_bytes
            << " heapAfter=" << final_memory.v8_used_heap_bytes
            << " rssBefore=" << rss_before
            << " rssAfter=" << rss_after << '\n';
        webscene_engine_destroy(engine);
    }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    require(observations.size() == 2U && observations[0] == observations[1],
        "unrelated rules amplified nested functional selector invalidation work");
#endif
}

void test_relative_sibling_has_scaling()
{
    constexpr auto affected_count = 128;
    constexpr auto transition_count = 1000;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    std::vector<std::array<uint64_t, 7>> observations;
#endif
    for (const auto unrelated_count : {32, 1024}) {
        auto* engine = webscene_engine_create(0);
        require(engine != nullptr, "relative sibling :has engine creation failed");
        resize(engine, 800, 600, 1U);
        wait_for_consumed_inputs(
            engine, 1U, "relative sibling :has viewport was not applied");

        webscene_engine_memory_metrics loaded_memory{sizeof(loaded_memory)};
        const auto rss_before = file_system_access_test_peak_rss_bytes();
        auto setup_source = std::string{R"JS((([affected, unrelated]) => {
          const style = document.createElement('style');
          style.id = 'relative-sibling-has-style';
          style.textContent = `
            .relative-result {display:block;width:11px;height:13px}
            .adjacent-subject:has(+ .adjacent-marker.selected) > .relative-result {width:41px}
            .general-subject:has(~ .general-marker [data-ready]) > .relative-result {height:43px}
          ` + Array.from({length: unrelated}, (_, index) =>
            `.noise-${index}:has(+ .unused-${index}) {color:rgb(7,8,9)}`
          ).join('');
          const root = document.createElement('main');
          root.id = 'relative-sibling-has-root';
          root.innerHTML = Array.from({length: affected}, () => `<section class="relative-group">
            <i class="adjacent-subject"><b class="relative-result"></b></i>
            <i class="adjacent-marker selected"></i>
            <i class="general-subject"><b class="relative-result"></b></i>
            <i></i><i class="general-marker"><b data-ready></b></i>
          </section>`).join('') + '<aside>' +
            Array.from({length: unrelated}, (_, index) =>
              `<i class="noise-${index}"></i>`).join('') + '</aside>';
          document.head.appendChild(style);
          document.body.appendChild(root);
          const adjacentMarkers = Array.from(root.querySelectorAll('.adjacent-marker'));
          const ready = Array.from(root.querySelectorAll('[data-ready]'));
          const adjacentResults = Array.from(root.querySelectorAll('.adjacent-subject .relative-result'));
          const generalResults = Array.from(root.querySelectorAll('.general-subject .relative-result'));
          const snapshot = enabled => adjacentResults.every(node =>
              getComputedStyle(node).width === (enabled ? '41px' : '11px')) &&
            generalResults.every(node =>
              getComputedStyle(node).height === (enabled ? '43px' : '13px'));
          globalThis.mutateRelativeSiblingHas = () => {
            for (let index = 0; index < TRANSITIONS; ++index) {
              const enabled = (index & 1) !== 0;
              adjacentMarkers.forEach(node => node.classList.toggle('selected', enabled));
              ready.forEach(node => node.toggleAttribute('data-ready', enabled));
              if (!snapshot(enabled)) return false;
            }
            return true;
          };
          return [adjacentResults.length, generalResults.length, snapshot(true),
            root.querySelector('.adjacent-subject').matches(':has(+ .adjacent-marker.selected)'),
            root.querySelector('.general-subject').matches(':has(~ .general-marker [data-ready])')];
        })(COUNTS))JS"};
        setup_source.replace(
            setup_source.find("TRANSITIONS"), 11, std::to_string(transition_count));
        setup_source.replace(
            setup_source.find("COUNTS"), 6,
            "[" + std::to_string(affected_count) + ","
                + std::to_string(unrelated_count) + "]");
        const auto setup = evaluate(
            engine, setup_source, "relative-sibling-has-scale-setup.js");
        require(setup == "[128,128,true,true,true]",
            "relative sibling :has scale setup diverged: " + setup);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        std::this_thread::sleep_for(std::chrono::milliseconds(260));
        execute_and_wait_for_wrapper_retention_scene(
            engine,
            "document.body.style.backgroundColor='rgb(1,2,3)'",
            "relative-sibling-has-scale-baseline.js");
        const std::array<std::string_view, 7> names{
            "selector-invalidation-plan-lookups",
            "selector-invalidation-candidate-visits",
            "selector-invalidation-fallback-visits",
            "css-compound-match-checks",
            "css-rule-match-checks",
            "css-cascade-applications",
            "css-cascade-candidate-checks"};
        std::array<uint64_t, 7> before{};
        for (size_t index = 0; index < names.size(); ++index)
            before[index] = runtime_counter_diagnostic(engine, names[index]);
#endif
        for (auto attempt = 0; attempt < 250; ++attempt) {
            require(webscene_engine_get_memory_metrics(engine, &loaded_memory) != 0,
                "relative sibling :has loaded memory metrics were unavailable");
            if (loaded_memory.v8_used_heap_bytes > 0U
                && loaded_memory.native_dom_node_count >= affected_count) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        require(loaded_memory.v8_used_heap_bytes > 0U
                && loaded_memory.native_dom_node_count >= affected_count,
            "relative sibling :has loaded memory metrics did not settle");
        const auto started = std::chrono::steady_clock::now();
        require(evaluate(
                engine,
                "mutateRelativeSiblingHas()",
                "relative-sibling-has-scale-mutations.js") == "true",
            "relative sibling :has transitions produced stale computed style");
        const auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        require(elapsed < 5000.0,
            "1,000 relative sibling :has transitions exceeded five seconds");

#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        std::this_thread::sleep_for(std::chrono::milliseconds(260));
        execute_and_wait_for_wrapper_retention_scene(
            engine,
            "document.body.style.backgroundColor='rgb(3,2,1)'",
            "relative-sibling-has-scale-result.js");
        std::array<uint64_t, 7> delta{};
        for (size_t index = 0; index < names.size(); ++index)
            delta[index] = runtime_counter_diagnostic(engine, names[index])
                - before[index];
        std::cout << "relative-sibling-has-counters unrelated=" << unrelated_count;
        for (size_t index = 0; index < names.size(); ++index)
            std::cout << ' ' << names[index] << '=' << delta[index];
        std::cout << '\n';
        require(delta[0] > 0U && delta[1] > 0U && delta[2] == 0U,
            "relative sibling :has mutation used no plan or document fallback");
        const std::array<uint64_t, 7> upper_bounds{
            transition_count * affected_count * 4U,
            transition_count * affected_count * 80U,
            0U,
            transition_count * affected_count * 180U,
            transition_count * affected_count * 8U,
            transition_count * affected_count * 4U,
            transition_count * affected_count * 8U};
        for (size_t index = 0; index < delta.size(); ++index)
            require(delta[index] <= upper_bounds[index],
                "relative sibling :has work exceeded its affected-sibling bound");
        observations.push_back(delta);
#endif

        require(evaluate(engine, R"JS((() => {
          document.getElementById('relative-sibling-has-root').remove();
          document.getElementById('relative-sibling-has-style').remove();
          delete globalThis.mutateRelativeSiblingHas;
          return document.querySelectorAll('.relative-group').length;
        })())JS", "relative-sibling-has-scale-retire.js") == "0",
            "relative sibling :has fixture did not detach");
        require(webscene_engine_request_low_memory(engine) != 0,
            "relative sibling :has low-memory request was rejected");
        webscene_engine_memory_metrics final_memory{sizeof(final_memory)};
        for (auto attempt = 0; attempt < 250; ++attempt) {
            require(webscene_engine_get_memory_metrics(engine, &final_memory) != 0,
                "relative sibling :has final memory metrics were unavailable");
            if (final_memory.low_memory_notifications
                > loaded_memory.low_memory_notifications) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        const auto rss_after = file_system_access_test_peak_rss_bytes();
        require(final_memory.low_memory_notifications
                > loaded_memory.low_memory_notifications
                && final_memory.v8_used_heap_bytes
                    <= loaded_memory.v8_used_heap_bytes + 1024U * 1024U
                && final_memory.native_dom_node_count
                    <= loaded_memory.native_dom_node_count,
            "relative sibling :has teardown retained heap or DOM nodes");
        require(rss_before == 0U || rss_after <= rss_before + 64U * 1024U * 1024U,
            "relative sibling :has peak RSS grew by more than 64 MiB");
        std::cout << "relative-sibling-has affected=" << affected_count
            << " unrelated=" << unrelated_count
            << " transitions=" << transition_count
            << " ms=" << elapsed
            << " heapBefore=" << loaded_memory.v8_used_heap_bytes
            << " heapAfter=" << final_memory.v8_used_heap_bytes
            << " rssBefore=" << rss_before
            << " rssAfter=" << rss_after << '\n';
        webscene_engine_destroy(engine);
    }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    require(observations.size() == 2U && observations[0] == observations[1],
        "unrelated rules amplified relative sibling :has invalidation work");
#endif
}

void test_compiled_css_invalidation_scaling()
{
    test_nested_functional_selector_scaling();
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

void test_cascade_layer_mutation_scaling()
{
    for (const auto affected_count : {8, 128}) {
        std::vector<std::array<uint64_t, 5>> observations;
        for (const auto unrelated_count : {32, 1024}) {
            auto* engine = webscene_engine_create(0);
            require(engine != nullptr, "cascade layer scaling engine creation failed");
            resize(engine, 800, 600, 1);
            wait_for_consumed_inputs(engine, 1, "cascade layer scaling viewport not applied");
            auto source = std::string{R"JS((() => {
              document.body.innerHTML='<main id="layer-scope">'+
                '<div class="layer-target"></div>'.repeat(AFFECTED)+'</main><aside>'+
                Array.from({length:UNRELATED},(_,i)=>'<div class="layer-noise-'+i+'"></div>').join('')+'</aside>';
              const style=document.createElement('style');
              style.textContent='@layer reset, components;'+
                '@layer reset {.layer-target {display:block;width:11px;height:13px!important;'+
                  'margin:1px 2px 3px 4px;padding:5px 6px 7px 8px}}'+
                '@layer components {#layer-scope.active .layer-target {'+
                  'width:41px;height:43px!important;margin:20px 21px 22px 23px;'+
                  'margin-left:revert-layer;padding:30px 31px 32px 33px;'+
                  'padding-right:revert-layer}}'+
                Array.from({length:UNRELATED},(_,i)=>
                  '@layer noise-'+i+' {.layer-noise-'+i+' {width:7px}}').join('');
              document.head.appendChild(style);
              const scope=document.getElementById('layer-scope');
              const targets=Array.from(document.querySelectorAll('.layer-target'));
              const snapshot=enabled=>targets.every(node=>{
                const computed=getComputedStyle(node);
                return computed.width===(enabled?'41px':'11px') &&
                  computed.height==='13px' && computed.marginLeft==='4px' &&
                  computed.marginTop===(enabled?'20px':'1px') &&
                  computed.paddingRight==='6px' &&
                  computed.paddingTop===(enabled?'30px':'5px');
              });
              globalThis.mutateLayerScope=enabled=>{
                scope.classList.toggle('active',enabled);
                return snapshot(enabled);
              };
              return snapshot(false);
            })())JS"};
            source.replace(source.find("AFFECTED"), 8, std::to_string(affected_count));
            for (size_t at = source.find("UNRELATED"); at != std::string::npos;
                 at = source.find("UNRELATED")) {
                source.replace(at, 9, std::to_string(unrelated_count));
            }
            require(evaluate(engine, source, "cascade-layer-scaling-setup.js") == "true",
                "cascade layer scaling initial winner mismatch");
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            const std::array<std::string_view, 5> names{
                "selector-invalidation-candidate-visits",
                "selector-invalidation-fallback-visits",
                "css-rule-match-checks",
                "css-cascade-applications",
                "css-cascade-candidate-checks"};
            std::this_thread::sleep_for(std::chrono::milliseconds(260));
            execute_and_wait_for_wrapper_retention_scene(engine,
                "document.body.style.backgroundColor='rgb(1,2,3)'",
                "cascade-layer-scaling-before.js");
            std::array<uint64_t, 5> before{};
            for (size_t i = 0; i < names.size(); ++i)
                before[i] = runtime_counter_diagnostic(engine, names[i]);
#endif
            const auto started = std::chrono::steady_clock::now();
            require(evaluate(engine, "mutateLayerScope(true)",
                "cascade-layer-scaling-enable.js") == "true",
                "cascade layer activation chose the wrong normal/important winners");
            require(evaluate(engine, "mutateLayerScope(false)",
                "cascade-layer-scaling-disable.js") == "true",
                "cascade layer deactivation left stale winners");
            std::cout << "cascade-layer-scaling affected=" << affected_count
                << " unrelated=" << unrelated_count << " mutation-and-check-ms="
                << std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - started).count();
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            std::this_thread::sleep_for(std::chrono::milliseconds(260));
            execute_and_wait_for_wrapper_retention_scene(engine,
                "document.body.style.backgroundColor='rgb(3,2,1)'",
                "cascade-layer-scaling-after.js");
            std::array<uint64_t, 5> delta{};
            for (size_t i = 0; i < names.size(); ++i) {
                delta[i] = runtime_counter_diagnostic(engine, names[i]) - before[i];
                std::cout << ' ' << names[i] << '=' << delta[i];
            }
            require(delta[1] == 0,
                "layer mutation unexpectedly used document-wide selector fallback");
            require(delta[3] >= static_cast<uint64_t>(2 * affected_count)
                && delta[3] <= static_cast<uint64_t>(2 * affected_count + 8),
                "layer mutation did not stay bounded to the affected subtree");
            for (const auto value : delta)
                require(value <= static_cast<uint64_t>(16 * affected_count + 64),
                    "layer mutation exceeded its linear cascade-work budget");
            observations.push_back(delta);
#endif
            std::cout << '\n';
            webscene_engine_destroy(engine);
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        require(observations[0] == observations[1],
            "unrelated layered rules/nodes amplified targeted cascade work");
#endif
    }
}

void test_character_data_stable_style_scaling()
{
    for (const bool methods : {false, true})
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
            for(let i=0;i<200;i++)for(const n of nodes) {
              if(METHODS) {
                n.appendData('x');n.insertData(0,'y');n.deleteData(0,1);n.replaceData(n.length-1,1,'');
                if(n.substringData(0,99)!=='seed')return false;
              } else n[properties[i%3]]='edit-'+i;
            }
            return nodes.every(n=>n.data===(METHODS?'seed':'edit-199') && n.parentNode.childNodes.length===1) &&
              targets.every(n=>getComputedStyle(n).width==='11px');
          };
          return targets.every(n=>getComputedStyle(n).width==='11px');
        })())JS"};
        source.replace(source.find("AFFECTED"), 8, std::to_string(affected));
        for (size_t at = source.find("METHODS"); at != std::string::npos; at = source.find("METHODS"))
            source.replace(at, 7, methods ? "true" : "false");
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
        std::cout << "character-data-stable mode=" << (methods ? "methods" : "setters")
            << " affected=" << affected << " unrelated=" << unrelated
            << " edits=" << (methods ? 800 : 200) * affected << " edit-and-check-ms="
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

void test_text_topology_css_work_scaling()
{
    for (const auto affected : {8, 128}) for (const auto unrelated : {32, 1024}) {
        auto* engine = webscene_engine_create(0);
        require(engine != nullptr, "text-topology scaling engine creation failed");
        resize(engine, 800, 600, 1);
        wait_for_consumed_inputs(engine, 1, "text-topology viewport not applied");
        auto source = std::string{R"JS((() => {
          document.body.innerHTML='<main>'+('<div class="topology-shell">seed</div><div class="topology-target"></div>').repeat(AFFECTED)+
            '</main><aside>'+Array.from({length:UNRELATED},(_,i)=>'<div class="topology-noise-'+i+'"></div>').join('')+'</aside>';
          const style=document.createElement('style');
          style.textContent='.topology-target{display:block;width:11px}.topology-shell:empty + .topology-target{width:41px}'+
            Array.from({length:UNRELATED},(_,i)=>'.topology-noise-'+i+':empty{height:1px}').join('');
          document.head.appendChild(style);
          const shells=Array.from(document.querySelectorAll('.topology-shell'));
          const originals=shells.map(n=>n.firstChild);
          const targets=Array.from(document.querySelectorAll('.topology-target'));
          globalThis.editTextTopology=()=>{
            for(let pass=0;pass<100;pass++) {
              for(const original of originals) {
                const suffix=original.splitText(2);
                if(original.data!=='se'||suffix.data!=='ed'||original.nextSibling!==suffix)return false;
              }
              for(const shell of shells)shell.normalize();
            }
            return shells.every((shell,i)=>shell.childNodes.length===1 && shell.firstChild===originals[i] && shell.textContent==='seed') &&
              targets.every(n=>getComputedStyle(n).width==='11px');
          };
          return targets.every(n=>getComputedStyle(n).width==='11px');
        })())JS"};
        source.replace(source.find("AFFECTED"), 8, std::to_string(affected));
        for (size_t at = source.find("UNRELATED"); at != std::string::npos; at = source.find("UNRELATED"))
            source.replace(at, 9, std::to_string(unrelated));
        require(evaluate(engine, source, "text-topology-setup.js") == "true",
            "text-topology setup mismatch");
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const std::array<std::string_view, 9> names{
            "selector-invalidation-plan-lookups", "selector-invalidation-candidate-visits",
            "selector-invalidation-fallback-visits", "css-compound-match-checks", "css-rule-match-checks",
            "css-cascade-applications", "css-cascade-candidate-checks", "css-positional-sibling-visits",
            "detached-dom-root-containment-checks"};
        std::this_thread::sleep_for(std::chrono::milliseconds(260));
        execute_and_wait_for_wrapper_retention_scene(engine,
            "document.body.style.backgroundColor='rgb(1,2,3)'", "text-topology-before.js");
        std::array<uint64_t, 9> before{};
        for (size_t i = 0; i < names.size(); ++i) before[i] = runtime_counter_diagnostic(engine, names[i]);
#endif
        const auto started = std::chrono::steady_clock::now();
        require(evaluate(engine, "editTextTopology()", "text-topology-edit.js") == "true",
            "text splitting/normalization lost data, identity, order or computed style");
        std::cout << "text-topology affected=" << affected << " unrelated=" << unrelated
            << " split-normalize-pairs=" << 100 * affected << " edit-and-check-ms="
            << std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        std::this_thread::sleep_for(std::chrono::milliseconds(260));
        execute_and_wait_for_wrapper_retention_scene(engine,
            "document.body.style.backgroundColor='rgb(3,2,1)'", "text-topology-after.js");
        bool unchanged = true;
        for (size_t i = 0; i < names.size(); ++i) {
            const auto delta = runtime_counter_diagnostic(engine, names[i]) - before[i];
            std::cout << ' ' << names[i] << '=' << delta;
            unchanged &= delta == 0;
        }
        require(unchanged,
            "text splitting/normalization performed CSS work or quadratic detached-root scans");
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
            "tree-data", "tree-node-value", "tree-text-content", "tree-append-data", "tree-insert-data",
            "tree-delete-data", "tree-replace-data"}) {
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
                  } else if (shape === 'tree-data' || shape === 'tree-node-value' || shape === 'tree-text-content'
                      || ['tree-append-data','tree-insert-data','tree-delete-data','tree-replace-data'].includes(shape)) {
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
                      if (shape === 'tree-data' || shape === 'tree-node-value' || shape === 'tree-text-content'
                          || ['tree-append-data','tree-insert-data','tree-delete-data','tree-replace-data'].includes(shape)) {
                        const property=shape==='tree-data'?'data':shape==='tree-node-value'?'nodeValue':'textContent';
                        const n=list.firstChild;
                        if(shape==='tree-replace-data')n.replaceData(0,n.length,enabled?'':'restored');
                        else if(shape==='tree-delete-data' && enabled)n.deleteData(0,n.length);
                        else if(shape==='tree-append-data' && !enabled)n.appendData('restored');
                        else if(shape==='tree-insert-data' && !enabled)n.insertData(0,'restored');
                        else n[property]=enabled?'':'restored';
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
