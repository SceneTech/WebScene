void test_fragment_replacement_and_inline_script_lifecycle(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          const host = document.createElement('div');
          document.body.appendChild(host);
          const wrapper = document.createElement('section');
          const children = ['a', 'b', 'c', 'd'].map(value => {
            const child = document.createElement('span');
            child.textContent = value;
            wrapper.appendChild(child);
            return child;
          });
          host.appendChild(wrapper);
          const fragment = document.createDocumentFragment();
          children.forEach(child => fragment.appendChild(child));
          const returned = host.replaceChild(fragment, wrapper);
          const fragmentResult = [
            returned === wrapper,
            wrapper.parentNode === null,
            fragment.childNodes.length,
            host.childNodes.length,
            children.every((child, index) => host.childNodes[index] === child),
            children.every(child => child.parentNode === host)
          ];

          globalThis.__nativeDynamicScriptRuns = 0;
          const outerScript = document.currentScript;
          const script = document.createElement('script');
          globalThis.__nativeDynamicScript = script;
          script.textContent = 'globalThis.__nativeDynamicScriptRuns += 1;'
            + 'globalThis.__nativeDynamicCurrentScript = '
            + 'document.currentScript === globalThis.__nativeDynamicScript;';
          host.appendChild(script);
          const firstRun = globalThis.__nativeDynamicScriptRuns;
          const currentScriptLifecycle = [
            globalThis.__nativeDynamicCurrentScript,
            document.currentScript === outerScript
          ];
          const scriptWrapper = document.createElement('b');
          scriptWrapper.appendChild(script);
          host.appendChild(scriptWrapper);

          globalThis.__nativeInertScriptRuns = 0;
          const parsed = document.createElement('div');
          parsed.innerHTML = '<script>globalThis.__nativeInertScriptRuns += 1;<\/script>';
          host.appendChild(parsed.firstChild);
          return {
            fragment: fragmentResult,
            scripts: [firstRun, globalThis.__nativeDynamicScriptRuns,
              globalThis.__nativeInertScriptRuns],
            currentScript: currentScriptLifecycle
          };
        })()
    )JS", "native-fragment-replacement-script-lifecycle.js");
    require(
        result == R"JSON({"fragment":[true,true,0,4,true,true],"scripts":[1,1,0],"currentScript":[true,true]})JSON",
        "fragment replacement or inline script lifecycle diverged: " + result);
}

void test_fragment_append_applies_structural_selectors_after_atomic_attachment(
    webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = `<style>
            #fragment-host:empty { height: 1px; }
            #fragment-host:has(.item) { min-height: 2px; }
            #fragment-host > .item:first-child { width: 3px; }
            #fragment-host > .item:nth-child(2) { width: 4px; }
            #fragment-host > .item:last-child { width: 5px; }
            #populated > .before + .item { margin-left: 6px; }
            #populated > .item:nth-child(3) { padding-left: 7px; }
            #populated > .item:last-child {
              border-left-width: 8px; border-left-style: solid;
            }
          </style><section id="fragment-host"></section>
          <section id="populated"><div class="before"></div></section>`;
          const host = document.getElementById('fragment-host');
          const fragment = document.createDocumentFragment();
          for (let index = 0; index < 3; ++index) {
            const item = document.createElement('div');
            item.className = 'item';
            fragment.appendChild(item);
          }
          const returned = host.appendChild(fragment);
          const populated = document.getElementById('populated');
          const siblingFragment = document.createDocumentFragment();
          for (let index = 0; index < 3; ++index) {
            const item = document.createElement('div');
            item.className = 'item';
            siblingFragment.appendChild(item);
          }
          populated.appendChild(siblingFragment);
          const populatedItems = populated.querySelectorAll('.item');
          return {
            returnedFragment: returned === fragment,
            fragmentEmpty: fragment.childNodes.length === 0,
            childCount: host.children.length,
            widths: Array.from(host.children, item =>
              getComputedStyle(item).width),
            minHeight: getComputedStyle(host).minHeight,
            height: getComputedStyle(host).height,
            siblingBoundaries: [
              getComputedStyle(populatedItems[0]).marginLeft,
              getComputedStyle(populatedItems[1]).paddingLeft,
              getComputedStyle(populatedItems[2]).borderLeftWidth
            ]
          };
        })()
    )JS", "native-fragment-atomic-attachment.js");
    require(
        result == R"JSON({"returnedFragment":true,"fragmentEmpty":true,"childCount":3,"widths":["3px","4px","5px"],"minHeight":"2px","height":"2px","siblingBoundaries":["6px","7px","8px"]})JSON",
        "fragment attachment structural cascade diverged: " + result);
}

void test_dom_box_dimensions_primitives(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              .border-box, .border-box * { box-sizing: border-box; }
              table { border-spacing: 0; }
              td { margin: 0; padding: 0; border: 0; }
              #authored { padding:9px !important; border:7px solid !important; }
            </style>
            <table id="tracks"><col id="single"><col id="double" span="2"><tbody><tr>
              <td></td><td id="a">a</td><td id="b">b</td>
            </tr></tbody></table>
            <table><tbody><tr id="row"><td style="width:42px;height:42px"></td></tr></tbody></table>
            <table class="border-box"><thead>
              <tr><th id="h1" style="width:200px;padding:5px"></th></tr>
              <tr><th id="h2" style="width:190px;padding:5px"></th></tr>
              <tr><th id="h3" style="width:180px;padding:5px"></th></tr>
            </thead></table>
            <table class="border-box"><tbody><tr><td id="cell"
              style="height:20px;padding:5px;border:1px solid;line-height:18px">text</td></tr></tbody></table>
            <textarea id="textarea" style="height:0;padding:2px;border:1px solid;box-sizing:border-box"></textarea>`;
          const original = document.createElement('div');
          original.style.cssText = 'width:100px;height:80px;padding:2px;border:3px solid;margin:4px';
          const clone = original.cloneNode(true);
          const authored = document.createElement('div');
          authored.id = 'authored';
          authored.style.cssText = 'padding:2px;border:3px solid';
          const disconnected = document.createElement('div');
          disconnected.style.cssText = 'width:40px;height:30px';
          const disconnectedRect = disconnected.getBoundingClientRect();
          const deepRoot = document.createElement('section');
          let deepest = deepRoot;
          for (let depth = 0; depth < 32; ++depth) {
            const child = document.createElement('div');
            deepest.appendChild(child);
            deepest = child;
          }
          deepest.style.cssText = 'display:block;width:41px;height:31px';
          document.body.appendChild(deepRoot);
          const connectedDeepRect = deepest.getBoundingClientRect();
          const connectedDeep = [
            deepest.isConnected,
            connectedDeepRect.width,
            connectedDeepRect.height
          ];
          deepRoot.remove();
          const disconnectedDeepRect = deepest.getBoundingClientRect();
          const disconnectedDeep = [
            deepest.isConnected,
            deepest.getClientRects().length,
            disconnectedDeepRect.width,
            disconnectedDeepRect.height
          ];
          document.body.appendChild(deepRoot);
          const reconnectedDeepRect = deepest.getBoundingClientRect();
          document.body.appendChild(authored);
          single.style.width = '300px';
          a.style.width = '200px';
          b.style.width = '400px';
          const width = id => document.getElementById(id).getBoundingClientRect().width;
          const height = id => document.getElementById(id).getBoundingClientRect().height;
          return {
            clone: [
              clone.style.getPropertyValue('width'),
              clone.style.getPropertyValue('padding'),
              clone.style.paddingLeft,
              clone.style.borderLeftWidth
            ],
            authoredEdges: [
              authored.style.paddingLeft,
              authored.style.borderLeftWidth,
              getComputedStyle(authored).paddingLeft,
              getComputedStyle(authored).borderLeftWidth
            ],
            disconnected: [
              disconnected.isConnected,
              disconnected.getClientRects().length,
              disconnectedRect.x,
              disconnectedRect.y,
              disconnectedRect.width,
              disconnectedRect.height
            ],
            deepConnectionTransitions: [
              connectedDeep,
              disconnectedDeep,
              [deepest.isConnected, reconnectedDeepRect.width, reconnectedDeepRect.height]
            ],
            columns: [width('single'), width('double')],
            row: width('row'),
            headers: [width('h1'), width('h2'), width('h3')],
            heights: [height('cell'), height('textarea')]
          };
        })()
    )JS", "native-dom-box-dimensions-primitives.js");
    const auto expected = R"JSON({"clone":["100px","2px","2px","3px"],"authoredEdges":["2px","3px","9px","7px"],"disconnected":[false,0,0,0,0,0],"deepConnectionTransitions":[[true,41,31],[false,0,0,0],[true,41,31]],"columns":[300,600],"row":42,"headers":[200,200,200],"heights":[30,6]})JSON";
    require(
        result == expected,
        "DOM box dimension primitives diverged: " + result);
}

void test_dom_traversal_cloning_primitives(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <section id="subject" test="value"><p><span></span></p><em></em></section>
            <template id="template"><article data-source="template"><strong>copied</strong></article></template>`;
          const subject = document.getElementById('subject');
          const source = document.getElementById('template').content.firstElementChild;
          const deep = document.importNode(source, true);
          const shallow = document.importNode(source, false);
          const detached = document.createElement('div');
          const position = detached.compareDocumentPosition(subject);
          return {
            has: [subject.matches(':has(p)'), subject.matches(':has(p):has(em)')],
            empty: [
              subject.matches("[test^='']"),
              subject.matches("[test$='']"),
              subject.matches("[test*='']")
            ],
            imported: [
              deep !== source,
              deep.parentNode === null,
              deep.getAttribute('data-source'),
              deep.firstElementChild?.localName,
              deep.textContent,
              shallow.childNodes.length
            ],
            contains: [document.contains(subject), document.contains(detached)],
            position: [
              position,
              Node.DOCUMENT_POSITION_DISCONNECTED,
              Node.DOCUMENT_POSITION_IMPLEMENTATION_SPECIFIC
            ]
          };
        })()
    )JS", "native-dom-traversal-cloning-primitives.js");
    const auto expected = R"JSON({"has":[true,true],"empty":[false,false,false],"imported":[true,true,"template","strong","copied",0],"contains":[true,false],"position":[35,1,32]})JSON";
    require(
        result == expected,
        "DOM traversal/cloning primitives diverged: " + result);
}

void test_specialized_content_properties(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          const meta = document.createElement('meta');
          const template = document.createElement('template');
          const ordinary = document.createElement('div');
          meta.content = 'theme-color';
          template.innerHTML = '<span>template child</span>';
          ordinary.content = 'application expando';

          // Keep specialized wrapper creation bounded. Five hundred iterations
          // exercise constructor selection, reflected meta content, template
          // fragment ownership, and ordinary expandos. The two-second ceiling
          // allows 4 ms per iteration, well above the normal Release cost while
          // still catching accidental blocking or quadratic wrapper setup.
          const started = performance.now();
          let wrapperChecksum = 0;
          for (let index = 0; index < 500; index++) {
            const boundedMeta = document.createElement('meta');
            const boundedTemplate = document.createElement('template');
            const boundedOrdinary = document.createElement('div');
            boundedMeta.content = `theme-${index}`;
            boundedTemplate.innerHTML = '<span>bounded child</span>';
            boundedOrdinary.content = index;
            wrapperChecksum += boundedMeta.content.length
              + boundedTemplate.content.childNodes.length
              + boundedOrdinary.content;
          }
          const elapsed = performance.now() - started;
          if (elapsed > 2000) {
            throw new Error(`specialized content wrapper gate exceeded: ${elapsed}ms`);
          }
          return {
            constructors: [
              meta.constructor.name,
              template.constructor.name,
              meta instanceof HTMLMetaElement,
              template instanceof HTMLTemplateElement
            ],
            meta: [meta.content, meta.getAttribute('content')],
            template: [template.content.nodeType, template.content.firstElementChild.localName],
            ordinary: [ordinary.content, ordinary.hasAttribute('content')],
            ownership: [
              Object.hasOwn(HTMLMetaElement.prototype, 'content'),
              Object.hasOwn(HTMLTemplateElement.prototype, 'content'),
              Object.hasOwn(HTMLElement.prototype, 'content')
            ],
            wrapperChecksumPositive: wrapperChecksum > 0
          };
        })()
    )JS", "native-specialized-content-properties.js");
    const auto expected = R"JSON({"constructors":["HTMLMetaElement","HTMLTemplateElement",true,true],"meta":["theme-color","theme-color"],"template":[11,"span"],"ordinary":["application expando",false],"ownership":[true,true,false],"wrapperChecksumPositive":true})JSON";
    require(
        result == expected,
        "specialized meta/template content properties diverged: " + result);
}

void test_html_select_add_contract(webscene_engine* engine)
{
    resize(engine, 600, 300, 1U);
    const auto result = evaluate(engine, R"JS(
        (() => {
          const select = document.createElement('select');
          const option = value => {
            const result = document.createElement('option');
            result.value = value;
            result.text = `  ${value}\t label  `;
            return result;
          };
          const first = option('first');
          const second = option('second');
          const third = option('third');
          select.add(first);
          select.add(third);
          select.add(second, third);

          const group = document.createElement('optgroup');
          const nested = option('nested');
          group.appendChild(nested);
          select.add(group);
          const indexed = option('indexed');
          select.add(indexed, 1);
          select.add(third, nested);

          let notFound = '';
          try { select.add(option('rejected'), option('foreign')); }
          catch (error) { notFound = error.name; }
          const beforeSelf = select.length;
          const selfResult = select.add(first, first);

          select.id = 'dynamic-select';
          select.style.cssText = 'box-sizing:border-box;width:320px;height:26px;padding:2px 6px;border:1px solid #345;background:#f4f4f4;color:#123456';
          select.selectedIndex = 2;
          document.body.appendChild(select);
          const rect = select.getBoundingClientRect();

          // Exercise the VS Code population shape with a direct ceiling that
          // catches blocking publication or accidental quadratic insertion.
          const bounded = document.createElement('select');
          const started = performance.now();
          let checksum = 0;
          for (let index = 0; index < 4096; ++index) {
            const item = option(String(index));
            bounded.add(item);
            checksum += item.value.length;
          }
          const elapsed = performance.now() - started;
          if (elapsed > 2000) {
            throw new Error(`HTMLSelectElement.add gate exceeded: ${elapsed}ms`);
          }
          return {
            type: typeof select.add,
            option: [
              first instanceof HTMLOptionElement,
              first.text,
              first.textContent,
              Object.getPrototypeOf(first) === HTMLOptionElement.prototype
            ],
            order: Array.from({length: select.length}, (_, index) => select[index].value),
            principal: [rect.width, rect.height,
              document.elementFromPoint(rect.left + 20, rect.top + 13) === select],
            parents: [third.parentNode === group, group.firstChild === third],
            rejection: notFound,
            self: [selfResult === undefined, select.length === beforeSelf],
            bounded: [bounded.length, checksum > 0]
          };
        })()
    )JS", "native-html-select-add-contract.js");
    const auto expected = R"JSON({"type":"function","option":[true,"first label","  first\t label  ",true],"order":["first","indexed","second","third","nested"],"principal":[320,26,true],"parents":[true,true],"rejection":"NotFoundError","self":[true,true],"bounded":[4096,true]})JSON";
    require(result == expected, "HTMLSelectElement.add contract diverged: " + result);

    webscene_engine_memory_metrics memory{sizeof(webscene_engine_memory_metrics)};
    for (auto attempt = 0; attempt < 100 && memory.v8_used_heap_bytes == 0U; ++attempt) {
        require(webscene_engine_get_memory_metrics(engine, &memory) != 0,
            "dynamic select memory metrics were unavailable");
        if (memory.v8_used_heap_bytes == 0U) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(memory.v8_used_heap_bytes > 0U
            && memory.v8_used_heap_bytes <= 256U * 1024U * 1024U,
        "dynamic select fixture exceeded the 256 MiB V8 heap ceiling: "
            + std::to_string(memory.v8_used_heap_bytes));
}

void test_child_collection_item(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          const host = document.createElement('div');
          host.append('leading text');
          const first = document.createElement('span');
          const second = document.createElement('button');
          host.append(first, second);
          return {
            children: [
              typeof host.children.item,
              host.children.item(0) === first,
              host.children.item(1) === second,
              host.children.item(2)
            ],
            childNodes: [
              typeof host.childNodes.item,
              host.childNodes.item(0).nodeType,
              host.childNodes.item(1) === first,
              host.childNodes.item(9)
            ]
          };
        })()
    )JS", "native-child-collection-item.js");
    const auto expected = R"JSON({"children":["function",true,true,null],"childNodes":["function",3,true,null]})JSON";
    require(
        result == expected,
        "child collection item lookup diverged: " + result);
}

void test_textarea_child_text_value_lifecycle(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <form id="textarea-form">
              <textarea id="parsed" name="message" value="ignored">?\nZ</textarea>
            </form>`;
          const form = document.getElementById('textarea-form');
          const parsed = document.getElementById('parsed');
          const initial = [
            parsed.textContent,
            parsed.defaultValue,
            parsed.value,
            parsed.getAttribute('value')
          ];
          parsed.value = 'dirty';
          parsed.defaultValue = 'replacement\r\ndefault';
          const dirty = [
            parsed.textContent,
            parsed.defaultValue,
            parsed.firstChild?.data,
            parsed.innerHTML,
            parsed.value
          ];
          form.reset();
          const reset = parsed.value;
          parsed.textContent = 'clean update';
          const clean = [parsed.defaultValue, parsed.value];
          const dynamic = document.createElement('textarea');
          document.body.appendChild(dynamic);
          void dynamic.value;
          const child = document.createTextNode('appended');
          dynamic.appendChild(child);
          child.data = 'updated';
          const appended = [dynamic.defaultValue, dynamic.value];
          document.body.appendChild(child);
          const removed = [dynamic.defaultValue, dynamic.value];
          return { initial, dirty, reset, clean, appended, removed };
        })()
    )JS", "native-textarea-child-text-value-lifecycle.js");
    const auto expected = R"JSON({"initial":["?\nZ","?\nZ","?\nZ","ignored"],"dirty":["replacement\r\ndefault","replacement\r\ndefault","replacement\r\ndefault","replacement\r\ndefault","dirty"],"reset":"replacement\ndefault","clean":["clean update","clean update"],"appended":["updated","updated"],"removed":["",""]})JSON";
    require(
        result == expected,
        "textarea child-text/default/current/reset lifecycle diverged: " + result);
}

void test_secondary_click(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const target = document.createElement('div');
          target.style.width = '100px';
          target.style.height = '100px';
          document.body.appendChild(target);
          globalThis.__secondaryEvents = [];
          for (const type of ['pointerdown','mousedown','contextmenu','pointerup','mouseup','auxclick']) {
            target.addEventListener(type, event => __secondaryEvents.push([
              event.type, event.button, event.buttons, event.which, event.metaKey, event.shiftKey
            ]));
          }
        })()
    )JS", "native-secondary-click-setup.js");
    require(
        evaluate(engine, "globalThis.__secondaryEvents.length", "native-secondary-click-ready.js") == "0",
        "secondary-click target setup did not complete");

    const auto pointer_modifiers =
        WEBSCENE_INPUT_POINTER_MODIFIER_META | WEBSCENE_INPUT_POINTER_MODIFIER_SHIFT;
    const webscene_input_event down{
        WEBSCENE_INPUT_POINTER_DOWN, 2U | (3U << 8U) | pointer_modifiers, 3U, 10, 10, 0, 0};
    const webscene_input_event up{
        WEBSCENE_INPUT_POINTER_UP, (3U << 8U) | pointer_modifiers, 4U, 10, 10, 0, 0};
    const auto consumed_before_click = consumed_input_count(engine);
    require(webscene_engine_enqueue(engine, &down) != 0, "right-button down was rejected");
    require(webscene_engine_enqueue(engine, &up) != 0, "right-button up was rejected");
    wait_for_consumed_inputs(
        engine,
        consumed_before_click + 2,
        "secondary click inputs were not consumed");
    const auto result = evaluate(
        engine,
        "globalThis.__secondaryEvents",
        "native-secondary-click-result.js");
    if (result != R"([["pointerdown",2,2,3,true,true],["mousedown",2,2,3,true,true],["contextmenu",2,2,3,true,true],["pointerup",2,0,3,true,true],["mouseup",2,0,3,true,true],["auxclick",2,0,3,true,true]])") {
        fail("secondary-click event sequence, button fields, or modifiers regressed: " + result);
    }
}

void test_primary_click_mouse_event_detail(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const target = document.createElement('div');
          target.style.position = 'absolute';
          target.style.left = '0';
          target.style.top = '0';
          target.style.width = '100px';
          target.style.height = '100px';
          target.style.zIndex = '999999';
          document.body.appendChild(target);
          globalThis.__primaryEventDetails = [];
          for (const type of ['pointerdown', 'mousedown', 'pointerup', 'mouseup', 'click']) {
            target.addEventListener(type, event => {
              __primaryEventDetails.push([event.type, event.detail]);
            });
          }
          target.addEventListener('mousedown', event => event.preventDefault());
        })()
    )JS", "native-primary-click-detail-setup.js");
    require(
        evaluate(engine, "document.body.firstElementChild?.offsetWidth", "native-primary-click-detail-ready.js")
            == "100",
        "primary click detail target did not complete layout");
    const webscene_input_event down{
        WEBSCENE_INPUT_POINTER_DOWN, 1U | (1U << 8U), 91U, 10, 10, 0, 0};
    const webscene_input_event up{
        WEBSCENE_INPUT_POINTER_UP, (1U << 8U), 92U, 10, 10, 0, 0};
    const auto consumed_before_click = consumed_input_count(engine);
    require(webscene_engine_enqueue(engine, &down) != 0, "primary click down was rejected");
    require(webscene_engine_enqueue(engine, &up) != 0, "primary click up was rejected");
    wait_for_consumed_inputs(
        engine,
        consumed_before_click + 2,
        "primary click inputs were not consumed");
    const auto result = evaluate(
        engine,
        "({ events: globalThis.__primaryEventDetails, activeTag: document.activeElement?.tagName, activeId: document.activeElement?.id || null })",
        "native-primary-click-detail-result.js");
    if (result != R"({"events":[["pointerdown",0],["mousedown",1],["pointerup",0],["mouseup",1],["click",1]],"activeTag":"BODY","activeId":null})") {
        fail("primary event detail or canceled-mousedown focus default diverged from Chrome: " + result);
    }
}

void test_event_listener_options_reach_native_input_and_resize(webscene_engine* engine)
{
    resize(engine, 320, 200, 880U);
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const target = document.createElement('div');
          target.id = 'listener-options-target';
          target.style.position = 'fixed';
          target.style.left = '0';
          target.style.top = '0';
          target.style.width = '100px';
          target.style.height = '100px';
          target.style.background = 'black';
          target.style.zIndex = '999999';
          document.body.appendChild(target);
          globalThis.__listenerOptions = {
            passiveDefaultPrevented: null,
            abortedMouseup: 0,
            onceClicks: 0,
            onceResizes: 0,
            abortedResizes: 0,
            abortEvents: 0,
            abortReasonStable: false,
            abortThrowStable: false,
            objectMouseups: 0,
            objectResizes: 0,
            removedObjectCalls: 0,
            objectThisStable: true
          };
          target.addEventListener('mousedown', event => {
            event.preventDefault();
            __listenerOptions.passiveDefaultPrevented = event.defaultPrevented;
          }, { passive: true });
          const inputController = new AbortController();
          target.addEventListener('mouseup', () => __listenerOptions.abortedMouseup++, {
            signal: inputController.signal
          });
          inputController.abort();
          const inputObjectListener = {
            handleEvent() {
              throw new Error('stale object listener method');
            }
          };
          target.addEventListener('mouseup', inputObjectListener, { once: true });
          target.addEventListener('mouseup', inputObjectListener);
          inputObjectListener.handleEvent = function(event) {
            __listenerOptions.objectMouseups++;
            __listenerOptions.objectThisStable &&=
              this === inputObjectListener && event.currentTarget === target;
          };
          const removedObjectListener = {
            handleEvent() {
              __listenerOptions.removedObjectCalls++;
            }
          };
          target.addEventListener('click', removedObjectListener);
          target.removeEventListener('click', removedObjectListener);
          const click = () => __listenerOptions.onceClicks++;
          target.addEventListener('click', click, { once: true });
          target.addEventListener('click', click);
          addEventListener('resize', () => __listenerOptions.onceResizes++, { once: true });
          const resizeObjectListener = {
            handleEvent(event) {
              __listenerOptions.objectResizes++;
              __listenerOptions.objectThisStable &&=
                this === resizeObjectListener && event.currentTarget === window;
            }
          };
          addEventListener('resize', resizeObjectListener, { once: true });
          const resizeController = new AbortController();
          addEventListener('resize', () => __listenerOptions.abortedResizes++, {
            signal: resizeController.signal
          });
          resizeController.abort();
          const semanticsController = new AbortController();
          const semanticsReason = { source: 'listener-options-test' };
          semanticsController.signal.onabort = () => __listenerOptions.abortEvents++;
          semanticsController.abort(semanticsReason);
          semanticsController.abort({ source: 'ignored-second-abort' });
          __listenerOptions.abortReasonStable =
            semanticsController.signal.reason === semanticsReason;
          try {
            semanticsController.signal.throwIfAborted();
          } catch (error) {
            __listenerOptions.abortThrowStable = error === semanticsReason;
          }
        })()
    )JS", "native-event-listener-options-setup.js");
    require(
        evaluate(
            engine,
            "document.getElementById('listener-options-target')?.offsetWidth",
            "native-event-listener-options-ready.js") == "100",
        "native listener-option target did not complete layout");

    auto consumed = consumed_input_count(engine);
    pointer_button(engine, WEBSCENE_INPUT_POINTER_DOWN, 20, 20, 881U, true);
    pointer_button(engine, WEBSCENE_INPUT_POINTER_UP, 20, 20, 882U, false);
    pointer_button(engine, WEBSCENE_INPUT_POINTER_DOWN, 20, 20, 883U, true);
    pointer_button(engine, WEBSCENE_INPUT_POINTER_UP, 20, 20, 884U, false);
    wait_for_consumed_inputs(
        engine,
        consumed + 4U,
        "native listener-option pointer inputs were not consumed");
    consumed = consumed_input_count(engine);
    resize(engine, 321, 201, 885U);
    wait_for_consumed_inputs(
        engine,
        consumed + 1U,
        "first listener-option resize was not consumed");
    consumed = consumed_input_count(engine);
    resize(engine, 322, 202, 886U);
    wait_for_consumed_inputs(
        engine,
        consumed + 1U,
        "second listener-option resize was not consumed");

    const auto result = evaluate(
        engine,
        "globalThis.__listenerOptions",
        "native-event-listener-options-result.js");
    if (result != R"({"passiveDefaultPrevented":false,"abortedMouseup":0,"onceClicks":1,"onceResizes":1,"abortedResizes":0,"abortEvents":1,"abortReasonStable":true,"abortThrowStable":true,"objectMouseups":1,"objectResizes":1,"removedObjectCalls":0,"objectThisStable":true})") {
        fail("listener options diverged across native input or resize dispatch: " + result);
    }
}

void test_synthetic_window_resize_dispatch_uses_outer_listener_registry(
    webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          const state = {
            calls: 0,
            captureCalls: 0,
            passiveCalls: 0,
            passiveDefaultPrevented: true,
            abortedCalls: 0,
            removedCalls: 0,
            target: true,
            currentTarget: true,
            objectThis: true
          };
          const listener = {
            handleEvent(event) {
              state.calls++;
              state.target &&= event.target === window;
              state.currentTarget &&= event.currentTarget === window;
              state.objectThis &&= this === listener;
            }
          };
          const capture = () => state.captureCalls++;
          const removed = () => state.removedCalls++;
          const controller = new AbortController();
          window.addEventListener('resize', capture, true);
          window.addEventListener('resize', listener, { once: true });
          window.addEventListener('resize', event => {
            state.passiveCalls++;
            event.preventDefault();
            state.passiveDefaultPrevented &&= event.defaultPrevented;
          }, { passive: true, once: true });
          window.addEventListener('resize', () => state.abortedCalls++, {
            signal: controller.signal
          });
          controller.abort();
          window.addEventListener('resize', removed);
          window.removeEventListener('resize', removed);
          const first = window.dispatchEvent(new Event('resize', { cancelable: true }));
          const second = window.dispatchEvent(new Event('resize'));
          return { ...state, first, second };
        })()
    )JS", "native-synthetic-window-resize.js");
    require(
        result == R"({"calls":1,"captureCalls":2,"passiveCalls":1,"passiveDefaultPrevented":false,"abortedCalls":0,"removedCalls":0,"target":true,"currentTarget":true,"objectThis":true,"first":true,"second":true})",
        "synthetic Window resize bypassed outer listener dispatch: " + result);
}

void test_document_create_event_and_init_event(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          const event = document.createEvent('Event');
          const before = [
            event instanceof Event,
            event.type,
            event.bubbles,
            event.cancelable
          ];
          event.initEvent('legacy-activate', true, true);
          const target = document.createElement('button');
          document.body.appendChild(target);
          let observed = null;
          target.addEventListener('legacy-activate', received => {
            observed = {
              identity: received === event,
              target: received.target === target,
              currentTarget: received.currentTarget === target
            };
            received.preventDefault();
          });
          const dispatchResult = target.dispatchEvent(event);
          const canceled = event.defaultPrevented;
          event.initEvent('reinitialized', false, false);
          let unsupported = null;
          try {
            document.createEvent('UnknownEvent');
          } catch (error) {
            unsupported = [error instanceof DOMException, error.name];
          }
          let missingCreateArgument = false;
          let missingInitArgument = false;
          try {
            document.createEvent();
          } catch (error) {
            missingCreateArgument = error instanceof TypeError;
          }
          try {
            document.createEvent('Event').initEvent();
          } catch (error) {
            missingInitArgument = error instanceof TypeError;
          }
          return {
            before,
            observed,
            dispatchResult,
            canceled,
            reinitialized: [
              event.type,
              event.bubbles,
              event.cancelable,
              event.defaultPrevented
            ],
            unsupported,
            missingCreateArgument,
            missingInitArgument
          };
        })()
    )JS", "native-document-create-event.js");
    require(
        result == R"({"before":[true,"",false,false],"observed":{"identity":true,"target":true,"currentTarget":true},"dispatchResult":false,"canceled":true,"reinitialized":["reinitialized",false,false,false],"unsupported":[true,"NotSupportedError"],"missingCreateArgument":true,"missingInitArgument":true})",
        "Document.createEvent or Event.initEvent diverged from the bounded contract: " + result);
}

void test_native_mouseup_honors_immediate_propagation_stop(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const parent = document.createElement('div');
          parent.style.position = 'fixed';
          parent.style.left = '0';
          parent.style.top = '0';
          parent.style.width = '160px';
          parent.style.height = '100px';
          const target = document.createElement('button');
          target.style.width = '100px';
          target.style.height = '60px';
          parent.appendChild(target);
          document.body.appendChild(parent);
          globalThis.__nativeMouseupStopTrace = [];
          target.addEventListener('mouseup', event => {
            globalThis.__nativeMouseupStopTrace.push('first-target');
            event.stopImmediatePropagation();
          });
          target.addEventListener('mouseup', () => {
            globalThis.__nativeMouseupStopTrace.push('second-target');
          });
          parent.addEventListener('mouseup', () => {
            globalThis.__nativeMouseupStopTrace.push('parent');
          });
        })()
    )JS", "native-mouseup-stop-setup.js");
    require(
        evaluate(
            engine,
            "Array.isArray(globalThis.__nativeMouseupStopTrace)",
            "native-mouseup-stop-ready.js") == "true",
        "native mouseup propagation fixture did not initialize");

    pointer_move(engine, 50, 30, 240U);
    pointer_button(
        engine,
        WEBSCENE_INPUT_POINTER_DOWN,
        50,
        30,
        241U,
        true);
    pointer_button(
        engine,
        WEBSCENE_INPUT_POINTER_UP,
        50,
        30,
        242U,
        false);
    const auto result = evaluate(
        engine,
        "globalThis.__nativeMouseupStopTrace",
        "native-mouseup-stop-result.js");
    if (result != R"(["first-target"])") {
        fail("native mouseup ignored stopImmediatePropagation: " + result);
    }
}

void test_component_library_dom_discovery_primitives(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <div id="scope-root">
              <a id="trigger"><span id="nested"></span></a>
              <div class="collapse"><div><div class="collapse" id="nested-collapse"></div></div></div>
            </div>`;
          const root = document.querySelector('#scope-root');
          const nested = document.querySelector('#nested');
          const trigger = document.querySelector('#trigger');
          const escaped = document.createElement('div');
          escaped.id = '0/my id';
          root.appendChild(escaped);
          const loneSurrogate = document.createElement('div');
          loneSurrogate.id = '\ud83dsurrogateFirst';
          root.appendChild(loneSurrogate);
          const replacement = document.createElement('div');
          replacement.setAttribute('id', '\ufffdsurrogateFirst');
          root.appendChild(replacement);
          const original = Event.prototype.preventDefault;
          let prototypeCalls = 0;
          let clickContract = null;
          Event.prototype.preventDefault = function() {
            prototypeCalls++;
            return original.call(this);
          };
          document.addEventListener('click', event => {
            if (event.target !== nested) return;
            event.delegateTarget = trigger;
            event.preventDefault();
          }, { once: true, capture: true });
          trigger.addEventListener('click', event => {
            clickContract = {
              target: event.target.isEqualNode(nested),
              delegate: event.delegateTarget.isEqualNode(trigger),
              prevented: event.defaultPrevented,
              prototype: Event.prototype.isPrototypeOf(event)
            };
          }, { once: true });
          nested.click();
          Event.prototype.preventDefault = original;
          const clone = root.cloneNode(true);
          const input = document.createElement('input');
          input.setAttribute('value', 'tester');
          void input.value;
          const clonedInput = input.cloneNode(true);
          clonedInput.setAttribute('value', 'test');
          const dirtyInput = input.cloneNode(true);
          dirtyInput.value = 'dirty';
          dirtyInput.setAttribute('value', 'new default');
          return {
            scope: Array.from(root.querySelectorAll(':scope .collapse .collapse'), node => node.id),
            escape: CSS.escape('0/my id'),
            escapedMatch: root.querySelector('#' + CSS.escape('0/my id')) === escaped,
            surrogate: {
              propertyRoundTrip: loneSurrogate.id === '\ud83dsurrogateFirst',
              attributeRoundTrip: loneSurrogate.getAttribute('id') === '\ud83dsurrogateFirst',
              literalMatch: root.querySelector('#\ud83dsurrogateFirst') === loneSurrogate,
              escapedReplacement: root.querySelector('#\\d83d surrogateFirst') === replacement
            },
            click: clickContract,
            prototypeCalls,
            same: root.isSameNode(root) && !root.isSameNode(clone),
            equal: root.isEqualNode(clone),
            clonedValueReflectsAttribute: clonedInput.value === 'test',
            dirtyValueSurvivesAttribute: dirtyInput.value === 'dirty'
          };
        })()
    )JS", "native-component-library-dom-discovery-primitives.js");
    const auto expected = R"({"scope":["nested-collapse"],"escape":"\\30 \\/my\\ id","escapedMatch":true,"surrogate":{"propertyRoundTrip":true,"attributeRoundTrip":true,"literalMatch":true,"escapedReplacement":true},"click":{"target":true,"delegate":true,"prevented":true,"prototype":true},"prototypeCalls":1,"same":true,"equal":true,"clonedValueReflectsAttribute":true,"dirtyValueSurvivesAttribute":true})";
    if (result != expected) {
        fail("component-library DOM discovery primitives regressed: " + result);
    }
}

