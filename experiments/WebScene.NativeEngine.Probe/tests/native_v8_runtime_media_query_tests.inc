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

void test_media_query_candidate_scaling()
{
    bool bounded=true;
    for(const auto affected:{8,128}) {
        std::vector<std::array<uint64_t,4>> observations;
        for(const auto nodes:{32,1024}) for(const auto rules:{32,1024}) {
            auto* engine=webscene_engine_create(0);
            require(engine!=nullptr,"media candidate engine creation failed");
            resize(engine,800,600,1);
            wait_for_consumed_inputs(engine,1,"media candidate initial viewport failed");
            auto source=std::string{R"JS((([affected,nodes,rules])=>{
              const style=document.createElement('style');
              style.textContent='.media-affected {display:block;width:11px;height:13px}'+
                '@media(max-width:600px){.media-affected {width:41px}'+
                Array.from({length:rules},(_,i)=>'.media-absent-'+i+' {width:71px}').join('')+'}';
              document.body.innerHTML=Array.from({length:affected},()=>'<b class="media-affected"></b>').join('')+
                Array.from({length:nodes},(_,i)=>'<i class="media-unrelated-'+i+'"></i>').join('');
              document.head.appendChild(style);
              const targets=Array.from(document.querySelectorAll('.media-affected'));
              globalThis.mediaCandidateSnapshot=()=>targets.every(node=>
                getComputedStyle(node).width===(innerWidth<=600?'41px':'11px'));
              return mediaCandidateSnapshot();
            })(COUNTS))JS"};
            source.replace(source.find("COUNTS"),6,"["+std::to_string(affected)+","+
                std::to_string(nodes)+","+std::to_string(rules)+"]");
            const auto setup_result=evaluate(engine,source,"media-candidate-setup.js");
            require(setup_result=="true","media candidate setup styles failed: "+setup_result+" "+
                (setup_result=="true"?std::string{}:evaluate(engine,
                    "JSON.stringify({width:innerWidth,items:Array.from(document.querySelectorAll('.media-affected')).map(n=>getComputedStyle(n).width)})",
                    "media-candidate-setup-diagnostic.js")));
            for(int transition=0;transition<2;++transition) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                constexpr std::array counters{"css-rule-match-checks","css-compound-match-checks",
                    "css-cascade-applications","css-cascade-candidate-checks"};
                std::this_thread::sleep_for(std::chrono::milliseconds(260));
                execute_and_wait_for_wrapper_retention_scene(engine,
                    "document.body.style.backgroundColor='rgb(1,2,3)'","media-candidate-before.js");
                std::array<uint64_t,4> before{};
                for(size_t i=0;i<counters.size();++i) before[i]=runtime_counter_diagnostic(engine,counters[i]);
#endif
                resize(engine,transition?800:480,600,transition+2);
                wait_for_consumed_inputs(engine,transition+2,"media candidate transition failed");
                require(evaluate(engine,"mediaCandidateSnapshot()","media-candidate-after.js")=="true",
                    "media candidate activation/deactivation styles failed");
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                std::this_thread::sleep_for(std::chrono::milliseconds(260));
                execute_and_wait_for_wrapper_retention_scene(engine,
                    "document.body.style.backgroundColor='rgb(3,2,1)'","media-candidate-publish.js");
                std::array<uint64_t,4> delta{};
                std::cout<<"media-candidate-scaling affected="<<affected<<" unrelated-nodes="<<nodes
                    <<" unrelated-media-rules="<<rules<<" transition="<<transition;
                for(size_t i=0;i<counters.size();++i) {
                    delta[i]=runtime_counter_diagnostic(engine,counters[i])-before[i];
                    std::cout<<' '<<counters[i]<<'='<<delta[i];
                    bounded &= delta[i]<=static_cast<uint64_t>(4*affected+4);
                }
                std::cout<<'\n';observations.push_back(delta);
#endif
            }
            webscene_engine_destroy(engine);
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        for(size_t i=2;i<observations.size();++i) bounded &= observations[i]==observations[i%2];
#endif
    }
    require(bounded,"unrelated media-rule/node growth amplified breakpoint selector work");
}

