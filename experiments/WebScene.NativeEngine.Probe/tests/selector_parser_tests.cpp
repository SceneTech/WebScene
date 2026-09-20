#include "webscene_selector_parser.h"
#include "webscene_css_invalidation.h"
#include "webscene_css_matching.h"
#include "webscene_css_compound.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using webscene_native::parse_selector_syntax;

[[noreturn]] void fail(std::string_view message)
{
    std::cerr << "selector parser test failed: " << message << '\n';
    std::exit(1);
}

void require(bool condition, std::string_view message)
{
    if (!condition) fail(message);
}

void require_selector_shape()
{
    const auto parsed = parse_selector_syntax(
        R"CSS(main > .card[data-label="a,b"] + a:hover::before, #escaped\+id ~ section:nth-child(2n + 1))CSS");
    require(static_cast<bool>(parsed), parsed.error);
    require(parsed.selectors.size() == 2U, "selector-list cardinality");

    const auto& first = parsed.selectors[0];
    require(first.compounds.size() == 3U, "first selector compound count");
    require(first.combinators.size() == 2U, "first selector combinator count");
    require(first.combinators[0] == '>' && first.combinators[1] == '+',
        "first selector combinator values");
    require(first.compounds[0] == "main", "type compound");
    require(first.compounds[1].find(".card") != std::string::npos,
        "class compound serialization");
    require(first.compounds[1].find("a,b") != std::string::npos,
        "attribute comma remains inside compound");
    require(first.compounds[2].find("a:hover::before") != std::string::npos,
        "pseudo-element remains on originating compound");

    const auto& second = parsed.selectors[1];
    require(second.compounds.size() == 2U, "second selector compound count");
    require(second.combinators.size() == 1U && second.combinators[0] == '~',
        "second selector sibling combinator");
}

void require_specificity()
{
    const auto parsed = parse_selector_syntax(
        ".card:where(#ignored), article:is(.note, #winner), div:not(.a, #b)");
    require(static_cast<bool>(parsed), parsed.error);
    require(parsed.selectors.size() == 3U, "specificity selector count");
    require(parsed.selectors[0].specificity == 0x000100U,
        ":where contributes zero specificity");
    require(parsed.selectors[1].specificity == 0x010001U,
        ":is uses its most specific argument");
    require(parsed.selectors[2].specificity == 0x010001U,
        ":not uses its most specific argument");
}

void require_validation()
{
    require(!parse_selector_syntax("").operator bool(), "empty selector rejected");
    require(!parse_selector_syntax(".a,").operator bool(), "empty list item rejected");
    require(!parse_selector_syntax("div >").operator bool(), "dangling combinator rejected");
    require(!parse_selector_syntax("div:unknown-state").operator bool(),
        "unknown pseudo-class rejected");
    require(!parse_selector_syntax("[data-value=]").operator bool(),
        "missing attribute value rejected");
    require(!parse_selector_syntax(":lang(c++)").operator bool(),
        "invalid language identifier rejected");
    require(static_cast<bool>(parse_selector_syntax(":lang(ara\\b)")),
        "escaped language identifier accepted");
    require(static_cast<bool>(parse_selector_syntax("div:is(.valid, :unknown-state)")),
        "forgiving :is list retains valid selector");
    require(static_cast<bool>(parse_selector_syntax(":local-link(0)"))
        &&static_cast<bool>(parse_selector_syntax(":local-link(+12)"))
        &&static_cast<bool>(parse_selector_syntax(":local-link(-0)"))
        &&static_cast<bool>(parse_selector_syntax(":local-link(/* bounded */ 2)")),
        "local-link must accept one non-negative integer argument and signed zero");
    for(const auto invalid:{":local-link()",":local-link(-1)",
            ":local-link(1.0)",":local-link(1 2)",":local-link(all)",
            ":local-link(2147483648)"})
        require(!parse_selector_syntax(invalid),
            "malformed, negative, or overflowing local-link argument accepted");
}