void test_document_id_index_preserves_tree_and_root_semantics(
    webscene_engine* engine)
{
    const auto result = evaluate(
        engine,
        R"JS((() => {
          const host = document.createElement('div');
          const first = document.createElement('span');
          const second = document.createElement('span');
          first.id = 'duplicate-id';
          second.id = 'duplicate-id';
          host.appendChild(first);
          host.appendChild(second);
          document.body.appendChild(host);

          const initialWinner = document.getElementById('duplicate-id') === first;
          host.insertBefore(second, first);
          const reorderedWinner = document.getElementById('duplicate-id') === second;
          second.id = 'renamed-id';
          const mutationUpdated = document.getElementById('duplicate-id') === first
            && document.getElementById('renamed-id') === second;
          host.removeChild(first);
          const detachedExcluded = document.getElementById('duplicate-id') === null;
          first.id = 'detached-id';
          const detachedMutationExcluded = document.getElementById('detached-id') === null;

          const parsed = new DOMParser().parseFromString(
            '<main id="renamed-id"></main>',
            'text/html');
          const parsedScoped = parsed.getElementById('renamed-id') !== second
            && parsed.getElementById('renamed-id').tagName.toLowerCase() === 'main';
          const outerScoped = document.getElementById('renamed-id') === second;

          const namedFirst = document.createElement('span');
          const namedSecond = document.createElement('span');
          namedFirst.setAttribute('name', 'duplicate-name');
          namedSecond.setAttribute('name', 'duplicate-name');
          host.appendChild(namedFirst);
          host.appendChild(namedSecond);
          const namedInitial = globalThis['duplicate-name'] === namedFirst;
          host.insertBefore(namedSecond, namedFirst);
          const namedReordered = globalThis['duplicate-name'] === namedSecond;
          namedSecond.setAttribute('name', 'renamed-name');
          const namedMutation = globalThis['duplicate-name'] === namedFirst
            && globalThis['renamed-name'] === namedSecond;
          namedFirst.remove();
          const namedDetached = globalThis['duplicate-name'] === undefined;

          const nonForm = document.createElement('input');
          nonForm.setAttribute('name', 'document-named');
          const namedForm = document.createElement('form');
          namedForm.setAttribute('name', 'document-named');
          host.appendChild(nonForm);
          host.appendChild(namedForm);
          const documentFormsOnly = document['document-named'] === namedForm;
          host.remove();
          return {
            initialWinner,
            reorderedWinner,
            mutationUpdated,
            detachedExcluded,
            detachedMutationExcluded,
            parsedScoped,
            outerScoped,
            namedInitial,
            namedReordered,
            namedMutation,
            namedDetached,
            documentFormsOnly
          };
        })())JS",
        "document-id-index-semantics.js");
    require(
        result
            == R"({"initialWinner":true,"reorderedWinner":true,"mutationUpdated":true,"detachedExcluded":true,"detachedMutationExcluded":true,"parsedScoped":true,"outerScoped":true,"namedInitial":true,"namedReordered":true,"namedMutation":true,"namedDetached":true,"documentFormsOnly":true})",
        "document ID index lost mutation, tree-order, or root scoping semantics: "
            + result);
}

void test_dom_selector_apis_throw_syntax_error_for_invalid_selectors(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '<div id="selector-root"><span class="item"></span></div>';
          const root = document.querySelector('#selector-root');
          const item = root.querySelector('.item');
          const capture = operation => {
            try {
              operation();
              return null;
            } catch (error) {
              return [error instanceof DOMException, error.name];
            }
          };
          return {
            errors: [
              capture(() => document.querySelector(':webscene-unknown-pseudo')),
              capture(() => document.querySelectorAll(':webscene-unknown-pseudo')),
              capture(() => root.querySelector(':webscene-unknown-pseudo')),
              capture(() => root.querySelectorAll(':webscene-unknown-pseudo')),
              capture(() => item.matches(':webscene-unknown-pseudo')),
              capture(() => item.closest(':webscene-unknown-pseudo')),
              capture(() => document.querySelector('[')),
              capture(() => item.matches(''))
            ],
            valid: document.querySelector('#selector-root') === root
              && root.querySelectorAll('.item').length === 1
              && item.matches('span.item')
              && !item.matches('button.item')
              && item.closest('#selector-root') === root
          };
        })()
    )JS", "native-dom-selector-syntax-errors.js");
    const auto expected = R"JSON({"errors":[[true,"SyntaxError"],[true,"SyntaxError"],[true,"SyntaxError"],[true,"SyntaxError"],[true,"SyntaxError"],[true,"SyntaxError"],[true,"SyntaxError"],[true,"SyntaxError"]],"valid":true})JSON";
    if (result != expected) {
        fail("DOM selector error semantics diverged from browser behavior: " + result);
    }
}

void test_positional_selector_sibling_semantics(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              #pos-root > span:nth-child(3) { width: 37px; }
              #pos-root > span:nth-last-of-type(2) { height: 29px; }
            </style>
            <div id="pos-root">
              <span id="a"></span>
              <em id="b"></em>
              <span id="c"></span>
              <span id="d"></span>
            </div>
            <div id="only-root"> text <i id="only"></i> tail </div>`;
          const root = document.querySelector('#pos-root');
          const ids = selector => Array.from(
            root.querySelectorAll(selector), node => node.id);
          const c = document.querySelector('#c');
          return {
            firstChild: ids(':scope > :first-child'),
            lastChild: ids(':scope > :last-child'),
            nthChild: ids(':scope > :nth-child(3)'),
            nthLastChild: ids(':scope > :nth-last-child(2)'),
            firstOfType: ids(':scope > span:first-of-type'),
            lastOfType: ids(':scope > span:last-of-type'),
            nthOfType: ids(':scope > span:nth-of-type(2)'),
            nthLastOfType: ids(':scope > span:nth-last-of-type(2)'),
            onlyOfType: ids(':scope > em:only-of-type'),
            onlyChild: document.querySelector('#only:only-child')?.id ?? null,
            cascade: [getComputedStyle(c).width, getComputedStyle(c).height]
          };
        })()
    )JS", "native-positional-selector-sibling-semantics.js");
    const auto expected = R"JSON({"firstChild":["a"],"lastChild":["d"],"nthChild":["c"],"nthLastChild":["c"],"firstOfType":["a"],"lastOfType":["d"],"nthOfType":["c"],"nthLastOfType":["c"],"onlyOfType":["b"],"onlyChild":"only","cascade":["37px","29px"]})JSON";
    require(
        result == expected,
        "positional selector sibling semantics regressed: " + result);
}

void test_dropdown_runtime_primitives(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const toggle = document.createElement('button');
          toggle.id = 'dropdown-runtime-toggle';
          toggle.className = 'open';
          toggle.setAttribute('data-toggle', 'component');
          const whitespace = document.createTextNode('\n  ');
          const menu = document.createElement('div');
          menu.id = 'dropdown-runtime-menu';
          menu.className = 'dropdown-runtime-menu';
          document.body.append(toggle, whitespace, menu);

          const style = document.createElement('style');
          style.textContent = '.dropdown-runtime-menu { display: none; }';
          document.body.appendChild(style);
          const hidden = getComputedStyle(menu).display;
          style.remove();
          const restored = getComputedStyle(menu).display;
          style.textContent = '.dropdown-runtime-menu { display: inline-block; }';
          document.body.appendChild(style);
          const reinserted = getComputedStyle(menu).display;

          const selector = '[data-toggle="component"]:not(.disabled):not(:disabled).open';
          let observed = null;
          document.addEventListener('click', event => {
            if (event.target === toggle) observed = event;
          }, { once: true });
          const click = new MouseEvent('click', { bubbles: true });
          toggle.dispatchEvent(click);
          return {
            selector: document.querySelector(selector) === toggle && toggle.matches(selector),
            siblings: toggle.nextElementSibling === menu && menu.previousElementSibling === toggle,
            visibility: [hidden, restored, reinserted],
            event: {
              identity: observed === click,
              target: click.target === toggle,
              currentTarget: click.currentTarget,
              eventPhase: click.eventPhase,
              button: click.button,
              buttons: click.buttons,
              mouse: click instanceof MouseEvent && click instanceof Event
            }
          };
        })()
    )JS", "native-dropdown-runtime-primitives.js");
    const auto expected = R"JSON({"selector":true,"siblings":true,"visibility":["none","block","inline-block"],"event":{"identity":true,"target":true,"currentTarget":null,"eventPhase":0,"button":0,"buttons":0,"mouse":true}})JSON";
    if (result != expected) {
        fail("dropdown runtime primitives regressed: " + result);
    }
}

void test_input_dispatch_failures_are_attributed_and_consumable(webscene_engine* engine)
{
    struct original_input_dispatch_metrics final {
        uint32_t struct_size;
        uint32_t reserved;
        uint64_t last_dispatch_nanoseconds;
        uint64_t maximum_dispatch_nanoseconds;
        uint64_t last_dispatch_sequence;
    };
    static_assert(
        sizeof(original_input_dispatch_metrics)
            == offsetof(webscene_input_dispatch_metrics, dispatched_inputs));
    original_input_dispatch_metrics original{
        sizeof(original_input_dispatch_metrics)};
    require(
        webscene_engine_get_input_dispatch_metrics(
            engine,
            reinterpret_cast<webscene_input_dispatch_metrics*>(&original)) != 0,
        "input-dispatch metrics rejected the original ABI prefix");

    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const target = document.createElement('button');
          target.style.width = '100px';
          target.style.height = '100px';
          target.addEventListener('click', () => {
            throw new Error('certification-input-failure-marker');
          });
          document.body.appendChild(target);
        })()
    )JS", "native-input-failure-setup.js");
    require(
        evaluate(engine, "document.body.firstElementChild?.offsetWidth", "native-input-failure-ready.js")
            == "100",
        "input-failure fixture did not complete layout");

    const webscene_input_event down{
        WEBSCENE_INPUT_POINTER_DOWN, 1U | (1U << 8U), 101U, 10, 10, 0, 0};
    const webscene_input_event up{
        WEBSCENE_INPUT_POINTER_UP, (1U << 8U), 102U, 10, 10, 0, 0};
    const auto consumed_before_click = consumed_input_count(engine);
    require(webscene_engine_enqueue(engine, &down) != 0, "throwing click down was rejected");
    require(webscene_engine_enqueue(engine, &up) != 0, "throwing click up was rejected");
    wait_for_consumed_inputs(
        engine,
        consumed_before_click + 2,
        "throwing click inputs were not consumed");

    webscene_input_dispatch_metrics dispatch_metrics{
        sizeof(webscene_input_dispatch_metrics)};
    require(
        webscene_engine_get_input_dispatch_metrics(engine, &dispatch_metrics) != 0,
        "input-dispatch metrics ABI was rejected");
    require(
        dispatch_metrics.last_dispatch_sequence == up.sequence,
        "input-dispatch timing was not attributed to the last consumed sequence");
    require(
        dispatch_metrics.last_dispatch_nanoseconds > 0
            && dispatch_metrics.maximum_dispatch_nanoseconds
                >= dispatch_metrics.last_dispatch_nanoseconds
            && dispatch_metrics.dispatched_inputs >= 2U
            && dispatch_metrics.total_dispatch_nanoseconds
                >= dispatch_metrics.last_dispatch_nanoseconds,
        "input-dispatch timing metrics were not populated monotonically");

    const auto failure = take_input_dispatch_failure(engine);
    require(
        failure.starts_with("102\n3\nNative ")
            && failure.find(" dispatch failed:") != std::string::npos
            && failure.find("certification-input-failure-marker") != std::string::npos,
        "input failure omitted its sequence, kind, or JavaScript stack: " + failure);
    require(
        take_input_dispatch_failure(engine).empty(),
        "input-dispatch failure was not consumed exactly once");
}

void test_listener_added_during_dispatch_waits_for_next_event(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '<button id="anchor">open</button>';
          const anchor = document.getElementById('anchor');
          const calls = [];
          const late = event => calls.push(`late:${event.target.id}`);
          document.addEventListener('click', event =>
            calls.push(`early:${event.target.id}`));
          anchor.addEventListener('click', () => {
            calls.push('anchor');
            document.addEventListener('click', late);
          });
          anchor.click();
          const first = calls.slice();
          document.body.click();
          return { first, all: calls };
        })()
    )JS", "native-listener-added-during-dispatch.js");
    require(
        result == R"JSON({"first":["anchor","early:anchor"],"all":["anchor","early:anchor","early:","late:"]})JSON",
        "a listener added during dispatch observed the active event: " + result);
}

void test_node_filter_tree_walker_focus_navigation(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <div id="root">
              <button id="first"></button>
              <div class="skip"><button id="nested"></button></div>
              <div class="reject"><button id="rejected"></button></div>
              <button id="last"><span class="skip"><span id="deep"></span></span></button>
            </div>`;
          const root = document.getElementById('root');
          const filter = node => {
            if (node.classList.contains('reject')) return NodeFilter.FILTER_REJECT;
            if (node.classList.contains('skip')) return NodeFilter.FILTER_SKIP;
            return node.matches('button, #deep')
              ? NodeFilter.FILTER_ACCEPT
              : NodeFilter.FILTER_SKIP;
          };
          const walker = document.createTreeWalker(
            root,
            NodeFilter.SHOW_ELEMENT,
            filter);
          const forward = [];
          for (let node = walker.nextNode(); node; node = walker.nextNode()) {
            forward.push(node.id);
          }
          walker.currentNode = root;
          const last = walker.lastChild()?.id ?? null;
          const deep = walker.lastChild()?.id ?? null;
          const leaf = walker.lastChild();
          const previous = walker.previousNode()?.id ?? null;
          const siblingWalker = document.createTreeWalker(
            root, NodeFilter.SHOW_ELEMENT, filter);
          siblingWalker.currentNode = document.getElementById('nested');
          const siblings = [
            siblingWalker.previousSibling()?.id ?? null,
            siblingWalker.nextSibling()?.id ?? null,
            siblingWalker.nextSibling()?.id ?? null
          ];
          siblingWalker.currentNode = root;
          const rootBounds = [
            siblingWalker.parentNode(),
            siblingWalker.previousSibling(),
            siblingWalker.nextSibling()
          ];
          return {
            constants: [
              NodeFilter.SHOW_ELEMENT,
              NodeFilter.FILTER_ACCEPT,
              NodeFilter.FILTER_REJECT,
              NodeFilter.FILTER_SKIP
            ],
            instance: walker instanceof TreeWalker,
            root: walker.root === root,
            forward,
            reverse: [last, deep, leaf, previous],
            siblings,
            rootBounds
          };
        })()
    )JS", "native-node-filter-tree-walker.js");
    require(
        result == R"JSON({"constants":[1,1,2,3],"instance":true,"root":true,"forward":["first","nested","last","deep"],"reverse":["last","deep",null,"last"],"siblings":["first","nested","last"],"rootBounds":[null,null,null]})JSON",
        "NodeFilter/TreeWalker focus navigation regressed: " + result);
}

void test_node_iterator_dompurify_and_filter_contracts(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '<main id="root">before<!--comment--><section><span>inside</span></section>after</main>';
          const root = document.getElementById('root');
          const mask = NodeFilter.SHOW_ELEMENT
            | NodeFilter.SHOW_TEXT
            | NodeFilter.SHOW_COMMENT;
          let filterCalls = 0;
          const iterator = document.createNodeIterator(root, mask, {
            acceptNode(node) {
              ++filterCalls;
              return node.nodeType === Node.ELEMENT_NODE
                  && node.localName === 'section'
                ? NodeFilter.FILTER_REJECT
                : NodeFilter.FILTER_ACCEPT;
            }
          });
          const callsAfterConstruction = filterCalls;
          const forward = [];
          for (let node = iterator.nextNode(); node; node = iterator.nextNode()) {
            forward.push(node.nodeType === Node.ELEMENT_NODE
              ? node.localName
              : node.nodeType === Node.COMMENT_NODE
                ? `#comment:${node.data}`
                : `#text:${node.data}`);
          }
          const forwardCalls = filterCalls;
          const endState = [
            iterator.referenceNode.nodeName,
            iterator.pointerBeforeReferenceNode
          ];
          const backwards = [
            iterator.previousNode()?.nodeName ?? null,
            iterator.previousNode()?.nodeName ?? null,
            iterator.nextNode()?.nodeName ?? null
          ];

          const thrown = { marker: 'filter failure' };
          const throwing = document.createNodeIterator(
            root, NodeFilter.SHOW_ELEMENT, () => { throw thrown; });
          let constructorDeferredException = true;
          let propagatedExactException = false;
          try { throwing.nextNode(); } catch (error) {
            propagatedExactException = error === thrown;
          }

          let recursive;
          let depth = 0;
          recursive = document.createNodeIterator(
            root,
            NodeFilter.SHOW_ELEMENT,
            node => {
              if (recursive.referenceNode !== root && depth === 0) {
                ++depth;
                recursive.nextNode();
              }
              return NodeFilter.FILTER_ACCEPT;
            });
          recursive.nextNode();
          recursive.nextNode();
          let recursiveNext = null;
          try { recursive.nextNode(); } catch (error) {
            recursiveNext = error.name;
          }
          --depth;
          let recursivePrevious = null;
          try { recursive.previousNode(); } catch (error) {
            recursivePrevious = error.name;
          }

          const functionFilter = document.createNodeIterator(
            root,
            NodeFilter.SHOW_ELEMENT,
            node => node.localName === 'span'
              ? NodeFilter.FILTER_ACCEPT
              : NodeFilter.FILTER_SKIP);
          const functionFiltered = [];
          for (let node = functionFilter.nextNode(); node;
               node = functionFilter.nextNode()) {
            functionFiltered.push(node.localName);
          }

          const fragment = document.createDocumentFragment();
          const detached = document.createElement('strong');
          fragment.appendChild(detached);
          const detachedIterator = document.createNodeIterator(
            fragment, NodeFilter.SHOW_ALL);

          let invalidRoot = false;
          let invalidFilter = false;
          try { document.createNodeIterator(null); } catch (error) {
            invalidRoot = error instanceof TypeError;
          }
          try { document.createNodeIterator(root, mask, {}); } catch (error) {
            invalidFilter = error instanceof TypeError;
          }

          return {
            instance: iterator instanceof NodeIterator,
            brand: iterator.toString(),
            root: iterator.root === root,
            whatToShow: iterator.whatToShow,
            callsAfterConstruction,
            forwardCalls,
            forward,
            endState,
            backwards,
            constructorDeferredException,
            propagatedExactException,
            recursiveNext,
            recursivePrevious,
            functionFiltered,
            detached: [
              detachedIterator.nextNode() === fragment,
              detachedIterator.nextNode() === detached,
              detachedIterator.nextNode()
            ],
            invalidRoot,
            invalidFilter
          };
        })()
    )JS", "native-node-iterator-dompurify.js");
    require(
        result == R"JSON({"instance":true,"brand":"[object NodeIterator]","root":true,"whatToShow":133,"callsAfterConstruction":0,"forwardCalls":7,"forward":["main","#text:before","#comment:comment","span","#text:inside","#text:after"],"endState":["#text",false],"backwards":["#text","#text","#text"],"constructorDeferredException":true,"propagatedExactException":true,"recursiveNext":"InvalidStateError","recursivePrevious":"InvalidStateError","functionFiltered":["span"],"detached":[true,true,null],"invalidRoot":true,"invalidFilter":true})JSON",
        "NodeIterator DOMPurify/filter contract regressed: " + result);
}

void test_node_iterator_isolated_scaling_and_memory_gate()
{
    auto engine = std::unique_ptr<webscene_engine, decltype(&webscene_engine_destroy)>{
        webscene_engine_create(0), &webscene_engine_destroy};
    require(engine != nullptr, "NodeIterator performance engine creation failed");

    const auto result = evaluate(engine.get(), R"JS(
        (() => {
          const run = count => {
            document.body.innerHTML = `<div id="iterator-root">${'<i></i>'.repeat(count)}</div>`;
            const root = document.getElementById('iterator-root');
            let filterCalls = 0;
            const started = performance.now();
            const iterator = document.createNodeIterator(
              root,
              NodeFilter.SHOW_ELEMENT,
              () => {
                ++filterCalls;
                return NodeFilter.FILTER_ACCEPT;
              });
            const callsAfterConstruction = filterCalls;
            let visited = 0;
            while (iterator.nextNode()) ++visited;
            return {
              count,
              visited,
              filterCalls,
              callsAfterConstruction,
              elapsed: performance.now() - started
            };
          };

          const small = run(10000);
          const large = run(50000);
          return {
            smallCounts: [small.visited, small.filterCalls, small.callsAfterConstruction],
            largeCounts: [large.visited, large.filterCalls, large.callsAfterConstruction],
            underFiveSeconds: large.elapsed < 5000,
            scalesLinearly: large.elapsed <= small.elapsed * 8 + 250
          };
        })()
    )JS", "native-node-iterator-isolated-performance.js");

    webscene_engine_memory_metrics memory{
        sizeof(webscene_engine_memory_metrics)};
    require(
        webscene_engine_get_memory_metrics(engine.get(), &memory) != 0,
        "NodeIterator performance memory metrics were unavailable");
    constexpr uint64_t maximum_fixture_nodes = 70'000U;
    constexpr uint64_t maximum_fixture_heap_bytes = 128U * 1024U * 1024U;
    const auto bounded_memory =
        memory.native_dom_node_count <= maximum_fixture_nodes
        && memory.native_wrapper_handle_count <= maximum_fixture_nodes
        && memory.v8_used_heap_bytes <= maximum_fixture_heap_bytes;

    engine.reset();
    require(
        result == R"JSON({"smallCounts":[10001,10001,0],"largeCounts":[50001,50001,0],"underFiveSeconds":true,"scalesLinearly":true})JSON",
        "isolated NodeIterator scaling gate failed: " + result);
    require(
        bounded_memory,
        "isolated NodeIterator memory gate exceeded its 70,000-node/handle or 128 MiB V8 bound");
}

void test_native_performance_timeline_identity(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS((() => {
      const probe = (performance, prefix) => {
        const mark = performance.mark;
        const measure = performance.measure;
        const origin = performance.timeOrigin;
        mark(prefix + '-start', { startTime: 12 });
        mark(prefix + '-end', { startTime: 42 });
        const entry = measure(
          prefix + '-span', prefix + '-start', prefix + '-end');
        try { performance.timeOrigin = 0; } catch {}
        return {
          finiteOrigin: Number.isFinite(origin),
          wallClockAligned:
            Math.abs(Date.now() - (origin + performance.now())) < 1000,
          readOnlyOrigin: performance.timeOrigin === origin,
          capturedIdentity:
            mark === performance.mark && measure === performance.measure,
          sameTimeline:
            performance.getEntriesByName(prefix + '-span', 'measure')[0]
              === entry,
          startTime: entry.startTime,
          duration: entry.duration
        };
      };
      const frame = document.createElement('iframe');
      document.body.appendChild(frame);
      return {
        main: probe(performance, 'native-main'),
        frame: probe(frame.contentWindow.performance, 'native-frame')
      };
    })())JS", "native-performance-timeline-identity.js");
    require(
        result
            == R"JSON({"main":{"finiteOrigin":true,"wallClockAligned":true,"readOnlyOrigin":true,"capturedIdentity":true,"sameTimeline":true,"startTime":12,"duration":30},"frame":{"finiteOrigin":true,"wallClockAligned":true,"readOnlyOrigin":true,"capturedIdentity":true,"sameTimeline":true,"startTime":12,"duration":30}})JSON",
        "main/frame Performance methods did not share one retained timeline: "
            + result);
}