void test_media_query_non_inherited_root_cascade_work()
{
    bool bounded = true;
    for (const auto descendants : {32, 1024}) {
        auto* engine = webscene_engine_create(0);
        require(engine != nullptr, "media root cascade engine creation failed");
        resize(engine, 800, 600, 1);
        wait_for_consumed_inputs(engine, 1, "media root initial viewport failed");
        auto source = std::string{R"JS((descendants => {
          const style=document.createElement('style');
          style.textContent=`.media-root {display:block;width:17px;outline-offset:2px;text-overflow:clip}
            .media-noise {display:block;height:3px}
            @media(max-width:600px) {.media-root {width:41px;outline-offset:6px;text-overflow:ellipsis}}`;
          const root=document.createElement('section'); root.className='media-root';
          root.innerHTML=Array.from({length:descendants},()=>'<i class="media-noise"></i>').join('');
          document.head.appendChild(style); document.body.appendChild(root);
          globalThis.mediaRootSnapshot=()=>[getComputedStyle(root).width,
            Array.from(root.children).every(node=>getComputedStyle(node).height==='3px')];
          return mediaRootSnapshot();
        })(COUNT))JS"};
        source.replace(source.find("COUNT"), 5, std::to_string(descendants));
        const auto setup_result = evaluate(engine, source, "media-root-setup.js");
        require(setup_result == R"(["17px",true])",
            "media root setup styles failed: " + setup_result);
        for (int transition = 0; transition < 2; ++transition) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            std::this_thread::sleep_for(std::chrono::milliseconds(260));
            execute_and_wait_for_wrapper_retention_scene(engine,
                "document.body.style.backgroundColor='rgb(1,2,3)'",
                "media-root-before.js");
            const auto before = runtime_counter_diagnostic(
                engine, "css-cascade-applications");
#endif
            resize(engine, transition == 0 ? 480 : 800, 600, transition + 2);
            wait_for_consumed_inputs(
                engine, transition + 2, "media root transition failed");
            const auto expected = transition == 0
                ? R"(["41px",true])" : R"(["17px",true])";
            require(evaluate(engine, "mediaRootSnapshot()", "media-root-after.js") == expected,
                "media root activation/deactivation styles failed");
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            std::this_thread::sleep_for(std::chrono::milliseconds(260));
            execute_and_wait_for_wrapper_retention_scene(engine,
                "document.body.style.backgroundColor='rgb(3,2,1)'",
                "media-root-publish.js");
            const auto applications = runtime_counter_diagnostic(
                engine, "css-cascade-applications") - before;
            std::cout << "media-root-cascade descendants=" << descendants
                << " transition=" << transition
                << " css-cascade-applications=" << applications << '\n';
            bounded = bounded && applications <= 3U;
#endif
        }
        webscene_engine_destroy(engine);
    }
    require(bounded,
        "non-inherited media root change recascaded unrelated descendants");
}

void test_media_query_custom_property_consumer_cascade_work()
{
    bool bounded = true;
    std::vector<std::array<uint64_t, 2>> observations;
    for (const auto descendants : {32, 1024}) {
        auto* engine = webscene_engine_create(0);
        require(engine != nullptr,
            "media custom-property engine creation failed");
        resize(engine, 800, 600, 1);
        wait_for_consumed_inputs(
            engine, 1, "media custom-property initial viewport failed");
        auto source = std::string{R"JS((descendants => {
          const style=document.createElement('style');
          style.textContent=`
            .media-variable-root {--responsive-width:17px}
            .media-variable-direct {display:block;width:var(--responsive-width);height:3px}
            .media-variable-sheet-alias {--sheet-width:var(--responsive-width)}
            .media-variable-sheet-consumer {display:block;width:var(--sheet-width);height:3px}
            .media-variable-inline-consumer {display:block;width:var(--inline-width);height:3px}
            .media-variable-noise {display:block;height:2px}
            @media(max-width:600px) {.media-variable-root {--responsive-width:41px}}`;
          const root=document.createElement('section');
          root.className='media-variable-root';
          root.innerHTML=
            Array.from({length:8},()=>'<b class="media-variable-direct"></b>').join('')+
            '<div class="media-variable-sheet-alias"><i class="media-variable-sheet-consumer"></i></div>'+
            '<div style="--inline-width:var(--responsive-width)"><i class="media-variable-inline-consumer"></i></div>'+
            Array.from({length:descendants},()=>'<span class="media-variable-noise"></span>').join('');
          document.head.appendChild(style); document.body.appendChild(root);
          const consumers=Array.from(root.querySelectorAll(
            '.media-variable-direct,.media-variable-sheet-consumer,.media-variable-inline-consumer'));
          globalThis.mediaVariableSnapshot=()=>consumers.map(node=>getComputedStyle(node).width);
          return mediaVariableSnapshot();
        })(COUNT))JS"};
        source.replace(source.find("COUNT"), 5, std::to_string(descendants));
        const auto setup_result = evaluate(
            engine, source, "media-custom-property-setup.js");
        require(setup_result ==
                R"(["17px","17px","17px","17px","17px","17px","17px","17px","17px","17px"])",
            "media custom-property setup styles failed: " + setup_result);
        std::array<uint64_t, 2> applications{};
        for (int transition = 0; transition < 2; ++transition) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            std::this_thread::sleep_for(std::chrono::milliseconds(260));
            execute_and_wait_for_wrapper_retention_scene(engine,
                "document.body.style.backgroundColor='rgb(1,2,3)'",
                "media-custom-property-before.js");
            const auto before = runtime_counter_diagnostic(
                engine, "css-cascade-applications");