void require_wtf8_domstring_round_trip()
{
    auto selector = std::string("#");
    selector.append("\xF3\xB0\x80\x80", 4U);
    selector.append("\xED\xA0\xBD", 3U);
    selector += "surrogateFirst";
    const auto parsed = parse_selector_syntax(selector);
    require(static_cast<bool>(parsed), parsed.error);
    require(parsed.selectors.size() == 1U, "WTF-8 selector count");
    require(parsed.selectors[0].serialized == selector,
        "lone surrogate and private-use sentinel round-trip through Servo");
    require(parsed.selectors[0].compounds.size() == 1U
            && parsed.selectors[0].compounds[0] == selector,
        "compiled compound restores the original WTF-8 bytes");

    constexpr char invalid[]{static_cast<char>(0xFF)};
    require(!parse_selector_syntax(std::string_view(invalid, 1U)),
        "non-WTF-8 invalid input remains rejected");
}

void require_nested_has_selector_list_tokenization()
{
    std::vector<std::string> arms;
    const auto matched = webscene_native::css::css_selector_list_any(
        R"(.monaco-icon-label:is(.item-color, .badge), [data-token="a,b"], .fallback)",
        [&](std::string_view arm) {
            arms.emplace_back(arm);
            return arm == ".fallback";
        });
    require(matched, "depth-aware selector-list traversal did not reach the final arm");
    require(arms == std::vector<std::string>{
            ".monaco-icon-label:is(.item-color, .badge)",
            "[data-token=\"a,b\"]",
            ".fallback"},
        "nested function or quoted attribute comma split an outer :has() arm");
}

void require_namespace_resolution()
{
    using namespace webscene_native;
    using namespace webscene_native::css;
    selector_namespace_context namespaces;
    namespaces.prefixes.emplace(
        "svg", "http://www.w3.org/2000/svg");
    namespaces.default_namespace = "urn:webscene:default";
    namespaces.has_default_namespace = true;

    const auto prefixed = compile_selector("svg|circle", &namespaces);
    require(prefixed.compiled_compounds.size() == 1U
        && prefixed.compiled_compounds[0].tag == "circle"
        && prefixed.compiled_compounds[0].namespace_uri
            == "http://www.w3.org/2000/svg",
        "declared namespace prefix was not retained by compiled matching");
    const auto unprefixed = compile_selector("circle", &namespaces);
    require(unprefixed.compiled_compounds.size() == 1U
        && unprefixed.compiled_compounds[0].namespace_uri
            == "urn:webscene:default",
        "default namespace was not applied to an unprefixed type selector");
    const auto any = compile_selector("*|circle", &namespaces);
    require(any.compiled_compounds.size() == 1U
        && !any.compiled_compounds[0].namespace_uri.has_value(),
        "explicit any namespace must not constrain matching");
    const auto empty = compile_selector("|circle", &namespaces);
    require(empty.compiled_compounds.size() == 1U
        && empty.compiled_compounds[0].namespace_uri == std::string{},
        "explicit empty namespace was not retained");
    require(compile_selector("missing|circle", &namespaces).compounds.empty(),
        "undeclared namespace prefix must reject the selector");
}