void test_native_mutable_stylesheet_cssom(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS((() => {
      const exercise = (document, view, className, display) => {
        const style = document.createElement('style');
        const target = document.createElement('div');
        target.className = className;
        style.textContent = `.${className} { display: block; color: rgb(1, 2, 3); }`;
        document.head.appendChild(style);
        document.body.appendChild(target);
        const sheet = style.sheet;
        const rules = sheet.cssRules;
        const authoredText = style.textContent;
        const observer = new MutationObserver(() => {});
        observer.observe(style, { childList: true, characterData: true, subtree: true });
        let parseError = '';
        try { sheet.insertRule(`.${className} {`); }
        catch (error) { parseError = error.name; }
        const index = sheet.insertRule(`.${className} { display: none; }`, rules.length);
        const rule = rules[index];
        const inserted = [index, rules.length, rule.selectorText,
          view.getComputedStyle(target).display, style.textContent === authoredText,
          style.sheet === sheet, target.matches(`.${className}`), parseError];
        rule.style.setProperty('display', display);
        const mutated = [rules.length, rule.style.display,
          view.getComputedStyle(target).display];
        sheet.deleteRule(index);
        const mediaIndex = sheet.insertRule(
          `@media (min-width: 1px) { .${className} { display: ${display}; } }`,
          rules.length);
        const mediaRule = rules[mediaIndex];
        const mediaRules = mediaRule.cssRules;
        const mediaOriginal = mediaRules?.[0];
        const nestedIndex = mediaRule.insertRule(
          `.${className} { display: grid; }`, mediaRules.length);
        const nestedRule = mediaRules[nestedIndex];
        const media = [rules.length,
          mediaRule instanceof view.CSSMediaRule,
          mediaRule instanceof view.CSSGroupingRule,
          mediaRules instanceof view.CSSRuleList,
          mediaRule.type,
          mediaRule.conditionText,
          mediaRule.cssRules === mediaRules,
          mediaRules.length,
          mediaOriginal.parentRule === mediaRule,
          mediaOriginal.parentStyleSheet === sheet,
          nestedRule.parentRule === mediaRule,
          nestedRule.parentStyleSheet === sheet,
          view.getComputedStyle(target).display,
          mediaRule.cssText.includes('display: grid')];
        const mediaList = mediaRule.media;
        const mediaReflection = [
          mediaList instanceof view.MediaList,
          mediaRule.media === mediaList,
          mediaList.mediaText,
          mediaList.length,
          mediaList[0],
          mediaList.item(0),
          mediaList.toString()];
        mediaRule.media = '(max-width:1px)';
        const mediaForwarded = [mediaRule.media === mediaList,
          mediaList.mediaText, mediaRule.conditionText,
          view.getComputedStyle(target).display,
          mediaRule.cssText.includes('(max-width: 1px)')];
        mediaList.appendMedium('screen');
        mediaList.appendMedium('SCREEN');
        mediaList.appendMedium('screen, print');
        const mediaAppended = [mediaList.mediaText, mediaList.length,
          mediaRule.conditionText, view.getComputedStyle(target).display];
        mediaList.deleteMedium('screen');
        let missingMedium = '';
        try { mediaList.deleteMedium('speech'); }
        catch (error) { missingMedium = error.name; }
        const mediaListDeleted = [mediaList.mediaText, mediaList.length,
          mediaRule.conditionText, missingMedium,
          view.getComputedStyle(target).display];
        mediaList.mediaText = '(min-width: 1px)';
        mediaRule.deleteRule(nestedIndex);
        const mediaDeleted = [mediaRules.length,
          nestedRule.parentRule === null,
          nestedRule.parentStyleSheet === null,
          view.getComputedStyle(target).display];
        sheet.deleteRule(mediaIndex);
        sheet.insertRule(`.${className} { display: grid; }`, rules.length);
        style.remove();
        const detached = style.sheet === null;
        document.head.appendChild(style);
        const reconnected = [style.sheet === sheet,
          view.getComputedStyle(target).display, style.textContent === authoredText];
        sheet.disabled = true;
        const disabled = [sheet.disabled, view.getComputedStyle(target).display];
        sheet.disabled = false;
        const enabled = [sheet.disabled, view.getComputedStyle(target).display];
        sheet.media.mediaText = '(max-width: 1px)';
        const mediaMiss = [sheet.media.mediaText, sheet.media.length,
          sheet.media.item(0), view.getComputedStyle(target).display];
        sheet.media.mediaText = '(min-width: 1px)';
        const mediaMatch = [sheet.media.mediaText,
          view.getComputedStyle(target).display];
        const deleted = [rules.length, rule.parentStyleSheet === null,
          observer.takeRecords().length, detached];
        observer.disconnect();
        style.remove();
        target.remove();
        return { inserted, mutated, media, mediaReflection, mediaForwarded,
          mediaAppended, mediaListDeleted, mediaDeleted, reconnected, disabled,
          enabled, mediaMiss, mediaMatch, deleted };
      };
      const frame = document.createElement('iframe');
      document.body.appendChild(frame);
      return {
        main: exercise(document, window, 'monaco-main', 'inline-block'),
        frame: exercise(
          frame.contentDocument,
          frame.contentWindow,
          'monaco-frame',
          'inline-flex')
      };
    })())JS", "native-mutable-stylesheet-cssom.js");
    require(
        result
            == R"JSON({"main":{"inserted":[1,2,".monaco-main","none",true,true,true,"SyntaxError"],"mutated":[2,"inline-block","inline-block"],"media":[2,true,true,true,4,"(min-width: 1px)",true,2,true,true,true,true,"grid",true],"mediaReflection":[true,true,"(min-width: 1px)",1,"(min-width: 1px)","(min-width: 1px)","(min-width: 1px)"],"mediaForwarded":[true,"(max-width: 1px)","(max-width: 1px)","block",true],"mediaAppended":["(max-width: 1px), screen",2,"(max-width: 1px), screen","grid"],"mediaListDeleted":["(max-width: 1px)",1,"(max-width: 1px)","NotFoundError","block"],"mediaDeleted":[1,true,true,"inline-block"],"reconnected":[true,"grid",true],"disabled":[true,"block"],"enabled":[false,"grid"],"mediaMiss":["(max-width: 1px)",1,"(max-width: 1px)","block"],"mediaMatch":["(min-width: 1px)","grid"],"deleted":[2,true,0,true]},"frame":{"inserted":[1,2,".monaco-frame","none",true,true,true,"SyntaxError"],"mutated":[2,"inline-flex","inline-flex"],"media":[2,true,true,true,4,"(min-width: 1px)",true,2,true,true,true,true,"grid",true],"mediaReflection":[true,true,"(min-width: 1px)",1,"(min-width: 1px)","(min-width: 1px)","(min-width: 1px)"],"mediaForwarded":[true,"(max-width: 1px)","(max-width: 1px)","block",true],"mediaAppended":["(max-width: 1px), screen",2,"(max-width: 1px), screen","grid"],"mediaListDeleted":["(max-width: 1px)",1,"(max-width: 1px)","NotFoundError","block"],"mediaDeleted":[1,true,true,"inline-flex"],"reconnected":[true,"grid",true],"disabled":[true,"block"],"enabled":[false,"grid"],"mediaMiss":["(max-width: 1px)",1,"(max-width: 1px)","block"],"mediaMatch":["(min-width: 1px)","grid"],"deleted":[2,true,0,true]}})JSON",
        "mutable stylesheet CSSOM did not recascade main/frame styles: "
            + result);

    const auto supports_result = evaluate(engine, R"JS((() => {
      const exercise = (document, view, className) => {
        const style = document.createElement('style');
        const target = document.createElement('div');
        style.textContent = `@supports selector(:focus-visible) {
          .${className} { display: grid; }
        }`;
        target.className = className;
        document.head.appendChild(style);
        document.body.appendChild(target);
        const sheet = style.sheet;
        const group = sheet.cssRules[0];
        const rules = group.cssRules;
        const original = rules[0];
        const authoredText = style.textContent;
        const initial = [
          group.constructor.name,
          group instanceof view.CSSSupportsRule,
          group instanceof view.CSSConditionRule,
          group instanceof view.CSSGroupingRule,
          group instanceof view.CSSRule,
          group.type,
          view.CSSRule.SUPPORTS_RULE,
          group.conditionText,
          group.cssRules === rules,
          original.parentRule === group,
          original.parentStyleSheet === sheet,
          view.getComputedStyle(target).display
        ];
        let assignmentError = '';
        try { group.conditionText = '(display:none)'; }
        catch (error) { assignmentError = error.name; }
        const assignment = [group.conditionText, assignmentError];
        const errors = [];
        for (const invoke of [
          () => group.insertRule(),
          () => group.deleteRule(),
          () => group.insertRule(`.${className} {}`, 99),
          () => group.insertRule(`.${className} {} .other {}`, 0),
          () => group.insertRule('@import url("x.css");', 0)
        ]) {
          try { invoke(); errors.push('none'); }
          catch (error) { errors.push(error.name); }
        }
        const insertedIndex = group.insertRule(
          `.${className} { display: none; }`, rules.length);
        const inserted = rules[insertedIndex];
        const afterInsert = [insertedIndex, rules.length,
          inserted.parentRule === group,
          inserted.parentStyleSheet === sheet,
          view.getComputedStyle(target).display,
          group.cssText.includes('display: none')];
        group.deleteRule(insertedIndex);
        const afterDelete = [rules.length, inserted.parentRule,
          inserted.parentStyleSheet, view.getComputedStyle(target).display];
        const mediaIndex = sheet.insertRule(
          `@media (min-width:1px) { @supports selector(:focus-visible) {
            .${className} { display: inline-grid; }
          } }`, sheet.cssRules.length);
        const media = sheet.cssRules[mediaIndex];
        const nested = media.cssRules[0];
        const nestedIndex = nested.insertRule(
          `.${className} { display: block; }`, nested.cssRules.length);
        const nestedState = [nestedIndex, nested.cssRules.length,
          nested.parentRule === media,
          nested.parentStyleSheet === sheet,
          view.getComputedStyle(target).display,
          media.cssText.includes('display: block')];
        const textStable = style.textContent === authoredText;
        style.remove();
        target.remove();
        return { initial, assignment, errors, afterInsert, afterDelete,
          nestedState, textStable };
      };
      const frame = document.createElement('iframe');
      document.body.appendChild(frame);
      const value = {
        main: exercise(document, window, 'supports-main'),
        frame: exercise(frame.contentDocument, frame.contentWindow, 'supports-frame')
      };
      frame.remove();
      return value;
    })())JS", "native-css-supports-rule-mutation.js");
    const auto supports_expected = [] {
        return std::string{"{\"initial\":[\"CSSSupportsRule\",true,true,true,true,12,12,\"selector(:focus-visible)\",true,true,true,\"grid\"],\"assignment\":[\"selector(:focus-visible)\",\"\"],\"errors\":[\"TypeError\",\"TypeError\",\"IndexSizeError\",\"SyntaxError\",\"HierarchyRequestError\"],\"afterInsert\":[1,2,true,true,\"none\",true],\"afterDelete\":[1,null,null,\"grid\"],\"nestedState\":[1,2,true,true,\"block\",true],\"textStable\":true}"};
    };
    require(
        supports_result == "{\"main\":" + supports_expected()
                + ",\"frame\":" + supports_expected() + "}",
        "CSSSupportsRule mutation diverged from the browser contract: "
            + supports_result);

    const auto container_result = evaluate(engine, R"JS((() => {
      const exercise = (document, view, className) => {
        const style = document.createElement('style');
        const container = document.createElement('div');
        const target = document.createElement('div');
        container.style.containerType = 'inline-size';
        container.style.containerName = className;
        container.style.width = '320px';
        target.className = className;
        container.appendChild(target);
        style.textContent = `@container ${className} (width > 300px) {
          .${className} { display: grid; }
        }`;
        document.head.appendChild(style);
        document.body.appendChild(container);
        const sheet = style.sheet;
        const group = sheet.cssRules[0];
        const rules = group.cssRules;
        const original = rules[0];
        const conditions = group.conditions;
        const authoredText = style.textContent;
        const initial = [
          group.constructor.name,
          group instanceof view.CSSContainerRule,
          group instanceof view.CSSConditionRule,
          group instanceof view.CSSGroupingRule,
          group instanceof view.CSSRule,
          group.type,
          group.conditionText,
          group.containerName,
          group.containerQuery,
          group.conditions === conditions,
          Object.isFrozen(conditions),
          Object.isFrozen(conditions[0]),
          conditions[0].name,
          conditions[0].query,
          group.cssRules === rules,
          original.parentRule === group,
          original.parentStyleSheet === sheet,
          view.getComputedStyle(target).display
        ];
        const errors = [];
        for (const invoke of [
          () => group.insertRule(),
          () => group.deleteRule(),
          () => group.insertRule(`.${className} {}`, 99),
          () => group.insertRule(`.${className} {} .other {}`, 0),
          () => group.insertRule('@import url("x.css");', 0),
          () => group.insertRule('@namespace x url("x");', 0)
        ]) {
          try { invoke(); errors.push('none'); }
          catch (error) { errors.push(error.name); }
        }
        const insertedIndex = group.insertRule(
          `.${className} { display: none; }`, rules.length);
        const inserted = rules[insertedIndex];
        const afterInsert = [insertedIndex, rules.length,
          inserted.parentRule === group,
          inserted.parentStyleSheet === sheet,
          view.getComputedStyle(target).display,
          group.cssText.includes('display: none'),
          style.textContent === authoredText];
        group.deleteRule(insertedIndex);
        const afterDelete = [rules.length, inserted.parentRule,
          inserted.parentStyleSheet, view.getComputedStyle(target).display];
        const mediaIndex = sheet.insertRule(
          `@media (min-width:1px) { @container ${className} (width > 300px) {
            .${className} { display: inline-grid; }
          } }`, sheet.cssRules.length);
        const media = sheet.cssRules[mediaIndex];
        const nested = media.cssRules[0];
        nested.insertRule(`.${className} { display: block; }`, nested.cssRules.length);
        const nestedState = [nested.constructor.name,
          nested instanceof view.CSSContainerRule,
          nested.parentRule === media,
          nested.parentStyleSheet === sheet,
          nested.cssRules.length,
          view.getComputedStyle(target).display,
          media.cssText.includes('display: block')];
        const reflectionSheet = new view.CSSStyleSheet();
        reflectionSheet.replaceSync(
          '@container (width > 300px) {}'
          + '@container Named {}'
          + '@container (width > 300px), Named (width < 1000px) {}');
        const reflected = Array.from(reflectionSheet.cssRules, item => [
          item.containerName, item.containerQuery,
          Array.from(item.conditions, condition => [condition.name, condition.query])
        ]);
        sheet.deleteRule(0);
        group.insertRule(`.${className} { display: none; }`, group.cssRules.length);
        const detached = [group.parentStyleSheet, group.cssRules.length,
          group.cssText.includes('display: none'),
          view.getComputedStyle(target).display,
          style.textContent === authoredText];
        style.remove();
        container.remove();
        return { initial, errors, afterInsert, afterDelete, nestedState,
          reflected, detached };
      };
      const frame = document.createElement('iframe');
      document.body.appendChild(frame);
      const value = {
        main: exercise(document, window, 'container-main'),
        frame: exercise(frame.contentDocument, frame.contentWindow, 'container-frame')
      };
      frame.remove();
      return value;
    })())JS", "native-css-container-rule-mutation.js");
    const auto container_expected = [](std::string_view name) {
        return std::string{"{\"initial\":[\"CSSContainerRule\",true,true,true,true,0,\""}
            + std::string{name} + " (width > 300px)\",\"" + std::string{name}
            + "\",\"(width > 300px)\",true,true,true,\"" + std::string{name}
            + "\",\"(width > 300px)\",true,true,true,\"grid\"],"
              "\"errors\":[\"TypeError\",\"TypeError\",\"IndexSizeError\","
              "\"SyntaxError\",\"HierarchyRequestError\",\"HierarchyRequestError\"],"
              "\"afterInsert\":[1,2,true,true,\"none\",true,true],"
              "\"afterDelete\":[1,null,null,\"grid\"],"
              "\"nestedState\":[\"CSSContainerRule\",true,true,true,2,\"block\",true],"
              "\"reflected\":[[\"\",\"(width > 300px)\",[[\"\",\"(width > 300px)\"]]],"
              "[\"Named\",\"\",[[\"Named\",\"\"]]],[\"\",\"\",[[\"\","
              "\"(width > 300px)\"],[\"Named\",\"(width < 1000px)\"]]]],"
              "\"detached\":[null,2,true,\"block\",true]}";
    };
    require(
        container_result == "{\"main\":" + container_expected("container-main")
                + ",\"frame\":" + container_expected("container-frame") + "}",
        "CSSContainerRule mutation diverged from the WPT-derived contract: "
            + container_result);

    const auto keyframes_result = evaluate(engine, R"JS((() => {
      const check = (condition, message) => {
        if (!condition) throw new Error(message);
      };
      const exercise = (document, view, prefix) => {
        const name = `${prefix}-pulse`;
        const style = document.createElement('style');
        const target = document.createElement('div');
        style.textContent = `@keyframes ${name} {
          from { opacity: 0; }
          25%, 75% { opacity: .25; }
          to { opacity: 1; }
        }
        @-webkit-keyframes ${prefix}-webkit {
          from { opacity: 0; }
          to { opacity: 1; }
        }`;
        target.style.cssText = `opacity: .2; animation: ${name}
          1000000s -500000s linear infinite;`;
        document.head.appendChild(style);
        document.body.appendChild(target);
        const sheet = style.sheet;
        const group = sheet.cssRules[0];
        const rules = group.cssRules;
        const from = rules[0];
        const middle = rules[1];
        const to = rules[2];
        const authoredText = style.textContent;
        check(group.constructor.name === 'CSSKeyframesRule'
            && group instanceof view.CSSKeyframesRule
            && group instanceof view.CSSRule && group.type === 7
            && view.CSSRule.KEYFRAMES_RULE === 7,
          `${prefix}: keyframes interface or type`);
        check(from.constructor.name === 'CSSKeyframeRule'
            && from instanceof view.CSSKeyframeRule
            && from instanceof view.CSSRule && from.type === 8
            && view.CSSRule.KEYFRAME_RULE === 8,
          `${prefix}: keyframe interface or type`);
        check(group.cssRules === rules && group.length === 3
            && group[0] === from && group[2] === to && group[3] === undefined
            && rules.item(1) === middle
            && from.parentRule === group && from.parentStyleSheet === sheet,
          `${prefix}: list, indexed getter, or parent identity`);
        check(Array.from(rules, rule => rule.keyText).join('|')
              === '0%|25%, 75%|100%'
            && group.findRule('from') === from
            && group.findRule('0%') === from
            && group.findRule('25%,75%') === middle
            && group.findRule('75%') === null
            && group.findRule('invalid') === null,
          `${prefix}: selector reflection or lookup`);
        check(sheet.cssRules[1] instanceof view.CSSKeyframesRule
            && sheet.cssRules[1].cssText.startsWith('@-webkit-keyframes'),
          `${prefix}: native-supported prefixed keyframes reflection`);
        const errorNames = [];
        for (const invoke of [
          () => group.appendRule(),
          () => group.deleteRule(),
          () => group.findRule(),
          () => group.appendRule(Symbol()),
          () => { from.keyText = '101%'; }
        ]) {
          try { invoke(); errorNames.push('none'); }
          catch (error) { errorNames.push(error.name); }
        }
        check(errorNames.join('|')
              === 'TypeError|TypeError|TypeError|TypeError|SyntaxError'
            && from.keyText === '0%', `${prefix}: exact mutation errors`);
        for (const invalid of [
          'from { opacity: 0 } garbage',
          'from { opacity: 0 } to { opacity: 1 }',
          'invalid { opacity: 1 }',
          '@media all {}'
        ]) group.appendRule(invalid);
        check(group.length === 3, `${prefix}: invalid append was not silent`);
        group.appendRule('to { opacity: .4; }');
        const appended = rules[3];
        check(group.length === 4 && group[3] === appended
            && group.findRule('100%') === appended
            && appended.parentRule === group
            && appended.parentStyleSheet === sheet
            && group.cssText.includes('opacity: 0.4')
            && style.textContent === authoredText,
          `${prefix}: append identity or serialization`);
        group.deleteRule('to');
        check(group.length === 3 && group.findRule('to') === to
            && appended.parentRule === null
            && appended.parentStyleSheet === null,
          `${prefix}: last-match deletion or detachment`);
        const declaration = middle.style;
        middle.keyText = '.5%, 1e1%, TO';
        middle.style = 'opacity: .8; transform: rotate(90deg);';
        check(middle.keyText === '0.5%, 10%, 100%'
            && middle.style === declaration && middle.style.opacity === '0.8'
            && middle.style.transform === 'rotate(90deg)'
            && middle.cssText.includes('rotate(90deg)')
            && group.cssText.includes('0.5%, 10%, 100%')
            && style.textContent === authoredText,
          `${prefix}: keyText, style identity, or containing serialization`);
        group.name = `${name}-renamed`;
        check(group.name === `${name}-renamed`
            && group.cssText.startsWith(`@keyframes ${name}-renamed`)
            && Math.abs(Number(view.getComputedStyle(target).opacity) - .2) < .001,
          `${prefix}: rename did not invalidate animation synchronously`);
        group.name = 'initial';
        check(group.name === 'initial'
            && group.cssText.startsWith('@keyframes "initial"'),
          `${prefix}: reserved name serialization`);
        const mediaIndex = sheet.insertRule(`@media (min-width: 1px) {
          @keyframes ${prefix}-nested { from { opacity: 0; } }
        }`, sheet.cssRules.length);
        const media = sheet.cssRules[mediaIndex];
        const nested = media.cssRules[0];
        nested.appendRule('to { opacity: 1; }');
        check(nested instanceof view.CSSKeyframesRule
            && nested.parentRule === media && nested.parentStyleSheet === sheet
            && nested[1].parentRule === nested
            && nested[1].parentStyleSheet === sheet
            && media.cssText.includes('to { opacity: 1')
            && style.textContent === authoredText,
          `${prefix}: containing group identity or serialization`);
        const retained = group.cssRules[0];
        sheet.deleteRule(0);
        group.appendRule('50% { opacity: .6; }');
        check(group.parentStyleSheet === null
            && retained.parentRule === null && retained.parentStyleSheet === null
            && group.length === 4 && group.cssText.includes('opacity: 0.6')
            && Math.abs(Number(view.getComputedStyle(target).opacity) - .2) < .001
            && style.textContent === authoredText,
          `${prefix}: detached mutation leaked publication or parents`);
        const constructed = new view.CSSStyleSheet();
        constructed.replaceSync('@keyframes made { from { opacity: 0 } }');
        const made = constructed.cssRules[0];
        made.appendRule('to { opacity: 1 }');
        check(made instanceof view.CSSKeyframesRule && made.length === 2
            && made[1] === made.cssRules[1] && made.findRule('to') === made[1],
          `${prefix}: constructed keyframes identity`);
        style.remove();
        target.remove();
      };
      const frame = document.createElement('iframe');
      document.body.appendChild(frame);
      exercise(document, window, 'keyframes-main');
      exercise(frame.contentDocument, frame.contentWindow, 'keyframes-frame');
      frame.remove();
      return 'ok';
    })())JS", "native-css-keyframes-rule-mutation.js");
    require(
        keyframes_result == "\"ok\"",
        "CSS keyframes rule mutation diverged from the WPT-derived contract: "
            + keyframes_result);

    const auto font_face_result = evaluate(engine, R"JS((() => {
      const check = (condition, message) => {
        if (!condition) throw new Error(message);
      };
      const exercise = (document, view, prefix) => {
        const style = document.createElement('style');
        const target = document.createElement('div');
        target.className = `${prefix}-target`;
        style.textContent = `@font-face {
          font-family: "${prefix}-initial";
          src: url("${prefix}-400.ttf") format("truetype");
          font-style: normal;
          font-weight: 400;
        }
        @media (min-width: 1px) {
          @font-face {
            font-family: "${prefix}-nested";
            src: url("${prefix}-nested.ttf");
            font-weight: 400;
          }
        }
        .${prefix}-target { width: 10px; }`;
        document.head.appendChild(style);
        document.body.appendChild(target);
        const sheet = style.sheet;
        const rule = sheet.cssRules[0];
        const declaration = rule.style;
        const authoredText = style.textContent;
        check(rule.constructor.name === 'CSSFontFaceRule'
            && rule instanceof view.CSSFontFaceRule
            && rule instanceof view.CSSRule
            && rule.type === 5 && view.CSSRule.FONT_FACE_RULE === 5
            && view.CSSRule.prototype.FONT_FACE_RULE === 5
            && rule.parentRule === null && rule.parentStyleSheet === sheet
            && rule.style === declaration && declaration.parentRule === rule,
          `${prefix}: interface, type, parent, or SameObject identity`);
        check(declaration.fontFamily === `"${prefix}-initial"`
            && declaration.src.includes(`${prefix}-400.ttf`)
            && declaration.fontWeight === '400'
            && Array.from({ length: declaration.length }, (_, index) =>
              declaration[index]).join('|')
                === 'font-family|src|font-style|font-weight'
            && !rule.cssText.includes('\n'),
          `${prefix}: descriptor reflection or serialization`);
        const errors = [];
        for (const invoke of [
          () => { rule.style = Symbol('descriptors'); },
          () => { declaration.cssText = Symbol('descriptors'); }
        ]) {
          try { invoke(); errors.push('none'); }
          catch (error) { errors.push(error.name); }
        }
        check(errors.join('|') === 'TypeError|TypeError',
          `${prefix}: descriptor conversion errors`);
        declaration.cssText = `font-family: "${prefix}-live";
          src: url("${prefix}-400.ttf"); font-style: normal;
          font-weight: 400;`;
        check(rule.style === declaration
            && declaration.fontFamily === `"${prefix}-live"`
            && declaration.getPropertyValue('src').includes(`${prefix}-400.ttf`)
            && declaration.fontWeight === '400'
            && rule.cssText.includes(`font-family: "${prefix}-live";`)
            && !rule.cssText.includes('\n')
            && view.getComputedStyle(target).width === '10px'
            && style.textContent === authoredText,
          `${prefix}: cssText mutation or attached publication`);
        rule.style = `font-family: "${prefix}-live";
          src: url("${prefix}-700.ttf"); font-style: normal;
          font-weight: 700;`;
        check(rule.style === declaration
            && declaration.src.includes(`${prefix}-700.ttf`)
            && declaration.fontWeight === '700'
            && rule.cssText.includes('font-weight: 700;')
            && view.getComputedStyle(target).width === '10px'
            && style.textContent === authoredText,
          `${prefix}: PutForwards mutation or synchronous layout`);
        const group = sheet.cssRules[1];
        const nested = group.cssRules[0];
        const nestedDeclaration = nested.style;
        nested.style.fontWeight = '700';
        check(nested instanceof view.CSSFontFaceRule
            && nested.parentRule === group && nested.parentStyleSheet === sheet
            && nested.style === nestedDeclaration
            && group.cssText.includes('font-weight: 700;'),
          `${prefix}: nested ownership or containing serialization`);
        sheet.deleteRule(1);
        const groupSnapshot = group.cssText;
        nested.style.fontFamily = `"${prefix}-detached-nested"`;
        check(group.parentStyleSheet === null
            && nested.parentRule === null && nested.parentStyleSheet === null
            && nested.cssText.includes(`${prefix}-detached-nested`)
            && group.cssText === groupSnapshot
            && !Array.from(sheet.cssRules, item => item.cssText).join('\n')
              .includes(`${prefix}-detached-nested`),
          `${prefix}: recursive detach or nested isolation`);
        const constructed = new view.CSSStyleSheet();
        constructed.replaceSync(`@font-face {
          font-family: "${prefix}-constructed";
          src: url("${prefix}-constructed.ttf"); font-weight: 400; }
          .${prefix}-target { width: 21px; }`);
        const constructedRule = constructed.cssRules[0];
        document.adoptedStyleSheets.push(constructed);
        constructedRule.style.fontWeight = '700';
        check(constructedRule instanceof view.CSSFontFaceRule
            && constructed.ownerNode === null
            && constructedRule.parentStyleSheet === constructed
            && document.adoptedStyleSheets.includes(constructed)
            && constructedRule.cssText.includes('font-weight: 700;')
            && view.getComputedStyle(target).width === '21px',
          `${prefix}: constructed/adopted ownership or publication`);
        document.adoptedStyleSheets.pop();
        const index = Array.from(sheet.cssRules).indexOf(rule);
        sheet.deleteRule(index);
        const sheetSnapshot = Array.from(
          sheet.cssRules, item => item.cssText).join('\n');
        declaration.fontFamily = `"${prefix}-detached"`;
        check(rule.parentRule === null && rule.parentStyleSheet === null
            && rule.style === declaration
            && rule.cssText.includes(`${prefix}-detached`)
            && Array.from(sheet.cssRules, item => item.cssText).join('\n')
              === sheetSnapshot
            && view.getComputedStyle(target).width === '10px'
            && style.textContent === authoredText,
          `${prefix}: detached local mutation leaked publication`);
        style.remove();
        target.remove();
      };
      const frame = document.createElement('iframe');
      document.body.appendChild(frame);
      exercise(document, window, 'font-face-main');
      exercise(frame.contentDocument, frame.contentWindow, 'font-face-frame');
      frame.remove();
      return 'ok';
    })())JS", "native-css-font-face-rule-mutation.js");
    require(
        font_face_result == "\"ok\"",
        "CSS font-face rule mutation diverged from the WPT-derived contract: "
            + font_face_result);

    const auto layer_result = evaluate(engine, R"JS((() => {
      const exercise = (document, view, className) => {
        const style = document.createElement('style');
        const target = document.createElement('div');
        style.textContent = `@layer base, theme;
          @layer base { .${className} { color: rgb(1, 2, 3); } }
          @layer theme { .${className} { color: rgb(4, 5, 6); } }`;
        target.className = className;
        document.head.appendChild(style);
        document.body.appendChild(target);
        const sheet = style.sheet;
        const group = sheet.cssRules[2];
        const rules = group.cssRules;
        const original = rules[0];
        const authoredText = style.textContent;
        const initial = [
          group.constructor.name,
          group instanceof view.CSSLayerBlockRule,
          group instanceof view.CSSGroupingRule,
          group instanceof view.CSSRule,
          group.type,
          typeof view.CSSRule.LAYER_BLOCK_RULE,
          group.name,
          group.cssRules === rules,
          original.parentRule === group,
          original.parentStyleSheet === sheet,
          view.getComputedStyle(target).color
        ];
        let assignmentError = '';
        try { group.name = 'changed'; }
        catch (error) { assignmentError = error.name; }
        const assignment = [group.name, assignmentError];
        const errors = [];
        for (const invoke of [
          () => group.insertRule(),
          () => group.deleteRule(),
          () => group.insertRule(`.${className} {}`, 99),
          () => group.insertRule(`.${className} {} .other {}`, 0),
          () => group.insertRule('@import url("x.css");', 0),
          () => group.insertRule('@namespace x url("x");', 0)
        ]) {
          try { invoke(); errors.push('none'); }
          catch (error) { errors.push(error.name); }
        }
        const insertedIndex = group.insertRule(
          `.${className} { color: rgb(7, 8, 9); }`, rules.length);
        const inserted = rules[insertedIndex];
        const afterInsert = [insertedIndex, rules.length,
          inserted.parentRule === group,
          inserted.parentStyleSheet === sheet,
          view.getComputedStyle(target).color,
          group.cssText.includes('rgb(7, 8, 9)')];
        group.deleteRule(insertedIndex);
        const afterDelete = [rules.length, inserted.parentRule,
          inserted.parentStyleSheet, view.getComputedStyle(target).color];
        const mediaIndex = sheet.insertRule(
          `@media (min-width:1px) { @layer nested {
            .${className} { color: rgb(10, 11, 12); }
          } }`, sheet.cssRules.length);
        const media = sheet.cssRules[mediaIndex];
        const nested = media.cssRules[0];
        const nestedIndex = nested.insertRule(
          `.${className} { color: rgb(13, 14, 15); }`, nested.cssRules.length);
        const nestedState = [nestedIndex, nested.cssRules.length,
          nested.constructor.name,
          nested instanceof view.CSSLayerBlockRule,
          nested.name,
          nested.parentRule === media,
          nested.parentStyleSheet === sheet,
          view.getComputedStyle(target).color,
          media.cssText.includes('rgb(13, 14, 15)')];
        const anonymousIndex = sheet.insertRule(
          `@layer { .${className} { color: rgb(16, 17, 18); } }`,
          sheet.cssRules.length);
        const anonymous = sheet.cssRules[anonymousIndex];
        const anonymousState = [anonymous.constructor.name, anonymous.name,
          anonymous instanceof view.CSSLayerBlockRule, anonymous.type,
          view.getComputedStyle(target).color];
        const textStable = style.textContent === authoredText;
        style.remove();
        target.remove();
        return { initial, assignment, errors, afterInsert, afterDelete,
          nestedState, anonymousState, textStable };
      };
      const frame = document.createElement('iframe');
      document.body.appendChild(frame);
      const value = {
        main: exercise(document, window, 'layer-main'),
        frame: exercise(frame.contentDocument, frame.contentWindow, 'layer-frame')
      };
      frame.remove();
      return value;
    })())JS", "native-css-layer-block-rule-mutation.js");
    const auto layer_expected = [] {
        return std::string{"{\"initial\":[\"CSSLayerBlockRule\",true,true,true,0,\"undefined\",\"theme\",true,true,true,\"rgb(4, 5, 6)\"],\"assignment\":[\"theme\",\"\"],\"errors\":[\"TypeError\",\"TypeError\",\"IndexSizeError\",\"SyntaxError\",\"HierarchyRequestError\",\"HierarchyRequestError\"],\"afterInsert\":[1,2,true,true,\"rgb(7, 8, 9)\",true],\"afterDelete\":[1,null,null,\"rgb(4, 5, 6)\"],\"nestedState\":[1,2,\"CSSLayerBlockRule\",true,\"nested\",true,true,\"rgb(13, 14, 15)\",true],\"anonymousState\":[\"CSSLayerBlockRule\",\"\",true,0,\"rgb(16, 17, 18)\"],\"textStable\":true}"};
    };
    require(
        layer_result == "{\"main\":" + layer_expected()
                + ",\"frame\":" + layer_expected() + "}",
        "CSSLayerBlockRule mutation diverged from the browser contract: "
            + layer_result);

    const auto plain_frame_style_result = evaluate(engine, R"JS((() => {
      const frame = document.createElement('iframe');
      document.body.appendChild(frame);
      const style = frame.contentDocument.createElement('style');
      const target = frame.contentDocument.createElement('div');
      style.textContent = '.plain-frame-style { color: rgb(9, 8, 7); }';
      target.className = 'plain-frame-style';
      frame.contentDocument.head.appendChild(style);
      frame.contentDocument.body.appendChild(target);
      const result = frame.contentWindow.getComputedStyle(target).color;
      frame.remove();
      return result;
    })())JS", "native-plain-frame-style-debug.js");
    require(
        plain_frame_style_result == "\"rgb(9, 8, 7)\"",
        "plain provisional-frame stylesheet control regressed: "
            + plain_frame_style_result);

    const auto layer_statement_result = evaluate(engine, R"JS((() => {
      const exercise = (document, view, className) => {
        const style = document.createElement('style');
        const target = document.createElement('div');
        style.textContent = `@layer early, branch.base, late;
          @layer late { .${className} { color: rgb(255, 0, 0); } }
          @layer early { .${className} { color: rgb(0, 0, 255); } }`;
        target.className = className;
        document.head.appendChild(style);
        document.body.appendChild(target);
        const sheet = style.sheet;
        const authoredText = style.textContent;
        const statement = sheet.cssRules[0];
        const names = statement.nameList;
        const initial = [
          statement.constructor.name,
          statement instanceof view.CSSLayerStatementRule,
          statement instanceof view.CSSRule,
          statement instanceof view.CSSGroupingRule,
          statement.type,
          typeof view.CSSRule.LAYER_STATEMENT_RULE,
          names.join('|'),
          Array.isArray(names),
          Object.isFrozen(names),
          statement.nameList !== statement.nameList,
          statement.parentStyleSheet === sheet,
          statement.parentRule,
          statement.cssText,
          view.getComputedStyle(target).color
        ];
        let assignmentError = '';
        try { statement.nameList = ['changed']; }
        catch (error) { assignmentError = error.name; }
        let pushError = '';
        try { names.push('changed'); }
        catch (error) { pushError = error.name; }
        const readonly = [assignmentError, pushError,
          statement.nameList.join('|'), names.join('|')];
        const errors = [];
        for (const source of [
          '@layer;', '@layer invalid,;', '@layer invalid name;',
          '@layer invalid..name;'
        ]) {
          try { sheet.insertRule(source, 0); errors.push('none'); }
          catch (error) { errors.push(error.name); }
        }
        sheet.deleteRule(0);
        const afterDelete = [
          view.getComputedStyle(target).color,
          statement.parentStyleSheet,
          statement.parentRule,
          statement.nameList.join('|'),
          statement.cssText
        ];
        const insertedIndex = sheet.insertRule('@layer early, late;', 0);
        const inserted = sheet.cssRules[insertedIndex];
        const afterInsert = [
          insertedIndex,
          inserted.constructor.name,
          inserted instanceof view.CSSLayerStatementRule,
          inserted.nameList.join('|'),
          inserted.cssText,
          view.getComputedStyle(target).color
        ];
        const mediaIndex = sheet.insertRule('@media all {}', sheet.cssRules.length);
        const media = sheet.cssRules[mediaIndex];
        const nestedIndex = media.insertRule(
          '@layer nested.one, nested.two;', media.cssRules.length);
        const nested = media.cssRules[nestedIndex];
        const nestedState = [
          nested.constructor.name,
          nested instanceof view.CSSLayerStatementRule,
          nested.nameList.join('|'),
          nested.parentRule === media,
          nested.parentStyleSheet === sheet,
          media.cssText.includes('@layer nested.one, nested.two;')
        ];
        const outerIndex = sheet.insertRule('@layer outer {}', sheet.cssRules.length);
        const outer = sheet.cssRules[outerIndex];
        const innerIndex = outer.insertRule('@layer inner.one, inner.two;', 0);
        const inner = outer.cssRules[innerIndex];
        const layerNested = [
          inner instanceof view.CSSLayerStatementRule,
          inner.nameList.join('|'),
          inner.parentRule === outer,
          inner.parentStyleSheet === sheet,
          outer.cssText.includes('@layer inner.one, inner.two;')
        ];
        sheet.deleteRule(0);
        const detached = [
          inserted.parentStyleSheet,
          inserted.parentRule,
          inserted.nameList.join('|'),
          inserted.cssText,
          view.getComputedStyle(target).color
        ];
        const textStable = style.textContent === authoredText;
        style.remove();
        target.remove();
        return { initial, readonly, errors, afterDelete, afterInsert,
          nestedState, layerNested, detached, textStable };
      };
      const frame = document.createElement('iframe');
      document.body.appendChild(frame);
      const value = {
        main: exercise(document, window, 'layer-statement-main'),
        frame: exercise(
          frame.contentDocument, frame.contentWindow, 'layer-statement-frame')
      };
      frame.remove();
      return value;
    })())JS", "native-css-layer-statement-rule-mutation.js");
    const auto layer_statement_expected = [] {
        return std::string{"{\"initial\":[\"CSSLayerStatementRule\",true,true,false,0,\"undefined\",\"early|branch.base|late\",true,true,true,true,null,\"@layer early, branch.base, late;\",\"rgb(255, 0, 0)\"],\"readonly\":[\"\",\"TypeError\",\"early|branch.base|late\",\"early|branch.base|late\"],\"errors\":[\"SyntaxError\",\"SyntaxError\",\"SyntaxError\",\"SyntaxError\"],\"afterDelete\":[\"rgb(0, 0, 255)\",null,null,\"early|branch.base|late\",\"@layer early, branch.base, late;\"],\"afterInsert\":[0,\"CSSLayerStatementRule\",true,\"early|late\",\"@layer early, late;\",\"rgb(255, 0, 0)\"],\"nestedState\":[\"CSSLayerStatementRule\",true,\"nested.one|nested.two\",true,true,true],\"layerNested\":[true,\"inner.one|inner.two\",true,true,true],\"detached\":[null,null,\"early|late\",\"@layer early, late;\",\"rgb(0, 0, 255)\"],\"textStable\":true}"};
    };
    require(
        layer_statement_result == "{\"main\":" + layer_statement_expected()
                + ",\"frame\":" + layer_statement_expected() + "}",
        "CSSLayerStatementRule mutation diverged from the browser contract: "
            + layer_statement_result);

    execute(engine, R"JS((async () => {
      globalThis.__ownerSheetReplacement = { status: 'pending' };
      try {
        const exercise = async (document, view, className) => {
          const style = document.createElement('style');
          const target = document.createElement('div');
          style.textContent = `.${className} { color: rgb(17, 23, 31); }`;
          target.className = className;
          document.head.appendChild(style);
          document.body.appendChild(target);
          const sheet = style.sheet;
          const rules = sheet.cssRules;
          const rule = rules[0];
          const authoredText = style.textContent;
          const initialCssText = rule.cssText;
          const syncErrors = [];
          for (const invoke of [
            () => sheet.replaceSync(),
            () => sheet.replaceSync(Symbol('css')),
            () => sheet.replaceSync(`.${className} { color: red; }`)
          ]) {
            try { invoke(); syncErrors.push('none'); }
            catch (error) {
              syncErrors.push(error.name);
            }
          }
          const asyncOperations = [
            sheet.replace(),
            sheet.replace(Symbol('css')),
            sheet.replace(`.${className} { color: blue; }`)
          ];
          if (!asyncOperations.every(operation => operation
              && typeof operation.then === 'function'
              && operation.constructor === asyncOperations[0].constructor))
            throw new Error('replace did not return consistent Promise objects');
          const asyncErrors = await Promise.all(asyncOperations.map(operation =>
            operation.then(() => 'none', error => error.name)));
          style.remove();
          let detachedSync = '';
          try { sheet.replaceSync(`.${className} { color: green; }`); }
          catch (error) { detachedSync = error.name; }
          const detachedAsync = await sheet.replace(`.${className} { color: green; }`)
            .then(() => 'none', error => error.name);
          const stable = sheet.cssRules === rules
            && rules[0] === rule
            && rule.cssText === initialCssText
            && style.textContent === authoredText
            && style.sheet === null;
          document.head.appendChild(style);
          const reattached = style.sheet === sheet
            && view.getComputedStyle(target).color === 'rgb(17, 23, 31)';
          style.remove();
          target.remove();
          return {
            syncErrors, asyncErrors, detachedSync, detachedAsync, stable,
            reattached, lengths: [sheet.replaceSync.length, sheet.replace.length]
          };
        };
        const frame = document.createElement('iframe');
        document.body.appendChild(frame);
        const main = await exercise(document, window, 'replace-main');
        const nested = await exercise(
          frame.contentDocument, frame.contentWindow, 'replace-frame');
        frame.remove();
        const pass = value => JSON.stringify(value.syncErrors)
              === JSON.stringify(['TypeError', 'TypeError', 'NotAllowedError'])
            && JSON.stringify(value.asyncErrors)
              === JSON.stringify(['TypeError', 'TypeError', 'NotAllowedError'])
            && value.detachedSync === 'NotAllowedError'
            && value.detachedAsync === 'NotAllowedError'
            && value.stable && value.reattached
            && value.lengths[0] === 1 && value.lengths[1] === 1;
        if (!pass(main) || !pass(nested))
          throw new Error(JSON.stringify({ main, nested }));
        globalThis.__ownerSheetReplacement = { status: 'passed', main, nested };
      } catch (error) {
        globalThis.__ownerSheetReplacement = {
          status: 'failed', error: String(error), stack: String(error?.stack || '')
        };
      }
    })())JS", "native-owner-stylesheet-replacement.js");
    const auto replacement_status = evaluate_until_equals(
        engine,
        "globalThis.__ownerSheetReplacement?.status ?? 'pending'",
        "native-owner-stylesheet-replacement-status.js",
        R"("passed")",
        500);
    require(replacement_status == R"("passed")",
        "owner stylesheet replacement rejection diverged: "
            + evaluate(engine, "globalThis.__ownerSheetReplacement",
                "native-owner-stylesheet-replacement-result.js"));

    execute(engine, R"JS((async () => {
      globalThis.__constructedSheetContract = { status: 'pending' };
      try {
        const sheet = new CSSStyleSheet({
          disabled: true, media: 'screen, print', baseURL: './assets/'
        });
        const rules = sheet.cssRules;
        const media = sheet.media;
        if (CSSStyleSheet.name !== 'CSSStyleSheet' || CSSStyleSheet.length !== 0
            || !(sheet instanceof CSSStyleSheet)
            || Object.getPrototypeOf(sheet) !== CSSStyleSheet.prototype
            || sheet.ownerNode !== null || sheet.ownerRule !== null
            || sheet.href !== null || sheet.title !== null
            || sheet.type !== 'text/css' || !sheet.disabled
            || media !== sheet.media || media.mediaText !== 'screen, print'
            || media.length !== 2 || media.item(0) !== 'screen'
            || media.item(1) !== 'print' || rules !== sheet.cssRules
            || rules !== sheet.rules || rules.length !== 0) {
          throw new Error('constructed sheet constructor/options/reflection diverged');
        }
        let constructorCall = '';
        try { CSSStyleSheet(); } catch (error) { constructorCall = error.name; }
        if (constructorCall !== 'TypeError')
          throw new Error('constructed sheet call guard diverged');
        if (new CSSStyleSheet({ title: 'ignored' }).title !== null)
          throw new Error('constructed sheet title option diverged');
        let baseURLError = '';
        try { new CSSStyleSheet({ baseURL: 'https://test:test/' }); }
        catch (error) { baseURLError = error.name; }
        if (baseURLError !== 'NotAllowedError')
          throw new Error('constructed sheet baseURL validation diverged');

        sheet.insertRule('.temp{}');
        const sanitizerRule = rules[0];
        sanitizerRule.style.cssText =
          'display:block;position:absolute;background-image:url(evil);width:12px';
        const allowed = [];
        for (let index = 0; index < sanitizerRule.style.length; index++) {
          const name = sanitizerRule.style[index];
          if (['display', 'position', 'width'].includes(name))
            allowed.push(`${name}: ${sanitizerRule.style.getPropertyValue(name)}`);
        }
        if (!(sanitizerRule instanceof CSSStyleRule)
            || sanitizerRule.parentStyleSheet !== sheet
            || allowed.join(';') !== 'display: block;position: absolute;width: 12px') {
          throw new Error('Code OSS constructed-sheet sanitizer contract diverged');
        }

        const detached = rules[0];
        const listIdentity = rules;
        sheet.replaceSync("@import url('ignored.css'); .sync { color: green; }");
        if (sheet.cssRules !== listIdentity || rules.length !== 1
            || rules[0].selectorText !== '.sync'
            || rules[0].style.color !== 'green'
            || detached.parentStyleSheet !== null
            || !detached.cssText.includes('background-image')) {
          throw new Error('replaceSync identity/import/detach behavior diverged');
        }

        const sequence = [];
        const replacement = sheet.replace('.async { color: blue; }').then(value => {
          sequence.push('fulfilled');
          return value;
        });
        sequence.push('sync');
        const immediate = rules[0];
        queueMicrotask(() => sequence.push('microtask'));
        const resolved = await replacement;
        if (resolved !== sheet || immediate.selectorText !== '.async'
            || sequence.join('|') !== 'sync|fulfilled|microtask') {
          throw new Error('replace Promise mutation/result/order diverged');
        }
        const errors = [];
        for (const invoke of [
          () => sheet.insertRule("@import url('x.css');"),
          () => sheet.insertRule('.bad{}', 99),
          () => sheet.deleteRule(99),
          () => sheet.replaceSync(),
          () => sheet.replaceSync(Symbol('css'))
        ]) {
          try { invoke(); errors.push('none'); }
          catch (error) { errors.push(error.name); }
        }
        for (const invoke of [() => sheet.replace(), () => sheet.replace(Symbol('css'))]) {
          try { await invoke(); errors.push('fulfilled'); }
          catch (error) { errors.push(error.name); }
        }
        if (errors.join('|')
            !== 'SyntaxError|IndexSizeError|IndexSizeError|TypeError|TypeError|TypeError|TypeError') {
          throw new Error(`constructed sheet errors diverged: ${errors.join('|')}`);
        }
        const first = sheet.replace('.one{}');
        const second = sheet.replace('.two{}');
        await Promise.all([first, second]);
        const target = document.createElement('div');
        target.className = 'two';
        document.body.appendChild(target);
        if (rules.length !== 1 || rules[0].selectorText !== '.two'
            || getComputedStyle(target).display !== 'block'
            || getComputedStyle(target).color !== 'rgb(0, 0, 0)') {
          throw new Error('concurrent replacement or unadopted cascade diverged');
        }
        target.remove();
        globalThis.__constructedSheetContract = {
          status: 'passed', ruleCount: rules.length, allowed, errors, sequence
        };
      } catch (error) {
        globalThis.__constructedSheetContract = {
          status: 'failed', error: String(error), stack: String(error?.stack || '')
        };
      }
    })())JS", "native-constructed-stylesheet-contract.js");
    const auto constructed_status = evaluate_until_equals(
        engine,
        "globalThis.__constructedSheetContract?.status ?? 'pending'",
        "native-constructed-stylesheet-contract-status.js",
        R"("passed")",
        500);
    require(constructed_status == R"("passed")",
        "constructed stylesheet contract diverged: "
            + evaluate(engine, "globalThis.__constructedSheetContract",
                "native-constructed-stylesheet-contract-result.js"));
}

