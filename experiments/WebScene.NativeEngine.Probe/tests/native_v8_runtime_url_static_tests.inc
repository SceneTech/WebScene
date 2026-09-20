void test_url_static_helpers_contract()
{
    auto* engine = webscene_engine_create(64);
    require(engine != nullptr, "URL static helper engine was not created");

    require(evaluate(engine, R"JS(
        (() => {
          let missingCanParse = null;
          let missingParse = null;
          try { URL.canParse(); } catch (error) { missingCanParse = error.name; }
          try { URL.parse(); } catch (error) { missingParse = error.name; }
          const parsed = URL.parse('../c?q=1#x', 'https://example.test/a/b');
          const custom = URL.parse('custom:value');
          return JSON.stringify([
            typeof URL.canParse,
            typeof URL.parse,
            URL.canParse.length,
            URL.parse.length,
            URL.canParse('https://example.test/path'),
            URL.canParse('../c', 'https://example.test/a/b'),
            URL.canParse('../c'),
            URL.canParse('https://example.test', 'invalid'),
            URL.canParse('http://'),
            URL.canParse('custom:value'),
            parsed instanceof URL,
            parsed && parsed.href,
            custom && custom.protocol,
            URL.parse('relative') === null,
            URL.parse('http://') === null,
            missingCanParse,
            missingParse
          ]);
        })()
    )JS", "native-url-static-shape.js")
            == R"(["function","function",1,1,true,true,false,false,false,true,true,"https://example.test/c?q=1#x","custom:",true,true,"TypeError","TypeError"])",
        "URL static helper shape or parse results changed");

    require(evaluate(engine, R"JS(
        (() => {
          let valueConversions = 0;
          let baseConversions = 0;
          const value = { toString() { ++valueConversions; return 'child'; } };
          const base = { toString() { ++baseConversions; return 'https://example.test/root/'; } };
          const parsed = URL.parse(value, base);
          return JSON.stringify([parsed.href, valueConversions, baseConversions]);
        })()
    )JS", "native-url-static-conversion.js")
            == R"(["https://example.test/root/child",1,1])",
        "URL.parse converted an input more than once");

    webscene_engine_destroy(engine);
}
