#include "webscene_selector_parser.h"
#include "webscene_css_invalidation.h"

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

} // namespace

int main()
{
    require_selector_shape();
    require_specificity();
    require_validation();
    require_wtf8_domstring_round_trip();
    require_nested_has_selector_list_tokenization();
    require_namespace_resolution();
    test_compiled_css_invalidation_plans();
    std::cout << "selector parser tests passed\n";
    return 0;
}