void test_adopted_stylesheet_multi_root_contract_and_lifecycle()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "adopted stylesheet engine creation failed");
    resize(engine, 800, 600, 1U);
    wait_for_consumed_inputs(
        engine, 1U, "adopted stylesheet viewport was not consumed");
    const webscene_scene_view* initial_scene = nullptr;
    for (auto attempt = 0; attempt < 100 && initial_scene == nullptr; ++attempt) {
        initial_scene = webscene_engine_acquire_latest_scene(engine);
        if (initial_scene == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(initial_scene != nullptr,
        "adopted stylesheet initial scene was not published");
    require(webscene_scene_acknowledge(initial_scene) != 0,
        "adopted stylesheet initial scene acknowledgement failed");
    webscene_scene_release(initial_scene);
    require(webscene_engine_set_runtime_work_metrics_enabled(engine, 1U) != 0,
        "adopted stylesheet runtime-work metrics could not be enabled");
    webscene_engine_metrics engine_before{};
    webscene_engine_get_metrics(engine, &engine_before);
    webscene_runtime_work_metrics work_before{
        sizeof(webscene_runtime_work_metrics)};
    require(webscene_engine_get_runtime_work_metrics(engine, &work_before) != 0,
        "adopted stylesheet baseline work metrics were unavailable");
    webscene_engine_memory_metrics memory_before{
        sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_memory_metrics(engine, &memory_before) != 0,
        "adopted stylesheet baseline memory metrics were unavailable");
    const auto rss_before = file_system_access_test_peak_rss_bytes();

    const auto result = evaluate(engine, R"JS((() => {
      const failures = [];
      const expect = (condition, message) => {
        if (!condition) failures.push(message);
      };
      const errorName = callback => {
        try { callback(); return 'none'; }
        catch (error) { return error.name; }
      };
      const author = document.createElement('style');
      author.textContent = '.adopted-target { color: rgb(255, 0, 0); width: 10px; }';
      document.head.appendChild(author);
      const target = document.createElement('div');
      target.className = 'adopted-target';
      document.body.appendChild(target);
      const host = document.createElement('section');
      document.body.appendChild(host);
      const shadow = host.attachShadow({ mode: 'open' });
      shadow.innerHTML = '<style>.adopted-target{color:rgb(1,2,3)}</style>'
        + '<div class="adopted-target"></div>';
      const shadowTarget = shadow.querySelector('.adopted-target');
      const first = new CSSStyleSheet();
      const second = new CSSStyleSheet();
      first.replaceSync('.adopted-target { color: rgb(0, 0, 255); width: 20px; }');
      second.replaceSync('.adopted-target { color: rgb(0, 128, 0); width: 30px; }');
      const documentList = document.adoptedStyleSheets;
      expect(Array.isArray(documentList), 'document list is not an Array');
      expect(documentList === document.adoptedStyleSheets, 'document list lost SameObject');
      document.adoptedStyleSheets = [first, second];
      expect(document.adoptedStyleSheets === documentList, 'assignment replaced list identity');
      expect(getComputedStyle(target).color === 'rgb(0, 128, 0)',
        'adopted order or author-after-adopted cascade diverged');
      documentList.reverse();
      expect(getComputedStyle(target).color === 'rgb(0, 0, 255)',
        'observable reverse did not republish order');
      documentList.splice(0, documentList.length, first, second, first);
      expect(documentList.length === 3 && getComputedStyle(target).color === 'rgb(0, 0, 255)',
        'duplicates or splice ordering diverged');
      documentList.pop();
      documentList.length = 12;
      documentList[8] = first;
      expect(documentList.length === 2,
        'observable list accepted a length gap');
      documentList[2] = first;
      expect(documentList.length === 3 && documentList[2] === first,
        'observable list rejected next-index append');
      delete documentList[1];
      expect(documentList.length === 3 && documentList[1] === second,
        'observable list deleted a non-final index');
      delete documentList[2];
      expect(documentList.length === 2,
        'observable list did not delete its final index');
      documentList.splice(0, documentList.length, first, second);
      second.cssRules[0].style.color = 'rgb(255, 255, 0)';
      expect(getComputedStyle(target).color === 'rgb(255, 255, 0)',
        'live declaration mutation did not invalidate adopter');
      second.disabled = true;
      expect(getComputedStyle(target).color === 'rgb(0, 0, 255)',
        'disabled constructed sheet remained published');
      second.disabled = false;
      second.media.mediaText = 'print';
      expect(getComputedStyle(target).color === 'rgb(0, 0, 255)',
        'nonmatching adopted media remained active');
      second.media.mediaText = 'all';
      expect(getComputedStyle(target).color === 'rgb(255, 255, 0)',
        'media mutation did not restore publication');

      const shadowList = shadow.adoptedStyleSheets;
      shadow.adoptedStyleSheets = [first];
      expect(Array.isArray(shadowList) && shadowList === shadow.adoptedStyleSheets,
        'shadow list identity diverged');
      expect(getComputedStyle(shadowTarget).color === 'rgb(0, 0, 255)',
        'shadow-root publication did not override shadow author style');
      host.remove();
      document.body.appendChild(host);
      expect(shadow.adoptedStyleSheets[0] === first
          && getComputedStyle(shadowTarget).color === 'rgb(0, 0, 255)',
        'disconnect/reconnect lost shadow adoption');
      shadowList.length = 0;
      expect(getComputedStyle(shadowTarget).color === 'rgb(1, 2, 3)',
        'shadow adoption removal did not restore author style');

      const ownerStyle = document.createElement('style');
      document.head.appendChild(ownerStyle);
      const errors = [
        errorName(() => document.adoptedStyleSheets = [ownerStyle.sheet]),
        errorName(() => document.adoptedStyleSheets = [42])
      ];
      expect(errors.join('|') === 'NotAllowedError|TypeError',
        `adoption validation diverged: ${errors.join('|')}`);
      expect(documentList.length === 2,
        'failed assignment partially mutated the observable list');
      const frame = document.createElement('iframe');
      document.body.appendChild(frame);
      if (typeof frame.contentWindow.CSSStyleSheet === 'function') {
        const foreign = new frame.contentWindow.CSSStyleSheet();
        expect(errorName(() => document.adoptedStyleSheets = [foreign])
            === 'NotAllowedError',
          'cross-document constructed sheet did not reject with NotAllowedError');
      }
      frame.remove();

      const durations = [];
      const performanceSheet = new CSSStyleSheet();
      performanceSheet.replaceSync(Array.from({ length: 64 }, (_, index) =>
        `.adopted-perf-${index}{width:${index + 1}px}`).join('\n'));
      documentList.push(performanceSheet);
      shadowList.push(performanceSheet);
      for (let cycle = 0; cycle < 20; cycle++) {
        const started = performance.now();
        performanceSheet.replaceSync(Array.from({ length: 64 }, (_, index) =>
          `.adopted-perf-${index}{width:${index + cycle + 1}px}`).join('\n'));
        durations.push(performance.now() - started);
      }
      const ordered = durations.slice().sort((left, right) => left - right);
      const p95 = ordered[18], max = ordered[19];
      expect(p95 <= 50 && max <= 500,
        `adopted replacement latency exceeded ${p95}/${max}`);
      document.adoptedStyleSheets = [];
      shadow.adoptedStyleSheets = [];
      for (let cycle = 0; cycle < 100; cycle++) {
        const lifecycleHost = document.createElement('div');
        document.body.appendChild(lifecycleHost);
        const lifecycleRoot = lifecycleHost.attachShadow({ mode: 'open' });
        lifecycleRoot.innerHTML = `<div class="lifecycle-${cycle}"></div>`;
        const lifecycleSheet = new CSSStyleSheet();
        lifecycleSheet.replaceSync(
          `.lifecycle-${cycle}{display:block;width:${cycle + 1}px}`);
        lifecycleRoot.adoptedStyleSheets = [lifecycleSheet];
        expect(getComputedStyle(lifecycleRoot.firstChild).width === `${cycle + 1}px`,
          `lifecycle publication ${cycle} diverged`);
        lifecycleRoot.adoptedStyleSheets = [];
        lifecycleHost.remove();
      }
      ownerStyle.remove();
      host.remove();
      target.remove();
      author.remove();
      if (failures.length) throw new Error(failures.join('; '));
      return { status: 'passed', errors, p95, max, cycles: 100 };
    })())JS", "adopted-stylesheet-contract.js");
    require(result.find(R"("status":"passed")") != std::string::npos,
        "adopted stylesheet contract failed: " + result);
    for (auto attempt = 0; attempt < 100; ++attempt) {
        webscene_engine_metrics current{};
        webscene_engine_get_metrics(engine, &current);
        if (current.published_scenes > engine_before.published_scenes) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const webscene_scene_view* final_scene = nullptr;
    for (auto attempt = 0; attempt < 100 && final_scene == nullptr; ++attempt) {
        final_scene = webscene_engine_acquire_latest_scene(engine);
        if (final_scene == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(final_scene != nullptr,
        "adopted stylesheet changes did not publish a scene");
    require(webscene_scene_acknowledge(final_scene) != 0,
        "adopted stylesheet final scene acknowledgement failed");
    webscene_scene_release(final_scene);
    webscene_engine_metrics engine_after{};
    webscene_engine_get_metrics(engine, &engine_after);
    webscene_runtime_work_metrics work_after{
        sizeof(webscene_runtime_work_metrics)};
    require(webscene_engine_get_runtime_work_metrics(engine, &work_after) != 0,
        "adopted stylesheet final work metrics were unavailable");
    std::cout << "Adopted stylesheet publication work: layout="
              << engine_before.layout_passes << "/" << engine_after.layout_passes
              << " scenes=" << engine_before.published_scenes << "/"
              << engine_after.published_scenes << " builds="
              << work_before.scene_builds << "/" << work_after.scene_builds << '\n';
    require(engine_after.layout_passes <= engine_before.layout_passes + 1024U,
        "adopted stylesheet invalidation did not remain bounded");
    require(engine_after.published_scenes > engine_before.published_scenes
            && work_after.scene_builds <= work_before.scene_builds + 16U,
        "adopted stylesheet scene publication did not coalesce");
    require(webscene_engine_request_low_memory(engine) != 0,
        "adopted stylesheet low-memory request was rejected");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    webscene_engine_memory_metrics memory_after{
        sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_memory_metrics(engine, &memory_after) != 0,
        "adopted stylesheet final memory metrics were unavailable");
    const auto rss_after = file_system_access_test_peak_rss_bytes();
    require(memory_after.native_css_rule_count
            <= memory_before.native_css_rule_count + 8U,
        "adopted stylesheet teardown retained native CSS rules");
    require(memory_after.v8_used_heap_bytes
            <= memory_before.v8_used_heap_bytes + 12U * 1024U * 1024U,
        "100 adopted stylesheet cycles retained more than 12 MiB of V8 heap");
    require(rss_before == 0U
            || rss_after <= rss_before + 96U * 1024U * 1024U,
        "100 adopted stylesheet cycles increased peak RSS by more than 96 MiB");
    std::cout << "Adopted stylesheet gate: " << result
              << " rules=" << memory_before.native_css_rule_count << "/"
              << memory_after.native_css_rule_count
              << " heap=" << memory_before.v8_used_heap_bytes << "/"
              << memory_after.v8_used_heap_bytes
              << " rss=" << rss_before << "/" << rss_after
              << " layout=" << engine_before.layout_passes << "/"
              << engine_after.layout_passes
              << " scenes=" << engine_before.published_scenes << "/"
              << engine_after.published_scenes
              << " builds=" << work_before.scene_builds << "/"
              << work_after.scene_builds << '\n';
    webscene_engine_destroy(engine);
}

void test_nested_style_rule_cssom_performance_and_lifecycle()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "nested CSSOM performance engine creation failed");
    resize(engine, 800, 600, 1U);
    wait_for_consumed_inputs(
        engine, 1U, "nested CSSOM performance viewport was not consumed");
    execute_and_wait(engine, "globalThis.__nestedCssomBaseline = true",
        "nested-cssom-performance-baseline.js");
    webscene_engine_metrics engine_before{};
    webscene_engine_get_metrics(engine, &engine_before);
    webscene_runtime_work_metrics work_before{sizeof(webscene_runtime_work_metrics)};
    require(webscene_engine_get_runtime_work_metrics(engine, &work_before) != 0,
        "nested CSSOM baseline work metrics were unavailable");
    webscene_engine_memory_metrics memory_before{sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_memory_metrics(engine, &memory_before) != 0,
        "nested CSSOM baseline memory metrics were unavailable");
    const auto rss_before = file_system_access_test_peak_rss_bytes();
    const auto result = evaluate(engine, R"JS((() => {
      const durations = [];
      for (let cycle = 0; cycle < 20; cycle++) {
        const style = document.createElement('style');
        const host = document.createElement('div');
        const child = document.createElement('span');
        host.className = `nested-performance-${cycle}`;
        child.className = 'seed';
        host.appendChild(child);
        style.textContent = `.${host.className} {
          color: rgb(1, 2, 3); & > .seed { width: 1px; }
        }`;
        document.head.appendChild(style);
        document.body.appendChild(host);
        const outer = style.sheet.cssRules[0];
        const rules = outer.cssRules;
        const started = performance.now();
        for (let index = 0; index < 64; index++) {
          outer.insertRule(
            `& > .rule-${index} { width: ${index + 1}px; }`, rules.length);
        }
        child.className = 'rule-63';
        if (rules.length !== 65 || rules[64].selectorText !== '& > .rule-63'
            || getComputedStyle(child).width !== '64px') {
          throw new Error('nested CSSOM insertion state diverged');
        }
        while (rules.length > 1) outer.deleteRule(rules.length - 1);
        if (rules.length !== 1 || getComputedStyle(child).width !== 'auto')
          throw new Error('nested CSSOM deletion state diverged');
        durations.push(performance.now() - started);
        style.remove(); host.remove();
      }
      const ordered = durations.slice().sort((left, right) => left - right);
      const p95 = ordered[18], maximum = ordered[19];
      if (p95 > 32 || maximum > 500)
        throw new Error(`nested CSSOM latency exceeded ${p95}/${maximum}`);

      for (let cycle = 0; cycle < 100; cycle++) {
        const frame = document.createElement('iframe');
        document.body.appendChild(frame);
        for (const [doc, prefix] of [
          [document, `nested-main-life-${cycle}`],
          [frame.contentDocument, `nested-frame-life-${cycle}`]
        ]) {
          const style = doc.createElement('style');
          const host = doc.createElement('div');
          const child = doc.createElement('span');
          host.className = prefix;
          child.className = 'child';
          host.appendChild(child);
          style.textContent = `.${prefix} {
            color: rgb(1, 2, 3); & > .child { display: block; }
            background: rgb(4, 5, 6);
          }`;
          doc.head.appendChild(style);
          doc.body.appendChild(host);
          const outer = style.sheet.cssRules[0];
          const rules = outer.cssRules;
          if (rules.length !== 2
              || rules[0].parentRule !== outer
              || rules[1].constructor.name !== 'CSSNestedDeclarations') {
            throw new Error('nested CSSOM lifecycle reflection diverged');
          }
          const inserted = outer.insertRule('&.active { display: none; }', 2);
          outer.deleteRule(inserted);
          style.sheet.deleteRule(0);
          if (outer.parentStyleSheet !== null
              || rules[0].parentStyleSheet !== null) {
            throw new Error('nested CSSOM lifecycle detachment diverged');
          }
          style.remove(); host.remove();
        }
        frame.remove();
      }
      return { status: 'passed', cycles: durations.length,
        lifecycleCycles: 100, roots: 200, p95, max: maximum };
    })())JS", "nested-cssom-performance-lifecycle.js");
    require(result.find(R"("status":"passed")") != std::string::npos,
        "nested CSSOM performance/lifecycle workload failed: " + result);
    require(webscene_engine_request_low_memory(engine) != 0,
        "nested CSSOM low-memory request was rejected");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    webscene_engine_metrics engine_after{};
    webscene_engine_get_metrics(engine, &engine_after);
    webscene_runtime_work_metrics work_after{sizeof(webscene_runtime_work_metrics)};
    require(webscene_engine_get_runtime_work_metrics(engine, &work_after) != 0,
        "nested CSSOM final work metrics were unavailable");
    webscene_engine_memory_metrics memory_after{sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_memory_metrics(engine, &memory_after) != 0,
        "nested CSSOM final memory metrics were unavailable");
    const auto rss_after = file_system_access_test_peak_rss_bytes();
    require(memory_after.v8_used_heap_bytes
            <= memory_before.v8_used_heap_bytes + 8U * 1024U * 1024U,
        "nested CSSOM cycles retained more than 8 MiB of V8 heap");
    require(memory_after.native_css_rule_count
            <= memory_before.native_css_rule_count + 4U,
        "nested CSSOM cycles retained native CSS rules");
    require(memory_after.native_dom_node_count
            <= memory_before.native_dom_node_count + 32U,
        "nested CSSOM cycles retained native DOM nodes");
    require(memory_after.pending_scene_count <= 2U,
        "nested CSSOM cycles left an unbounded pending scene queue");
    require(rss_before == 0U
            || rss_after <= rss_before + 64U * 1024U * 1024U,
        "nested CSSOM cycles increased peak RSS by more than 64 MiB");
    require(engine_after.layout_passes <= engine_before.layout_passes + 64U
            && engine_after.published_scenes <= engine_before.published_scenes + 64U
            && work_after.scene_builds <= work_before.scene_builds + 64U,
        "nested CSSOM mutation exceeded bounded layout/scene publication");
    std::cout << "nested-cssom-profile=" << result
              << " heap=" << memory_before.v8_used_heap_bytes << "/"
              << memory_after.v8_used_heap_bytes
              << " rules=" << memory_before.native_css_rule_count << "/"
              << memory_after.native_css_rule_count
              << " nodes=" << memory_before.native_dom_node_count << "/"
              << memory_after.native_dom_node_count
              << " rss=" << rss_before << "/" << rss_after
              << " layout=" << engine_before.layout_passes << "/"
              << engine_after.layout_passes
              << " scenes=" << engine_before.published_scenes << "/"
              << engine_after.published_scenes
              << " builds=" << work_before.scene_builds << "/"
              << work_after.scene_builds << '\n';
    webscene_engine_destroy(engine);
}