void require_attribute_namespace_resolution()
{
    using namespace webscene_native;
    using namespace webscene_native::css;
    selector_namespace_context first;
    first.prefixes.emplace("p", "urn:webscene:first");
    const auto selector = compile_selector(
        R"CSS([p|name="VALUE" i][*|token~="beta" s][|plain^="pre"])CSS",
        &first);
    require(selector.compiled_compounds.size() == 1U
        && selector.compiled_compounds[0].attributes.size() == 3U,
        "Servo attribute metadata was not retained per compound");
    const auto& exact = selector.compiled_compounds[0].attributes[0];
    const auto& any = selector.compiled_compounds[0].attributes[1];
    const auto& empty = selector.compiled_compounds[0].attributes[2];
    require(exact.local_name == "name"
        && exact.namespace_uri == "urn:webscene:first"
        && exact.operator_kind == 1U
        && exact.value == "VALUE"
        && exact.case_sensitivity == 1U,
        "prefixed attribute selector identity/operator/case flag changed");
    require(any.local_name == "token" && !any.namespace_uri.has_value()
        && any.operator_kind == 2U && any.case_sensitivity == 2U,
        "any-namespace attribute selector metadata changed");
    require(empty.local_name == "plain" && empty.namespace_uri == std::string{}
        && empty.operator_kind == 4U,
        "empty-namespace attribute selector metadata changed");
    require(compile_selector("[missing|name]", &first).compounds.empty(),
        "undeclared attribute namespace prefix must reject the selector");

    selector_namespace_context second;
    second.prefixes.emplace("p", "urn:webscene:second");
    const auto first_again = compile_selector("[p|name]", &first);
    const auto second_selector = compile_selector("[p|name]", &second);
    require(first_again.compiled_compounds[0].attributes[0].namespace_uri
            == "urn:webscene:first"
        && second_selector.compiled_compounds[0].attributes[0].namespace_uri
            == "urn:webscene:second",
        "selector cache reused attribute namespace identity across contexts");

    dom_node node;
    node.tag = "div";
    node.attributes.set_namespaced(
        "p:name", "name", "urn:webscene:first", "value");
    node.attributes["name"] = "plain";
    require(attribute_matches(node, first_again.compiled_compounds[0].attributes[0])
        && !attribute_matches(node, second_selector.compiled_compounds[0].attributes[0]),
        "DOM attribute matching did not compare expanded namespace identity");
    const auto no_namespace = compile_selector("[|name]", &first);
    const auto any_namespace = compile_selector("[*|name]", &first);
    require(attribute_matches(node, no_namespace.compiled_compounds[0].attributes[0])
        && attribute_matches(node, any_namespace.compiled_compounds[0].attributes[0]),
        "empty and any attribute namespace constraints changed semantics");
}

void require_functional_namespace_propagation()
{
    using namespace webscene_native;
    using namespace webscene_native::css;
    selector_namespace_context first;
    first.prefixes.emplace("p", "urn:webscene:functional:first");
    first.default_namespace = "urn:webscene:functional:default";
    first.has_default_namespace = true;

    const auto selector = compile_selector(
        ".subject:is(p|circle, *|circle, |circle):where(circle):not(p|rect)",
        &first);
    require(compiled_selector_is_valid(selector),
        "functional namespace selector failed top-level compilation");
    const auto& pseudos = selector.compiled_compounds[0].pseudos;
    require(pseudos.size() == 3U
        && std::all_of(pseudos.begin(), pseudos.end(), [](const auto& pseudo) {
            return pseudo.compiled_argument_valid
                && pseudo.compiled_argument != nullptr;
        }),
        "functional selector lists were not retained as compiled payloads");
    const auto& alternatives = pseudos[0].compiled_argument->selectors;
    require(alternatives.size() == 3U
        && alternatives[0].compiled_compounds[0].namespace_uri
            == "urn:webscene:functional:first"
        && !alternatives[1].compiled_compounds[0].namespace_uri.has_value()
        && alternatives[2].compiled_compounds[0].namespace_uri == std::string{},
        "prefixed, any, and empty namespaces changed inside :is()");
    require(pseudos[1].compiled_argument->selectors[0]
            .compiled_compounds[0].namespace_uri
            == "urn:webscene:functional:default",
        "default namespace did not propagate into :where()");

    const auto relational = compile_selector(
        ".host:has(> p|circle[p|state]:is(p|circle))", &first);
    require(compiled_selector_is_valid(relational),
        "namespace context did not propagate through :has()");
    const auto& has = relational.compiled_compounds[0].pseudos[0];
    require(has.compiled_argument_valid && has.compiled_argument != nullptr
        && has.compiled_argument->selectors.size() == 1U
        && has.compiled_argument->selectors[0].compiled_compounds.back().namespace_uri
            == "urn:webscene:functional:first"
        && has.compiled_argument->selectors[0].compiled_compounds.back()
            .attributes[0].namespace_uri == "urn:webscene:functional:first",
        "relative functional payload lost type or attribute namespace identity");
    const auto dependencies = compile_invalidation_plan(relational);
    require(dependencies[0].attributes.at("state").routes
            == std::vector<css_invalidation_route>{{css_invalidation_step::parent}},
        "namespace-bound :has() invalidation did not retain its relative route");

    const auto unknown_only = compile_selector(
        ".subject:is(missing|circle)", &first);
    require(!compiled_selector_is_valid(unknown_only),
        "an unknown-prefix-only functional list must fail closed");
    const auto mixed = compile_selector(
        ".subject:is(missing|circle, p|circle)", &first);
    require(compiled_selector_is_valid(mixed),
        "a valid forgiving-list arm was lost beside an unknown prefix");

    selector_namespace_context second = first;
    second.prefixes["p"] = "urn:webscene:functional:second";
    const auto first_cached = compile_selector(".subject:is(p|circle)", &first);
    const auto second_cached = compile_selector(".subject:is(p|circle)", &second);
    require(first_cached.compiled_compounds[0].pseudos[0]
            .compiled_argument->selectors[0].compiled_compounds[0].namespace_uri
            == "urn:webscene:functional:first"
        && second_cached.compiled_compounds[0].pseudos[0]
            .compiled_argument->selectors[0].compiled_compounds[0].namespace_uri
            == "urn:webscene:functional:second",
        "nested selector cache identity crossed namespace contexts");
    require(compile_selector("div:is(#winner, p|circle)", &first).specificity
            == 0x010001U,
        "namespace propagation changed functional specificity");

    auto over_depth = std::string{".subject"};
    for (size_t depth = 0U; depth < 40U; ++depth) over_depth += ":is(";
    over_depth += "p|circle";
    for (size_t depth = 0U; depth < 40U; ++depth) over_depth += ')';
    require(!compiled_selector_is_valid(compile_selector(over_depth, &first)),
        "functional namespace compilation exceeded its recursion bound");
}

