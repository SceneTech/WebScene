void test_abort_signal_composition_contract()
{
    auto* engine = webscene_engine_create(64);
    require(engine != nullptr,
        "AbortSignal composition engine was not created");

    require(evaluate(engine, R"JS(
        (() => {
          const controller = new AbortController();
          let illegal = null;
          try { new AbortSignal(); } catch (error) { illegal = error.name; }
          return JSON.stringify([
            typeof AbortSignal,
            typeof AbortSignal.abort,
            typeof AbortSignal.timeout,
            typeof AbortSignal.any,
            controller.signal instanceof AbortSignal,
            controller.signal instanceof EventTarget,
            Object.prototype.toString.call(controller.signal),
            Object.prototype.toString.call(controller),
            AbortSignal.length,
            AbortSignal.abort.length,
            AbortSignal.timeout.length,
            AbortSignal.any.length,
            AbortController.length,
            AbortController.prototype.abort.length,
            illegal
          ]);
        })()
    )JS", "native-abort-signal-shape.js")
            == R"(["function","function","function","function",true,true,"[object AbortSignal]","[object AbortController]",0,0,1,1,0,0,"TypeError"])",
        "AbortSignal or AbortController browser shape changed");

    require(evaluate(engine, R"JS(
        (() => {
          const defaultSignal = AbortSignal.abort();
          const custom = { marker: 7 };
          const customSignal = AbortSignal.abort(custom);
          let thrown = null;
          try { customSignal.throwIfAborted(); } catch (error) { thrown = error; }
          return JSON.stringify([
            defaultSignal.aborted,
            defaultSignal.reason.name,
            customSignal.reason === custom,
            thrown === custom
          ]);
        })()
    )JS", "native-abort-signal-abort.js")
            == R"([true,"AbortError",true,true])",
        "AbortSignal.abort reason or throwIfAborted behavior changed");

    execute(engine, R"JS(
        globalThis.__abortFirst = new AbortController();
        globalThis.__abortSecond = new AbortController();
        globalThis.__abortEvents = [];
        globalThis.__abortCombined = AbortSignal.any([
          __abortFirst.signal, __abortSecond.signal, __abortFirst.signal
        ]);
        __abortCombined.onabort = event => {
          __abortEvents.push([
            event.type,
            event.target === __abortCombined,
            __abortCombined.reason
          ]);
        };
        __abortSecond.abort('second');
        __abortFirst.abort('first');
    )JS", "native-abort-signal-any.js");
    require(evaluate(engine, R"JS(
        JSON.stringify([
          __abortCombined.aborted,
          __abortCombined.reason,
          __abortEvents
        ])
    )JS", "native-abort-signal-any-result.js")
            == R"([true,"second",[["abort",true,"second"]]])",
        "AbortSignal.any did not preserve first settlement or event identity");

    require(evaluate(engine, R"JS(
        (() => {
          const first = AbortSignal.abort('first');
          const second = AbortSignal.abort('second');
          let iteratorClosed = false;
          function* invalid() {
            try { yield first; yield {}; }
            finally { iteratorClosed = true; }
          }
          let invalidName = null;
          let negativeName = null;
          let missingName = null;
          try { AbortSignal.any(invalid()); } catch (error) { invalidName = error.name; }
          try { AbortSignal.timeout(-1); } catch (error) { negativeName = error.name; }
          try { AbortSignal.timeout(); } catch (error) { missingName = error.name; }
          return JSON.stringify([
            AbortSignal.any([first, second]).reason,
            iteratorClosed,
            invalidName,
            negativeName,
            missingName
          ]);
        })()
    )JS", "native-abort-signal-errors.js")
            == R"(["first",true,"TypeError","RangeError","TypeError"])",
        "AbortSignal iterable, ordering, or timeout errors changed");

    execute(engine, R"JS(
        globalThis.__abortTimedEvents = 0;
        globalThis.__abortTimed = AbortSignal.timeout(0);
        __abortTimed.addEventListener('abort', () => ++__abortTimedEvents);
    )JS", "native-abort-signal-timeout.js");
    require(evaluate(engine,
                "JSON.stringify([__abortTimed.aborted, __abortTimedEvents])",
                "native-abort-signal-before-timeout.js")
            == R"([false,0])",
        "AbortSignal.timeout settled synchronously");
    require(evaluate_until_equals(
                engine,
                "JSON.stringify([__abortTimed.aborted, __abortTimed.reason.name, __abortTimedEvents])",
                "native-abort-signal-timeout-drain.js",
                R"([true,"TimeoutError",1])")
            == R"([true,"TimeoutError",1])",
        "AbortSignal.timeout did not settle on a timer task");

    execute(engine, R"JS(
        globalThis.__abortLong = AbortSignal.timeout(1000000);
        globalThis.__abortNever = AbortSignal.any([new AbortController().signal]);
    )JS", "native-abort-signal-retirement.js");
    webscene_engine_destroy(engine);
}
