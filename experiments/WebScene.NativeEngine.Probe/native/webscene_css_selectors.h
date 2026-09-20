#pragma once
#include "webscene_css_state.h"
#include "webscene_selector_parser.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <optional>

namespace webscene_native::css {
inline std::string_view trim_css_view(std::string_view value) {
    constexpr std::string_view whitespace=" \t\r\n\f";
    const auto start=value.find_first_not_of(whitespace);
    if(start==std::string_view::npos) return {};
    return value.substr(start,value.find_last_not_of(whitespace)-start+1);
}
inline std::optional<uint32_t> parse_local_link_path_depth(
    std::string_view value)
{
    value=trim_css_view(value);
    if(value.starts_with('+'))value.remove_prefix(1U);
    if(value.empty())return std::nullopt;
    uint32_t result=0U;
    constexpr auto maximum=static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
    for(const auto character:value) {
        if(character<'0'||character>'9')return std::nullopt;
        const auto digit=static_cast<uint32_t>(character-'0');
        if(result>(maximum-digit)/10U)return std::nullopt;
        result=result*10U+digit;
    }
    return result;
}
inline size_t skip_css_escape_sequence(std::string_view text, size_t slash);

// Functional selector arguments are forgiving selector lists. Only commas at
// the current list depth separate alternatives; nested functions and attribute
// values keep their commas. Keep this allocation-free because matching calls it
// for every subject considered by :is(), :where(), and :not().
template<typename Predicate>
inline bool css_selector_list_any(std::string_view value, Predicate&& predicate)
{
    size_t start = 0U;
    int parenthesis_depth = 0;
    int bracket_depth = 0;
    char quote = 0;
    for (size_t index = 0U; index <= value.size(); ++index) {
        const auto at_end = index == value.size();
        const auto character = at_end ? ',' : value[index];
        if (!at_end && character == '\\') {
            index = skip_css_escape_sequence(value, index) - 1U;
            continue;
        }
        if (!at_end && quote != 0) {
            if (character == quote) quote = 0;
            continue;
        }
        if (!at_end && (character == '\'' || character == '"')) {
            quote = character;
            continue;
        }
        if (!at_end && character == '[') {
            ++bracket_depth;
            continue;
        }
        if (!at_end && character == ']' && bracket_depth > 0) {
            --bracket_depth;
            continue;
        }
        if (!at_end && bracket_depth == 0 && character == '(') {
            ++parenthesis_depth;
            continue;
        }
        if (!at_end && bracket_depth == 0 && character == ')'
            && parenthesis_depth > 0) {
            --parenthesis_depth;
            continue;
        }
        if (character != ',' || parenthesis_depth != 0 || bracket_depth != 0) {
            continue;
        }
        if (predicate(trim_css_view(value.substr(start, index - start)))) {
            return true;
        }
        start = index + 1U;
    }
    return false;
}
inline void append_utf8_codepoint(std::string& result, uint32_t codepoint)
    {
        if (codepoint <= 0x7FU) {
            result.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7FFU) {
            result.push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
            result.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
        } else if (codepoint <= 0xFFFFU) {
            result.push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
            result.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
        } else {
            result.push_back(static_cast<char>(0xF0U | (codepoint >> 18U)));
            result.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
        }
    }

inline bool is_css_hex_digit(unsigned char character)
    {
        return (character >= '0' && character <= '9')
            || (character >= 'a' && character <= 'f')
            || (character >= 'A' && character <= 'F');
    }

inline size_t skip_css_escape_sequence(std::string_view text, size_t slash)
    {
        auto cursor = slash + 1U;
        if (cursor >= text.size()) return cursor;
        if (text[cursor] == '\r') {
            ++cursor;
            if (cursor < text.size() && text[cursor] == '\n') ++cursor;
            return cursor;
        }
        if (text[cursor] == '\n' || text[cursor] == '\f') return cursor + 1U;
        const auto hex_start = cursor;
        while (cursor < text.size()
            && cursor - hex_start < 6U
            && is_css_hex_digit(static_cast<unsigned char>(text[cursor]))) ++cursor;
        if (cursor > hex_start) {
            if (cursor < text.size() && text[cursor] == '\r') {
                ++cursor;
                if (cursor < text.size() && text[cursor] == '\n') ++cursor;
            } else if (cursor < text.size()
                && std::isspace(static_cast<unsigned char>(text[cursor]))) ++cursor;
        } else {
            ++cursor;
        }
        return cursor;
    }

inline std::string read_css_identifier(std::string_view text, size_t& cursor)
    {
        std::string result;
        while (cursor < text.size()) {
            const auto character = static_cast<unsigned char>(text[cursor]);
            if (character == '\\') {
                ++cursor;
                if (cursor >= text.size()) {
                    append_utf8_codepoint(result, 0xfffdU);
                    break;
                }
                if (text[cursor] == '\r') {
                    ++cursor;
                    if (cursor < text.size() && text[cursor] == '\n') ++cursor;
                    continue;
                }
                if (text[cursor] == '\n' || text[cursor] == '\f') {
                    ++cursor;
                    continue;
                }
                const auto hex_start = cursor;
                uint32_t codepoint = 0U;
                while (cursor < text.size()
                    && cursor - hex_start < 6U
                    && is_css_hex_digit(static_cast<unsigned char>(text[cursor]))) {
                    const auto digit = static_cast<unsigned char>(text[cursor++]);
                    codepoint = codepoint * 16U
                        + (digit >= '0' && digit <= '9'
                            ? digit - '0'
                            : static_cast<unsigned char>(std::tolower(digit)) - 'a' + 10U);
                }
                if (cursor > hex_start) {
                    if (cursor < text.size() && text[cursor] == '\r') {
                        ++cursor;
                        if (cursor < text.size() && text[cursor] == '\n') ++cursor;
                    } else if (cursor < text.size()
                        && std::isspace(static_cast<unsigned char>(text[cursor]))) ++cursor;
                    append_utf8_codepoint(
                        result,
                        codepoint == 0U || codepoint > 0x10ffffU
                            || (codepoint >= 0xd800U && codepoint <= 0xdfffU)
                            ? 0xfffdU
                            : codepoint);
                } else {
                    result.push_back(text[cursor++]);
                }
                continue;
            }
            if (character == 0U) {
                append_utf8_codepoint(result, 0xfffdU);
                ++cursor;
                continue;
            }
            if (!std::isalnum(character) && character != '-' && character != '_'
                && character < 0x80U) break;
            result.push_back(text[cursor++]);
        }
        return result;
    }

inline std::optional<selector_namespace_context>
forgiving_namespace_retry_context(
    std::string_view selector,
    const selector_namespace_context& namespaces)
{
    auto result = namespaces;
    auto added = size_t{0U};
    for (size_t cursor = 0U; cursor < selector.size();) {
        const auto character = static_cast<unsigned char>(selector[cursor]);
        if (character == '\\' || std::isalpha(character) || character == '_'
            || character == '-' || character >= 0x80U) {
            auto end = cursor;
            auto prefix = read_css_identifier(selector, end);
            if (!prefix.empty() && end < selector.size() && selector[end] == '|'
                && (end + 1U >= selector.size() || selector[end + 1U] != '=')
                && !result.prefixes.contains(prefix)) {
                if (added == 64U) return std::nullopt;
                result.prefixes.emplace(
                    std::move(prefix), "urn:webscene:undeclared-namespace");
                ++added;
            }
            cursor = std::max(end, cursor + 1U);
            continue;
        }
        if (character == '\'' || character == '"') {
            const auto quote = static_cast<char>(character);
            ++cursor;
            while (cursor < selector.size() && selector[cursor] != quote) {
                if (selector[cursor] == '\\') {
                    cursor = skip_css_escape_sequence(selector, cursor);
                } else {
                    ++cursor;
                }
            }
            if (cursor < selector.size()) ++cursor;
            continue;
        }
        ++cursor;
    }
    return added == 0U
        ? std::nullopt
        : std::optional<selector_namespace_context>{std::move(result)};
}

inline size_t find_css_attribute_close(
        std::string_view selector,
        size_t cursor)
    {
        char quote = 0;
        for (; cursor < selector.size(); ++cursor) {
            const auto character = selector[cursor];
            if (character == '\\') {
                cursor = skip_css_escape_sequence(selector, cursor) - 1U;
                continue;
            }
            if (quote != 0) {
                if (character == quote) quote = 0;
                continue;
            }
            if (character == '\'' || character == '"') {
                quote = character;
            } else if (character == ']') {
                return cursor;
            }
        }
        return std::string_view::npos;
    }

// Anchor every relative arm before compiling :has(). Commas within strings,
// attributes and nested functions do not separate the outer relative list.
inline std::string anchor_relative_selector_list(std::string_view text)
{
    std::string result = ":scope ";
    int brackets = 0, parentheses = 0;
    char quote = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const auto c = text[i];
        if (c == '\\') {
            const auto end = skip_css_escape_sequence(text, i);
            result.append(text.substr(i, end - i));
            i = end - 1U;
            continue;
        }
        result.push_back(c);
        if (quote != 0) { if (c == quote) quote = 0; continue; }
        if (c == '\'' || c == '"') { quote = c; continue; }
        if (c == '[') ++brackets;
        else if (c == ']') --brackets;
        else if (c == '(') ++parentheses;
        else if (c == ')') --parentheses;
        else if (c == ',' && brackets == 0 && parentheses == 0)
            result += ":scope ";
    }
    return result;
}