void test_compiled_css_invalidation_plans()
{
    using namespace webscene_native::css;
    const auto compile = [](std::string_view text) {
        auto selector = compile_selector(text);
        require(!selector.compounds.empty(), "invalidation test selector failed parsing");
        return compile_invalidation_plan(selector);
    };
    auto nested = compile(R"(.card:not(:has([data-ready])))");
    require(nested[0].attributes.at("data-ready").scope == invalidation_ancestors,
        "nested relational dependency was not compiled to ancestor scope");
    auto relative = compile(R"(.card:has(> [data-a], > [data-b="x,y"]))");
    require(relative[0].attributes.at("data-a").routes
            == std::vector<css_invalidation_route>{{css_invalidation_step::parent}}
        && relative[0].attributes.at("data-b").routes
            == std::vector<css_invalidation_route>{{css_invalidation_step::parent}},
        "relative selector-list arms were not independently anchored");
    auto escaped = compile(R"(.escaped\:active[data\2d ready] > .target)");
    require(escaped[0].classes.contains("escaped:active")
        && escaped[0].attributes.contains("data-ready"),
        "compiled dependency identifiers were not decoded");
    auto complex = compile(R"(.target:is(.active .target))");
    require(complex[0].classes.at("active").routes
            == std::vector<css_invalidation_route>{{css_invalidation_step::descendants}},
        "complex nested selectors must route to their final compound");
    auto inherited = compile("input:disabled");
    require(inherited[0].attributes.at("disabled").routes
            == std::vector<css_invalidation_route>{{css_invalidation_step::inclusive_descendants}},
        "inherited disabled state must include the subject and descendants");
    auto reverse = compile(".card:has(.branch > [data-ready])");
    require(reverse[0].attributes.at("data-ready").routes
            == std::vector<css_invalidation_route>{{css_invalidation_step::parent,
                css_invalidation_step::ancestors}},
        "relational invalidation must reverse the relative combinators");
    auto adjacent_has = compile(".row:has(+ .row.selected)");
    require(adjacent_has[0].classes.at("selected").routes
            == std::vector<css_invalidation_route>{{css_invalidation_step::previous_sibling}},
        "adjacent :has dependency must route to the previous sibling");
    require(std::find(adjacent_has[0].child_list.routes.begin(),
            adjacent_has[0].child_list.routes.end(),
            css_invalidation_route{css_invalidation_step::children})
            != adjacent_has[0].child_list.routes.end(),
        "adjacent :has removal must revisit surviving sibling subjects");
    auto general_has = compile(".row:has(~ .row [data-ready])");
    require(general_has[0].attributes.at("data-ready").routes
            == std::vector<css_invalidation_route>{{css_invalidation_step::ancestors,
                css_invalidation_step::preceding_siblings}},
        "general-sibling :has dependency must reverse descendant and sibling steps");
    require(std::find(general_has[0].child_list.routes.begin(),
            general_has[0].child_list.routes.end(),
            css_invalidation_route{css_invalidation_step::children})
            != general_has[0].child_list.routes.end(),
        "general-sibling :has removal must revisit surviving sibling subjects");
    require(compile(".target:is(.on ~ .target)")[0].child_list_sensitive,
        "nested sibling matching must record child-list sensitivity");
    require(compile(".target:not(:last-child)")[0].child_list_sensitive,
        "nested structural pseudo must record child-list sensitivity");
    require(!compile(".parent > .target")[0].child_list_sensitive,
        "ordinary child matching does not require restyling existing siblings");
    require(compile(".target:not(:last-child)")[0].child_list.routes
        == std::vector<css_invalidation_route>{{css_invalidation_step::children}},
        "positional invalidation starts at the changed parent's children");
    require(compile(".parent:empty + .target")[0].child_list.scope == invalidation_subject,
        "empty invalidation starts at the changed parent");
    require((compile(".outer:has(.marker)")[0].child_list.scope
        & (invalidation_subject | invalidation_ancestors))
        == (invalidation_subject | invalidation_ancestors),
        "relational tree mutations must reach both parent and ancestor anchors");
    require(compile(".left + .right > .target")[1].child_list.routes
        == std::vector<css_invalidation_route>{{css_invalidation_step::children}},
        "sibling mutations must start at the right-hand compound");
    const auto radio_validity=compile(".choice:invalid");
    require(radio_validity[0].attributes.at(
            "$live-form-radio-group-checkedness").scope==invalidation_subject,
        "radio group validity must compile a subject-routed checkedness dependency");
    const auto radio_relational=compile(".field:has(.choice:invalid)");
    require(radio_relational[0].attributes.at(
            "$live-form-radio-group-checkedness").scope==invalidation_ancestors,
        "relational radio validity must retain its compiled ancestor route");
    const auto numeric_validity=compile(".amount:invalid");
    for(const auto* attribute:{"min","max","step"})
        require(numeric_validity[0].attributes.at(attribute).scope==invalidation_subject,
            "numeric validity attributes must retain a subject route");
    const auto numeric_relational=compile(".field:has(.amount:invalid)");
    for(const auto* attribute:{"min","max","step"})
        require(numeric_relational[0].attributes.at(attribute).scope==invalidation_ancestors,
            "relational numeric validity must retain its compiled ancestor route");
    const auto link_subject=compile("a:any-link");
    require(link_subject[0].attributes.at("href").scope==invalidation_subject,
        "any-link must compile href mutation to its subject");
    const auto link_descendant=compile(".navigation:link > .label");
    require(link_descendant[0].attributes.at("href").scope==invalidation_subject
        &&link_descendant[0].attributes.at("href").routes.empty(),
        "link href must invalidate its trigger before runtime child traversal");
    const auto link_relational=compile(".shell:has(> a:any-link)");
    require(link_relational[0].attributes.at("href").routes
            ==std::vector<css_invalidation_route>{{css_invalidation_step::parent}},
        "nested any-link must retain its reverse relational href route");
    const auto local_link_subject=compile("a:local-link");
    require(local_link_subject[0].attributes.at("href").scope
            ==invalidation_subject
        && local_link_subject[0].attributes.at("$local-link-document").scope
            ==invalidation_subject,
        "local-link must retain both href and document URL subject routes");
    const auto local_link_sibling=compile("a:local-link + .marker");
    require(local_link_sibling[0].attributes.at("$local-link-document").scope
            ==invalidation_subject
        &&local_link_sibling[0].attributes.at("$local-link-document").routes.empty(),
        "local-link document changes must invalidate the trigger before runtime sibling traversal");
    const auto local_link_relational=compile(".shell:has(> a:local-link)");
    require(local_link_relational[0].attributes.at("$local-link-document").routes
            ==std::vector<css_invalidation_route>{{css_invalidation_step::parent}},
        "nested local-link must retain its reverse relational document route");
    const auto local_link_depth=compile("a:is(:local-link(2)) + .marker");
    require(local_link_depth[0].attributes.at("href").scope==invalidation_subject
        &&local_link_depth[0].attributes.at("href").routes.empty()
        &&local_link_depth[0].attributes.at("$local-link-document").scope
            ==invalidation_subject
        &&local_link_depth[0].attributes.at("$local-link-document").routes.empty(),
        "functional local-link must preserve trigger-scoped href/document invalidation");
    const auto target_within=compile(".branch:target-within");
    for(const auto* dependency:{"id","$target-document"})
        require((target_within[0].attributes.at(dependency).scope
                &(invalidation_subject|invalidation_ancestors))
                ==(invalidation_subject|invalidation_ancestors),
            "target-within must route target identity to its inclusive ancestor chain");
    require(target_within[0].child_list_sensitive
        &&(target_within[0].child_list.scope
            &(invalidation_subject|invalidation_ancestors))
            ==(invalidation_subject|invalidation_ancestors),
        "target-within must route tree transitions to the changed parent chain");
    const auto target_sibling=compile(".branch:target-within + .marker");
    const auto& target_sibling_dependency=target_sibling[0].attributes.at(
        "$target-document");
    require((target_sibling_dependency.scope
            &(invalidation_subject|invalidation_ancestors))
            ==(invalidation_subject|invalidation_ancestors)
        &&target_sibling_dependency.routes.empty(),
        "target-within must invalidate inclusive triggers before runtime sibling traversal");

    std::vector<css_child_list_bucket> buckets;
    const auto index = [&](size_t id, std::string_view text) {
        const auto selector = compile_selector(text);
        index_child_list_rule(id, selector, compile_invalidation_plan(selector), buckets);
    };
    index(0, R"(#escaped\2d id:empty)");
    index(1, R"(.escaped\:class:empty)");
    index(2, "SECTION:empty");
    index(3, R"([DATA\2d STATE]:empty)");
    index(4, ":not(.missing):empty");
    index(5, ":is(.a, .b):empty");
    require(buckets.size() == 1 && buckets[0].route.empty(),
        "structural rules must share one bucket for an identical route");
    require(buckets[0].by_id.at("escaped-id") == std::vector<size_t>{0}
        && buckets[0].by_class.at("escaped:class") == std::vector<size_t>{1}
        && buckets[0].by_tag.at("section") == std::vector<size_t>{2}
        && buckets[0].by_attribute.at("data-state") == std::vector<size_t>{3}
        && buckets[0].universal == std::vector<size_t>({4, 5}),
        "structural keys must be decoded mandatory outer features, never optional pseudo arms");
    index(6, ".parent:has(.marker)");
    index(6, ".parent:has(.marker)");
    require(buckets.size() == 2
        && buckets[0].by_class.at("parent") == std::vector<size_t>{6}
        && buckets[1].route == css_invalidation_route{css_invalidation_step::ancestors}
        && buckets[1].by_class.at("parent") == std::vector<size_t>{6},
        "relational subjects must index both parent and ancestor routes without duplicates");
}