void test_batched_stylesheet_rule_mutation_performance()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "stylesheet mutation performance engine creation failed");
    const auto acknowledge_latest_scene = [&](std::string_view failure) {
        const webscene_scene_view* scene = nullptr;
        for (auto attempt = 0; attempt < 100 && scene == nullptr; ++attempt) {
            scene = webscene_engine_acquire_latest_scene(engine);
            if (scene == nullptr) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
        require(scene != nullptr, failure);
        require(webscene_scene_acknowledge(scene) != 0,
            "stylesheet mutation scene acknowledgement failed");
        webscene_scene_release(scene);
    };
    resize(engine, 800, 600, 1U);
    wait_for_consumed_inputs(
        engine, 1U, "stylesheet mutation performance viewport was not consumed");
    const auto oversized_result = evaluate(engine, R"JS((() => {
      const style = document.createElement('style');
      document.head.appendChild(style);
      const sheet = style.sheet;
      const stage = sheet.__webSceneStageRules;
      style.remove();
      // Detached owners retain their CSSOM source for a future reconnect, but
      // an oversized first source must not enter the bounded pending queue.
      const queued = stage('.oversized{' + 'x'.repeat(32 * 1024 * 1024) + '}');
      return [queued, style.isConnected, style.sheet === null];
    })())JS", "oversized-stylesheet-staging.js");
    require(oversized_result == R"([false,false,true])",
        "oversized stylesheet source entered pending replacement storage: "
            + oversized_result);
    require(webscene_engine_request_low_memory(engine) != 0,
        "oversized stylesheet source low-memory request was rejected");
    acknowledge_latest_scene(
        "constructed stylesheet publication baseline had no settled scene");
    require(webscene_engine_set_runtime_work_metrics_enabled(engine, 1U) != 0,
        "stylesheet mutation runtime-work metrics could not be enabled");
    webscene_engine_metrics constructed_control_engine_before{};
    webscene_engine_get_metrics(engine, &constructed_control_engine_before);
    webscene_runtime_work_metrics constructed_control_work_before{
        sizeof(webscene_runtime_work_metrics)};
    require(webscene_engine_get_runtime_work_metrics(
                engine, &constructed_control_work_before) != 0,
        "constructed stylesheet control work metrics were unavailable");
    require(evaluate(engine, R"JS((() => {
          const started = performance.now();
          let checksum = 0;
          while (performance.now() - started < 25) {
            for (let index = 0; index < 4096; index++) checksum += index;
          }
          return { status: 'control', checksum: checksum > 0 };
        })())JS",
                "constructed-stylesheet-publication-control.js")
            == R"({"status":"control","checksum":true})",
        "constructed stylesheet publication control evaluation failed");
    webscene_engine_metrics constructed_control_engine_after{};
    webscene_engine_get_metrics(engine, &constructed_control_engine_after);
    webscene_runtime_work_metrics constructed_control_work_after{
        sizeof(webscene_runtime_work_metrics)};
    require(webscene_engine_get_runtime_work_metrics(
                engine, &constructed_control_work_after) != 0,
        "constructed stylesheet final control work metrics were unavailable");
    acknowledge_latest_scene(
        "constructed stylesheet heartbeat control had no scene");
    webscene_engine_metrics constructed_engine_before{};
    webscene_engine_get_metrics(engine, &constructed_engine_before);
    webscene_runtime_work_metrics constructed_work_before{
        sizeof(webscene_runtime_work_metrics)};
    require(webscene_engine_get_runtime_work_metrics(
                engine, &constructed_work_before) != 0,
        "constructed stylesheet baseline work metrics were unavailable");
    webscene_engine_memory_metrics constructed_memory_before{
        sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_memory_metrics(
                engine, &constructed_memory_before) != 0,
        "constructed stylesheet baseline memory metrics were unavailable");
    const auto constructed_rss_before = file_system_access_test_peak_rss_bytes();
    const auto constructed_insertion_first = evaluate(engine, R"JS((() => {
      const sheet = new CSSStyleSheet();
      const rules = sheet.cssRules;
      const durations = [];
      for (let cycle = 0; cycle < 10; cycle++) {
        const started = performance.now();
        while (rules.length) sheet.deleteRule(0);
        for (let index = 0; index < 64; index++) {
          sheet.insertRule(
            `.inserted-${cycle}-${index} { height: ${index + 1}px; }`, index);
        }
        if (rules.length !== 64
            || rules[63].selectorText !== `.inserted-${cycle}-63`
            || rules[63].style.height !== '64px') {
          throw new Error('constructed insertion state diverged');
        }
        durations.push(performance.now() - started);
      }
      globalThis.__constructedInsertionPerformance = { sheet, rules, durations };
      return { status: 'partial', cycles: durations.length };
    })())JS", "constructed-stylesheet-insertion-performance-first.js");
    require(constructed_insertion_first == R"({"status":"partial","cycles":10})",
        "constructed stylesheet first insertion batch failed: "
            + constructed_insertion_first);
    const auto constructed_insertion_result = evaluate(engine, R"JS((() => {
      const state = globalThis.__constructedInsertionPerformance;
      const { sheet, rules, durations } = state;
      for (let cycle = 10; cycle < 20; cycle++) {
        const started = performance.now();
        while (rules.length) sheet.deleteRule(0);
        for (let index = 0; index < 64; index++) {
          sheet.insertRule(
            `.inserted-${cycle}-${index} { height: ${index + 1}px; }`, index);
        }
        if (rules.length !== 64
            || rules[63].selectorText !== `.inserted-${cycle}-63`
            || rules[63].style.height !== '64px') {
          throw new Error('constructed insertion state diverged');
        }
        durations.push(performance.now() - started);
      }
      const ordered = durations.slice().sort((left, right) => left - right);
      const p95 = ordered[18], maximum = ordered[19];
      if (p95 > 16 || maximum > 500)
        throw new Error(`constructed insertion latency exceeded ${p95}/${maximum}`);
      delete globalThis.__constructedInsertionPerformance;
      return { status: 'passed', cycles: durations.length, p95, max: maximum };
    })())JS", "constructed-stylesheet-insertion-performance-second.js");
    require(constructed_insertion_result.find(R"("status":"passed")")
            != std::string::npos,
        "constructed stylesheet insertion performance workload failed: "
            + constructed_insertion_result);
    webscene_engine_metrics constructed_insertion_engine_after{};
    webscene_engine_get_metrics(engine, &constructed_insertion_engine_after);
    webscene_runtime_work_metrics constructed_insertion_work_after{
        sizeof(webscene_runtime_work_metrics)};
    require(webscene_engine_get_runtime_work_metrics(
                engine, &constructed_insertion_work_after) != 0,
        "constructed stylesheet insertion work metrics were unavailable");
    webscene_engine_memory_metrics constructed_insertion_memory_after{
        sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_memory_metrics(
                engine, &constructed_insertion_memory_after) != 0,
        "constructed stylesheet insertion memory metrics were unavailable");
    require(
        constructed_insertion_engine_after.layout_passes
                == constructed_engine_before.layout_passes
            && constructed_insertion_engine_after.published_scenes
                == constructed_engine_before.published_scenes
            && constructed_insertion_work_after.scene_builds
                == constructed_work_before.scene_builds
            && constructed_insertion_memory_after.native_css_rule_count
                == constructed_memory_before.native_css_rule_count
            && constructed_insertion_memory_after.pending_scene_count
                == constructed_memory_before.pending_scene_count,
        "unadopted constructed insertion caused invalidation or publication");
    webscene_engine_get_metrics(engine, &constructed_engine_before);
    require(webscene_engine_get_runtime_work_metrics(
                engine, &constructed_work_before) != 0,
        "constructed stylesheet replacement baseline work metrics were unavailable");
    const auto constructed_performance_result = evaluate(engine, R"JS((() => {
      globalThis.__constructedSheetPerformance = { status: 'pending' };
      try {
        let sheet = new CSSStyleSheet();
        const rules = sheet.cssRules;
        const durations = [];
        let previous = null;
        for (let cycle = 0; cycle < 20; cycle++) {
          const source = Array.from({ length: 64 }, (_, index) =>
            `.constructed-${cycle}-${index} { width: ${index + 1}px; }`).join('\n');
          const started = performance.now();
          sheet.replaceSync(source);
          if (sheet.cssRules !== rules || rules.length !== 64
              || rules[63].selectorText !== `.constructed-${cycle}-63`
              || rules[63].style.width !== '64px'
              || (previous && previous.parentStyleSheet !== null)) {
            throw new Error('constructed replacement state or detachment diverged');
          }
          previous = rules[0];
          durations.push(performance.now() - started);
        }
        const ordered = durations.slice().sort((left, right) => left - right);
        const p95 = ordered[18], maximum = ordered[19];
        if (p95 > 16 || maximum > 500)
          throw new Error(`constructed replacement latency exceeded ${p95}/${maximum}`);
        sheet = null;

        for (let cycle = 0; cycle < 100; cycle++) {
          const lifecycle = new CSSStyleSheet({ media: 'screen' });
          lifecycle.replaceSync(
            `.lifecycle-${cycle} { display: block; width: ${cycle + 1}px; }`
            + ` @media (min-width: 1px) { .nested-${cycle} { height: 1px; } }`);
          const lifecycleRules = lifecycle.cssRules;
          const snapshot = lifecycleRules[0];
          snapshot.style.setProperty('display', 'grid');
          if (snapshot.style.display !== 'grid'
              || snapshot.parentStyleSheet !== lifecycle
              || lifecycleRules.length !== 2) {
            throw new Error('constructed lifecycle declaration mutation diverged');
          }
          lifecycle.replaceSync(`.final-${cycle} { color: rgb(1, 2, 3); }`);
          if (snapshot.parentStyleSheet !== null || lifecycleRules.length !== 1
              || lifecycleRules[0].selectorText !== `.final-${cycle}`) {
            throw new Error('constructed lifecycle replacement/detach diverged');
          }
        }
        globalThis.__constructedSheetPerformance = {
          status: 'passed', cycles: durations.length, lifecycleCycles: 100,
          p95, max: maximum, ruleCount: rules.length
        };
      } catch (error) {
        globalThis.__constructedSheetPerformance = {
          status: 'failed', error: String(error), stack: String(error?.stack || '')
        };
      }
      return globalThis.__constructedSheetPerformance;
    })())JS", "constructed-stylesheet-performance.js");
    require(constructed_performance_result.find(R"("status":"passed")")
            != std::string::npos,
        "constructed stylesheet performance workload failed: "
            + constructed_performance_result);
    webscene_engine_metrics constructed_engine_after{};
    webscene_engine_get_metrics(engine, &constructed_engine_after);
    webscene_runtime_work_metrics constructed_work_after{
        sizeof(webscene_runtime_work_metrics)};
    require(webscene_engine_get_runtime_work_metrics(
                engine, &constructed_work_after) != 0,
        "constructed stylesheet final work metrics were unavailable");
    webscene_engine_memory_metrics constructed_memory_after{
        sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_memory_metrics(
                engine, &constructed_memory_after) != 0,
        "constructed stylesheet final memory metrics were unavailable");
    std::cout << "Constructed stylesheet native work: layout="
              << constructed_engine_before.layout_passes << "/"
              << constructed_engine_after.layout_passes << " scenes="
              << constructed_engine_before.published_scenes << "/"
              << constructed_engine_after.published_scenes << " builds="
              << constructed_work_before.scene_builds << "/"
              << constructed_work_after.scene_builds << " rules="
              << constructed_memory_before.native_css_rule_count << "/"
              << constructed_memory_after.native_css_rule_count << " pending="
              << constructed_memory_before.pending_scene_count << "/"
              << constructed_memory_after.pending_scene_count << " insertion="
              << constructed_insertion_result << " replacement="
              << constructed_performance_result << '\n';
    require(
        constructed_engine_after.layout_passes
                == constructed_engine_before.layout_passes
            && constructed_engine_after.published_scenes
                == constructed_engine_before.published_scenes
            && constructed_work_after.scene_builds
                == constructed_work_before.scene_builds
            && constructed_memory_after.native_css_rule_count
                == constructed_memory_before.native_css_rule_count
            && constructed_memory_after.pending_scene_count
                == constructed_memory_before.pending_scene_count,
        "unadopted constructed stylesheet exceeded control-task invalidation or publication");
    require(webscene_engine_request_low_memory(engine) != 0,
        "constructed stylesheet lifecycle low-memory request was rejected");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    webscene_engine_memory_metrics constructed_memory_collected{
        sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_memory_metrics(
                engine, &constructed_memory_collected) != 0,
        "constructed stylesheet collected memory metrics were unavailable");
    const auto constructed_rss_after = file_system_access_test_peak_rss_bytes();
    require(constructed_memory_collected.v8_used_heap_bytes
            <= constructed_memory_before.v8_used_heap_bytes + 8U * 1024U * 1024U,
        "100 constructed stylesheet cycles retained more than 8 MiB of V8 heap");
    require(constructed_memory_collected.native_dom_node_count
            <= constructed_memory_before.native_dom_node_count + 16U,
        "100 constructed stylesheet cycles retained native declaration scratch nodes");
    require(constructed_rss_before == 0U
            || constructed_rss_after <= constructed_rss_before + 64U * 1024U * 1024U,
        "100 constructed stylesheet cycles increased peak RSS by more than 64 MiB");
    std::cout << "Constructed stylesheet gate: "
              << evaluate(engine, "globalThis.__constructedSheetPerformance",
                  "constructed-stylesheet-performance-metrics.js")
              << " heapBefore=" << constructed_memory_before.v8_used_heap_bytes
              << " heapAfter=" << constructed_memory_collected.v8_used_heap_bytes
              << " rssBefore=" << constructed_rss_before
              << " rssAfter=" << constructed_rss_after << '\n';
    execute_and_wait(engine, R"JS((() => {
      const style = document.createElement('style');
      const target = document.createElement('div');
      style.textContent = Array.from({ length: 64 }, (_, index) =>
        `.replacement-rule-${index} { width: ${index + 1}px; }`).join('\n');
      target.className = 'replacement-rule-63';
      document.head.appendChild(style);
      document.body.appendChild(target);
      if (getComputedStyle(target).width !== '64px')
        throw new Error('replacement performance fixture did not settle');
      globalThis.__replacementPerformanceFixture = { style, target };
    })())JS", "owner-stylesheet-replacement-performance-setup.js");
    acknowledge_latest_scene(
        "owner stylesheet replacement performance fixture had no scene");
    webscene_engine_metrics replacement_engine_before{};
    webscene_engine_get_metrics(engine, &replacement_engine_before);
    webscene_runtime_work_metrics replacement_work_before{
        sizeof(webscene_runtime_work_metrics)};
    require(webscene_engine_get_runtime_work_metrics(
                engine, &replacement_work_before) != 0,
        "owner stylesheet replacement baseline work metrics were unavailable");
    webscene_engine_memory_metrics replacement_memory_before{
        sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_memory_metrics(
                engine, &replacement_memory_before) != 0,
        "owner stylesheet replacement baseline memory metrics were unavailable");
    execute(engine, R"JS((async () => {
      globalThis.__ownerSheetReplacementPerformance = { status: 'pending' };
      try {
        const { style, target } = globalThis.__replacementPerformanceFixture;
        const sheet = style.sheet;
        const rules = sheet.cssRules;
        const first = rules[0], last = rules[63];
        const authoredText = style.textContent;
        const durations = [];
        for (let cycle = 0; cycle < 20; cycle++) {
          const started = performance.now();
          let syncError = '';
          try { sheet.replaceSync(`.replacement-${cycle} { width: 1px; }`); }
          catch (error) { syncError = error.name; }
          const asyncError = await sheet.replace(
            `.replacement-${cycle} { width: 2px; }`)
            .then(() => 'none', error => error.name);
          if (syncError !== 'NotAllowedError'
              || asyncError !== 'NotAllowedError') {
            throw new Error(`replacement denial diverged: ${syncError}/${asyncError}`);
          }
          durations.push(performance.now() - started);
        }
        const ordered = durations.slice().sort((left, right) => left - right);
        const p95 = ordered[18], maximum = ordered[19];
        if (p95 > 16 || maximum > 500)
          throw new Error(`replacement denial latency exceeded ${p95}/${maximum}`);
        if (sheet.cssRules !== rules || rules.length !== 64
            || rules[0] !== first || rules[63] !== last
            || style.textContent !== authoredText
            || getComputedStyle(target).width !== '64px') {
          throw new Error('denied replacement changed sheet or computed style state');
        }
        globalThis.__ownerSheetReplacementPerformance = {
          status: 'passed', cycles: durations.length, ruleCount: rules.length,
          p95, max: maximum, authoredTextStable: style.textContent === authoredText
        };
      } catch (error) {
        globalThis.__ownerSheetReplacementPerformance = {
          status: 'failed', error: String(error), stack: String(error?.stack || '')
        };
      }
    })())JS", "owner-stylesheet-replacement-performance.js");
    const auto replacement_performance_status = evaluate_until_equals(
        engine,
        "globalThis.__ownerSheetReplacementPerformance?.status ?? 'pending'",
        "owner-stylesheet-replacement-performance-status.js",
        R"("passed")",
        500);
    require(replacement_performance_status == R"("passed")",
        "owner stylesheet replacement performance workload failed: "
            + evaluate(engine, "globalThis.__ownerSheetReplacementPerformance",
                "owner-stylesheet-replacement-performance-result.js"));
    const auto replacement_metrics = evaluate(
        engine,
        "globalThis.__ownerSheetReplacementPerformance",
        "owner-stylesheet-replacement-performance-metrics.js");
    webscene_engine_metrics replacement_engine_after{};
    webscene_engine_get_metrics(engine, &replacement_engine_after);
    webscene_runtime_work_metrics replacement_work_after{
        sizeof(webscene_runtime_work_metrics)};
    require(webscene_engine_get_runtime_work_metrics(
                engine, &replacement_work_after) != 0,
        "owner stylesheet replacement final work metrics were unavailable");
    webscene_engine_memory_metrics replacement_memory_after{
        sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_memory_metrics(
                engine, &replacement_memory_after) != 0,
        "owner stylesheet replacement final memory metrics were unavailable");
    require(
        replacement_engine_after.layout_passes
                == replacement_engine_before.layout_passes
            && replacement_engine_after.published_scenes
                == replacement_engine_before.published_scenes
            && replacement_work_after.scene_builds
                == replacement_work_before.scene_builds
            && replacement_memory_after.native_css_rule_count
                == replacement_memory_before.native_css_rule_count
            && replacement_memory_after.pending_scene_count
                == replacement_memory_before.pending_scene_count,
        "denied stylesheet replacement caused invalidation, publication, or native rule churn");
    execute_and_wait(engine, R"JS((() => {
      const fixture = globalThis.__replacementPerformanceFixture;
      fixture.style.remove();
      fixture.target.remove();
      delete globalThis.__replacementPerformanceFixture;
    })())JS", "owner-stylesheet-replacement-performance-cleanup.js");
    webscene_engine_metrics engine_before{};
    webscene_engine_get_metrics(engine, &engine_before);
    webscene_runtime_work_metrics work_before{sizeof(webscene_runtime_work_metrics)};
    require(webscene_engine_get_runtime_work_metrics(engine, &work_before) != 0,
        "stylesheet mutation baseline work metrics were unavailable");
    execute_and_wait(engine, "globalThis.__stylesheetMutationBaseline = true",
        "stylesheet-rule-mutation-baseline.js");
    webscene_engine_memory_metrics memory_before{sizeof(webscene_engine_memory_metrics)};
    for (auto attempt = 0; attempt < 500; ++attempt) {
        require(webscene_engine_get_memory_metrics(engine, &memory_before) != 0,
            "stylesheet mutation baseline memory metrics were unavailable");
        if (memory_before.v8_used_heap_bytes != 0U
            && memory_before.native_dom_node_count != 0U) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(memory_before.v8_used_heap_bytes != 0U,
        "stylesheet mutation baseline heap snapshot was unavailable");
    const auto rss_before = file_system_access_test_peak_rss_bytes();
    execute(engine, R"JS((() => {
      const style = document.createElement('style');
      globalThis.__themeConnections = 0;
      customElements.define('x-theme-target', class extends HTMLElement {
        connectedCallback() { globalThis.__themeConnections++; }
      });
      const target = document.createElement('x-theme-target');
      target.className = 'theme-target theme-rule-255';
      style.textContent = '.theme-seed { color: rgb(1, 2, 3); }';
      document.head.appendChild(style);
      document.body.appendChild(target);
      const authoredText = style.textContent;
      const observer = new MutationObserver(() => {});
      observer.observe(style, { childList: true, characterData: true, subtree: true });
      globalThis.__stylesheetMutationResult = null;
      setTimeout(async () => {
        try {
          const sheet = style.sheet;
          const rules = sheet.cssRules;
          if (style.sheet !== sheet || rules !== sheet.cssRules) {
            throw new Error('stylesheet or rule-list identity changed');
          }
          const durations = [];
          for (let cycle = 0; cycle < 20; cycle++) {
            const started = performance.now();
            while (rules.length) sheet.deleteRule(0);
            for (let index = 0; index < 256; index++) {
              const red = (cycle + index) & 255;
              sheet.insertRule(`.theme-rule-${index} { color: rgb(${red}, 7, 9); }`, 0);
            }
            if (rules.length !== 256
                || !rules[0].cssText.includes('.theme-rule-255')
                || style.textContent !== authoredText) {
              throw new Error('synchronous CSSOM state mismatch');
            }
            const expected = `rgb(${(cycle + 255) & 255}, 7, 9)`;
            if (getComputedStyle(target).color !== expected) {
              throw new Error('computed style did not observe final rule set');
            }
            if (!target.matches('.theme-target.theme-rule-255')
                || target.getBoundingClientRect().width < 0) {
              throw new Error('selector or layout read failed');
            }
            durations.push(performance.now() - started);
          }
          const ordered = durations.slice().sort((left, right) => left - right);
          const p95 = ordered[18], maximum = ordered[19];
          if (p95 > 16 || maximum > 500) {
            throw new Error(`latency exceeded p95=${p95} max=${maximum}`);
          }
          while (rules.length) sheet.deleteRule(0);
          const mediaIndex = sheet.insertRule('@media (min-width: 1px) {}', 0);
          const mediaRule = rules[mediaIndex];
          const mediaRules = mediaRule.cssRules;
          const nestedUpdateDurations = [];
          const nestedLookupDurations = [];
          for (let cycle = 0; cycle < 20; cycle++) {
            let started = performance.now();
            while (mediaRules.length) mediaRule.deleteRule(0);
            for (let index = 0; index < 64; index++) {
              const red = (cycle + index) & 255;
              mediaRule.insertRule(
                `.nested-theme-${index} { color: rgb(${red}, 11, 13); }`, 0);
            }
            target.className = 'theme-target nested-theme-63';
            const expected = `rgb(${(cycle + 63) & 255}, 11, 13)`;
            if (mediaRules.length !== 64
                || getComputedStyle(target).color !== expected) {
              throw new Error('nested media mutation was not synchronously observable');
            }
            nestedUpdateDurations.push(performance.now() - started);
            started = performance.now();
            let checksum = 0;
            for (let index = 0; index < 4096; index++) {
              checksum += mediaRules[index & 63].cssText.length;
            }
            if (checksum === 0 || mediaRule.cssRules !== mediaRules) {
              throw new Error('nested media lookup was not live or stable');
            }
            nestedLookupDurations.push(performance.now() - started);
          }
          const nestedUpdates = nestedUpdateDurations.slice().sort((a, b) => a - b);
          const nestedLookups = nestedLookupDurations.slice().sort((a, b) => a - b);
          const nestedUpdateP95 = nestedUpdates[18];
          const nestedUpdateMax = nestedUpdates[19];
          const nestedLookupP95 = nestedLookups[18];
          const nestedLookupMax = nestedLookups[19];
          if (nestedUpdateP95 > 16 || nestedUpdateMax > 500
              || nestedLookupP95 > 16 || nestedLookupMax > 500) {
            throw new Error(`nested latency exceeded update=${nestedUpdateP95}/${nestedUpdateMax} lookup=${nestedLookupP95}/${nestedLookupMax}`);
          }
          sheet.deleteRule(mediaIndex);
          const selectorRules = [];
          for (let index = 0; index < 64; index++) {
            const ruleIndex = sheet.insertRule(
              `.selector-before-${index} { width: ${index + 1}px; }`,
              rules.length);
            selectorRules.push(rules[ruleIndex]);
          }
          const selectorMutationDurations = [];
          for (let cycle = 0; cycle < 20; cycle++) {
            const suffix = (cycle & 1) === 0 ? 'after' : 'before';
            target.className = `theme-target selector-${suffix}-63`;
            const started = performance.now();
            for (let index = 0; index < selectorRules.length; index++) {
              selectorRules[index].selectorText = `.selector-${suffix}-${index}`;
            }
            if (getComputedStyle(target).width !== '64px'
                || selectorRules[63].selectorText !== `.selector-${suffix}-63`
                || rules[rules.length - 1] !== selectorRules[63]) {
              throw new Error('selectorText mutation was not synchronously observable');
            }
            selectorMutationDurations.push(performance.now() - started);
          }
          const selectorMutations = selectorMutationDurations.slice()
            .sort((a, b) => a - b);
          const selectorMutationP95 = selectorMutations[18];
          const selectorMutationMax = selectorMutations[19];
          if (selectorMutationP95 > 16 || selectorMutationMax > 500) {
            throw new Error(`selector mutation latency exceeded ${selectorMutationP95}/${selectorMutationMax}`);
          }
          while (rules.length) sheet.deleteRule(0);
          sheet.insertRule('.theme-target { height: 1px; }', 0);
          const mediaListIndex = sheet.insertRule('@media (min-width: 1px) {}', 1);
          const mediaListRule = rules[mediaListIndex];
          const mediaList = mediaListRule.media;
          for (let index = 0; index < 64; index++) {
            mediaListRule.insertRule(
              `.media-list-${index} { height: ${index + 1}px; }`, index);
          }
          target.className = 'theme-target media-list-63';
          const mediaMutationDurations = [];
          for (let cycle = 0; cycle < 20; cycle++) {
            const matches = (cycle & 1) !== 0;
            const started = performance.now();
            mediaList.mediaText = matches
              ? '(min-width:1px)' : '(max-width:1px)';
            if (mediaListRule.media !== mediaList
                || mediaList.mediaText !== (matches
                  ? '(min-width: 1px)' : '(max-width: 1px)')
                || mediaListRule.conditionText !== mediaList.mediaText
                || getComputedStyle(target).height !== (matches ? '64px' : '1px')) {
              throw new Error('MediaList mutation was not synchronously observable');
            }
            mediaMutationDurations.push(performance.now() - started);
          }
          const mediaMutations = mediaMutationDurations.slice()
            .sort((a, b) => a - b);
          const mediaMutationP95 = mediaMutations[18];
          const mediaMutationMax = mediaMutations[19];
          if (mediaMutationP95 > 16 || mediaMutationMax > 500) {
            throw new Error(`MediaList mutation latency exceeded ${mediaMutationP95}/${mediaMutationMax}`);
          }
          while (rules.length) sheet.deleteRule(0);
          sheet.insertRule('.theme-target { width: 1px; }', 0);
          const supportsIndex = sheet.insertRule('@supports selector(:focus-visible) {}', 1);
          const supportsRule = rules[supportsIndex];
          const supportsRules = supportsRule.cssRules;
          const supportsMutationDurations = [];
          target.className = 'theme-target supports-rule-63';
          for (let cycle = 0; cycle < 20; cycle++) {
            const started = performance.now();
            while (supportsRules.length) supportsRule.deleteRule(0);
            for (let index = 0; index < 64; index++) {
              supportsRule.insertRule(
                `.supports-rule-${index} { width: ${index + 1}px; }`, index);
            }
            if (!(supportsRule instanceof CSSSupportsRule)
                || supportsRule.conditionText !== 'selector(:focus-visible)'
                || supportsRule.cssRules !== supportsRules
                || supportsRules.length !== 64
                || getComputedStyle(target).width !== '64px') {
              throw new Error('CSSSupportsRule mutation was not synchronously observable');
            }
            supportsMutationDurations.push(performance.now() - started);
          }
          const supportsMutations = supportsMutationDurations.slice()
            .sort((a, b) => a - b);
          const supportsMutationP95 = supportsMutations[18];
          const supportsMutationMax = supportsMutations[19];
          if (supportsMutationP95 > 16 || supportsMutationMax > 500) {
            throw new Error(`CSSSupportsRule mutation latency exceeded ${supportsMutationP95}/${supportsMutationMax}`);
          }
          while (rules.length) sheet.deleteRule(0);
          const layerIndex = sheet.insertRule('@layer performance {}', 0);
          const layerRule = rules[layerIndex];
          const layerRules = layerRule.cssRules;
          const layerMutationDurations = [];
          target.className = 'layer-rule-63';
          for (let cycle = 0; cycle < 20; cycle++) {
            const started = performance.now();
            while (layerRules.length) layerRule.deleteRule(0);
            for (let index = 0; index < 64; index++) {
              layerRule.insertRule(
                `.layer-rule-${index} { width: ${index + 1}px; }`, index);
            }
            if (!(layerRule instanceof CSSLayerBlockRule)
                || layerRule.name !== 'performance'
                || layerRule.cssRules !== layerRules
                || layerRules.length !== 64
                || getComputedStyle(target).width !== '64px') {
              throw new Error('CSSLayerBlockRule mutation was not synchronously observable');
            }
            layerMutationDurations.push(performance.now() - started);
          }
          const layerMutations = layerMutationDurations.slice()
            .sort((a, b) => a - b);
          const layerMutationP95 = layerMutations[18];
          const layerMutationMax = layerMutations[19];
          if (layerMutationP95 > 16 || layerMutationMax > 500) {
            throw new Error(`CSSLayerBlockRule mutation latency exceeded ${layerMutationP95}/${layerMutationMax}`);
          }
          const layerStatementMutationDurations = [];
          for (let cycle = 0; cycle < 20; cycle++) {
            const started = performance.now();
            while (rules.length) sheet.deleteRule(0);
            for (let index = 0; index < 64; index++) {
              sheet.insertRule(`@layer statement-${cycle}-${index};`, index);
            }
            sheet.insertRule(
              `@layer statement-${cycle}-0 { .layer-statement-target { width: 1px; } }`,
              rules.length);
            sheet.insertRule(
              `@layer statement-${cycle}-63 { .layer-statement-target { width: 64px; } }`,
              rules.length);
            target.className = 'layer-statement-target';
            const firstStatement = rules[0];
            const lastStatement = rules[63];
            if (!(firstStatement instanceof CSSLayerStatementRule)
                || !(lastStatement instanceof CSSLayerStatementRule)
                || firstStatement.nameList.join('') !== `statement-${cycle}-0`
                || lastStatement.nameList.join('') !== `statement-${cycle}-63`
                || !Object.isFrozen(firstStatement.nameList)
                || getComputedStyle(target).width !== '64px') {
              throw new Error('CSSLayerStatementRule mutation was not synchronously observable');
            }
            layerStatementMutationDurations.push(performance.now() - started);
          }
          const layerStatementMutations = layerStatementMutationDurations.slice()
            .sort((a, b) => a - b);
          const layerStatementMutationP95 = layerStatementMutations[18];
          const layerStatementMutationMax = layerStatementMutations[19];
          if (layerStatementMutationP95 > 16 || layerStatementMutationMax > 500) {
            throw new Error(`CSSLayerStatementRule mutation latency exceeded ${layerStatementMutationP95}/${layerStatementMutationMax}`);
          }
          while (rules.length) sheet.deleteRule(0);
          const replacementPromises = [];
          const expectSyncReplacementDenial = (candidate, label) => {
            try { candidate.replaceSync(`.${label} { display: none; }`); }
            catch (error) {
              if (error.name === 'NotAllowedError') return;
              throw error;
            }
            throw new Error(`${label} replaceSync unexpectedly succeeded`);
          };
          const expectAsyncReplacementDenial = (candidate, label) =>
            candidate.replace(`.${label} { display: none; }`).then(
              () => { throw new Error(`${label} replace unexpectedly succeeded`); },
              error => {
                if (error.name !== 'NotAllowedError') throw error;
                return true;
              });
          for (let cycle = 0; cycle < 100; cycle++) {
            const lifecycleStyle = document.createElement('style');
            lifecycleStyle.textContent = '@media (min-width: 1px) {} @supports selector(:focus-visible) {} @layer lifecycle {} @layer lifecycle-first, lifecycle-second;';
            document.head.appendChild(lifecycleStyle);
            const lifecycleSheet = lifecycleStyle.sheet;
            const lifecycleGroup = lifecycleSheet.cssRules[0];
            expectSyncReplacementDenial(
              lifecycleSheet, `lifecycle-attached-${cycle}`);
            const lifecycleMedia = lifecycleGroup.media;
            lifecycleGroup.media = '(max-width:1px)';
            lifecycleMedia.appendMedium('screen');
            lifecycleMedia.deleteMedium('screen');
            lifecycleMedia.mediaText = '(min-width: 1px)';
            if (lifecycleGroup.media !== lifecycleMedia
                || lifecycleGroup.conditionText !== '(min-width: 1px)') {
              throw new Error('lifecycle MediaList identity or condition was lost');
            }
            const lifecycleSupports = lifecycleSheet.cssRules[1];
            const supportsChildIndex = lifecycleSupports.insertRule(
              `.lifecycle-supports-${cycle} { display: grid; }`, 0);
            const supportsChild = lifecycleSupports.cssRules[supportsChildIndex];
            lifecycleSupports.deleteRule(supportsChildIndex);
            if (!(lifecycleSupports instanceof CSSSupportsRule)
                || lifecycleSupports.conditionText !== 'selector(:focus-visible)'
                || supportsChild.parentRule !== null
                || supportsChild.parentStyleSheet !== null) {
              throw new Error('lifecycle supports rule identity or ownership was lost');
            }
            const lifecycleLayer = lifecycleSheet.cssRules[2];
            const layerChildIndex = lifecycleLayer.insertRule(
              `.lifecycle-layer-${cycle} { display: block; }`, 0);
            const layerChild = lifecycleLayer.cssRules[layerChildIndex];
            lifecycleLayer.deleteRule(layerChildIndex);
            if (!(lifecycleLayer instanceof CSSLayerBlockRule)
                || lifecycleLayer.name !== 'lifecycle'
                || layerChild.parentRule !== null
                || layerChild.parentStyleSheet !== null) {
              throw new Error('lifecycle layer rule identity or ownership was lost');
            }
            const lifecycleStatement = lifecycleSheet.cssRules[3];
            const lifecycleStatementNames = lifecycleStatement.nameList;
            lifecycleSheet.deleteRule(3);
            if (!(lifecycleStatement instanceof CSSLayerStatementRule)
                || lifecycleStatementNames.join('|')
                  !== 'lifecycle-first|lifecycle-second'
                || !Object.isFrozen(lifecycleStatementNames)
                || lifecycleStatement.parentRule !== null
                || lifecycleStatement.parentStyleSheet !== null
                || lifecycleStatement.nameList.join('|')
                  !== 'lifecycle-first|lifecycle-second') {
              throw new Error('lifecycle layer statement identity or ownership was lost');
            }
            const childIndex = lifecycleGroup.insertRule(
              `.lifecycle-${cycle} { display: block; }`, 0);
            const child = lifecycleGroup.cssRules[childIndex];
            lifecycleGroup.deleteRule(childIndex);
            if (child.parentRule !== null || child.parentStyleSheet !== null) {
              throw new Error('deleted nested rule retained its parents');
            }
            const selectorIndex = lifecycleStyle.sheet.insertRule(
              `.lifecycle-before-${cycle} { display: block; }`,
              lifecycleStyle.sheet.cssRules.length);
            const selectorRule = lifecycleStyle.sheet.cssRules[selectorIndex];
            selectorRule.selectorText = `.lifecycle-after-${cycle}`;
            if (selectorRule.selectorText !== `.lifecycle-after-${cycle}`) {
              throw new Error('lifecycle selectorText mutation was not retained');
            }
            lifecycleStyle.sheet.deleteRule(selectorIndex);
            if (selectorRule.parentStyleSheet !== null) {
              throw new Error('deleted selector rule retained its stylesheet');
            }
            lifecycleStyle.remove();
            document.head.appendChild(lifecycleStyle);
            lifecycleStyle.remove();
            replacementPromises.push(expectAsyncReplacementDenial(
              lifecycleSheet, `lifecycle-detached-${cycle}`));

            const realm = document.createElement('iframe');
            document.body.appendChild(realm);
            const realmDocument = realm.contentDocument;
            const realmStyle = realmDocument.createElement('style');
            const realmTarget = realmDocument.createElement('div');
            realmStyle.textContent = `@media (min-width: 1px) {
              @supports selector(:focus-visible) {
                @layer realm {
                  @layer iframe-first, iframe-second;
                  .realm-before-${cycle} { height: 7px; }
                }
              }
            }`;
            realmTarget.className = `realm-after-${cycle}`;
            realmDocument.head.appendChild(realmStyle);
            realmDocument.body.appendChild(realmTarget);
            const realmGroup = realmStyle.sheet.cssRules[0];
            const realmSheet = realmStyle.sheet;
            const realmMedia = realmGroup.media;
            const realmSupports = realmGroup.cssRules[0];
            const realmLayer = realmSupports.cssRules[0];
            const realmStatement = realmLayer.cssRules[0];
            const realmRule = realmLayer.cssRules[1];
            expectSyncReplacementDenial(realmSheet, `realm-attached-${cycle}`);
            realmGroup.media = '(max-width: 1px)';
            realmMedia.appendMedium('screen');
            realmRule.selectorText = `.realm-after-${cycle}`;
            if (!(realmMedia instanceof realm.contentWindow.MediaList)
                || realmGroup.media !== realmMedia
                || !(realmSupports instanceof realm.contentWindow.CSSSupportsRule)
                || realmSupports.parentRule !== realmGroup
                || !(realmLayer instanceof realm.contentWindow.CSSLayerBlockRule)
                || realmLayer.parentRule !== realmSupports
                || realmLayer.name !== 'realm'
                || !(realmStatement
                  instanceof realm.contentWindow.CSSLayerStatementRule)
                || realmStatement.nameList.join('|')
                  !== 'iframe-first|iframe-second'
                || realmStatement.parentRule !== realmLayer
                || !(realmRule instanceof realm.contentWindow.CSSStyleRule)
                || realmRule.selectorText !== `.realm-after-${cycle}`
                || realm.contentWindow.getComputedStyle(realmTarget).height !== '7px') {
              throw new Error('iframe MediaList, supports, layer, or selector mutation lost realm ownership');
            }
            realm.remove();
            replacementPromises.push(expectAsyncReplacementDenial(
              realmSheet, `realm-detached-${cycle}`));
          }
          await Promise.all(replacementPromises);
          target.className = 'theme-target';
          sheet.insertRule('.theme-target { display: none; }', 0);
          const order = ['sync'];
          queueMicrotask(() => {
            order.push('microtask');
            if (rules.length !== 1 || style.textContent !== authoredText) {
              throw new Error('microtask observed stale CSSOM or changed DOM text');
            }
          });
          setTimeout(() => {
            try {
              order.push('timer');
              if (getComputedStyle(target).display !== 'none'
                  || order.join(',') !== 'sync,microtask,timer'
                  || globalThis.__themeConnections !== 1
                  || observer.takeRecords().length !== 0) {
                throw new Error('checkpoint, observer, or custom-element ordering failed');
              }
              observer.disconnect();
              style.remove();
              target.remove();
              globalThis.__stylesheetMutationResult = {
                status: 'passed', cycles: durations.length,
                p95, max: maximum, rules: style.sheet === null ? 0 : -1,
                nestedUpdateP95, nestedUpdateMax,
                nestedLookupP95, nestedLookupMax,
                selectorMutationP95, selectorMutationMax,
                selectorMutations: selectorMutationDurations.length,
                mediaMutationP95, mediaMutationMax,
                mediaMutations: mediaMutationDurations.length,
                supportsMutationP95, supportsMutationMax,
                supportsMutations: supportsMutationDurations.length,
                layerMutationP95, layerMutationMax,
                layerMutations: layerMutationDurations.length,
                layerStatementMutationP95, layerStatementMutationMax,
                layerStatementMutations: layerStatementMutationDurations.length,
                replacementRejections: replacementPromises.length,
                lifecycleCycles: 100,
                realmLifecycleCycles: 100,
                authoredTextStable: style.textContent === authoredText,
                order
              };
            } catch (error) {
              globalThis.__stylesheetMutationResult = {
                status: 'failed', error: String(error)
              };
            }
          }, 0);
        } catch (error) {
          globalThis.__stylesheetMutationResult = {
            status: 'failed', error: String(error)
          };
        }
      }, 0);
    })())JS", "stylesheet-rule-mutation-setup.js");
    const auto result = evaluate_until_equals(
        engine,
        "globalThis.__stylesheetMutationResult?.status === 'passed' ? 'passed' : "
        "globalThis.__stylesheetMutationResult?.status ?? 'pending'",
        "stylesheet-rule-mutation-status.js",
        R"("passed")",
        500);
    require(result == R"("passed")",
        "stylesheet mutation performance workload failed: "
            + evaluate(engine, "globalThis.__stylesheetMutationResult",
                "stylesheet-rule-mutation-failure.js"));
    const auto metrics = evaluate(
        engine,
        "globalThis.__stylesheetMutationResult",
        "stylesheet-rule-mutation-result.js");
    require(webscene_engine_request_low_memory(engine) != 0,
        "stylesheet mutation low-memory request was rejected");
    webscene_engine_memory_metrics memory_after{sizeof(webscene_engine_memory_metrics)};
    for (auto attempt = 0; attempt < 500; ++attempt) {
        require(webscene_engine_get_memory_metrics(engine, &memory_after) != 0,
            "stylesheet mutation final memory metrics were unavailable");
        if (memory_after.low_memory_notifications
                > memory_before.low_memory_notifications
            && memory_after.native_css_rule_count
                <= memory_before.native_css_rule_count
            && memory_after.native_dom_node_count
                <= memory_before.native_dom_node_count + 8U
            && memory_after.native_wrapper_handle_count
                <= memory_before.native_wrapper_handle_count + 8U) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    webscene_engine_metrics engine_after{};
    webscene_engine_get_metrics(engine, &engine_after);
    webscene_runtime_work_metrics work_after{sizeof(webscene_runtime_work_metrics)};
    require(webscene_engine_get_runtime_work_metrics(engine, &work_after) != 0,
        "stylesheet mutation final work metrics were unavailable");
    const auto rss_after = file_system_access_test_peak_rss_bytes();
    const auto lifecycle_bounded = memory_after.low_memory_notifications
            > memory_before.low_memory_notifications
        && memory_after.v8_used_heap_bytes
            <= memory_before.v8_used_heap_bytes + 8U * 1024U * 1024U
        && memory_after.native_css_rule_count == memory_before.native_css_rule_count
        && memory_after.native_dom_node_count
            <= memory_before.native_dom_node_count + 8U
        && memory_after.native_wrapper_handle_count
            <= memory_before.native_wrapper_handle_count + 8U;
    if (!lifecycle_bounded) {
        fail("stylesheet mutation lifecycle retained heap, rules, nodes, or wrappers: "
            + std::to_string(memory_before.low_memory_notifications) + "/"
            + std::to_string(memory_after.low_memory_notifications) + " heap="
            + std::to_string(memory_before.v8_used_heap_bytes) + "/"
            + std::to_string(memory_after.v8_used_heap_bytes) + " rules="
            + std::to_string(memory_before.native_css_rule_count) + "/"
            + std::to_string(memory_after.native_css_rule_count) + " nodes="
            + std::to_string(memory_before.native_dom_node_count) + "/"
            + std::to_string(memory_after.native_dom_node_count) + " wrappers="
            + std::to_string(memory_before.native_wrapper_handle_count) + "/"
            + std::to_string(memory_after.native_wrapper_handle_count));
    }
    require(rss_before == 0U || rss_after <= rss_before + 64U * 1024U * 1024U,
        "stylesheet mutation workload exceeded the 64 MiB peak RSS budget");
    // The established cold lifecycle allowance is 225 passes. Each of the 20
    // layer-statement cycles performs one synchronous computed-style read.
    if (!(engine_after.layout_passes <= engine_before.layout_passes + 245U
            && engine_after.published_scenes <= engine_before.published_scenes + 2U
            && memory_after.pending_scene_count <= 2U
            && work_after.timers_fired >= work_before.timers_fired + 2U)) {
        fail("stylesheet mutation workload exceeded layout, scene, queue, or task bounds: layout="
            + std::to_string(engine_after.layout_passes - engine_before.layout_passes)
            + " scenes="
            + std::to_string(engine_after.published_scenes - engine_before.published_scenes)
            + " pending=" + std::to_string(memory_after.pending_scene_count)
            + " timers="
            + std::to_string(work_after.timers_fired - work_before.timers_fired));
    }
    std::cout << "stylesheet-mutation-profile=" << metrics
              << " ownerReplacementProfile=" << replacement_metrics
              << " layoutDelta=" << (engine_after.layout_passes - engine_before.layout_passes)
              << " sceneDelta=" << (engine_after.published_scenes - engine_before.published_scenes)
              << " heapBefore=" << memory_before.v8_used_heap_bytes
              << " heapAfter=" << memory_after.v8_used_heap_bytes
              << " rssBefore=" << rss_before << " rssAfter=" << rss_after << '\n';
    webscene_engine_destroy(engine);
}