inline std::optional<compiled_css_attribute> compile_css_attribute_condition(
    std::string_view condition,
    const selector_namespace_context* namespaces)
{
    condition = trim_css_view(condition);
    auto equal = condition.find('=');
    auto name_end = equal;
    uint8_t operator_kind = 0U;
    if (equal != std::string_view::npos) {
        if (equal > 0U) {
            switch (condition[equal - 1U]) {
            case '~': operator_kind = 2U; break;
            case '|': operator_kind = 3U; break;
            case '^': operator_kind = 4U; break;
            case '*': operator_kind = 5U; break;
            case '$': operator_kind = 6U; break;
            default: operator_kind = 1U; break;
            }
            if (operator_kind != 1U) --name_end;
        } else {
            operator_kind = 1U;
        }
    }
    auto name_source = trim_css_view(condition.substr(0U, name_end));
    std::optional<std::string> namespace_uri = std::string{};
    size_t cursor = 0U;
    if (name_source.starts_with("*|")) {
        namespace_uri.reset();
        cursor = 2U;
    } else if (name_source.starts_with('|')) {
        cursor = 1U;
    } else {
        auto prefix_cursor = size_t{0U};
        auto first = read_css_identifier(name_source, prefix_cursor);
        if (first.empty()) return std::nullopt;
        if (prefix_cursor < name_source.size()
            && name_source[prefix_cursor] == '|') {
            if (namespaces == nullptr) return std::nullopt;
            const auto known = namespaces->prefixes.find(first);
            if (known == namespaces->prefixes.end()) return std::nullopt;
            namespace_uri = known->second;
            cursor = prefix_cursor + 1U;
        }
    }
    auto local_name = read_css_identifier(name_source, cursor);
    if (local_name.empty() || cursor != name_source.size()) return std::nullopt;
    compiled_css_attribute result{
        std::move(local_name), std::move(namespace_uri), operator_kind, {}, 0U};
    if (equal == std::string_view::npos) return result;
    auto value = trim_css_view(condition.substr(equal + 1U));
    if (value.size() >= 2U
        && (value.ends_with(" i") || value.ends_with(" I")
            || value.ends_with(" s") || value.ends_with(" S"))) {
        result.case_sensitivity = std::tolower(
            static_cast<unsigned char>(value.back())) == 'i' ? 1U : 2U;
        value = trim_css_view(value.substr(0U, value.size() - 2U));
    }
    if (value.size() >= 2U
        && (value.front() == '\'' || value.front() == '"')
        && value.back() == value.front()) {
        result.value.assign(value.substr(1U, value.size() - 2U));
    } else {
        size_t value_cursor = 0U;
        result.value = read_css_identifier(value, value_cursor);
        if (result.value.empty() || value_cursor != value.size()) return std::nullopt;
    }
    return result;
}