#endif
            resize(engine, transition == 0 ? 480 : 800, 600, transition + 2);
            wait_for_consumed_inputs(engine, transition + 2,
                "media custom-property transition failed");
            const auto expected = transition == 0
                ? R"(["41px","41px","41px","41px","41px","41px","41px","41px","41px","41px"])"
                : R"(["17px","17px","17px","17px","17px","17px","17px","17px","17px","17px"])";
            const auto snapshot = evaluate(
                engine, "mediaVariableSnapshot()", "media-custom-property-after.js");
            require(snapshot == expected,
                "media custom-property activation/deactivation failed: " + snapshot);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            std::this_thread::sleep_for(std::chrono::milliseconds(260));
            execute_and_wait_for_wrapper_retention_scene(engine,
                "document.body.style.backgroundColor='rgb(3,2,1)'",
                "media-custom-property-publish.js");
            applications[transition] = runtime_counter_diagnostic(
                engine, "css-cascade-applications") - before;
            std::cout << "media-custom-property-cascade descendants="
                << descendants << " transition=" << transition
                << " css-cascade-applications="
                << applications[transition] << '\n';
            // The token owner and two alias owners require complete cascades.
            // The ten ordinary consumers use property-targeted replay, so
            // growing unrelated descendants must not restore one full cascade
            // per consumer.
            bounded = bounded && applications[transition] <= 3U;
#endif
        }
        observations.push_back(applications);
        webscene_engine_destroy(engine);
    }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    bounded = bounded && observations.size() == 2U
        && observations[0] == observations[1];
#endif
    require(bounded,
        "unrelated descendants amplified media custom-property cascade work");
}

void test_media_query_variable_reference_metadata_work()
{
    bool bounded = true;
    for (const auto unrelated_rules : {32, 1024}) {
        auto* engine = webscene_engine_create(0);
        require(engine != nullptr,
            "media variable metadata engine creation failed");
        resize(engine, 800, 600, 1);
        wait_for_consumed_inputs(
            engine, 1, "media variable metadata initial viewport failed");
        auto source = std::string{R"JS((unrelatedRules => {
          let css=`
            .metadata-root {--responsive-width:17px}
            .metadata-consumer {display:block;width:var(--responsive-width);height:3px}
            @media(max-width:600px) {.metadata-root {--responsive-width:41px}}`;
          for(let index=0;index<unrelatedRules;++index) {
            css+=`.metadata-noise-${index}{width:var(--metadata-noise-${index})}`;
          }
          const style=document.createElement('style'); style.textContent=css;
          const root=document.createElement('section'); root.className='metadata-root';
          const consumer=document.createElement('b'); consumer.className='metadata-consumer';
          root.appendChild(consumer); document.head.appendChild(style);
          document.body.appendChild(root);
          globalThis.metadataWidth=()=>getComputedStyle(consumer).width;
          return metadataWidth();
        })(COUNT))JS"};
        source.replace(
            source.find("COUNT"), 5, std::to_string(unrelated_rules));
        require(evaluate(
                engine, source, "media-variable-metadata-setup.js") == R"("17px")",
            "media variable metadata setup failed");
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        std::this_thread::sleep_for(std::chrono::milliseconds(260));
        execute_and_wait_for_wrapper_retention_scene(engine,
            "document.body.style.backgroundColor='rgb(1,2,3)'",
            "media-variable-metadata-before.js");
        const auto before = runtime_counter_diagnostic(
            engine, "css-variable-runtime-token-scans");
