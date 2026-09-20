#pragma once
#include "webscene_css_property_mask.h"
#include <utility>

namespace webscene_native::css {
template<typename Decision>
bool apply_user_select_value(dom_node& node,const std::string& name,
    const std::string& raw_value,Decision& decision)
{
    if (canonical_property_name(name) != "user-select") return false;
    auto value = ascii_lower(trim_value(raw_value));
    if (value == "inherit") {
        value = node.parent == nullptr ? std::string{}
            : node.parent->style.textual().user_select;
    } else if (value == "initial" || value == "unset" || value == "revert"
        || value == "revert-layer") {
        value.clear();
    }
    if (!value.empty() && value != "auto" && value != "text"
        && value != "none" && value != "all") {
        decision.classification = "invalid-authoring";
        return true;
    }
    node.style.mutable_textual().user_select = value == "auto"
        ? std::string{} : std::move(value);
    return true;
}
} // namespace webscene_native::css