void require_local_link_matching()
{
    using namespace webscene_native;
    dom_node anchor;
    anchor.tag = "a";
    anchor.attributes["href"] = "guide.html?mode=full#chapter";
    require(css::local_link_matches(
            anchor,
            "https://example.test/docs/index.html",
            "https://example.test/docs/guide.html?mode=full#current"),
        "local-link must ignore fragments after resolving a relative URL");
    require(!css::local_link_matches(
            anchor,
            "https://example.test/docs/index.html",
            "https://example.test/docs/guide.html?mode=other"),
        "local-link must preserve query differences");
    anchor.attributes["href"] = "#chapter";
    require(css::local_link_matches(
            anchor,
            "https://example.test/docs/index.html?mode=full#old",
            "https://example.test/docs/index.html?mode=full#current"),
        "fragment-only local links must retain the base path and query");
    anchor.attributes["href"].clear();
    require(css::local_link_matches(
            anchor,
            "https://example.test/docs/index.html#base",
            "https://example.test/docs/index.html#current"),
        "an empty hyperlink must resolve to the effective document base");
    anchor.tag = "link";
    require(!css::local_link_matches(
            anchor,
            "https://example.test/docs/index.html",
            "https://example.test/docs/index.html"),
        "stylesheet metadata must not become a local hyperlink");

    anchor.tag = "a";
    anchor.attributes["href"] = "http://www.example.com/";
    constexpr auto document_url = "http://www.example.com/2011/03/";
    require(css::local_link_matches(
            anchor,document_url,document_url,uint32_t{0U})
        &&!css::local_link_matches(
            anchor,document_url,document_url,uint32_t{1U}),
        "local-link(0) must compare only origin while depth one requires a segment");
    anchor.attributes["href"] = "http://WWW.EXAMPLE.COM:80/2011";
    require(css::local_link_matches(
            anchor,document_url,document_url,uint32_t{1U})
        &&!css::local_link_matches(
            anchor,document_url,document_url,uint32_t{2U}),
        "functional local-link must normalize origin and require every requested segment");
    anchor.attributes["href"] = "http://user:secret@www.example.com/2011/03?other=1#chapter";
    require(css::local_link_matches(
            anchor,document_url,document_url,uint32_t{2U})
        &&!css::local_link_matches(
            anchor,document_url,document_url,uint32_t{3U}),
        "functional local-link must ignore credentials/query/fragment but preserve path depth");
    anchor.attributes["href"] = "http://www.example.com/2011/03/";
    require(css::local_link_matches(
            anchor,document_url,document_url,uint32_t{3U})
        &&!css::local_link_matches(
            anchor,document_url,document_url,uint32_t{4U}),
        "a trailing slash must contribute its empty path segment only once");
    anchor.attributes["href"] = "https://www.example.com/2011/03/";
    require(!css::local_link_matches(
            anchor,document_url,document_url,uint32_t{0U})
        &&!css::local_link_matches(
            anchor,"mailto:author@example.com","mailto:author@example.com",uint32_t{0U}),
        "functional local-link must reject cross-origin and non-hierarchical URLs");
    anchor.attributes["href"] = "webscene-test://host.test:0/section";
    require(!css::local_link_matches(
            anchor,
            "webscene-test://host.test/section",
            "webscene-test://host.test/section",
            uint32_t{0U}),
        "an explicit port zero must not equal no port for a scheme without a known default");
}

void require_target_identifier_matching()
{
    using namespace webscene_native;
    dom_node target;
    target.id_attribute="section one";
    require(css::target_matches(target,"#section%20one"),
        "target identifiers must decode percent-encoded URL fragments");
    require(!css::target_matches(target,"#section%20two")
        &&!css::target_matches(target,"#"),
        "target identifiers must reject a different or empty fragment");
}

} // namespace

int main()
{
    require_selector_shape();
    require_specificity();
    require_validation();
    require_wtf8_domstring_round_trip();
    require_nested_has_selector_list_tokenization();
    require_namespace_resolution();
    require_attribute_namespace_resolution();
    require_functional_namespace_propagation();
    test_compiled_css_invalidation_plans();
    require_local_link_matching();
    require_target_identifier_matching();
    std::cout << "selector parser tests passed\n";
    return 0;
}