#endif
        resize(engine, 480, 600, 2);
        wait_for_consumed_inputs(
            engine, 2, "media variable metadata activation failed");
        require(evaluate(engine, "metadataWidth()",
                "media-variable-metadata-narrow.js") == R"("41px")",
            "media variable metadata activation style failed");
        resize(engine, 800, 600, 3);
        wait_for_consumed_inputs(
            engine, 3, "media variable metadata deactivation failed");
        require(evaluate(engine, "metadataWidth()",
                "media-variable-metadata-wide.js") == R"("17px")",
            "media variable metadata deactivation style failed");
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        std::this_thread::sleep_for(std::chrono::milliseconds(260));
        execute_and_wait_for_wrapper_retention_scene(engine,
            "document.body.style.backgroundColor='rgb(3,2,1)'",
            "media-variable-metadata-after.js");
        const auto scans = runtime_counter_diagnostic(
            engine, "css-variable-runtime-token-scans") - before;
        std::cout << "media-variable-metadata unrelated-rules="
            << unrelated_rules << " runtime-token-scans=" << scans << '\n';
        bounded = bounded && scans == 0U;
#endif
        webscene_engine_destroy(engine);
    }
    require(bounded,
        "media transitions reparsed immutable stylesheet var() tokens");
}

void test_media_query_inherited_variable_consumer_replay_work()
{
    bool bounded = true;
    std::vector<std::array<uint64_t, 2>> observations;
    for (const auto descendants : {32, 1024}) {
        auto* engine = webscene_engine_create(0);
        require(engine != nullptr,
            "media inherited variable engine creation failed");
        resize(engine, 800, 600, 1);
        wait_for_consumed_inputs(
            engine, 1, "media inherited variable initial viewport failed");
        auto source = std::string{R"JS((descendants => {
          const style=document.createElement('style');
          style.textContent=`
            .inherited-variable-root {--responsive-color:rgb(17, 18, 19)}
            .inherited-variable-owner {color:var(--responsive-color)}
            .inherited-variable-override {color:rgb(31, 32, 33)}
            @media(max-width:600px) {
              .inherited-variable-root {--responsive-color:rgb(41, 42, 43)}
            }`;
          const root=document.createElement('section');
          root.className='inherited-variable-root';
          root.innerHTML=`<div class="inherited-variable-owner">
            <i class="inherited-variable-child"></i>
            <i class="inherited-variable-override"></i>
          </div>`+Array.from({length:descendants},()=>'<span></span>').join('');
          document.head.appendChild(style); document.body.appendChild(root);
          const owner=root.querySelector('.inherited-variable-owner');
          const child=root.querySelector('.inherited-variable-child');
          const override=root.querySelector('.inherited-variable-override');
          globalThis.inheritedVariableSnapshot=()=>[owner,child,override]
            .map(node=>getComputedStyle(node).color);
          return inheritedVariableSnapshot();
        })(COUNT))JS"};
        source.replace(source.find("COUNT"), 5, std::to_string(descendants));
        require(evaluate(engine, source,
                "media-inherited-variable-setup.js") ==
                R"JSON(["rgb(17, 18, 19)","rgb(17, 18, 19)","rgb(31, 32, 33)"])JSON",
            "media inherited variable setup styles failed");
        std::array<uint64_t, 2> applications{};
        for (int transition = 0; transition < 2; ++transition) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            std::this_thread::sleep_for(std::chrono::milliseconds(260));
            execute_and_wait_for_wrapper_retention_scene(engine,
                "document.body.style.backgroundColor='rgb(1,2,3)'",
                "media-inherited-variable-before.js");
            const auto before = runtime_counter_diagnostic(
                engine, "css-cascade-applications");