void test_node_has_child_nodes_contract_and_performance(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          const parent = document.createElement('div');
          const child = document.createElement('span');
          const text = document.createTextNode('leaf');
          const fragment = document.createDocumentFragment();
          const initial = [
            parent.hasChildNodes(),
            child.hasChildNodes(),
            text.hasChildNodes(),
            fragment.hasChildNodes()
          ];
          fragment.appendChild(child);
          const fragmentWithChild = fragment.hasChildNodes();
          parent.appendChild(fragment);
          const afterAppend = [
            parent.hasChildNodes(),
            child.hasChildNodes(),
            fragment.hasChildNodes()
          ];
          parent.removeChild(child);
          const afterRemove = parent.hasChildNodes();

          parent.innerHTML = '<i></i>'.repeat(4096);
          const started = performance.now();
          let truthyCalls = 0;
          for (let index = 0; index < 4096; ++index) {
            if (parent.hasChildNodes()) ++truthyCalls;
          }
          const elapsed = performance.now() - started;

          let invalidReceiver = false;
          try { Node.prototype.hasChildNodes.call({}); } catch (error) {
            invalidReceiver = error instanceof TypeError;
          }
          return {
            initial,
            fragmentWithChild,
            afterAppend,
            afterRemove,
            ownsMethod: Object.prototype.hasOwnProperty.call(
              Node.prototype, 'hasChildNodes'),
            length: Node.prototype.hasChildNodes.length,
            invalidReceiver,
            truthyCalls,
            underFiveSeconds: elapsed < 5000
          };
        })()
    )JS", "native-node-has-child-nodes.js");
    require(
        result == R"JSON({"initial":[false,false,false,false],"fragmentWithChild":true,"afterAppend":[true,false,false],"afterRemove":false,"ownsMethod":true,"length":0,"invalidReceiver":true,"truthyCalls":4096,"underFiveSeconds":true})JSON",
        "Node.hasChildNodes contract or constant-time performance regressed: " + result);
}

void test_dom_token_list_collection_performance_gate()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "DOMTokenList performance engine creation failed");

    const auto acknowledge_latest_scene = [&](std::string_view failure) {
        const webscene_scene_view* scene = nullptr;
        for (auto attempt = 0; attempt < 100 && scene == nullptr; ++attempt) {
            scene = webscene_engine_acquire_latest_scene(engine);
            if (scene == nullptr) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
        require(scene != nullptr, failure);
        require(webscene_scene_acknowledge(scene) != 0,
            "DOMTokenList performance scene acknowledgement failed");
        webscene_scene_release(scene);
    };

    acknowledge_latest_scene("DOMTokenList performance fixture had no initial scene");
    webscene_engine_metrics before_setup{};
    webscene_engine_get_metrics(engine, &before_setup);
    execute_and_wait(engine, R"JS(
        document.body.innerHTML = '<div id="tokens" class="alpha beta gamma delta"></div>'
    )JS", "native-dom-token-list-performance-setup.js");
    for (auto attempt = 0; attempt < 100; ++attempt) {
        webscene_engine_metrics current{};
        webscene_engine_get_metrics(engine, &current);
        if (current.published_scenes > before_setup.published_scenes) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    acknowledge_latest_scene("DOMTokenList performance setup scene was not published");

    webscene_engine_metrics baseline{};
    webscene_engine_get_metrics(engine, &baseline);
    const auto read_result = evaluate(engine, R"JS(
        (() => {
          const tokens = document.getElementById('tokens').classList;
          let checksum = 0;
          const started = performance.now();
          for (let index = 0; index < 100000; ++index) {
            checksum += tokens.length;
            checksum += tokens.item(index & 3).length;
            checksum += tokens[index & 3].length;
          }
          return `${checksum}|${performance.now() - started}`;
        })()
    )JS", "native-dom-token-list-read-performance.js");
    const auto read_payload = read_result.size() >= 2
            && read_result.front() == '"' && read_result.back() == '"'
        ? read_result.substr(1, read_result.size() - 2)
        : read_result;
    const auto read_separator = read_payload.find('|');
    require(read_separator != std::string::npos
            && read_payload.substr(0, read_separator) == "1350000",
        "DOMTokenList read performance checksum regressed: " + read_result);
    const auto read_milliseconds = std::stod(read_payload.substr(read_separator + 1));
    require(read_milliseconds < 2000.0,
        "100,000 DOMTokenList collection reads exceeded two seconds");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    webscene_engine_metrics after_read{};
    webscene_engine_get_metrics(engine, &after_read);
    require(after_read.published_scenes == baseline.published_scenes,
        "DOMTokenList collection reads published an unchanged scene");
    require(after_read.layout_passes == baseline.layout_passes,
        "DOMTokenList collection reads forced layout");

    const auto mutation_result = evaluate(engine, R"JS(
        (() => {
          const tokens = document.getElementById('tokens').classList;
          const started = performance.now();
          for (let index = 0; index < 10000; ++index) {
            if ((index & 1) === 0) tokens.replace('alpha', 'replacement');
            else tokens.replace('replacement', 'alpha');
          }
          return `${tokens.value}|${performance.now() - started}`;
        })()
    )JS", "native-dom-token-list-mutation-performance.js");
    const auto mutation_payload = mutation_result.size() >= 2
            && mutation_result.front() == '"' && mutation_result.back() == '"'
        ? mutation_result.substr(1, mutation_result.size() - 2)
        : mutation_result;
    const auto mutation_separator = mutation_payload.find('|');
    require(mutation_separator != std::string::npos
            && mutation_payload.substr(0, mutation_separator)
                == "alpha beta gamma delta",
        "DOMTokenList mutation performance result regressed: " + mutation_result);
    const auto mutation_milliseconds = std::stod(
        mutation_payload.substr(mutation_separator + 1));
    require(mutation_milliseconds < 5000.0,
        "10,000 DOMTokenList replacements exceeded five seconds");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    webscene_engine_metrics after_mutation{};
    webscene_engine_get_metrics(engine, &after_mutation);
    require(after_mutation.published_scenes <= baseline.published_scenes + 1U,
        "DOMTokenList mutation task published more than one coalesced scene");
    require(after_mutation.layout_passes <= baseline.layout_passes + 1U,
        "DOMTokenList mutation task forced repeated layout passes");
    std::cout << "DOMTokenList gate: reads=100000 readMs=" << read_milliseconds
              << " mutations=10000 mutationMs=" << mutation_milliseconds
              << " publishedDelta="
              << (after_mutation.published_scenes - baseline.published_scenes)
              << " layoutDelta="
              << (after_mutation.layout_passes - baseline.layout_passes) << '\n';
    webscene_engine_destroy(engine);
}

void test_iframe_sandbox_dom_token_list_security_and_lifecycle_gate()
{
    auto engine = std::unique_ptr<webscene_engine, decltype(&webscene_engine_destroy)>{
        webscene_engine_create(0), &webscene_engine_destroy};
    require(engine != nullptr, "iframe sandbox engine creation failed");

    webscene_engine_memory_metrics baseline{sizeof(webscene_engine_memory_metrics)};
    require(webscene_engine_get_memory_metrics(engine.get(), &baseline) != 0,
        "iframe sandbox baseline memory metrics were unavailable");
    const auto started = std::chrono::steady_clock::now();
    const auto result = evaluate(engine.get(), R"JS(
        (() => {
          const frame = document.createElement('iframe');
          const tokens = frame.sandbox;
          frame.sandbox.add(
            'allow-scripts', 'allow-same-origin', 'allow-forms',
            'allow-pointer-lock', 'allow-downloads');
          const codeOssShape = [
            frame instanceof HTMLIFrameElement,
            Object.prototype.toString.call(tokens),
            tokens === frame.sandbox,
            frame.getAttribute('sandbox'),
            [...tokens].join(','),
            tokens.supports('allow-scripts'),
            !tokens.supports('allow-webscene-bypass'),
            !('sandbox' in document.createElement('div'))
          ];

          frame.setAttribute('sandbox', 'allow-scripts allow-forms allow-scripts');
          const directAttributeIsLive = tokens.length === 2
            && tokens[0] === 'allow-scripts' && tokens.item(1) === 'allow-forms';
          tokens.replace('allow-forms', 'allow-downloads');
          tokens.value = 'allow-pointer-lock  allow-same-origin';
          frame.sandbox = 'allow-scripts allow-same-origin';
          const putForwardsIsLive = tokens === frame.sandbox
            && tokens.contains('allow-same-origin');
          frame.removeAttribute('sandbox');
          const removalIsLive = tokens.length === 0 && tokens.value === '';
          tokens.add('allow-scripts');
          const mutationRecreatesAttribute = frame.getAttribute('sandbox') === 'allow-scripts';

          let atomicInvalidMutation = false;
          frame.sandbox.value = 'allow-scripts';
          try { frame.sandbox.add('allow-forms', ''); } catch (error) {
            atomicInvalidMutation = error.name === 'SyntaxError'
              && frame.sandbox.value === 'allow-scripts';
          }
          let classSupportsTypeError = false;
          try { document.body.classList.supports('x'); } catch (error) {
            classSupportsTypeError = error instanceof TypeError;
          }

          const opaque = document.createElement('iframe');
          opaque.setAttribute('sandbox', '');
          document.body.appendChild(opaque);
          const opaqueDocumentDenied = opaque.contentDocument === null;
          opaque.remove();

          const cycleStarted = performance.now();
          let cycleChecksum = 0;
          for (let index = 0; index < 100; ++index) {
            const candidate = document.createElement('iframe');
            const retainedList = candidate.sandbox;
            retainedList.add('allow-scripts', 'allow-same-origin');
            document.body.appendChild(candidate);
            cycleChecksum += retainedList.length;
            candidate.remove();
          }
          return {
            codeOssShape,
            directAttributeIsLive,
            putForwardsIsLive,
            removalIsLive,
            mutationRecreatesAttribute,
            atomicInvalidMutation,
            classSupportsTypeError,
            opaqueDocumentDenied,
            cycleChecksum,
            cyclesUnderFiveSeconds: performance.now() - cycleStarted < 5000
          };
        })()
    )JS", "native-iframe-sandbox-dom-token-list.js");
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    require(
        result == R"JSON({"codeOssShape":[true,"[object DOMTokenList]",true,"allow-scripts allow-same-origin allow-forms allow-pointer-lock allow-downloads","allow-scripts,allow-same-origin,allow-forms,allow-pointer-lock,allow-downloads",true,true,true],"directAttributeIsLive":true,"putForwardsIsLive":true,"removalIsLive":true,"mutationRecreatesAttribute":true,"atomicInvalidMutation":true,"classSupportsTypeError":true,"opaqueDocumentDenied":true,"cycleChecksum":200,"cyclesUnderFiveSeconds":true})JSON",
        "iframe sandbox DOMTokenList/security contract regressed: " + result);
    execute(engine.get(), R"JS(
        (() => {
          const allowed = document.createElement('iframe');
          allowed.sandbox.add('allow-same-origin', 'allow-scripts');
          document.body.appendChild(allowed);
          const allowedDocument = allowed.contentDocument;
          allowedDocument.open();
          allowedDocument.write(
            '<html><body><script>window.__sandboxScriptRan = true;<\/script></body></html>');
          allowedDocument.close();
          globalThis.__sandboxAllowedFrame = allowed;
        })()
    )JS", "native-iframe-sandbox-allowed-script-setup.js");
    auto script_result = std::string{};
    for (auto attempt = 0; attempt < 250; ++attempt) {
        script_result = evaluate(engine.get(),
            "__sandboxAllowedFrame.contentWindow.__sandboxScriptRan === true",
            "native-iframe-sandbox-allowed-script-result.js");
        if (script_result == "true") break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(script_result == "true",
        "iframe sandbox allow-scripts permission regressed: " + script_result);
    execute_and_wait(engine.get(), R"JS(
        __sandboxAllowedFrame.remove();
        delete globalThis.__sandboxAllowedFrame;
        const blocked = document.createElement('iframe');
        blocked.sandbox.add('allow-same-origin');
        document.body.appendChild(blocked);
        const blockedDocument = blocked.contentDocument;
        blockedDocument.open();
        blockedDocument.write(
          '<html><body><script>window.__sandboxScriptRan = true;<\/script></body></html>');
        blockedDocument.close();
        globalThis.__sandboxBlockedFrame = blocked;
    )JS", "native-iframe-sandbox-blocked-script-setup.js");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    script_result = evaluate(engine.get(),
        "__sandboxBlockedFrame.contentWindow.__sandboxScriptRan === true",
        "native-iframe-sandbox-blocked-script-result.js");
    require(script_result == "false",
        "iframe sandbox executed script without allow-scripts: " + script_result);
    execute_and_wait(engine.get(), R"JS(
        __sandboxBlockedFrame.remove();
        delete globalThis.__sandboxBlockedFrame;
    )JS", "native-iframe-sandbox-script-cleanup.js");
    require(elapsed < 5.0,
        "100 iframe sandbox create/attach/detach cycles exceeded five seconds");

    require(webscene_engine_request_low_memory(engine.get()) != 0,
        "iframe sandbox low-memory request was rejected");
    webscene_engine_memory_metrics after{sizeof(webscene_engine_memory_metrics)};
    for (auto attempt = 0; attempt < 500; ++attempt) {
        require(webscene_engine_get_memory_metrics(engine.get(), &after) != 0,
            "iframe sandbox final memory metrics were unavailable");
        if (after.low_memory_notifications > baseline.low_memory_notifications
            && after.native_wrapper_handle_count
                <= baseline.native_wrapper_handle_count + 256U) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(after.low_memory_notifications > baseline.low_memory_notifications,
        "iframe sandbox low-memory collection did not complete");
    std::cout << "Iframe sandbox gate: cycles=100 elapsed=" << elapsed
              << "s heapBefore=" << baseline.v8_used_heap_bytes
              << " heapAfter=" << after.v8_used_heap_bytes
              << " wrappersBefore=" << baseline.native_wrapper_handle_count
              << " wrappersAfter=" << after.native_wrapper_handle_count
              << " wrapperBytesBefore=" << baseline.native_wrapper_storage_bytes
              << " wrapperBytesAfter=" << after.native_wrapper_storage_bytes << '\n';
    require(after.v8_used_heap_bytes
            <= baseline.v8_used_heap_bytes + 8U * 1024U * 1024U,
        "100 iframe sandbox cycles retained more than 8 MiB of V8 heap");
    require(after.native_wrapper_handle_count
            <= baseline.native_wrapper_handle_count + 256U,
        "100 iframe sandbox cycles retained more than 256 weak native wrapper handles");
    require(after.native_wrapper_storage_bytes
            <= baseline.native_wrapper_storage_bytes + 16U * 1024U,
        "100 iframe sandbox cycles retained more than 16 KiB of wrapper registry storage");
}

void test_animation_frame_dispatch_is_attributed()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "animation-frame metrics engine creation failed");
    webscene_animation_frame_metrics undersized{};
    require(
        webscene_engine_get_animation_frame_metrics(engine, &undersized) == 0,
        "animation-frame metrics accepted an undersized ABI structure");
    webscene_animation_frame_metrics before{
        sizeof(webscene_animation_frame_metrics)};
    require(
        webscene_engine_get_animation_frame_metrics(engine, &before) != 0,
        "animation-frame metrics ABI was rejected");

    const auto consumed_before = consumed_input_count(engine);
    animation_frame(engine, 12'345.5, 103U);
    wait_for_consumed_inputs(
        engine,
        consumed_before + 1U,
        "attributed animation frame was not consumed");

    webscene_animation_frame_metrics after{
        sizeof(webscene_animation_frame_metrics)};
    require(
        webscene_engine_get_animation_frame_metrics(engine, &after) != 0,
        "animation-frame metrics were unavailable after dispatch");
    require(
        after.dispatched_frames == before.dispatched_frames + 1U
            && after.total_dispatch_nanoseconds
                >= before.total_dispatch_nanoseconds
                    + after.last_dispatch_nanoseconds
            && after.last_dispatch_nanoseconds > 0U
            && after.maximum_dispatch_nanoseconds
                >= after.last_dispatch_nanoseconds
            && after.last_timestamp_microseconds == 12'345'500U,
        "animation-frame timing or timestamp attribution was inconsistent");
    webscene_engine_destroy(engine);
}

void test_runtime_work_is_attributed()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "runtime-work metrics engine creation failed");
    webscene_runtime_work_metrics undersized{};
    require(
        webscene_engine_get_runtime_work_metrics(engine, &undersized) == 0,
        "runtime-work metrics accepted an undersized ABI structure");
    webscene_runtime_work_metrics before{
        sizeof(webscene_runtime_work_metrics)};
    execute_and_wait(
        engine,
        R"JS(
          setTimeout(() => { globalThis.__disabledMetricsTimer = true; }, 0);
          requestAnimationFrame(() => { globalThis.__disabledMetricsFrame = true; });
        )JS",
        "disabled-runtime-work-metrics.js");
    animation_frame_and_wait(engine, 1'000.0, 103U);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    require(
        webscene_engine_get_runtime_work_metrics(engine, &before) != 0
            && before.timers_scheduled == 0U
            && before.timers_fired == 0U
            && before.animation_frames_requested == 0U
            && before.animation_frames_invoked == 0U
            && before.microtask_checkpoints == 0U
            && before.worker_waits == 0U
            && before.scene_builds == 0U
            && before.arbitrary_evaluation_calls == 0U,
        "disabled Release runtime work path updated diagnostic counters");
    require(
        webscene_engine_set_runtime_work_metrics_enabled(engine, 1U) != 0
            && webscene_engine_get_runtime_work_metrics(engine, &before) != 0,
        "runtime-work metrics ABI was rejected");

    execute_and_wait(
        engine,
        R"JS(
          globalThis.__runtimeWorkTimer = 0;
          globalThis.__runtimeWorkFrame = 0;
          const cancelledTimer = setTimeout(() => {}, 1000);
          clearTimeout(cancelledTimer);
          setTimeout(() => { __runtimeWorkTimer++; }, 0);
          const cancelledFrame = requestAnimationFrame(() => {});
          cancelAnimationFrame(cancelledFrame);
          requestAnimationFrame(() => { __runtimeWorkFrame++; });
        )JS",
        "runtime-work-metrics.js");
    animation_frame(engine, 1'234.5, 104U);

    webscene_runtime_work_metrics after{
        sizeof(webscene_runtime_work_metrics)};
    for (auto attempt = 0; attempt < 250; ++attempt) {
        require(
            webscene_engine_get_runtime_work_metrics(engine, &after) != 0,
            "runtime-work metrics became unavailable");
        if (after.timers_fired >= before.timers_fired + 1U
            && after.animation_frames_invoked
                >= before.animation_frames_invoked + 1U) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto work_is_consistent =
        after.timers_scheduled >= before.timers_scheduled + 2U
            && after.timers_fired >= before.timers_fired + 1U
            && after.timers_cancelled >= before.timers_cancelled + 1U
            && after.animation_frames_requested
                >= before.animation_frames_requested + 2U
            && after.animation_frames_invoked
                >= before.animation_frames_invoked + 1U
            && after.animation_frames_cancelled
                >= before.animation_frames_cancelled + 1U
            && after.microtask_checkpoints > before.microtask_checkpoints
            && after.worker_waits >= before.worker_waits
            && after.worker_signalled_wakes + after.worker_timeout_wakes
                <= after.worker_waits;
    if (!work_is_consistent) {
        fail(
            "timer, RAF, microtask, or worker-wake attribution was inconsistent: "
            "timers=" + std::to_string(after.timers_scheduled) + "/"
                + std::to_string(after.timers_fired) + "/"
                + std::to_string(after.timers_cancelled)
            + " raf=" + std::to_string(after.animation_frames_requested) + "/"
                + std::to_string(after.animation_frames_invoked) + "/"
                + std::to_string(after.animation_frames_cancelled)
            + " microtasks=" + std::to_string(after.microtask_checkpoints)
            + " waits=" + std::to_string(after.worker_waits) + "/"
                + std::to_string(after.worker_signalled_wakes) + "/"
                + std::to_string(after.worker_timeout_wakes));
    }

    const auto evaluation_before = after.arbitrary_evaluation_calls;
    require(
        evaluate(engine, "true", "runtime-work-evaluation.js") == "true",
        "runtime-work evaluation fixture failed");
    require(
        webscene_engine_get_runtime_work_metrics(engine, &after) != 0
            && after.arbitrary_evaluation_calls
                == evaluation_before + 1U
            && after.arbitrary_evaluation_source_bytes
                >= before.arbitrary_evaluation_source_bytes + 4U,
        "arbitrary evaluation was not attributed separately");
    webscene_engine_destroy(engine);
}

void test_scene_flow_is_attributed()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "scene-flow metrics engine creation failed");
    webscene_scene_flow_metrics undersized{};
    require(
        webscene_engine_get_scene_flow_metrics(engine, &undersized) == 0,
        "scene-flow metrics accepted an undersized ABI structure");
    webscene_scene_flow_metrics before{sizeof(webscene_scene_flow_metrics)};
    require(
        webscene_engine_get_scene_flow_metrics(engine, &before) != 0,
        "scene-flow metrics ABI was rejected");

    const webscene_scene_view* scene = nullptr;
    for (auto attempt = 0; attempt < 100 && scene == nullptr; ++attempt) {
        scene = webscene_engine_acquire_latest_scene(engine);
        if (scene == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(scene != nullptr, "scene-flow fixture did not publish an initial scene");
    const auto revision = scene->header.revision;
    require(
        webscene_scene_acknowledge(scene) != 0,
        "scene-flow fixture could not acknowledge its initial scene");
    webscene_scene_release(scene);

    webscene_scene_flow_metrics after{sizeof(webscene_scene_flow_metrics)};
    require(
        webscene_engine_get_scene_flow_metrics(engine, &after) != 0,
        "scene-flow metrics were unavailable after acknowledgement");
    require(
        after.publication_attempts >= before.publication_attempts
            && after.acknowledged_scenes == before.acknowledged_scenes + 1U
            && after.total_acknowledgement_nanoseconds
                >= before.total_acknowledgement_nanoseconds
                    + after.last_acknowledgement_nanoseconds
            && after.maximum_acknowledgement_nanoseconds
                >= after.last_acknowledgement_nanoseconds
            && after.acknowledged_revision == revision,
        "scene publication/backpressure attribution was inconsistent");
    webscene_engine_destroy(engine);
}

void test_read_only_evaluation_does_not_publish_scene()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "read-only evaluation engine creation failed");

    const webscene_scene_view* initial = nullptr;
    for (auto attempt = 0; attempt < 100 && initial == nullptr; ++attempt) {
        initial = webscene_engine_acquire_latest_scene(engine);
        if (initial == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(initial != nullptr, "read-only evaluation fixture had no initial scene");
    require(
        webscene_scene_acknowledge(initial) != 0,
        "read-only evaluation fixture could not acknowledge its initial scene");
    webscene_scene_release(initial);

    webscene_engine_metrics before_setup{};
    webscene_engine_get_metrics(engine, &before_setup);
    execute_and_wait(
        engine,
        "document.body.textContent = 'ready'",
        "read-only-evaluation-setup.js");

    const webscene_scene_view* setup_scene = nullptr;
    for (auto attempt = 0; attempt < 100 && setup_scene == nullptr; ++attempt) {
        webscene_engine_metrics current{};
        webscene_engine_get_metrics(engine, &current);
        if (current.published_scenes > before_setup.published_scenes) {
            setup_scene = webscene_engine_acquire_latest_scene(engine);
        }
        if (setup_scene == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(setup_scene != nullptr, "read-only evaluation setup scene was not published");
    require(
        webscene_scene_acknowledge(setup_scene) != 0,
        "read-only evaluation setup scene could not be acknowledged");
    webscene_scene_release(setup_scene);

    webscene_engine_metrics baseline{};
    webscene_engine_get_metrics(engine, &baseline);
    require(
        evaluate(engine, "1 + 1", "read-only-evaluation.js") == "2",
        "read-only evaluation returned the wrong value");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    webscene_engine_metrics after_read{};
    webscene_engine_get_metrics(engine, &after_read);
    if (after_read.published_scenes != baseline.published_scenes) {
        fail(
            "read-only evaluation published an unchanged scene: before="
            + std::to_string(baseline.published_scenes)
            + ", after=" + std::to_string(after_read.published_scenes));
    }

    require(
        evaluate(
            engine,
            "setTimeout(() => {}, 0); true",
            "non-visual-task-evaluation.js") == "true",
        "non-visual task evaluation returned the wrong value");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    webscene_engine_get_metrics(engine, &after_read);
    require(
        after_read.published_scenes == baseline.published_scenes,
        "a completed non-visual task published an unchanged scene");

    require(
        evaluate(
            engine,
            "document.body.textContent = 'changed'; true",
            "mutating-evaluation.js") == "true",
        "mutating evaluation returned the wrong value");
    for (auto attempt = 0; attempt < 100; ++attempt) {
        webscene_engine_get_metrics(engine, &after_read);
        if (after_read.published_scenes > baseline.published_scenes) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        after_read.published_scenes == baseline.published_scenes + 1U,
        "mutating evaluation did not publish its dirty scene");
    webscene_engine_destroy(engine);
}

void test_async_save_acknowledgement_publishes_without_pointer_input()
{
    auto* engine = webscene_engine_create(64);
    require(engine != nullptr, "async-save engine creation failed");
    resize(engine, 320, 180, 9100U);
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              body { margin: 0; background: #111; color: white; }
              button { width: 120px; height: 40px; }
            </style>
            <button id="save" type="button"><span id="label">Save</span></button>`;
          const button = document.getElementById('save');
          const label = document.getElementById('label');
          globalThis.__saveState = {
            activations: 0,
            requests: 0,
            acknowledgements: 0,
            pending: false,
            failNext: false,
            lastShouldFail: false
          };
          button.addEventListener('click', () => {
            const state = globalThis.__saveState;
            state.activations++;
            if (state.pending) return;
            state.pending = true;
            state.requests++;
            const shouldFail = state.failNext;
            state.failNext = false;
            state.lastShouldFail = shouldFail;
            setTimeout(() => {
              state.pending = false;
              state.acknowledgements++;
              if (!shouldFail) label.textContent = '';
            }, 250);
          });
        })()
    )JS", "async-save-publication-setup.js");
    // Host scripts and input have separate queues. An input/frame consumption
    // counter is not a barrier for installing the document and click listener.
    require(evaluate(engine,
        "document.getElementById('save').isConnected && __saveState.activations === 0",
        "async-save-publication-ready.js") == "true",
        "async-save fixture setup did not complete before activation");
    animation_frame_and_wait(engine, 0.0, 9101U);

    const auto consumed_before = consumed_input_count(engine);
    pointer_button(engine, WEBSCENE_INPUT_POINTER_DOWN, 30, 20, 9102U, true);
    pointer_button(engine, WEBSCENE_INPUT_POINTER_UP, 30, 20, 9103U, false);
    pointer_button(engine, WEBSCENE_INPUT_POINTER_DOWN, 30, 20, 9104U, true);
    pointer_button(engine, WEBSCENE_INPUT_POINTER_UP, 30, 20, 9105U, false);
    wait_for_consumed_inputs(
        engine,
        consumed_before + 4U,
        "async-save activations were not consumed");
    const auto coalesced_state = evaluate_until_equals(engine, R"JS(({
          activations: __saveState.activations,
          requests: __saveState.requests,
          pending: __saveState.pending,
          label: document.getElementById('label').textContent
        }))JS", "async-save-coalescing.js",
        R"JSON({"activations":2,"requests":1,"pending":true,"label":"Save"})JSON");
    require(
        coalesced_state
            == R"JSON({"activations":2,"requests":1,"pending":true,"label":"Save"})JSON",
        "duplicate activation was not coalesced while save was pending: "
            + coalesced_state);

    webscene_engine_metrics after_activation{};
    webscene_engine_get_metrics(engine, &after_activation);
    webscene_engine_metrics after_ack{};
    for (auto attempt = 0; attempt < 200; ++attempt) {
        webscene_engine_get_metrics(engine, &after_ack);
        if (after_ack.published_scenes > after_activation.published_scenes
            && evaluate(engine, "__saveState.acknowledgements",
                   "async-save-success-acknowledgement.js") == "1") {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        after_ack.published_scenes > after_activation.published_scenes,
        "successful save acknowledgement did not publish a scene without pointer movement");
    require(
        evaluate(engine, R"JS(({
          requests: __saveState.requests,
          acknowledgements: __saveState.acknowledgements,
          pending: __saveState.pending,
          label: document.getElementById('label').textContent
        }))JS", "async-save-success.js")
            == R"JSON({"requests":1,"acknowledgements":1,"pending":false,"label":""})JSON",
        "successful save acknowledgement did not clear the dirty label");

    execute(engine, R"JS(
      document.getElementById('label').textContent = 'Save';
      __saveState.failNext = true;
    )JS", "async-save-failure-setup.js");
    require(
        evaluate(engine, "__saveState.failNext", "async-save-failure-armed.js")
            == "true",
        "failing save fixture did not arm its failure result");
    const auto failure_consumed_before = consumed_input_count(engine);
    pointer_button(engine, WEBSCENE_INPUT_POINTER_DOWN, 30, 20, 9106U, true);
    pointer_button(engine, WEBSCENE_INPUT_POINTER_UP, 30, 20, 9107U, false);
    wait_for_consumed_inputs(
        engine,
        failure_consumed_before + 2U,
        "failing save activation was not consumed");
    for (auto attempt = 0; attempt < 200; ++attempt) {
        if (evaluate(engine, "__saveState.acknowledgements",
                "async-save-failure-wait.js") == "2") {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto failed_state = evaluate(engine, R"JS(({
          requests: __saveState.requests,
          acknowledgements: __saveState.acknowledgements,
          pending: __saveState.pending,
          lastShouldFail: __saveState.lastShouldFail,
          label: document.getElementById('label').textContent
        }))JS", "async-save-failure.js");
    require(
        failed_state
            == R"JSON({"requests":2,"acknowledgements":2,"pending":false,"lastShouldFail":true,"label":"Save"})JSON",
        "failed save acknowledgement incorrectly cleared the dirty label: "
            + failed_state);
    webscene_engine_destroy(engine);
}

void test_identical_custom_property_writes_do_not_schedule_visual_work()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "custom-property no-op engine creation failed");

    const webscene_scene_view* initial = nullptr;
    for (auto attempt = 0; attempt < 100 && initial == nullptr; ++attempt) {
        initial = webscene_engine_acquire_latest_scene(engine);
        if (initial == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(initial != nullptr, "custom-property no-op fixture had no initial scene");
    require(
        webscene_scene_acknowledge(initial) != 0,
        "custom-property no-op fixture could not acknowledge its initial scene");
    webscene_scene_release(initial);

    execute_and_wait(
        engine,
        R"JS(
          const style = document.createElement('style');
          style.textContent = '#custom-property-noop { width: var(--editor-width); height: 24px; }';
          document.head.appendChild(style);
          const target = document.createElement('div');
          target.id = 'custom-property-noop';
          target.style.setProperty('--editor-width', '120px');
          document.body.appendChild(target);
        )JS",
        "custom-property-noop-setup.js");
    require(
        evaluate(
            engine,
            "document.getElementById('custom-property-noop').offsetWidth",
            "custom-property-noop-ready.js") == "120",
        "custom-property no-op fixture did not complete initial layout");

    auto setup_scene_acknowledged = false;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            require(
                webscene_scene_acknowledge(scene) != 0,
                "custom-property no-op setup scene could not be acknowledged");
            webscene_scene_release(scene);
            setup_scene_acknowledged = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        setup_scene_acknowledged,
        "custom-property no-op setup scene was not published");
    std::this_thread::sleep_for(std::chrono::milliseconds(25));

    webscene_engine_metrics connected_before{};
    webscene_engine_get_metrics(engine, &connected_before);
    execute_and_wait(
        engine,
        R"JS(
          const target = document.getElementById('custom-property-noop');
          for (let index = 0; index < 4096; ++index) {
            target.style.setProperty('--editor-width', '120px');
          }
        )JS",
        "custom-property-noop-connected.js");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    webscene_engine_metrics connected_after{};
    webscene_engine_get_metrics(engine, &connected_after);
    require(
        connected_after.layout_passes == connected_before.layout_passes
            && connected_after.published_scenes == connected_before.published_scenes,
        "identical connected custom-property writes scheduled layout or publication");

    execute_and_wait(
        engine,
        R"JS(
          globalThis.__customPropertyNoopTarget =
            document.getElementById('custom-property-noop');
          globalThis.__customPropertyNoopTarget.remove();
        )JS",
        "custom-property-noop-detach.js");
    auto detach_scene_acknowledged = false;
    for (auto attempt = 0; attempt < 100; ++attempt) {
        const auto* scene = webscene_engine_acquire_latest_scene(engine);
        if (scene != nullptr) {
            require(
                webscene_scene_acknowledge(scene) != 0,
                "custom-property no-op detach scene could not be acknowledged");
            webscene_scene_release(scene);
            detach_scene_acknowledged = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        detach_scene_acknowledged,
        "custom-property no-op detach scene was not published");
    std::this_thread::sleep_for(std::chrono::milliseconds(25));

    webscene_engine_metrics detached_before{};
    webscene_engine_get_metrics(engine, &detached_before);
    execute_and_wait(
        engine,
        R"JS(
          for (let index = 0; index < 4096; ++index) {
            __customPropertyNoopTarget.style.setProperty('--editor-width', '120px');
          }
        )JS",
        "custom-property-noop-detached.js");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    webscene_engine_metrics detached_after{};
    webscene_engine_get_metrics(engine, &detached_after);
    require(
        detached_after.layout_passes == detached_before.layout_passes
            && detached_after.published_scenes == detached_before.published_scenes,
        "identical detached custom-property writes scheduled visual work");

    execute_and_wait(
        engine,
        R"JS(
          document.body.appendChild(__customPropertyNoopTarget);
          delete globalThis.__customPropertyNoopTarget;
        )JS",
        "custom-property-noop-reattach.js");
    webscene_engine_metrics reattached{};
    for (auto attempt = 0; attempt < 100; ++attempt) {
        webscene_engine_get_metrics(engine, &reattached);
        if (reattached.published_scenes > detached_after.published_scenes) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        evaluate(
            engine,
            R"JS((() => {
              const target = document.getElementById('custom-property-noop');
              return {
                specified: target.style.getPropertyValue('--editor-width'),
                width: target.offsetWidth,
                connected: target.isConnected
              };
            })())JS",
            "custom-property-noop-reattach-result.js")
            == R"({"specified":"120px","width":120,"connected":true})",
        "custom-property no-op state did not survive detach and reconnect");
    webscene_engine_get_metrics(engine, &reattached);
    require(
        reattached.layout_passes == detached_after.layout_passes + 1U
            && reattached.published_scenes == detached_after.published_scenes + 1U,
        "custom-property no-op reconnect escaped one layout/publication");
    webscene_engine_destroy(engine);
}

void test_ordered_scene_consumer_preserves_two_diff_chain()
{
    auto* engine = webscene_engine_create(64);
    require(engine != nullptr, "ordered-scene engine creation failed");
    const auto wait_for_publications = [engine](
        uint64_t minimum,
        const char* failure) {
        for (auto attempt = 0; attempt < 200; ++attempt) {
            webscene_engine_metrics metrics{};
            webscene_engine_get_metrics(engine, &metrics);
            if (metrics.published_scenes >= minimum) {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        fail(failure);
    };

    const webscene_scene_view* checkpoint = nullptr;
    for (auto attempt = 0; attempt < 200 && checkpoint == nullptr; ++attempt) {
        checkpoint = webscene_engine_acquire_next_scene(engine);
        if (checkpoint == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(checkpoint != nullptr, "ordered consumer did not acquire checkpoint");
    require(
        webscene_scene_acknowledge(checkpoint) != 0,
        "ordered consumer could not acknowledge checkpoint");
    webscene_scene_release(checkpoint);

    webscene_engine_metrics baseline{};
    webscene_engine_get_metrics(engine, &baseline);
    resize(engine, 401, 301, 1U);
    wait_for_publications(
        baseline.published_scenes + 1U,
        "ordered producer did not publish its first diff");
    const auto* first = webscene_engine_acquire_next_scene(engine);
    require(first != nullptr, "ordered consumer did not acquire first diff");
    const auto first_revision = first->header.revision;

    resize(engine, 402, 302, 2U);
    wait_for_publications(
        baseline.published_scenes + 2U,
        "ordered producer did not publish one frame ahead");
    const auto* latest = webscene_engine_acquire_latest_scene(engine);
    require(latest != nullptr, "ordered producer had no second diff");
    require(
        latest->header.base_revision == first_revision,
        "ordered second diff did not retain the first revision as its base");
    const auto second_revision = latest->header.revision;
    webscene_scene_release(latest);
    webscene_engine_memory_metrics queued_memory{
        sizeof(webscene_engine_memory_metrics)};
    require(
        webscene_engine_get_memory_metrics(engine, &queued_memory) != 0
            && queued_memory.pending_scene_count == 2U
            && queued_memory.pending_scene_bytes
                >= queued_memory.latest_scene_bytes
            && queued_memory.latest_scene_bytes > 0,
        "ordered scene memory was not bounded and attributed");

    webscene_scene_flow_metrics before_block{sizeof(webscene_scene_flow_metrics)};
    require(
        webscene_engine_get_scene_flow_metrics(engine, &before_block) != 0,
        "ordered scene-flow metrics were unavailable");
    resize(engine, 403, 303, 3U);
    webscene_scene_flow_metrics after_block{sizeof(webscene_scene_flow_metrics)};
    for (auto attempt = 0; attempt < 200; ++attempt) {
        require(
            webscene_engine_get_scene_flow_metrics(engine, &after_block) != 0,
            "ordered scene-flow metrics became unavailable");
        if (after_block.blocked_publications
            > before_block.blocked_publications) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        after_block.blocked_publications
            > before_block.blocked_publications,
        "ordered producer exceeded its two-scene bound");

    require(
        webscene_scene_acknowledge(first) != 0,
        "ordered consumer could not acknowledge first diff");
    webscene_scene_release(first);
    const auto* second = webscene_engine_acquire_next_scene(engine);
    require(
        second != nullptr && second->header.revision == second_revision,
        "ordered consumer skipped its second diff");
    require(
        webscene_scene_acknowledge(second) != 0,
        "ordered consumer could not acknowledge second diff");
    webscene_scene_release(second);
    wait_for_publications(
        baseline.published_scenes + 3U,
        "ordered producer did not resume after acknowledgement");
    const auto* third = webscene_engine_acquire_next_scene(engine);
    require(
        third != nullptr && third->header.base_revision == second_revision,
        "ordered consumer lost the resumed diff chain");
    require(
        webscene_scene_acknowledge(third) != 0,
        "ordered consumer could not acknowledge resumed diff");
    webscene_scene_release(third);
    webscene_engine_memory_metrics drained_memory{
        sizeof(webscene_engine_memory_metrics)};
    require(
        webscene_engine_get_memory_metrics(engine, &drained_memory) != 0
            && drained_memory.pending_scene_count == 0
            && drained_memory.pending_scene_bytes == 0,
        "acknowledged ordered scenes remained attributed");
    webscene_engine_destroy(engine);
}

void test_keyboard_and_pointer_focus_modality()
{
    auto* focus_engine = webscene_engine_create(16);
    require(focus_engine != nullptr, "focus-modality engine creation failed");
    resize(focus_engine, 320, 200, 200U);
    execute(focus_engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <style>
              @supports not selector(:focus-visible) {
                :focus { background-color: red; }
              }
              .always { outline: 2px solid blue; }
              :focus-visible { outline: 3px solid rgb(0, 128, 0); }
              :focus:not(:focus-visible) { background-color: rgb(0, 255, 0); }
            </style>
            <div id="keyboard" class="always" tabindex="0" style="width:120px;height:40px">keyboard</div>
            <button id="pointer" style="display:block;width:120px;height:40px">pointer</button>`;
          document.body.style.margin = '0';
          globalThis.__focusEvents = [];
          for (const target of [keyboard, pointer]) {
            target.addEventListener('focus', () => __focusEvents.push(`focus:${target.id}`));
            target.addEventListener('blur', () => __focusEvents.push(`blur:${target.id}`));
          }
        })()
    )JS", "native-focus-modality-setup.js");
    require(
        evaluate(focus_engine, "pointer.offsetTop", "native-focus-modality-ready.js") == "40",
        "focus-modality fixture did not complete layout");
    require(
        evaluate(focus_engine, "getComputedStyle(keyboard).outlineColor", "native-outline-ready.js")
            == "\"rgb(0, 0, 255)\"",
        "base outline shorthand did not reach computed style");
    const auto tab_index_reflection = evaluate(focus_engine, R"JS((() => {
      const menu = document.createElement('div');
      menu.setAttribute('tabindex', '-1');
      const authored = menu.tabIndex;
      menu.tabIndex = 2;
      return {
        keyboard: keyboard.tabIndex,
        pointer: pointer.tabIndex,
        body: document.body.tabIndex,
        authored,
        assigned: menu.tabIndex,
        reflected: menu.getAttribute('tabindex')
      };
    })())JS", "native-tab-index-reflection.js");
    if (tab_index_reflection != R"({"keyboard":0,"pointer":0,"body":-1,"authored":-1,"assigned":2,"reflected":"2"})") {
        fail("tabIndex IDL reflection regressed: " + tab_index_reflection);
    }

    keyboard_input(focus_engine, WEBSCENE_INPUT_KEY_DOWN, 9U, 201U);
    keyboard_input(focus_engine, WEBSCENE_INPUT_KEY_UP, 9U, 202U);
    const auto keyboard_result = evaluate(focus_engine, R"JS((() => ({
      active: document.activeElement?.id,
      focusVisible: document.querySelector(':focus-visible')?.id,
      notFocusVisible: document.querySelector(':focus:not(:focus-visible)')?.id ?? null,
      className: keyboard.className,
      outline: getComputedStyle(keyboard).outlineColor,
      background: getComputedStyle(keyboard).backgroundColor,
      events: __focusEvents
    }))())JS", "native-keyboard-focus-result.js");
    if (keyboard_result != R"JSON({"active":"keyboard","focusVisible":"keyboard","notFocusVisible":null,"className":"always","outline":"rgb(0, 128, 0)","background":"rgba(0, 0, 0, 0)","events":["focus:keyboard"]})JSON") {
        fail("Tab focus or :focus-visible keyboard modality regressed: " + keyboard_result);
    }

    const webscene_input_event down{
        WEBSCENE_INPUT_POINTER_DOWN, 1U | (1U << 8U), 203U, 10, 60, 0, 0};
    const webscene_input_event up{
        WEBSCENE_INPUT_POINTER_UP, (1U << 8U), 204U, 10, 60, 0, 0};
    require(webscene_engine_enqueue(focus_engine, &down) != 0, "pointer focus down was rejected");
    require(webscene_engine_enqueue(focus_engine, &up) != 0, "pointer focus up was rejected");
    const auto pointer_result = evaluate(focus_engine, R"JS((() => ({
      active: document.activeElement?.id,
      focusVisible: document.querySelector(':focus-visible')?.id ?? null,
      background: getComputedStyle(pointer).backgroundColor,
      events: __focusEvents
    }))())JS", "native-pointer-focus-result.js");
    if (pointer_result != R"JSON({"active":"pointer","focusVisible":null,"background":"rgb(0, 255, 0)","events":["focus:keyboard","blur:keyboard","focus:pointer"]})JSON") {
        fail("pointer focus or :focus-visible pointer modality regressed: " + pointer_result);
    }
    const auto provisional_frame = evaluate(focus_engine, R"JS((() => {
      const frame = document.createElement('iframe');
      document.body.appendChild(frame);
      const input = document.createElement('input');
      input.id = 'provisional-frame-input';
      frame.contentDocument.body.appendChild(input);
      return {
        hasBody: !!frame.contentDocument.body,
        child: frame.contentDocument.body.firstElementChild?.id
      };
    })())JS", "native-provisional-frame-document.js");
    if (provisional_frame != R"({"hasBody":true,"child":"provisional-frame-input"})") {
        fail("empty iframe contentDocument did not expose a mutable body: " + provisional_frame);
    }
    const auto reentrant_focus = evaluate(focus_engine, R"JS((() => {
      const target = document.createElement('input');
      target.id = 'reentrant-focus-target';
      document.body.appendChild(target);
      let focusOutCount = 0;
      pointer.addEventListener('focusout', () => {
        focusOutCount++;
        target.focus();
      });
      pointer.blur();
      return {
        active: document.activeElement?.id,
        focusOutCount
      };
    })())JS", "native-reentrant-focus.js");
    if (reentrant_focus != R"({"active":"reentrant-focus-target","focusOutCount":1})") {
        fail("focusout re-entrant focus transition regressed: " + reentrant_focus);
    }
    const auto detached_focus = evaluate(focus_engine, R"JS((() => {
      const removedHost = document.createElement('div');
      const removedInput = document.createElement('input');
      const removeEvents = [];
      removedInput.addEventListener('blur', () => removeEvents.push('blur'));
      removedInput.addEventListener('focusout', () => removeEvents.push('focusout'));
      removedHost.addEventListener('blur', () => removeEvents.push('host-blur'));
      removedHost.addEventListener('focusout', () => removeEvents.push('host-focusout'));
      removedHost.appendChild(removedInput);
      document.body.appendChild(removedHost);
      removedInput.focus();
      removedHost.remove();

      const replacedHost = document.createElement('div');
      const replacedInput = document.createElement('input');
      const innerHtmlEvents = [];
      replacedInput.addEventListener('blur', () => innerHtmlEvents.push('blur'));
      replacedInput.addEventListener('focusout', () => innerHtmlEvents.push('focusout'));
      replacedHost.addEventListener('blur', () => innerHtmlEvents.push('host-blur'));
      replacedHost.addEventListener('focusout', () => innerHtmlEvents.push('host-focusout'));
      replacedHost.appendChild(replacedInput);
      document.body.appendChild(replacedHost);
      replacedInput.focus();
      replacedHost.innerHTML = '';
      return {
        activeTag: document.activeElement?.tagName,
        focusMatch: document.querySelector(':focus')?.tagName ?? null,
        removeEvents,
        innerHtmlEvents,
        removedConnected: removedInput.isConnected,
        replacedConnected: replacedInput.isConnected
      };
    })())JS", "native-detached-focus.js");
    if (detached_focus != R"({"activeTag":"BODY","focusMatch":null,"removeEvents":["blur","focusout","host-focusout"],"innerHtmlEvents":["blur","focusout","host-focusout"],"removedConnected":false,"replacedConnected":false})") {
        fail("detached focused nodes remained document.activeElement: " + detached_focus);
    }
    webscene_engine_destroy(focus_engine);
}