inline compiled_css_compound compile_css_compound_selector(
        std::string_view selector,
        const selector_namespace_context* namespaces = nullptr,
        const std::vector<selector_syntax_attribute>* parsed_attributes = nullptr)
    {
        compiled_css_compound result;
        selector = trim_css_view(selector);
        if (selector.empty()) return result;

        size_t cursor = 0U;
        size_t attribute_index = 0U;
        if (selector.empty()) {
            // A bare structural pseudo-class has an implicit universal selector.
        } else if (selector[cursor] == '|') {
            result.namespace_uri = std::string{};
            ++cursor;
            if (cursor < selector.size() && selector[cursor] == '*') {
                ++cursor;
            } else {
                result.tag = read_css_identifier(selector, cursor);
                if (result.tag.empty()) return result;
            }
        } else if (selector[cursor] == '*') {
            ++cursor;
            if (cursor < selector.size() && selector[cursor] == '|') {
                ++cursor;
                if (cursor < selector.size() && selector[cursor] == '*') {
                    ++cursor;
                } else {
                    result.tag = read_css_identifier(selector, cursor);
                    if (result.tag.empty()) return result;
                }
            } else if (namespaces != nullptr && namespaces->has_default_namespace) {
                result.namespace_uri = namespaces->default_namespace;
            }
        } else if (std::isalpha(static_cast<unsigned char>(selector[cursor]))
            || selector[cursor] == '_' || selector[cursor] == '-'
            || selector[cursor] == '\\') {
            auto first = read_css_identifier(selector, cursor);
            if (cursor < selector.size() && selector[cursor] == '|') {
                if (namespaces == nullptr) return result;
                const auto found = namespaces->prefixes.find(first);
                if (found == namespaces->prefixes.end()) return result;
                result.namespace_uri = found->second;
                ++cursor;
                if (cursor < selector.size() && selector[cursor] == '*') {
                    ++cursor;
                } else {
                    result.tag = read_css_identifier(selector, cursor);
                    if (result.tag.empty()) return result;
                }
            } else {
                result.tag = std::move(first);
                if (namespaces != nullptr && namespaces->has_default_namespace) {
                    result.namespace_uri = namespaces->default_namespace;
                }
            }
        }
        while (cursor < selector.size()) {
            const auto marker = selector[cursor++];
            if (marker == '.' || marker == '#') {
                auto wanted = read_css_identifier(selector, cursor);
                if (wanted.empty()) return result;
                result.identities.emplace_back(marker, std::move(wanted));
            } else if (marker == '[') {
                const auto close = find_css_attribute_close(selector, cursor);
                if (close == std::string::npos) return result;
                if (parsed_attributes != nullptr) {
                    if (attribute_index >= parsed_attributes->size()) return result;
                    const auto& parsed = (*parsed_attributes)[attribute_index++];
                    result.attributes.push_back({
                        parsed.local_name,
                        parsed.namespace_kind == 0U
                            ? std::optional<std::string>{}
                            : std::optional<std::string>{parsed.namespace_url},
                        parsed.operator_kind,
                        parsed.value,
                        parsed.case_sensitivity});
                } else {
                    auto attribute = compile_css_attribute_condition(
                        selector.substr(cursor, close - cursor), namespaces);
                    if (!attribute.has_value()) return result;
                    result.attributes.push_back(std::move(*attribute));
                }
                cursor = close + 1U;
            } else if (marker == ':') {
                if (cursor < selector.size() && selector[cursor] == ':') {
                    result.pseudo_element = true;
                    ++cursor;
                }
                auto name = read_css_identifier(selector, cursor);
                if (name.empty()) return result;
                if (name == "before" || name == "after"
                    || name == "first-letter" || name == "first-line") {
                    result.pseudo_element = true;
                }
                auto argument = std::string{};
                if (cursor < selector.size() && selector[cursor] == '(') {
                    const auto argument_start = ++cursor;
                    auto depth = 1;
                    char quote = 0;
                    while (cursor < selector.size() && depth > 0) {
                        const auto value = selector[cursor];
                        if (quote != 0) {
                            if (value == quote
                                && (cursor == argument_start
                                    || selector[cursor - 1U] != '\\')) {
                                quote = 0;
                            }
                        } else if (value == '\'' || value == '"') {
                            quote = value;
                        } else if (value == '\\') {
                            cursor = skip_css_escape_sequence(selector, cursor) - 1U;
                        } else if (value == '(') {
                            ++depth;
                        } else if (value == ')') {
                            --depth;
                        }
                        if (depth > 0) ++cursor;
                    }
                    if (depth != 0) return result;
                    argument.assign(
                        selector.substr(argument_start, cursor - argument_start));
                    ++cursor;
                }
                if (name == "state") {
                    auto argument_cursor = size_t{0U};
                    auto decoded = read_css_identifier(argument, argument_cursor);
                    if (decoded.empty() || argument_cursor != argument.size()) return result;
                    argument = std::move(decoded);
                }
                if(name=="local-link"&&!argument.empty()
                    &&!parse_local_link_path_depth(argument).has_value())return result;
                result.pseudos.push_back(
                    compiled_css_pseudo{std::move(name), std::move(argument)});
            } else {
                return result;
            }
        }
        if (parsed_attributes != nullptr
            && attribute_index != parsed_attributes->size()) return result;
        result.valid = true;
        return result;
    }