#endif
            resize(engine, transition == 0 ? 480 : 800, 600, transition + 2);
            wait_for_consumed_inputs(engine, transition + 2,
                "media inherited variable transition failed");
            const auto expected = transition == 0
                ? R"JSON(["rgb(41, 42, 43)","rgb(41, 42, 43)","rgb(31, 32, 33)"])JSON"
                : R"JSON(["rgb(17, 18, 19)","rgb(17, 18, 19)","rgb(31, 32, 33)"])JSON";
            require(evaluate(engine, "inheritedVariableSnapshot()",
                    "media-inherited-variable-after.js") == expected,
                "media inherited variable activation/deactivation failed");
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            std::this_thread::sleep_for(std::chrono::milliseconds(260));
            execute_and_wait_for_wrapper_retention_scene(engine,
                "document.body.style.backgroundColor='rgb(3,2,1)'",
                "media-inherited-variable-publish.js");
            applications[transition] = runtime_counter_diagnostic(
                engine, "css-cascade-applications") - before;
            std::cout << "media-inherited-variable descendants="
                << descendants << " transition=" << transition
                << " css-cascade-applications="
                << applications[transition] << '\n';
            // The changed token owner needs one complete cascade. The color
            // consumer replays only color, then propagation recomputes its two
            // children while unrelated descendants remain outside the chain.
            bounded = bounded && applications[transition] <= 3U;
#endif
        }
        observations.push_back(applications);
        webscene_engine_destroy(engine);
    }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    bounded = bounded && observations.size() == 2U
        && observations[0] == observations[1];
#endif
    require(bounded,
        "inherited variable replay recascaded unrelated descendants");
}

void test_media_query_inherited_value_propagation_work()
{
    bool bounded = true;
    std::vector<std::array<uint64_t, 2>> observations;
    for (const auto descendants : {32, 1024}) {
        auto* engine = webscene_engine_create(0);
        require(engine != nullptr,
            "media inherited-value engine creation failed");
        resize(engine, 800, 600, 1);
        wait_for_consumed_inputs(
            engine, 1, "media inherited-value initial viewport failed");
        auto source = std::string{R"JS((descendants => {
          const style=document.createElement('style');
          style.textContent=`
            .media-color-root {color:rgb(17,18,19)}
            .media-color-override {color:rgb(31,32,33)}
            @media(max-width:600px) {.media-color-root {color:rgb(41,42,43)}}`;
          const root=document.createElement('section');
          root.className='media-color-root';
          root.innerHTML=
            Array.from({length:8},()=>'<b class="media-color-consumer"></b>').join('')+
            '<div class="media-color-override">'+
              Array.from({length:descendants},()=>'<i></i>').join('')+'</div>';
          document.head.appendChild(style); document.body.appendChild(root);
          const consumers=Array.from(root.querySelectorAll('.media-color-consumer'));
          const override=root.querySelector('.media-color-override');
          globalThis.mediaColorSnapshot=()=>[
            getComputedStyle(root).color,
            consumers.every(node=>getComputedStyle(node).color===getComputedStyle(root).color),
            getComputedStyle(override).color,
            Array.from(override.children).every(node=>
              getComputedStyle(node).color==='rgb(31, 32, 33)')];
          return mediaColorSnapshot();
        })(COUNT))JS"};
        source.replace(source.find("COUNT"), 5, std::to_string(descendants));
        const auto setup_result = evaluate(
            engine, source, "media-inherited-value-setup.js");
        require(setup_result ==
                R"JSON(["rgb(17, 18, 19)",true,"rgb(31, 32, 33)",true])JSON",
            "media inherited-value setup styles failed: " + setup_result);
        std::array<uint64_t, 2> applications{};
        for (int transition = 0; transition < 2; ++transition) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            std::this_thread::sleep_for(std::chrono::milliseconds(260));
            execute_and_wait_for_wrapper_retention_scene(engine,
                "document.body.style.backgroundColor='rgb(1,2,3)'",
                "media-inherited-value-before.js");
            const auto before = runtime_counter_diagnostic(
                engine, "css-cascade-applications");