void test_resize_precedes_new_viewport_pointer_input(webscene_engine* engine)
{
    resize(engine, 320, 200, 7U);
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          document.body.style.margin = '0';
          const target = document.createElement('div');
          target.style.width = '100vw';
          target.style.height = '100vh';
          target.addEventListener('pointerdown', () => {
            globalThis.__widthObservedByPointer = innerWidth;
          });
          document.body.appendChild(target);
          globalThis.__widthObservedByPointer = 0;
        })()
    )JS", "native-resize-pointer-setup.js");
    require(
        evaluate(engine, "globalThis.__widthObservedByPointer",
            "native-resize-pointer-ready.js") == "0",
        "resize-pointer fixture did not initialize");

    // Keep the worker occupied while both independently queued event streams
    // receive work. This makes the ordering assertion deterministic: the latest
    // coalesced resize and the following pointer are both pending together.
    execute(engine, R"JS(
        (() => {
          const until = Date.now() + 20;
          while (Date.now() < until) {}
        })()
    )JS", "native-resize-pointer-worker-barrier.js");
    resize(engine, 777, 333, 8U);
    const webscene_input_event down{
        WEBSCENE_INPUT_POINTER_DOWN, 1U | (1U << 8U), 9U, 10, 10, 0, 0};
    require(webscene_engine_enqueue(engine, &down) != 0,
        "post-resize pointer down was rejected");

    const auto result = evaluate(
        engine,
        "globalThis.__widthObservedByPointer",
        "native-resize-pointer-result.js");
    if (result != "777") {
        fail("pointer input was dispatched against the stale pre-resize viewport: " + result);
    }
}

void test_resize_updates_device_pixel_ratio(webscene_engine* engine)
{
    execute(engine, R"JS(
        (() => {
          document.body.innerHTML = '';
          const frame = document.createElement('iframe');
          document.body.appendChild(frame);
          const frameDocument = frame.contentDocument;
          frameDocument.open();
          frameDocument.write('<html><body></body></html>');
          frameDocument.close();
          globalThis.__scaleFrame = frame;
        })()
    )JS", "native-scale-factor-setup.js");

    resize(engine, 640, 360, 210U, 2.0);
    const auto scaled = evaluate(engine, R"JS((() => ({
      outer: devicePixelRatio,
      frame: globalThis.__scaleFrame.contentWindow.devicePixelRatio,
      width: innerWidth,
      height: innerHeight
    }))())JS", "native-scale-factor-two.js");
    if (scaled != R"({"outer":2,"frame":2,"width":640,"height":360})") {
        fail("devicePixelRatio did not follow the host scale transition: " + scaled);
    }

    resize(engine, 640, 360, 211U, 1.0);
    const auto restored = evaluate(engine, R"JS((() => ({
      outer: devicePixelRatio,
      frame: globalThis.__scaleFrame.contentWindow.devicePixelRatio
    }))())JS", "native-scale-factor-one.js");
    if (restored != R"({"outer":1,"frame":1})") {
        fail("devicePixelRatio did not restore after the host scale transition: " + restored);
    }
}
void wait_for_document_visibility(webscene_engine* engine, bool hidden)
{
    // Host visibility is signalled asynchronously, independently of the script
    // queue. An evaluation admitted in the current worker turn can precede it.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    const auto expected = hidden ? "true" : "false";
    while (evaluate(engine, "document.hidden", "document-visibility-ready.js") != expected) {
        require(std::chrono::steady_clock::now() < deadline,
            "host visibility did not reach the document");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void wait_for_document_focus(webscene_engine* engine, bool focused)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    const auto expected = focused ? "true" : "false";
    while (evaluate(engine, "document.hasFocus()", "document-focus-ready.js") != expected) {
        require(std::chrono::steady_clock::now() < deadline,
            "host focus did not reach the document");
        std::this_thread::yield();
    }
}

void test_document_direction_and_visibility_are_native_properties()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "document state regression engine creation failed");
    const auto initial = evaluate(engine, R"JS(
        ({
          dir: document.dir,
          hidden: document.hidden,
          visibilityState: document.visibilityState
        })
    )JS", "native-document-state-initial.js");
    require(
        initial == R"({"dir":"","hidden":false,"visibilityState":"visible"})",
        "Document direction or initial visibility was not browser-shaped: " + initial);

    const auto reflected = evaluate(engine, R"JS(
        (() => {
          document.dir = 'rtl';
          return {
            dir: document.dir,
            attribute: document.documentElement.getAttribute('dir')
          };
        })()
    )JS", "native-document-direction.js");
    require(
        reflected == R"({"dir":"rtl","attribute":"rtl"})",
        "Document.dir did not reflect the document-element attribute: " + reflected);

    execute_and_wait(
        engine,
        R"JS(
          globalThis.__visibilityEvents = [];
          globalThis.__focusEvents = [];
          document.addEventListener('visibilitychange', () => {
            __visibilityEvents.push(document.visibilityState);
            Promise.resolve().then(() =>
              __visibilityEvents.push(`microtask:${document.visibilityState}`));
          });
          addEventListener('focus', () => __focusEvents.push('focus'));
          addEventListener('blur', () => __focusEvents.push('blur'));
        )JS",
        "native-document-visibility-events.js");

    require(
        webscene_engine_set_visible(engine, 0U) != 0,
        "document state engine rejected hidden visibility");
    wait_for_document_visibility(engine, true);
    const auto hidden = evaluate(engine, R"JS(
        ({
          hidden: document.hidden,
          visibilityState: document.visibilityState,
          events: __visibilityEvents
        })
    )JS", "native-document-state-hidden.js");
    require(
        hidden == R"({"hidden":true,"visibilityState":"hidden","events":["hidden","microtask:hidden"]})",
        "Document visibility did not follow the hidden host state: " + hidden);

    require(
        webscene_engine_set_visible(engine, 0U) != 0,
        "document state engine rejected duplicate hidden visibility");

    require(
        webscene_engine_set_visible(engine, 1U) != 0,
        "document state engine rejected visible visibility");
    wait_for_document_visibility(engine, false);
    const auto visible = evaluate(engine, R"JS(
        ({
          hidden: document.hidden,
          visibilityState: document.visibilityState,
          events: __visibilityEvents
        })
    )JS", "native-document-state-visible.js");
    require(
        visible == R"({"hidden":false,"visibilityState":"visible","events":["hidden","microtask:hidden","visible","microtask:visible"]})",
        "Document visibility did not follow the restored host state: " + visible);

    require(webscene_engine_set_window_focused_v1(engine, 0U) != 0,
        "document state engine rejected native blur");
    wait_for_document_focus(engine, false);
    require(webscene_engine_set_window_focused_v1(engine, 1U) != 0,
        "document state engine rejected native focus");
    wait_for_document_focus(engine, true);
    require(evaluate(engine,
                "({ focused: document.hasFocus(), events: __focusEvents })",
                "native-document-focus-state.js")
                == R"({"focused":true,"events":["blur","focus"]})",
        "Document focus state/events did not follow native key-window changes");

    const auto transition_started = std::chrono::steady_clock::now();
    for (unsigned index = 0; index < 10000U; ++index) {
        require(webscene_engine_set_window_focused_v1(
                    engine, static_cast<uint8_t>(index & 1U)) != 0,
            "native focus transition stress input was rejected");
    }
    require(webscene_engine_set_window_focused_v1(engine, 1U) != 0,
        "final native focus state was rejected");
    wait_for_document_focus(engine, true);
    const auto transition_elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - transition_started).count();
    require(transition_elapsed < 1.0,
        "10,000 native focus transitions exceeded one second");
    std::cout << "Native focus transition gate: operations=10000 elapsed="
              << transition_elapsed << "s\n";
    webscene_engine_destroy(engine);
}

void test_hidden_document_defers_presentation_work()
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "hidden-presentation engine creation failed");
    const webscene_scene_view* initial = nullptr;
    for (auto attempt = 0; attempt < 100 && initial == nullptr; ++attempt) {
        initial = webscene_engine_acquire_latest_scene(engine);
        if (initial == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(initial != nullptr, "hidden-presentation fixture had no initial scene");
    require(
        webscene_scene_acknowledge(initial) != 0,
        "hidden-presentation fixture could not acknowledge its initial scene");
    webscene_scene_release(initial);
    require(
        webscene_engine_set_runtime_work_metrics_enabled(engine, 1U) != 0,
        "hidden-presentation fixture could not enable work metrics");
    require(
        webscene_engine_set_visible(engine, 0U) != 0,
        "hidden-presentation fixture rejected hidden state");
    wait_for_document_visibility(engine, true);

    webscene_engine_metrics before{};
    webscene_engine_get_metrics(engine, &before);
    webscene_runtime_work_metrics work_before{
        sizeof(webscene_runtime_work_metrics)};
    require(
        webscene_engine_get_runtime_work_metrics(engine, &work_before) != 0,
        "hidden-presentation work metrics were unavailable");
    execute_and_wait(
        engine,
        "setTimeout(() => { document.body.textContent = 'mutated while hidden'; }, 0)",
        "hidden-presentation-mutation.js");
    webscene_runtime_work_metrics work_hidden{
        sizeof(webscene_runtime_work_metrics)};
    for (auto attempt = 0; attempt < 250; ++attempt) {
        require(
            webscene_engine_get_runtime_work_metrics(engine, &work_hidden) != 0,
            "hidden-presentation work metrics became unavailable");
        if (work_hidden.timers_fired > work_before.timers_fired) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(75));

    webscene_engine_metrics hidden{};
    webscene_engine_get_metrics(engine, &hidden);
    require(
        webscene_engine_get_runtime_work_metrics(engine, &work_hidden) != 0
            && work_hidden.timers_fired == work_before.timers_fired + 1U
            && hidden.published_scenes == before.published_scenes
            && work_hidden.scene_builds == work_before.scene_builds,
        "hidden DOM work produced presentation scenes");

    require(
        webscene_engine_set_visible(engine, 1U) != 0,
        "hidden-presentation fixture rejected restored visibility");
    webscene_engine_metrics visible{};
    for (auto attempt = 0; attempt < 250; ++attempt) {
        webscene_engine_get_metrics(engine, &visible);
        if (visible.published_scenes > hidden.published_scenes) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        visible.published_scenes == hidden.published_scenes + 1U
            && evaluate(
                engine,
                "document.body.textContent",
                "hidden-presentation-restored.js")
                == "\"mutated while hidden\"",
        "restored visibility did not publish the deferred document state");
    webscene_engine_destroy(engine);
}
void test_component_catalog_mounts_interacts_and_unmounts()
{
    struct component_case final {
        std::string_view id;
        std::string_view visible_text;
        std::string_view button_text;
        std::string_view expected_text;
    };
    static constexpr std::array cases{
        component_case{"ComponentHost.Basic", "Basic component host", "Clicked 0 times", "Clicked 1 time"},
        component_case{"Hybrid.ReactIslands", "Independent component root", "Island count: 0", "Island count: 1"},
        component_case{"TypeScriptDesktop", "Desktop workspace", "Form", "Profile form"},
        component_case{"ReactDashboard", "Operations overview", "Inspect chart", "Chart details"},
        component_case{"PluginWorkbench", "Plugin workbench", "Load plugin", "Unload plugin"},
        component_case{"MultiInstanceWorkstation", "Isolated chart instance", "Local count: 0", "Local count: 1"},
        component_case{"WebToNativeMigration", "Existing React order panel", "#1048 · Contoso", "Legacy component note"},
        component_case{"OfflineKiosk", "Station kiosk", "Print ticket", "K-5499"},
        component_case{"HeadlessComponentTests", "Headless component tests", "Pointer events: 0", "Pointer events: 1"},
        component_case{"HostBridge.Services", "Host bridge services", "Invoke service", "Completed"},
        component_case{"AccessibilityGallery", "Accessibility gallery", "Save profile", "Profile saved successfully"},
        component_case{"CanvasWorkbench.Advanced", "Interactive canvas workbench", "Add indicator", "Moving Average"}
    };
    const auto repository_root = std::filesystem::path(__FILE__)
        .parent_path().parent_path().parent_path().parent_path();
    const auto quote = [](std::string_view value) {
        std::string result{"'"};
        for (const auto character : value) {
            if (character == '\\' || character == '\'') result.push_back('\\');
            result.push_back(character);
        }
        result.push_back('\'');
        return result;
    };
    for (const auto& fixture : cases) {
        const auto bundle_path = repository_root / "samples/components"
            / fixture.id / "dist/main.js";
        std::ifstream stream(bundle_path, std::ios::binary);
        require(stream.good(), "component catalog bundle was not found: " + std::string(fixture.id));
        const std::string bundle{
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
        auto* engine = webscene_engine_create(64);
        require(engine != nullptr, "component catalog engine creation failed");
        const auto release_engine = std::unique_ptr<webscene_engine, decltype(&webscene_engine_destroy)>(
            engine, webscene_engine_destroy);
        execute_and_wait(engine, R"JS(
          globalThis.webscene = {
            profileVersion: '1.0',
            host: new Proxy({}, { get: (_, capability) => ({
              invoke(method, argumentsValue = {}) {
                (globalThis.__componentInvocations ||= []).push({ capability, method, argumentsValue });
                return Promise.resolve({ accepted: true });
              }
            }) })
          };
        )JS", "component-catalog-host.js");
        execute_and_wait(engine, bundle, std::string(fixture.id) + "-bundle.js");
        require(
            evaluate(
                engine,
                "mount({instanceId:" + quote(fixture.id) + "}); true",
                std::string(fixture.id) + "-mount.js") == "true",
            "component catalog mount failed: " + std::string(fixture.id));
        auto mounted = false;
        for (auto attempt = 0; attempt < 500 && !mounted; ++attempt) {
            mounted = evaluate(
                engine,
                "!!document.querySelector('main') && document.body.textContent.includes("
                    + quote(fixture.visible_text) + ") && document.body.textContent.trim().length > 80",
                std::string(fixture.id) + "-mounted.js") == "true";
            if (!mounted) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        require(mounted, "component catalog visible mount failed: " + std::string(fixture.id));

        const auto interaction = R"JS((() => {
          const expected = )JS" + quote(fixture.button_text) + R"JS(;
          const button = Array.from(document.querySelectorAll('button'))
            .find(candidate => candidate.textContent.includes(expected));
          if (!button) return false;
          let keyObserved = false;
          button.addEventListener('keydown', event => { keyObserved = event.key === 'Enter'; }, { once: true });
          button.focus();
          const keyEvent = new Event('keydown', { bubbles: true });
          Object.defineProperty(keyEvent, 'key', { value: 'Enter' });
          button.dispatchEvent(keyEvent);
          if (document.activeElement !== button || !keyObserved) return false;
          button.click();
          return true;
        })())JS";
        require(
            evaluate(engine, interaction, std::string(fixture.id) + "-interaction.js") == "true",
            "component catalog keyboard/pointer interaction failed: " + std::string(fixture.id));
        auto updated = false;
        for (auto attempt = 0; attempt < 500 && !updated; ++attempt) {
            updated = evaluate(
                engine,
                "document.body.textContent.includes(" + quote(fixture.expected_text) + ")",
                std::string(fixture.id) + "-updated.js") == "true";
            if (!updated) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        require(updated, "component catalog update failed: " + std::string(fixture.id));
        require(
            evaluate(engine, "unmount(); document.body.children.length === 0",
                std::string(fixture.id) + "-unmount.js") == "true",
            "component catalog unmount failed: " + std::string(fixture.id));
    }
}
void test_generated_idl_attributes_are_prototype_accessors(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          const descriptor = Object.getOwnPropertyDescriptor(Element.prototype, 'id');
          const element = document.createElement('div');
          element.id = 'prototype-accessor';
          let rejectedPlainReceiver = false;
          try { descriptor.get.call({}); }
          catch (error) { rejectedPlainReceiver = error instanceof TypeError; }
          return {
            getter: typeof descriptor.get,
            setter: typeof descriptor.set,
            enumerable: descriptor.enumerable,
            configurable: descriptor.configurable,
            own: Object.prototype.hasOwnProperty.call(element, 'id'),
            value: element.getAttribute('id'),
            rejectedPlainReceiver
          };
        })()
    )JS", "native-generated-idl-prototype-accessors.js");
    require(
        result == R"JSON({"getter":"function","setter":"function","enumerable":true,"configurable":true,"own":false,"value":"prototype-accessor","rejectedPlainReceiver":true})JSON",
        "generated IDL attributes did not retain Web IDL prototype shape: " + result);
}

void test_document_links_is_a_live_named_html_collection(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '<a id="later" name="shared" href="/later"></a>';
          const links = document.links;
          const area = document.createElement('area');
          area.id = 'earlier';
          area.name = 'shared';
          area.href = '/earlier';
          document.body.prepend(area);
          let illegalConstructor = false;
          let illegalItemReceiver = false;
          let illegalNamedItemReceiver = false;
          let illegalLengthReceiver = false;
          try { new HTMLCollection(); }
          catch (error) { illegalConstructor = error instanceof TypeError; }
          try { HTMLCollection.prototype.item.call({}, 0); }
          catch (error) { illegalItemReceiver = error instanceof TypeError; }
          try { HTMLCollection.prototype.namedItem.call({}, 'earlier'); }
          catch (error) { illegalNamedItemReceiver = error instanceof TypeError; }
          try {
            Object.getOwnPropertyDescriptor(HTMLCollection.prototype, 'length').get.call({});
          } catch (error) { illegalLengthReceiver = error instanceof TypeError; }
          const beforeRemoval = {
            sameObject: links === document.links,
            brand: links instanceof HTMLCollection
              && Object.prototype.toString.call(links) === '[object HTMLCollection]',
            length: links.length,
            first: links[0]?.id,
            second: links.item(1)?.id,
            id: links.namedItem('earlier')?.id,
            nameFirst: links.namedItem('shared')?.id,
            namedProperty: links.earlier?.id,
            lengthOnPrototype: !Object.hasOwn(links, 'length')
              && typeof Object.getOwnPropertyDescriptor(
                HTMLCollection.prototype, 'length')?.get === 'function'
          };
          area.removeAttribute('href');
          const afterHrefRemoval = [
            links.length,
            links[0]?.id,
            links.namedItem('earlier'),
            links.earlier
          ];
          area.setAttribute('href', '/earlier');
          area.remove();
          return {
            beforeRemoval,
            afterHrefRemoval,
            afterDetach: [links.length, links[0]?.id],
            illegalConstructor,
            illegalItemReceiver,
            illegalNamedItemReceiver,
            illegalLengthReceiver
          };
        })()
    )JS", "native-document-links-html-collection.js");
    require(
        result == R"JSON({"beforeRemoval":{"sameObject":true,"brand":true,"length":2,"first":"earlier","second":"later","id":"earlier","nameFirst":"earlier","namedProperty":"earlier","lengthOnPrototype":true},"afterHrefRemoval":[1,"later",null,null],"afterDetach":[1,"later"],"illegalConstructor":true,"illegalItemReceiver":true,"illegalNamedItemReceiver":true,"illegalLengthReceiver":true})JSON",
        "Document.links did not retain live named HTMLCollection semantics: " + result);
}

void test_scrollspy_product_neutral_primitives(webscene_engine* engine)
{
    const auto immediate = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = `
            <div style="height:84px"></div>
            <div id="scrollspy-scroller" style="position:relative;height:100px;overflow:auto">
              <div style="position:relative;height:100%">one</div>
              <div style="position:relative;height:100%">two</div>
              <div style="position:relative;height:100%">three</div>
              <div style="position:relative;height:100%">four</div>
              <div style="position:relative;height:100%">five</div>
            </div>
            <div id="hidden-section" style="height:40px" hidden></div>
            <div id="following-section" style="height:10px"></div>
            <a id="bare-fragment" href="#"></a>
            <a id="relative-bare-fragment" href="relative#"></a>
            <a id="named-fragment" href="#section"></a>`;
          const scroller = document.getElementById('scrollspy-scroller');
          const hidden = document.getElementById('hidden-section');
          const following = document.getElementById('following-section');
          const hiddenFollowingTop = following.offsetTop;
          const hiddenInitial = hidden.offsetHeight === 0
            && hidden.getClientRects().length === 0;
          hidden.removeAttribute('hidden');
          const hiddenRestored = hidden.offsetHeight === 40
            && following.offsetTop === hiddenFollowingTop + 40;
          hidden.setAttribute('hidden', '');
          const hiddenReapplied = hidden.offsetHeight === 0
            && following.offsetTop === hiddenFollowingTop;
          globalThis.__programmaticScrollEarly = 0;
          globalThis.__programmaticScrollLate = 0;
          scroller.addEventListener('scroll', () => __programmaticScrollEarly++);
          scroller.scrollTop = 20;
          scroller.scrollTop = 40;
          scroller.addEventListener('scroll', () => __programmaticScrollLate++);
          return {
            position: getComputedStyle(scroller).getPropertyValue('position'),
            fragments: [
              document.getElementById('bare-fragment').hash,
              document.getElementById('relative-bare-fragment').hash,
              document.getElementById('named-fragment').hash
            ],
            hidden: [hiddenInitial, hiddenRestored, hiddenReapplied],
            range: [scroller.clientHeight, scroller.scrollHeight],
            offsets: Array.from(scroller.children, child => child.offsetTop),
            immediate: [scroller.scrollTop, __programmaticScrollEarly, __programmaticScrollLate]
          };
        })()
    )JS", "native-scrollspy-neutral-primitives.js");
    require(
        immediate == R"JSON({"position":"relative","fragments":["","","#section"],"hidden":[true,true,true],"range":[100,500],"offsets":[0,100,200,300,400],"immediate":[40,0,0]})JSON",
        "ScrollSpy neutral primitives were inconsistent before the queued scroll task: "
            + immediate);

    auto eventual = std::string{};
    for (auto attempt = 0; attempt < 250; ++attempt) {
        eventual = evaluate(engine, R"JS(
            ({
              scrollTop: document.getElementById('scrollspy-scroller').scrollTop,
              early: __programmaticScrollEarly,
              late: __programmaticScrollLate
            })
        )JS", "native-scrollspy-queued-scroll-result.js");
        if (eventual == R"JSON({"scrollTop":40,"early":1,"late":1})JSON") break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(
        eventual == R"JSON({"scrollTop":40,"early":1,"late":1})JSON",
        "programmatic scroll writes did not coalesce into one queued event: " + eventual);

    const auto unchanged = evaluate(engine, R"JS(
        (() => {
          const scroller = document.getElementById('scrollspy-scroller');
          scroller.scrollTop = 40;
          return [__programmaticScrollEarly, __programmaticScrollLate];
        })()
    )JS", "native-scrollspy-unchanged-scroll.js");
    require(
        unchanged == "[1,1]",
        "an unchanged programmatic offset synchronously altered event counts: " + unchanged);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    require(
        evaluate(engine, "[__programmaticScrollEarly, __programmaticScrollLate]",
            "native-scrollspy-unchanged-scroll-result.js") == "[1,1]",
        "an unchanged programmatic offset queued an additional scroll event");
}