inline bool compiled_selector_is_valid(const compiled_css_selector& selector)
{
    return !selector.compounds.empty()
        && selector.compiled_compounds.size() == selector.compounds.size()
        && std::all_of(
            selector.compiled_compounds.begin(),
            selector.compiled_compounds.end(),
            [](const auto& compound) { return compound.valid; });
}

// Reuse Servo parsing/specificity and the runtime's native compound preparation.
// One shared budget bounds the complete recursively compiled selector tree.
inline compiled_css_selector_list compile_selector_list_impl(
    std::string_view text,
    const selector_namespace_context* namespaces,
    size_t functional_depth,
    size_t& selector_budget) {
    compiled_css_selector_list result;
    constexpr size_t maximum_functional_depth = 32U;
    if (functional_depth > maximum_functional_depth || selector_budget == 0U)
        return result;
    auto parsed = namespaces == nullptr
        ? parse_selector_syntax(text)
        : parse_selector_syntax(text, *namespaces);
    if (!parsed && namespaces != nullptr && functional_depth > 0U) {
        if (auto retry = forgiving_namespace_retry_context(text, *namespaces)) {
            parsed = parse_selector_syntax(text, *retry);
        }
    }
    if(!parsed) return result;
    if (parsed.selectors.size() > selector_budget) return result;
    selector_budget -= parsed.selectors.size();
    for(const auto& source:parsed.selectors) {
        compiled_css_selector selector;
        selector.compounds=source.compounds;
        selector.combinators=source.combinators;
        selector.specificity=source.specificity;
        for(size_t compound_index = 0U;
            compound_index < selector.compounds.size(); ++compound_index) {
            const auto* attributes = compound_index < source.attributes.size()
                ? &source.attributes[compound_index] : nullptr;
            auto compiled = compile_css_compound_selector(
                selector.compounds[compound_index], namespaces, attributes);
            for (auto& pseudo : compiled.pseudos) {
                const auto functional = pseudo.name == "is" || pseudo.name == "where"
                    || pseudo.name == "not" || pseudo.name == "has";
                if (!functional || pseudo.argument.empty()) continue;
                if (functional_depth == maximum_functional_depth) {
                    compiled.valid = false;
                    continue;
                }
                const auto nested_source = pseudo.name == "has"
                    ? anchor_relative_selector_list(pseudo.argument)
                    : pseudo.argument;
                auto nested = std::make_shared<compiled_css_selector_list>(
                    compile_selector_list_impl(
                        nested_source,
                        namespaces,
                        functional_depth + 1U,
                        selector_budget));
                pseudo.compiled_argument_valid = std::any_of(
                    nested->selectors.begin(), nested->selectors.end(),
                    compiled_selector_is_valid);
                pseudo.compiled_argument = std::move(nested);
                if (!pseudo.compiled_argument_valid) compiled.valid = false;
            }
            selector.compiled_compounds.push_back(std::move(compiled));
        }
        selector.ancestor_requirements.resize(selector.compiled_compounds.size());
        for(size_t i=1;i<selector.compiled_compounds.size()
            && i<=selector.combinators.size();++i) {
            const auto relation=selector.combinators[i-1U];
            // A sibling's features need not occur in the subject's ancestors.
            // Stop collecting to the left of that relation. Functional arms
            // likewise contribute no mandatory outer-compound identities here.
            if(relation!=' ' && relation!='>') continue;
            selector.ancestor_requirements[i]=selector.ancestor_requirements[i-1U];
            const auto& compound=selector.compiled_compounds[i-1U];
            if(!compound.valid) continue;
            if(!compound.tag.empty() && compound.tag!="*")
                selector.ancestor_requirements[i].add('t',compound.tag);
            for(const auto& [kind,name]:compound.identities)
                selector.ancestor_requirements[i].add(kind,name);
        }
        result.selectors.push_back(std::move(selector));
    }
    return result;
}

inline compiled_css_selector_list compile_selector_list(
    std::string_view text,
    const selector_namespace_context* namespaces = nullptr) {
    auto selector_budget = size_t{4096U};
    return compile_selector_list_impl(text, namespaces, 0U, selector_budget);
}
inline compiled_css_selector compile_selector(
    std::string_view text,
    const selector_namespace_context* namespaces = nullptr) {
    auto result=compile_selector_list(text, namespaces);
    if(result.selectors.size()!=1) return {};
    return std::move(result.selectors.front());
}
} // namespace webscene_native::css