#endif
            resize(engine, transition == 0 ? 480 : 800, 600, transition + 2);
            wait_for_consumed_inputs(engine, transition + 2,
                "media inherited-value transition failed");
            const auto expected = transition == 0
                ? R"JSON(["rgb(41, 42, 43)",true,"rgb(31, 32, 33)",true])JSON"
                : R"JSON(["rgb(17, 18, 19)",true,"rgb(31, 32, 33)",true])JSON";
            const auto snapshot = evaluate(
                engine, "mediaColorSnapshot()", "media-inherited-value-after.js");
            require(snapshot == expected,
                "media inherited-value activation/deactivation failed: " + snapshot);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            std::this_thread::sleep_for(std::chrono::milliseconds(260));
            execute_and_wait_for_wrapper_retention_scene(engine,
                "document.body.style.backgroundColor='rgb(3,2,1)'",
                "media-inherited-value-publish.js");
            applications[transition] = runtime_counter_diagnostic(
                engine, "css-cascade-applications") - before;
            std::cout << "media-inherited-value-cascade descendants="
                << descendants << " transition=" << transition
                << " css-cascade-applications="
                << applications[transition] << '\n';
            bounded = bounded && applications[transition] <= 12U;
#endif
        }
        observations.push_back(applications);
        webscene_engine_destroy(engine);
    }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    bounded = bounded && observations.size() == 2U
        && observations[0] == observations[1];
#endif
    require(bounded,
        "overridden inherited value recascaded unrelated descendants");
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
            .media-shell { --leaf-width:17px; padding-left:3px; color:rgb(17,18,19); }
            .media-leaf { width:var(--leaf-width); height:11px; }
            .media-sheet-alias { --sheet-leaf-width:var(--leaf-width); }
            .media-sheet-alias-leaf { width:var(--sheet-leaf-width); }
            .media-inline-alias-leaf { width:var(--inline-leaf-width); }
            .unaffected-sibling { width:23px; height:5px; }
            .media-direct-shell { padding-left:2px; }
            .media-direct-leaf { height:7px; }
            .media-inherit-shell { width:17px; }
            .media-inherit-leaf { width:inherit; }
            .media-var-inherit-leaf { width:var(--missing-media-width, inherit); }
            @media (max-width:600px) {
              .media-shell { --leaf-width:41px; padding-left:13px; color:rgb(41,42,43); }
              .media-leaf { height:29px; }
              .media-direct-shell { padding-left:19px; }
              .media-direct-leaf { height:31px; }
              .media-inherit-shell { width:43px; }
            }
          </style>
          <section class="media-shell">
            <div class="media-leaf"></div>
            <div class="media-sheet-alias"><div class="media-sheet-alias-leaf"></div></div>
            <div style="--inline-leaf-width:var(--leaf-width)"><div class="media-inline-alias-leaf"></div></div>
          </section>
          <aside class="unaffected-sibling"></aside>
          <section class="media-direct-shell"><div class="media-direct-leaf"></div></section>
          <section class="media-inherit-shell">
            <div class="media-inherit-leaf"></div>
            <div class="media-inline-inherit-leaf" style="width:inherit"></div>
            <div class="media-var-inherit-leaf"></div>
          </section>`;
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
          getComputedStyle(document.querySelector('.media-inherit-leaf')).width,
          getComputedStyle(document.querySelector('.media-inline-inherit-leaf')).width,
          getComputedStyle(document.querySelector('.media-var-inherit-leaf')).width,
          getComputedStyle(document.querySelector('.media-sheet-alias-leaf')).width,
          getComputedStyle(document.querySelector('.media-inline-alias-leaf')).width,
          getComputedStyle(__mediaLeaf).color
        ])())JS", "targeted-media-recascade-snapshot.js");
    };
    const auto wide = snapshot();
    require(wide == R"JSON(["3px","17px","11px","23px","2px","7px","17px","17px","17px","17px","17px","rgb(17, 18, 19)"])JSON",
        "wide targeted-media cascade was incorrect: " + wide);

    resize(engine, 480, 600, 2);
    wait_for_consumed_inputs(engine, 2, "compact targeted-media viewport not applied");
    const auto compact = snapshot();
    require(compact == R"JSON(["13px","41px","29px","23px","19px","31px","43px","43px","43px","41px","41px","rgb(41, 42, 43)"])JSON",
        "media activation did not recascade matched descendants: " + compact);

    resize(engine, 800, 600, 3);
    wait_for_consumed_inputs(engine, 3, "restored targeted-media viewport not applied");
    const auto restored = snapshot();
    require(restored == R"JSON(["3px","17px","11px","23px","2px","7px","17px","17px","17px","17px","17px","rgb(17, 18, 19)"])JSON",
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