void test_window_scroll_primitives(webscene_engine* engine)
{
    const auto result = evaluate(engine, R"JS(
        (() => {
          document.body.innerHTML = '<div style="width:1800px;height:1600px"></div>';
          const methods = [typeof scrollTo, typeof scroll, scrollTo === scroll];
          scrollTo(120, 140);
          const numeric = [scrollX, scrollY, pageXOffset, pageYOffset];
          scrollTo({ top: 210 });
          const topOnly = [scrollX, scrollY];
          scroll({ left: 170 });
          const leftOnly = [scrollX, scrollY];
          scrollTo(-20, 100000);
          const maximumY = document.documentElement.scrollHeight - innerHeight;
          const clamped = [scrollX, scrollY === maximumY, maximumY > 210];
          scrollTo(0, 0);
          const frame = document.createElement('iframe');
          document.body.appendChild(frame);
          const frameMethods = [
            typeof frame.contentWindow.scrollTo,
            typeof frame.contentWindow.scroll
          ];
          frame.remove();
          return { methods, numeric, topOnly, leftOnly, clamped, frameMethods };
        })()
    )JS", "native-window-scroll-primitives.js");
    require(
        result == R"JSON({"methods":["function","function",true],"numeric":[120,140,120,140],"topOnly":[120,210],"leftOnly":[170,210],"clamped":[0,true,true],"frameMethods":["function","function"]})JSON",
        "Window scrolling primitives were inconsistent: " + result);
}

void test_clipboard_write_text_host_handoff(webscene_engine* engine)
{
    execute(engine, R"JS(
        globalThis.__clipboardWriteState = 'pending';
        navigator.clipboard.writeText('VS CODE OSS CLIPBOARD').then(
          () => { __clipboardWriteState = 'fulfilled'; },
          error => { __clipboardWriteState = error.name + ':' + error.message; });
    )JS", "native-clipboard-write-text.js");
    require(evaluate(engine, "true", "native-clipboard-write-text-barrier.js") == "true",
        "Clipboard.writeText did not drain through the engine worker");
    const auto request = take_typed_host_request(engine);
    require(
        request.kind == WEBSCENE_HOST_REQUEST_CLIPBOARD_WRITE_V1
            && request.flags == WEBSCENE_HOST_REQUEST_CLIPBOARD_REPLACE_V1
            && request.id != 0U
            && request.content_type == "text/plain"
            && std::string(request.bytes.begin(), request.bytes.end())
                == "VS CODE OSS CLIPBOARD",
        "Clipboard.writeText did not preserve bytes in its typed host handoff");
    require(
        webscene_engine_complete_host_request_v1(
            engine, request.id, 0U, "text/plain",
            nullptr, 0U, nullptr) != 0,
        "Clipboard.writeText completion was rejected");
    require(
        evaluate(
            engine,
            "__clipboardWriteState",
            "native-clipboard-write-text-result.js") == R"("fulfilled")",
        "Clipboard.writeText promise did not fulfill after native completion");
}

void test_clipboard_read_host_completion(webscene_engine* engine)
{
    auto* inactive_engine = webscene_engine_create(0);
    require(inactive_engine != nullptr,
        "clipboard activation engine creation failed");
    execute(inactive_engine, R"JS(
        globalThis.__clipboardActivationState = 'pending';
        navigator.clipboard.readText().catch(
          error => { __clipboardActivationState = error.name; });
    )JS", "native-clipboard-read-without-activation.js");
    require(evaluate(inactive_engine, "__clipboardActivationState",
                "native-clipboard-read-without-activation-result.js")
                == R"("NotAllowedError")"
            && !take_typed_host_request(inactive_engine),
        "clipboard read without user activation reached the host");
    webscene_engine_destroy(inactive_engine);
    keyboard_input(engine, WEBSCENE_INPUT_KEY_DOWN, 'C', 9751U);
    evaluate(engine, "true", "native-clipboard-user-activation-barrier.js");
    execute(engine, R"JS(
        globalThis.__clipboardReadState = 'pending';
        navigator.clipboard.readText().then(
          value => { __clipboardReadState = value; },
          error => { __clipboardReadState = error.name; });
    )JS", "native-clipboard-read-text.js");
    require(evaluate(engine, "true", "native-clipboard-read-text-barrier.js") == "true",
        "Clipboard.readText did not drain through the engine worker");
    auto request = take_typed_host_request(engine);
    require(
        request.kind == WEBSCENE_HOST_REQUEST_CLIPBOARD_READ_V1
            && request.id != 0U && request.content_type == "text/plain"
            && request.bytes.empty(),
        "Clipboard.readText did not emit the typed host request");
    constexpr uint8_t text[] = {'n','a','t','i','v','e',' ','p','a','s','t','e'};
    require(
        webscene_engine_complete_host_request_v1(
            engine, request.id, 0U, "text/plain",
            text, sizeof(text), nullptr) != 0,
        "Clipboard.readText completion was rejected");
    require(
        evaluate(
            engine,
            "__clipboardReadState",
            "native-clipboard-read-text-result.js") == R"("native paste")",
        "Clipboard.readText did not resolve with native UTF-8 text");
    require(webscene_engine_complete_host_request_v1(
                engine, request.id, 0U, "text/plain",
                text, sizeof(text), nullptr) != 0,
        "duplicate stale clipboard completion was not safely admitted");

    execute(engine, R"JS(
        globalThis.__clipboardUnsupportedState = 'pending';
        navigator.clipboard.read().then(
          () => { __clipboardUnsupportedState = 'fulfilled'; },
          error => { __clipboardUnsupportedState = error.name; });
    )JS", "native-clipboard-read-unsupported.js");
    require(evaluate(engine, "true",
                "native-clipboard-read-unsupported-barrier.js") == "true",
        "unsupported Clipboard.read did not drain through the engine worker");
    request = take_typed_host_request(engine);
    require(request.kind == WEBSCENE_HOST_REQUEST_CLIPBOARD_READ_V1
                && request.content_type == "*/*",
        "Clipboard.read did not emit its wildcard typed host request");
    constexpr uint8_t unsupported[] = {1U};
    require(webscene_engine_complete_host_request_v1(
                engine, request.id, 0U, "application/octet-stream",
                unsupported, sizeof(unsupported), nullptr) != 0,
        "unsupported clipboard host completion was rejected before delivery");
    require(evaluate(engine,
                "__clipboardUnsupportedState",
                "native-clipboard-read-unsupported-result.js")
                == R"("NotSupportedError")",
        "Clipboard.read admitted an unsupported host MIME type");

    execute(engine, R"JS(
        globalThis.__clipboardCancelState = 'pending';
        navigator.clipboard.readText().then(
          () => { __clipboardCancelState = 'fulfilled'; },
          error => { __clipboardCancelState = error.name; });
    )JS", "native-clipboard-read-cancel.js");
    require(evaluate(engine, "true", "native-clipboard-read-cancel-barrier.js") == "true",
        "cancelled Clipboard.readText did not drain through the engine worker");
    request = take_typed_host_request(engine);
    require(
        webscene_engine_complete_host_request_v1(
            engine, request.id, 1U, nullptr, nullptr, 0U, nullptr) != 0,
        "Clipboard cancellation completion was rejected");
    require(
        evaluate(
            engine,
            "__clipboardCancelState",
            "native-clipboard-read-cancel-result.js") == R"("AbortError")",
        "Clipboard cancellation did not reject with AbortError");

    require(
        webscene_engine_complete_host_request_v1(
            engine, 0U, 0U, "text/plain", text, sizeof(text), nullptr) == 0,
        "zero host request ID was accepted");
    require(
        webscene_engine_complete_host_request_v1(
            engine, 1U, 3U, nullptr, nullptr, 0U, nullptr) == 0,
        "invalid host completion status was accepted");

    execute(engine, R"JS(
        globalThis.__clipboardQuotaState = 'pending';
        const operations = [];
        for (let index = 0; index < 17; ++index) {
          operations.push(navigator.clipboard.readText().catch(error => error.name));
        }
        Promise.all(operations).then(values => {
          __clipboardQuotaState = values;
        });
    )JS", "native-clipboard-queue-saturation.js");
    require(evaluate(engine, "true", "native-clipboard-queue-saturation-barrier.js") == "true",
        "clipboard saturation batch did not drain through the engine worker");
    for (unsigned index = 0; index < 16U; ++index) {
        const auto pending = take_typed_host_request(engine);
        require(pending.kind == WEBSCENE_HOST_REQUEST_CLIPBOARD_READ_V1,
            "clipboard queue lost a bounded pending read");
        require(webscene_engine_complete_host_request_v1(
                    engine, pending.id, 1U, nullptr, nullptr, 0U, nullptr) != 0,
            "clipboard queue cancellation was rejected");
    }
    require(!take_typed_host_request(engine),
        "clipboard queue exceeded its 16-operation limit");
    require(evaluate(engine,
                "__clipboardQuotaState.filter(value => value === 'AbortError').length === 16"
                " && __clipboardQuotaState.at(-1) === 'QuotaExceededError'",
                "native-clipboard-queue-saturation-result.js") == "true",
        "clipboard queue saturation did not reject explicitly");
}

void test_native_clipboard_shortcut_events(webscene_engine*)
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr, "native clipboard shortcut engine creation failed");
    execute_and_wait(engine, R"JS(
        document.body.innerHTML = '<textarea id="editor"></textarea>';
        const editor = document.getElementById('editor');
        globalThis.__clipboardShortcutEvents = [];
        globalThis.__clipboardShortcutKeys = [];
        globalThis.__clipboardExecCommandResults = [];
        globalThis.__clipboardPasteItems = 'pending';
        editor.addEventListener('keydown', event => {
          __clipboardShortcutKeys.push([
            event.key, event.metaKey, event.ctrlKey, event.defaultPrevented]);
          const command = event.metaKey && /^[cxv]$/i.test(event.key)
            ? ({c: 'copy', x: 'cut', v: 'paste'})[event.key.toLowerCase()]
            : undefined;
          if (command) {
            event.preventDefault();
            __clipboardExecCommandResults.push(document.execCommand(command));
          }
        });
        editor.addEventListener('copy', event => {
          event.clipboardData.setData('text/plain', 'shortcut copy');
          event.clipboardData.setData('text/html', '<b>shortcut copy</b>');
          event.preventDefault();
          __clipboardShortcutEvents.push([
            event.type, event.bubbles, event.cancelable,
            event.clipboardData.types.join(',')]);
        });
        editor.addEventListener('cut', event => {
          event.clipboardData.setData('text/plain', 'shortcut cut');
          event.preventDefault();
          __clipboardShortcutEvents.push([event.type]);
        });
        editor.addEventListener('paste', event => {
          event.preventDefault();
          __clipboardShortcutEvents.push([
            event.type, event.clipboardData.getData('text/plain')]);
          const items = Array.from(event.clipboardData.items);
          let synchronous = true;
          Promise.all(items.map(item => {
            if (item.kind === 'file') {
              const file = item.getAsFile();
              return file.arrayBuffer().then(buffer => [
                item.kind, item.type, file.name, file.size,
                Array.from(new Uint8Array(buffer)).join(','), synchronous
              ]);
            }
            return new Promise(resolve => {
              item.getAsString(value => resolve([
                item.kind, item.type, value, item.getAsFile(), synchronous]));
            });
          })).then(values => {
            __clipboardPasteItems = {
              values,
              length: event.clipboardData.items.length,
              first: event.clipboardData.items.item(0)?.type,
              missing: event.clipboardData.items.item(items.length),
              types: Array.from(event.clipboardData.types),
              files: Array.from(event.clipboardData.files).map(file => [
                file.name, file.type, file.size
              ])
            };
          });
          synchronous = false;
        });
        editor.focus();
    )JS", "native-clipboard-shortcuts-setup.js");

    require(evaluate(engine, "document.execCommand('copy')",
                "native-clipboard-exec-command-without-input.js") == "false",
        "legacy clipboard command ran without current native user input");
    require(!take_typed_host_request(engine),
        "legacy clipboard command without user input reached the host");

    constexpr auto modifier = WEBSCENE_INPUT_MODIFIER_META;
    webscene_engine_metrics metrics{};
    keyboard_input(engine, WEBSCENE_INPUT_KEY_DOWN, 'C', 9761U, modifier);
    webscene_engine_get_metrics(engine, &metrics);
    wait_for_consumed_inputs(engine, metrics.enqueued_inputs,
        "native copy shortcut was not consumed");
    const auto copy_state = evaluate(engine,
        "JSON.stringify({ events: __clipboardShortcutEvents,"
        " keys: __clipboardShortcutKeys, active: document.activeElement?.id,"
        " helper: typeof __webSceneClipboardShortcut })",
        "native-clipboard-copy-shortcut-state.js");
    auto request = take_typed_host_request(engine);
    require(request.kind == WEBSCENE_HOST_REQUEST_CLIPBOARD_WRITE_V1
                && request.flags == WEBSCENE_HOST_REQUEST_CLIPBOARD_REPLACE_V1
                && request.content_type == "text/plain"
                && std::string(request.bytes.begin(), request.bytes.end()) == "shortcut copy",
        "native copy event did not hand its plain text to the host: " + copy_state);
    require(webscene_engine_complete_host_request_v1(
                engine, request.id, 0U, "text/plain", nullptr, 0U, nullptr) != 0,
        "native copy plain-text completion was rejected");
    request = take_typed_host_request(engine);
    for (auto attempt = 0; attempt < 250 && !request; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        request = take_typed_host_request(engine);
    }
    require(request.kind == WEBSCENE_HOST_REQUEST_CLIPBOARD_WRITE_V1
                && request.flags == 0U
                && request.content_type == "text/html"
                && std::string(request.bytes.begin(), request.bytes.end())
                    == "<b>shortcut copy</b>",
        "native copy event did not preserve its HTML representation");
    require(webscene_engine_complete_host_request_v1(
                engine, request.id, 0U, "text/html", nullptr, 0U, nullptr) != 0,
        "native copy HTML completion was rejected");

    keyboard_input(engine, WEBSCENE_INPUT_KEY_DOWN, 'X', 9762U, modifier);
    webscene_engine_get_metrics(engine, &metrics);
    wait_for_consumed_inputs(engine, metrics.enqueued_inputs,
        "native cut shortcut was not consumed");
    request = take_typed_host_request(engine);
    require(request.kind == WEBSCENE_HOST_REQUEST_CLIPBOARD_WRITE_V1
                && request.flags == WEBSCENE_HOST_REQUEST_CLIPBOARD_REPLACE_V1
                && request.content_type == "text/plain"
                && std::string(request.bytes.begin(), request.bytes.end()) == "shortcut cut",
        "native cut event did not hand its plain text to the host");
    require(webscene_engine_complete_host_request_v1(
                engine, request.id, 0U, "text/plain", nullptr, 0U, nullptr) != 0,
        "native cut completion was rejected");

    keyboard_input(engine, WEBSCENE_INPUT_KEY_DOWN, 'V', 9763U, modifier);
    webscene_engine_get_metrics(engine, &metrics);
    wait_for_consumed_inputs(engine, metrics.enqueued_inputs,
        "native paste shortcut was not consumed");
    request = take_typed_host_request(engine);
    require(request.kind == WEBSCENE_HOST_REQUEST_CLIPBOARD_READ_V1
                && request.content_type == "*/*",
        "native paste event did not request a supported item from the host");
    constexpr uint8_t paste[] = {'s','h','o','r','t','c','u','t',' ','p','a','s','t','e'};
    require(webscene_engine_complete_host_request_v1(
                engine, request.id, 0U, "text/plain",
                paste, sizeof(paste), nullptr) != 0,
        "native paste completion was rejected");
    for (auto attempt = 0; attempt < 250; ++attempt) {
        if (evaluate(engine,
                "__clipboardShortcutEvents.length === 3"
                " && __clipboardExecCommandResults.length === 3"
                " && __clipboardPasteItems !== 'pending'",
                "native-clipboard-shortcut-completion.js") == "true") {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto shortcut_events = evaluate(engine,
        "__clipboardShortcutEvents",
        "native-clipboard-shortcuts-result.js");
    require(shortcut_events
            == R"JSON([["copy",true,true,"text/plain,text/html"],["cut"],["paste","shortcut paste"]])JSON",
        "native clipboard shortcuts did not dispatch browser-compatible events: "
            + shortcut_events);
    const auto paste_items = evaluate(engine,
        "__clipboardPasteItems",
        "native-clipboard-paste-items-result.js");
    require(paste_items
            == R"JSON({"values":[["string","text/plain","shortcut paste",null,false]],"length":1,"first":"text/plain","missing":null,"types":["text/plain"],"files":[]})JSON",
        "native paste did not expose a Code OSS-compatible DataTransferItemList: "
            + paste_items);

    execute(engine, "__clipboardPasteItems = 'pending'",
        "native-clipboard-image-paste-reset.js");
    keyboard_input(engine, WEBSCENE_INPUT_KEY_DOWN, 'V', 9764U, modifier);
    webscene_engine_get_metrics(engine, &metrics);
    wait_for_consumed_inputs(engine, metrics.enqueued_inputs,
        "native image paste shortcut was not consumed");
    request = take_typed_host_request(engine);
    require(request.kind == WEBSCENE_HOST_REQUEST_CLIPBOARD_READ_V1
                && request.content_type == "*/*",
        "native image paste did not request a supported item from the host");
    constexpr uint8_t png[] = {137U, 80U, 78U, 71U, 13U, 10U, 26U, 10U};
    require(webscene_engine_complete_host_request_v1(
                engine, request.id, 0U, "image/png",
                png, sizeof(png), nullptr) != 0,
        "native image paste completion was rejected");
    for (auto attempt = 0; attempt < 250; ++attempt) {
        if (evaluate(engine,
                "__clipboardShortcutEvents.length === 4"
                " && __clipboardExecCommandResults.length === 4"
                " && __clipboardPasteItems !== 'pending'",
                "native-clipboard-image-completion.js") == "true") {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto image_paste_items = evaluate(engine,
        "__clipboardPasteItems",
        "native-clipboard-image-paste-items-result.js");
    require(image_paste_items
            == R"JSON({"values":[["file","image/png","clipboard.png",8,"137,80,78,71,13,10,26,10",false]],"length":1,"first":"image/png","missing":null,"types":["Files"],"files":[["clipboard.png","image/png",8]]})JSON",
        "native image paste did not expose a bounded file item to Code OSS: "
            + image_paste_items);
    require(evaluate(engine,
                "__clipboardShortcutEvents.at(-1)[0] === 'paste'"
                " && __clipboardShortcutEvents.at(-1)[1] === ''",
                "native-clipboard-image-event-result.js") == "true",
        "native image paste exposed non-text bytes through getData");
    const auto exec_command_results = evaluate(engine,
        "__clipboardExecCommandResults",
        "native-clipboard-exec-command-results.js");
    require(exec_command_results == "[true,true,true,true]",
        "prevented native shortcuts did not execute document clipboard commands: "
            + exec_command_results);
    require(evaluate(engine, "document.execCommand('unsupported-command')",
                "native-clipboard-unsupported-exec-command.js") == "false",
        "unsupported legacy command claimed success");
    require(!take_typed_host_request(engine),
        "native clipboard shortcuts retained an unexpected host request");
    webscene_engine_destroy(engine);
}

void test_native_image_clipboard_maximum_payload(webscene_engine*)
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr,
        "maximum image clipboard engine creation failed");
    execute_and_wait(engine, R"JS(
        document.body.innerHTML = '<textarea id="image-paste"></textarea>';
        const editor = document.getElementById('image-paste');
        globalThis.__maximumImagePaste = 'pending';
        editor.addEventListener('keydown', event => {
          if (event.metaKey && event.key.toLowerCase() === 'v') {
            event.preventDefault();
            document.execCommand('paste');
          }
        });
        editor.addEventListener('paste', event => {
          event.preventDefault();
          const item = event.clipboardData.items.item(0);
          const file = item?.getAsFile();
          if (!file) {
            __maximumImagePaste = 'missing-file';
            return;
          }
          file.arrayBuffer().then(buffer => {
            const bytes = new Uint8Array(buffer);
            __maximumImagePaste = {
              kind: item.kind,
              type: item.type,
              name: file.name,
              sizeExact: file.size === 16 * 1024 * 1024,
              lengthExact: bytes.byteLength === 16 * 1024 * 1024,
              first: bytes[0],
              last: bytes[bytes.length - 1],
              files: event.clipboardData.files.length,
              types: Array.from(event.clipboardData.types)
            };
          });
        });
        editor.focus();
    )JS", "native-maximum-image-paste-setup.js");

    keyboard_input(engine, WEBSCENE_INPUT_KEY_DOWN, 'V', 9771U,
        WEBSCENE_INPUT_MODIFIER_META);
    webscene_engine_metrics metrics{};
    webscene_engine_get_metrics(engine, &metrics);
    wait_for_consumed_inputs(engine, metrics.enqueued_inputs,
        "maximum image paste shortcut was not consumed");
    const auto request = take_typed_host_request(engine);
    require(request.kind == WEBSCENE_HOST_REQUEST_CLIPBOARD_READ_V1
                && request.content_type == "*/*",
        "maximum image paste did not request a supported host item");

    constexpr size_t maximum_bytes = 16U * 1024U * 1024U;
    std::vector<uint8_t> png(maximum_bytes, 97U);
    png.front() = 137U;
    png.back() = 10U;
    require(webscene_engine_complete_host_request_v1(
                engine, request.id, 0U, "image/png",
                png.data(), maximum_bytes + 1U, nullptr) == 0,
        "image clipboard completion admitted a payload above 16 MiB");
    const auto started = std::chrono::steady_clock::now();
    require(webscene_engine_complete_host_request_v1(
                engine, request.id, 0U, "image/png",
                png.data(), png.size(), nullptr) != 0,
        "maximum image clipboard completion was rejected");
    for (auto attempt = 0; attempt < 500; ++attempt) {
        if (evaluate(engine,
                "__maximumImagePaste !== 'pending'",
                "native-maximum-image-paste-completion.js") == "true") {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto result = evaluate(engine,
        "__maximumImagePaste",
        "native-maximum-image-paste-result.js");
    require(result
            == R"JSON({"kind":"file","type":"image/png","name":"clipboard.png","sizeExact":true,"lengthExact":true,"first":137,"last":10,"files":1,"types":["Files"]})JSON",
        "maximum image paste changed bytes or transfer metadata: " + result);
    require(!take_typed_host_request(engine),
        "maximum image paste retained a host request");
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    require(elapsed < 5.0,
        "maximum image clipboard paste exceeded five seconds");
    std::cout << "Image clipboard maximum-payload gate: bytes="
              << maximum_bytes << " elapsed=" << elapsed << "s\n";
    webscene_engine_destroy(engine);
}

void test_native_legacy_clipboard_completion_stress(webscene_engine*)
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr,
        "legacy clipboard completion stress engine creation failed");
    execute_and_wait(engine, R"JS(
        document.body.innerHTML = '<textarea id="legacy-copy-stress"></textarea>';
        const editor = document.getElementById('legacy-copy-stress');
        globalThis.__legacyCopyStressEvents = 0;
        globalThis.__legacyCopyStressResults = [];
        editor.addEventListener('keydown', event => {
          if (event.metaKey && event.key.toLowerCase() === 'c') {
            event.preventDefault();
            __legacyCopyStressResults.push(document.execCommand('copy'));
          }
        });
        editor.addEventListener('copy', event => {
          event.clipboardData.setData('text/plain', 'legacy stress');
          event.preventDefault();
          ++__legacyCopyStressEvents;
        });
        editor.focus();
    )JS", "native-legacy-clipboard-stress-setup.js");

    constexpr unsigned operation_count = 10000U;
    constexpr unsigned batch_size = 16U;
    constexpr auto modifier = WEBSCENE_INPUT_MODIFIER_META;
    uint64_t sequence = 20000U;
    const auto started = std::chrono::steady_clock::now();
    for (unsigned offset = 0; offset < operation_count; offset += batch_size) {
        for (unsigned index = 0; index < batch_size; ++index) {
            keyboard_input(
                engine, WEBSCENE_INPUT_KEY_DOWN, 'C', sequence++, modifier);
        }
        webscene_engine_metrics metrics{};
        webscene_engine_get_metrics(engine, &metrics);
        wait_for_consumed_inputs(
            engine, metrics.enqueued_inputs,
            "legacy clipboard stress input was not consumed");
        for (unsigned index = 0; index < batch_size; ++index) {
            const auto request = take_typed_host_request(engine);
            require(
                request.kind == WEBSCENE_HOST_REQUEST_CLIPBOARD_WRITE_V1
                    && request.flags
                        == WEBSCENE_HOST_REQUEST_CLIPBOARD_REPLACE_V1
                    && request.content_type == "text/plain"
                    && std::string(request.bytes.begin(), request.bytes.end())
                        == "legacy stress",
                "legacy clipboard stress request changed");
            require(webscene_engine_complete_host_request_v1(
                        engine, request.id, 0U, "text/plain",
                        nullptr, 0U, nullptr) != 0,
                "legacy clipboard stress completion was rejected");
        }
        const auto expected = offset + batch_size;
        require(evaluate(
                    engine,
                    "__legacyCopyStressEvents === " + std::to_string(expected)
                        + " && __legacyCopyStressResults.length === "
                        + std::to_string(expected)
                        + " && __legacyCopyStressResults.every(Boolean)",
                    "native-legacy-clipboard-stress-barrier.js") == "true",
            "legacy clipboard stress did not drain completions in order");
    }
    require(!take_typed_host_request(engine),
        "legacy clipboard stress retained a host request");
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    require(elapsed < 15.0,
        "10,000 legacy clipboard completions exceeded fifteen seconds");
    std::cout << "Legacy clipboard completion gate: operations="
              << operation_count << " pendingHighWater=" << batch_size
              << " elapsed=" << elapsed << "s\n";
    // Reproduce the packaged close boundary: the worker destroys all resolved
    // promise/microtask state immediately after the last public-ABI barrier.
    webscene_engine_destroy(engine);
}

void test_native_pending_legacy_clipboard_shutdown(webscene_engine*)
{
    auto* engine = webscene_engine_create(0);
    require(engine != nullptr,
        "pending legacy clipboard shutdown engine creation failed");
    execute_and_wait(engine, R"JS(
        document.body.innerHTML = '<textarea id="pending-copy"></textarea>';
        const editor = document.getElementById('pending-copy');
        editor.addEventListener('keydown', event => {
          if (event.metaKey && event.key.toLowerCase() === 'c') {
            event.preventDefault();
            document.execCommand('copy');
          }
        });
        editor.addEventListener('copy', event => {
          event.clipboardData.setData('text/plain', 'pending close');
          event.preventDefault();
        });
        editor.focus();
    )JS", "native-pending-legacy-clipboard-shutdown-setup.js");

    keyboard_input(
        engine, WEBSCENE_INPUT_KEY_DOWN, 'C', 31001U,
        WEBSCENE_INPUT_MODIFIER_META);
    webscene_engine_metrics metrics{};
    webscene_engine_get_metrics(engine, &metrics);
    wait_for_consumed_inputs(
        engine, metrics.enqueued_inputs,
        "pending legacy clipboard shutdown input was not consumed");
    const auto request = take_typed_host_request(engine);
    require(
        request.kind == WEBSCENE_HOST_REQUEST_CLIPBOARD_WRITE_V1
            && request.content_type == "text/plain"
            && std::string(request.bytes.begin(), request.bytes.end())
                == "pending close",
        "pending legacy clipboard shutdown did not retain its host promise");

    // A native window may close before the operating-system host completes a
    // request. Destruction must release the pending persistent resolver while
    // its V8 isolate is still alive.
    webscene_engine_destroy(engine);
}

void test_clipboard_maximum_payload_gate(webscene_engine* engine)
{
    constexpr size_t maximum_bytes = 16U * 1024U * 1024U;
    const auto started = std::chrono::steady_clock::now();
    execute(engine, R"JS(
        globalThis.__maximumClipboardState = 'pending';
        const bytes = new Uint8Array(16 * 1024 * 1024);
        bytes.fill(97);
        navigator.clipboard.write([
          new ClipboardItem({ 'text/plain': new Blob([bytes], { type: 'text/plain' }) })
        ]).then(
          () => { __maximumClipboardState = 'fulfilled'; },
          error => { __maximumClipboardState = error.name; });
    )JS", "native-clipboard-maximum-payload.js");
    require(evaluate(engine, "true", "native-clipboard-maximum-payload-barrier.js") == "true",
        "maximum clipboard payload did not drain through the engine worker");
    const auto request = take_typed_host_request(engine);
    require(request.kind == WEBSCENE_HOST_REQUEST_CLIPBOARD_WRITE_V1
                && request.content_type == "text/plain"
                && request.bytes.size() == maximum_bytes
                && request.bytes.front() == 'a'
                && request.bytes.back() == 'a',
        "maximum clipboard payload changed in the typed host ABI");
    require(webscene_engine_complete_host_request_v1(
                engine, request.id, 0U, "text/plain",
                nullptr, 0U, nullptr) != 0,
        "maximum clipboard payload completion was rejected");
    require(evaluate(engine, "__maximumClipboardState",
                "native-clipboard-maximum-payload-result.js")
                == R"("fulfilled")",
        "maximum clipboard payload promise did not settle");
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    require(elapsed < 5.0,
        "maximum clipboard payload round trip exceeded five seconds");
    std::cout << "Typed clipboard maximum-payload gate: bytes="
              << maximum_bytes << " elapsed=" << elapsed << "s\n";
}

void test_clipboard_small_round_trip_performance(webscene_engine* engine)
{
    constexpr unsigned operation_count = 10000U;
    constexpr unsigned batch_size = 16U;
    execute(engine, "globalThis.__clipboardPerformanceCompleted = 0",
        "native-clipboard-performance-setup.js");
    const auto started = std::chrono::steady_clock::now();
    for (unsigned offset = 0; offset < operation_count; offset += batch_size) {
        require(evaluate(engine, R"JS(
            for (let index = 0; index < 16; ++index) {
              navigator.clipboard.writeText('x').then(() => {
                ++globalThis.__clipboardPerformanceCompleted;
              });
            }
            true
        )JS", "native-clipboard-performance-batch.js") == "true",
            "typed clipboard performance batch did not drain through the engine worker");
        for (unsigned index = 0; index < batch_size; ++index) {
            const auto request = take_typed_host_request(engine);
            require(request.kind == WEBSCENE_HOST_REQUEST_CLIPBOARD_WRITE_V1
                    && request.id != 0U
                    && request.content_type == "text/plain"
                    && request.bytes == std::vector<uint8_t>{'x'},
                "typed clipboard request changed under round-trip load");
            require(webscene_engine_complete_host_request_v1(
                        engine, request.id, 0U, "text/plain",
                        nullptr, 0U, nullptr) != 0,
                "typed clipboard completion was rejected under load");
        }
    }
    require(evaluate(engine,
                "__clipboardPerformanceCompleted === 10000",
                "native-clipboard-performance-result.js") == "true",
        "typed clipboard promises did not all settle under load");
    require(!take_typed_host_request(engine),
        "typed clipboard round-trip retained a host request");
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    require(elapsed < 10.0,
        "10,000 typed clipboard round trips exceeded ten seconds");
    std::cout << "Typed clipboard round-trip gate: operations="
              << operation_count << " pendingHighWater=" << batch_size
              << " payloadBytes=1 elapsed=" << elapsed << "s\n";
}

void test_fullscreen_host_completion(webscene_engine* engine)
{
    execute(engine, R"JS(
        document.body.innerHTML = '<main id="surface">fullscreen</main>';
        globalThis.__fullscreenEvents = [];
        globalThis.__fullscreenState = 'pending';
        document.addEventListener('fullscreenchange', () => {
          __fullscreenEvents.push(document.fullscreenElement?.id || null);
        });
        document.getElementById('surface').requestFullscreen().then(
          () => { __fullscreenState = document.fullscreenElement?.id || 'missing'; },
          error => { __fullscreenState = error.name; });
    )JS", "native-fullscreen-enter.js");
    require(evaluate(engine, "true", "native-fullscreen-enter-barrier.js") == "true",
        "fullscreen enter did not drain through the engine worker");
    auto request = take_typed_host_request(engine);
    require(
        request.kind == WEBSCENE_HOST_REQUEST_FULLSCREEN_ENTER_V1
            && request.id != 0U && request.target_node_id != 0U,
        "Element.requestFullscreen did not emit a typed host request");
    require(webscene_engine_complete_host_request_v1(
                engine, request.id, 0U, nullptr,
                nullptr, 0U, nullptr) != 0,
        "fullscreen enter completion was rejected");
    require(evaluate(
                engine,
                "JSON.stringify([__fullscreenState,__fullscreenEvents])",
                "native-fullscreen-enter-result.js")
            == R"("[\"surface\",[\"surface\"]]")",
        "fullscreen enter state/event did not reach the document");

    execute(engine, R"JS(
        globalThis.__fullscreenExitState = 'pending';
        document.exitFullscreen().then(
          () => { __fullscreenExitState = document.fullscreenElement === null; },
          error => { __fullscreenExitState = error.name; });
    )JS", "native-fullscreen-exit.js");
    require(evaluate(engine, "true", "native-fullscreen-exit-barrier.js") == "true",
        "fullscreen exit did not drain through the engine worker");
    request = take_typed_host_request(engine);
    require(request.kind == WEBSCENE_HOST_REQUEST_FULLSCREEN_EXIT_V1
                && request.id != 0U,
        "Document.exitFullscreen did not emit a typed host request");
    require(webscene_engine_complete_host_request_v1(
                engine, request.id, 0U, nullptr,
                nullptr, 0U, nullptr) != 0,
        "fullscreen exit completion was rejected");
    require(evaluate(
                engine,
                "JSON.stringify([__fullscreenExitState,__fullscreenEvents])",
                "native-fullscreen-exit-result.js")
            == R"("[true,[\"surface\",null]]")",
        "fullscreen exit state/event did not reach the document");

    require(evaluate(engine, "(__fullscreenEvents = [], true)",
                "native-fullscreen-reset-events.js") == "true",
        "fullscreen event reset did not drain through the engine worker");
    require(webscene_engine_set_window_fullscreen_v1(engine, 1U) != 0,
        "native fullscreen entry was rejected");
    require(evaluate_until_equals(engine,
                "document.fullscreenElement === document.body",
                "native-fullscreen-state-entered.js", "true") == "true",
        "native-initiated fullscreen state did not reach the document");
    require(webscene_engine_set_window_fullscreen_v1(engine, 0U) != 0,
        "native fullscreen exit was rejected");
    require(evaluate_until_equals(engine,
                "document.fullscreenElement === null",
                "native-fullscreen-state-exited.js", "true") == "true",
        "native-initiated fullscreen exit did not reach the document");
    require(evaluate(engine, "__fullscreenEvents.length",
                "native-fullscreen-event-count.js") == "2",
        "native-initiated fullscreen did not dispatch enter and exit events");
}
