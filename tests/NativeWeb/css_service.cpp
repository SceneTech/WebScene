#include "webscene_css_declarations.h"
#include "webscene_css_selectors.h"
#include "webscene_css_matching.h"
#include "webscene_css_query.h"
#include "webscene_css_rule_operations.h"
#include "webscene_css_rule_index.h"
#include "webscene_css_candidates.h"
#include "webscene_css_variables.h"
#include "webscene_css_box_values.h"
#include "webscene_css_transitions.h"
#include "webscene_css_layout_values.h"
#include "webscene_css_pseudo_values.h"
#include "webscene_css_stylesheet_sink.h"
#include "webscene_css_rule_payload.h"
#include "webscene_css_resources.h"
#include "webscene_css_rule_preparation.h"
#include "webscene_css_stylesheet.h"
#include "webscene_css_stylesheet_owner.h"
#include "webscene_css_native_cascade.h"
#include "webscene_css_native_session.h"
#include "webscene_css_media.h"
#include "webscene_css_property_mask.h"
#include "webscene_css_reset.h"
#include "webscene_css_box_application.h"
#include "webscene_css_paint_values.h"
#include "webscene_css_visibility_values.h"
#include "webscene_css_text_values.h"
#include "webscene_css_decoration_values.h"
#include "webscene_css_application.h"
#include "webscene_css_cascade_reset.h"
#include "webscene_css_cascade_application.h"
#include "webscene_css_pseudo_application.h"
#include "webscene_css_rule_matching.h"
#include "webscene_css_cascade_finalization.h"
#include <array>
#include <iostream>
#include <fstream>
#include <iterator>

struct stylesheet_test_host {
    struct rule {
        std::string selector;
        std::vector<webscene_native::css::css_declaration> declarations;
        std::vector<std::string> media;
        std::string address;
    };
    std::vector<rule> rules;
    std::vector<std::string> unsupported;
    std::vector<std::string> layers;
    bool inventory_media_query(const std::string&) { return true; }
    uint32_t register_cascade_layer(const std::string& name) {
        if(!name.empty()) {
            const auto known=std::find(layers.begin(),layers.end(),name);
            if(known!=layers.end()) return static_cast<uint32_t>(
                std::distance(layers.begin(),known)+1);
        }
        layers.push_back(name);
        return static_cast<uint32_t>(layers.size());
    }
    void record_feature(std::string_view, const std::string& name,
        std::string_view classification, const std::string&, std::string_view) {
        if(classification=="unsupported") unsupported.push_back(name);
    }
    void append_parsed_css_style_rule(std::string selector,
        std::vector<webscene_native::css::css_declaration> declarations,
        const std::vector<std::string>& media,const std::string& address,
        uint32_t = 0U,
        const webscene_native::selector_namespace_context& namespaces = {}) {
        webscene_native::css::prepare_style_rule(selector,std::move(declarations),media,address,
            [](const auto&) {},
            [&](const auto& prepared_selector,const auto& values,const auto& conditions) {
                rules.push_back({prepared_selector,values,conditions,address});
            }, &namespaces);
    }

};

bool test_subject_candidate_index_scaling() {
    bool bounded=true;
    for(const auto targets:{8,128}) for(const auto unrelated:{32,1024}) {
        webscene_native::native_document document;
        webscene_native::css::stylesheet_owner sheets;
        std::string source=".target {width:31px}";
        for(int i=0;i<unrelated;++i)
            source+=":not([data-disabled]).unused-"+std::to_string(i)+" {width:7px}";
        auto sheet=webscene_native::css::prepare_stylesheet(source,"",[](const auto&) {return true;});
        if(!sheet) return false;
        sheets.replace(1,std::move(*sheet));
        size_t candidates=0;
        for(int i=0;i<targets;++i) {
            auto& node=document.create_element("div");node.class_name="target";
            document.append_child(document.body(),node);
            candidates+=sheets.candidates(node).size();
        }
        std::cout<<"subject-index targets="<<targets<<" unrelated="<<unrelated
            <<" candidates="<<candidates<<'\n';
        bounded=bounded && candidates==static_cast<size_t>(targets);
    }
    return bounded;
}

bool test_absent_ancestor_matching_work() {
    using namespace webscene_native;
    for(const auto depth:{8,64}) {
        native_document document;
        auto* parent=&document.body();
        for(int i=0;i<depth;++i) {
            auto& ancestor=document.create_element("section");
            ancestor.class_name="present";
            document.append_child(*parent,ancestor);parent=&ancestor;
        }
        auto& subject=document.create_element("div");subject.class_name="target";
        document.append_child(*parent,subject);
        auto selector=css::compile_selector(".absent .target");
        css::query_host query(document);
        css::selector_match_context context;
        css::selector_match_context prefix_only;
        prefix_only.ancestor_filter.capacity=0;
        size_t checks=0, baseline_checks=0, prefix_checks=0;
        const auto run=[&](css::selector_match_context* cache,size_t& count) {
            return css::selector_matches(document,subject,selector,
                selector.compounds.size()-1,nullptr,
                [&](const auto& node,const auto& compound,const auto* scope) {
                    ++count;return css::compound_matches(query,node,compound,scope);
                },cache);
        };
        bool matched=false;
        for(int i=0;i<128;++i) {
            matched=run(&context,checks) || matched;
            matched=run(nullptr,baseline_checks) || matched;
            matched=run(&prefix_only,prefix_checks) || matched;
        }
        std::cout<<"absent-ancestor depth="<<depth<<" compound-checks="<<checks<<'\n';
        // Summary construction is real work: each ancestor is indexed once,
        // not once per repeated match. Do not report zero total work.
        std::cout<<" ancestor-summaries="<<context.ancestor_filter.inclusive_ancestors.size()
            <<" uncached-compound-checks="<<baseline_checks
            <<" prefix-cache-only-compound-checks="<<prefix_checks<<'\n';
        if(matched || checks!=0 || baseline_checks!=128U*(depth+2U)
            || prefix_checks!=128U+depth+1U
            || context.ancestor_filter.inclusive_ancestors.size()!=depth+1U) return false;
    }
    return true;
}

bool test_ancestor_filter_parity() {
    using namespace webscene_native;
    native_document document;
    auto& outer=document.create_element("section");outer.class_name="outer";
    auto& sibling=document.create_element("aside");sibling.class_name="sibling";
    auto& parent=document.create_element("article");
    parent.class_name="parent\tTabbed\nline\fform\rcarriage escaped:name";
    parent.id_attribute="Parent";parent.attributes["id"]="Parent";
    auto& target=document.create_element("div");target.class_name="target";
    document.append_child(document.body(),outer);
    document.append_child(outer,sibling);document.append_child(outer,parent);
    document.append_child(parent,target);
    css::query_host query(document);
    const std::vector<std::pair<std::string,bool>> cases{
        {".outer .target",true},{".parent > .target",true},
        {".absent .target",false},{"#Parent .target",true},{"#parent .target",false},
        {"ARTICLE .target",true},{"html .target",true},{"body .target",true},
        {".sibling + .parent .target",true},{".sibling ~ .parent > .target",true},
        {".outer > .sibling + .parent .target",true},{".absent + .parent .target",false},
        {".sibling .target",false},{".Tabbed .target",true},{".tabbed .target",false},
        {".line .target",true},{".form .target",true},{".carriage .target",true},
        {".escaped\\:name .target",true},{":is(.absent,.parent) .target",true},
        {":where(.parent,.absent) .target",true},{":not(.absent) .target",true},
        {".parent:has(> .target) .target",true},{":scope > .target",true},
        {".parent:not(.parent) .target",false},{"[id=Parent] .target",true}
    };
    std::vector<css::compiled_css_selector> selectors;
    std::vector<std::vector<std::array<uint64_t,4>>> requirement_snapshots;
    for(const auto& [text,expected]:cases) {
        auto selector=css::compile_selector(text);
        if(selector.ancestor_requirements.size()!=selector.compiled_compounds.size()) {
            std::cerr<<"ancestor metadata alignment: "<<text<<'\n';
            return false;
        }
        std::vector<std::array<uint64_t,4>> snapshot;
        for(const auto& requirement:selector.ancestor_requirements)
            snapshot.push_back(requirement.words);
        requirement_snapshots.push_back(std::move(snapshot));
        selectors.push_back(std::move(selector));
    }
    for(const auto capacity:{size_t{0},size_t{1},size_t{2},size_t{16384}}) {
        css::selector_match_context context;context.ancestor_filter.capacity=capacity;
        for(int repeat=0;repeat<3;++repeat) for(size_t i=0;i<selectors.size();++i) {
            const auto& selector=selectors[i];
            const auto actual=css::selector_matches(document,target,selector,
                selector.compounds.size()-1,&parent,
                [&](const auto& node,const auto& compound,const auto* scope) {
                    return css::compound_matches(query,node,compound,scope);
                },&context);
            if(actual!=cases[i].second || actual!=query.matches_prepared(target,selector,&parent)) {
                std::cerr<<"ancestor parity: "<<cases[i].first<<" capacity="<<capacity<<'\n';
                return false;
            }
        }
        if(context.ancestor_filter.inclusive_ancestors.size()>capacity) return false;
        for(size_t i=0;i<selectors.size();++i) {
            if(selectors[i].ancestor_requirements.size()!=requirement_snapshots[i].size())
                return false;
            for(size_t component=0;component<requirement_snapshots[i].size();++component)
                if(selectors[i].ancestor_requirements[component].words
                    !=requirement_snapshots[i][component]) {
                    std::cerr<<"ancestor metadata mutated: "<<cases[i].first<<'\n';
                    return false;
                }
        }
    }
    auto sibling_chain=css::compile_selector(".sibling + .parent .target");
    css::selector_ancestor_filter::mask parent_only;parent_only.add('.',"parent");
    if(sibling_chain.ancestor_requirements.back().words!=parent_only.words) {
        std::cerr<<"sibling feature leaked into ancestor requirement\n";
        return false;
    }
    // Deliberate hash collision: the filter must defer to full matching.
    css::selector_ancestor_filter::mask present;present.add('.',"parent");
    std::string collision;
    for(int i=0;i<10000 && collision.empty();++i) {
        const auto name="collision-"+std::to_string(i);
        css::selector_ancestor_filter::mask candidate;candidate.add('.',name);
        if(candidate.words==present.words) collision=name;
    }
    if(collision.empty()) {std::cerr<<"no deliberate collision\n";return false;}
    auto colliding=css::compile_selector("."+collision+" .target");
    css::selector_match_context context;
    if(!context.ancestor_filter.may_match(document,target,colliding,1)) {std::cerr<<"collision rejected\n";return false;}
    const auto match=[&](const auto& selector) {
        return css::selector_matches(document,target,selector,selector.compounds.size()-1,nullptr,
            [&](const auto& node,const auto& compound,const auto* scope) {
                return css::compound_matches(query,node,compound,scope);
            },&context);
    };
    if(match(colliding)) {std::cerr<<"collision matched\n";return false;}
    // An immutable pass ends before mutation. A fresh pass must see new ancestry,
    // classes and IDs, including values that previously produced a rejection.
    auto changed=css::compile_selector("#Changed.changed .target");
    if(match(changed)) {std::cerr<<"premature mutation match\n";return false;}
    parent.class_name="changed";parent.id_attribute="Changed";context={};
    if(!match(changed)) {std::cerr<<"mutation not matched\n";return false;}
    if(!document.parser_append_child(sibling,target)) return false;
    context={};
    if(match(changed)) {std::cerr<<"reparent still matched\n";return false;}
    sibling.tag="Widget";sibling.xml_mode=true;
    auto exact=css::compile_selector("Widget .target");
    auto folded=css::compile_selector("widget .target");context={};
    if(!match(exact) || match(folded)) {std::cerr<<"XML parity failed\n";return false;}
    return true;
}

bool test_match_before_precedence_sorting() {
    using namespace webscene_native;
    for(const auto unrelated:{32,1024}) {
        native_document document;
        auto& node=document.create_element("div");
        node.id_attribute="target";node.class_name="target target";
        document.append_child(document.body(),node);
        std::string source="#target {width:43px} .target {width:11px} .target {width:17px}"
            "#target::before {content:'high'} .target::before {content:'low'}"
            ".target::after {content:'after'} #target {width:999px}";
        for(int i=0;i<unrelated;++i)
            source+=".missing-"+std::to_string(i)+" * {width:999px}";
        auto sheet=css::prepare_stylesheet(source,"",[](const auto&) {return true;});
        if(!sheet || sheet->rules.size()!=static_cast<size_t>(unrelated+7)) return false;
        std::vector<css::css_rule> rules;
        for(const auto& payload:sheet->rules) rules.push_back({payload,0,0,true});
        rules[6].media_matches=false;
        std::vector<size_t> indices;
        for(size_t i=rules.size();i>0;--i) indices.push_back(i-1);
        css::query_host query(document);
        const auto match=[&] {
            return css::match_candidates(document,node,rules,indices,
                [&](const auto& subject,const auto&,const auto& selector) {
                    return query.matches_prepared(subject,selector);
                },[&](const auto& subject,const auto& rule) {
                    return query.matches_prepared(subject,rule.compiled_selector());
                });
        };
        const auto reversed=match();
        if(reversed.ordinary!=std::vector<const css::css_rule*>{&rules[1],&rules[2],&rules[0]}
            || reversed.pseudo!=std::vector<std::pair<int,const css::css_rule*>>{
                {1,&rules[4]},{2,&rules[5]},{1,&rules[3]}}) return false;
        indices.push_back(0);indices.push_back(4);
        css::deduplicate_candidates(indices);
        const auto deduplicated=match();
        if(indices.size()!=rules.size() || deduplicated.ordinary!=reversed.ordinary
            || deduplicated.pseudo!=reversed.pseudo) return false;
        std::cout<<"matched-order unrelated="<<unrelated<<" candidates="<<indices.size()
            <<" precedence-entries="<<reversed.ordinary.size()+reversed.pseudo.size()<<'\n';
        // Also check the dense case against the previous precedence oracle:
        // filtering is not allowed to change ordering when every selector hits.
        const auto dense=css::match_candidates(document,node,rules,indices,
            [](const auto&,const auto&,const auto&) {return true;},
            [](const auto&,const auto&) {return true;});
        auto reference_indices=indices;
        css::sort_candidates(rules,reference_indices);
        std::vector<const css::css_rule*> reference_ordinary;
        std::vector<std::pair<int,const css::css_rule*>> reference_pseudo;
        for(const auto index:reference_indices) {
            const auto& rule=rules[index];
            if(!rule.media_matches) continue;
            if(rule.payload->pseudo_kind)
                reference_pseudo.emplace_back(rule.payload->pseudo_kind,&rule);
            else reference_ordinary.push_back(&rule);
        }
        if(dense.ordinary!=reference_ordinary || dense.pseudo!=reference_pseudo) return false;
    }
    return true;
}

int main(int argc,char** argv) {
    if(argc==2) {
        std::ifstream input(argv[1]);
        if(!input) return 82;
        const std::string source((std::istreambuf_iterator<char>(input)),{});
        const auto sheet=webscene_native::css::prepare_stylesheet(source,argv[1],
            [](const auto& query) {
                return webscene_native::css::inventory_media(query,
                    [](std::string_view,std::string_view,std::string_view,const std::string&,std::string_view) {});
            });
        if(!sheet || sheet->rules.empty()) return 83;
        size_t declarations=0;
        for(const auto& rule:sheet->rules) declarations+=rule->declarations.size();
        std::cout<<"Prepared CSS: rules="<<sheet->rules.size()
            <<" declarations="<<declarations<<" keyframes="<<sheet->keyframes.size()
            <<" diagnostics="<<sheet->diagnostics.size()<<"\n";
        for(const auto& diagnostic:sheet->diagnostics)
            std::cout<<diagnostic.classification<<": "<<diagnostic.feature
                <<" ("<<diagnostic.detail<<")\n";
        return 0;
    }
    if(!test_subject_candidate_index_scaling()) return 170;
    if(!test_absent_ancestor_matching_work()) return 174;
    if(!test_ancestor_filter_parity()) return 175;
    if(!test_match_before_precedence_sorting()) return 173;
    using webscene_native::css::parse_declarations;
    const auto values=parse_declarations(R"CSS(
      COLOR: red !important; --Theme: blue; --theme: green;
      background:linear-gradient(125deg,var(--Theme),rgb(0,0,0));
      content:"a;b:c"; --empty: ; -unknown:ignored;
    )CSS",true);
    if(values.size()!=6 || values[0].name!="color" || !values[0].important ||
       values[1].name!="--Theme" || values[2].name!="--theme" ||
       values[3].value.find("linear-gradient")==std::string::npos ||
       values[4].value!=R"("a;b:c")" || values[5].value!=" ") return 1;
    const auto recovered=parse_declarations("bad; color:blue; width:12px");
    if(recovered.size()!=2 || recovered[0].value!="blue") return 2;
    const auto escaped=parse_declarations(R"(c\6flor: red; --\54heme: black)");
    if(escaped.size()!=2 || escaped[0].name!="color" || escaped[1].name!="--Theme") return 3;
    const auto selectors=webscene_native::css::compile_selector_list(
        R"(#toolbar > button.active:hover, .panel::before)");
    if(selectors.selectors.size()!=2 || selectors.selectors[0].compounds.size()!=2 ||
       selectors.selectors[0].combinators[0]!='>' ||
       !selectors.selectors[1].compiled_compounds[0].pseudo_element) return 4;
    const auto escaped_selector=webscene_native::css::compile_selector(R"(.a\:b)");
    if(escaped_selector.compiled_compounds.size()!=1 ||
       escaped_selector.compiled_compounds[0].identities[0].second!="a:b") return 5;
    if(!webscene_native::css::compile_selector_list("div > > span").selectors.empty()) return 6;
    webscene_native::dom_node node;
    node.attributes["data-state"]="active selected";
    node.attributes["lang"]="en-GB";
    if(!webscene_native::css::attribute_matches(node,R"(data-state~="selected")") ||
       !webscene_native::css::attribute_matches(node,R"(lang|="en")") ||
       webscene_native::css::attribute_matches(node,R"(data-state^="")")) return 7;
    node.attributes["data-state"]="idle";
    if(webscene_native::css::attribute_matches(node,R"(data-state~="selected")")) return 8;
    using webscene_native::css::nth_matches;
    if(!nth_matches("2n+1",3) || nth_matches("2n+1",2) ||
       !nth_matches("-n+3",2) || nth_matches("-n+3",4) ||
       !nth_matches("n-2147483648",1) || nth_matches("n+oops",1)) return 9;
    webscene_native::native_document document;
    auto& fieldset=document.create_element("fieldset");
    auto& legend=document.create_element("legend");
    auto& exempt=document.create_element("button");
    auto& blocked=document.create_element("input");
    document.append_child(document.body(),fieldset);
    document.append_child(fieldset,legend);
    document.append_child(legend,exempt);
    document.append_child(fieldset,blocked);
    fieldset.attributes["disabled"]="";
    using webscene_native::css::is_actually_disabled;
    if(!is_actually_disabled(document,blocked) || is_actually_disabled(document,exempt)) return 10;
    fieldset.attributes.erase("disabled");
    if(is_actually_disabled(document,blocked)) return 11;
    blocked.attributes["disabled"]="";
    if(!is_actually_disabled(document,blocked)) return 12;
    auto& select=document.create_element("select");
    auto& option=document.create_element("option");
    document.append_child(document.body(),select); document.append_child(select,option);
    select.attributes["disabled"]="";
    if(!is_actually_disabled(document,option)) return 13;
    webscene_native::css::interaction_state interaction{&exempt,&exempt,false};
    using webscene_native::css::interaction_matches;
    if(!interaction_matches(document,fieldset,"hover",interaction,false) ||
       !interaction_matches(document,fieldset,"focus-within",interaction,false) ||
       interaction_matches(document,fieldset,"focus",interaction,false) ||
       interaction_matches(document,exempt,"focus-visible",interaction,false)) return 14;
    interaction.focus_visible=true;
    if(!interaction_matches(document,exempt,"focus-visible",interaction,false)) return 15;
    interaction={nullptr,&blocked,false};
    if(interaction_matches(document,fieldset,"hover",interaction,false) ||
       interaction_matches(document,exempt,"focus",interaction,false) ||
       !interaction_matches(document,blocked,"focus-visible",interaction,true)) return 16;
    using webscene_native::css::language_matches;
    using webscene_native::css::direction_matches;
    fieldset.attributes["lang"]="en-GB";
    fieldset.attributes["dir"]="rtl";
    if(!language_matches(document,exempt,"EN") ||
       language_matches(document,exempt,"fr") ||
       !direction_matches(document,exempt,"rtl") || direction_matches(document,exempt,"ltr")) return 17;
    legend.attributes["lang"]="fr";
    legend.attributes["dir"]="ltr";
    if(language_matches(document,exempt,"en") || !language_matches(document,exempt,"fr") ||
       !direction_matches(document,exempt,"ltr")) return 18;
    legend.attributes["lang"]="";
    if(language_matches(document,exempt,"en")) return 19;
    if(!direction_matches(document,select,"ltr")) return 20;
    using webscene_native::css::checked_matches;
    auto& second_option=document.create_element("option");
    document.append_child(select,second_option);
    if(!checked_matches(option) || checked_matches(second_option)) return 21;
    second_option.attributes["selected"]="";
    if(checked_matches(option) || !checked_matches(second_option)) return 22;
    second_option.mutable_form_control().selectedness_initialized=true;
    second_option.mutable_form_control().selectedness=false;
    if(checked_matches(second_option)) return 23;
    blocked.attributes["type"]="checkbox";blocked.attributes["checked"]="";
    if(!checked_matches(blocked)) return 24;
    blocked.mutable_form_control().checkedness_initialized=true;
    blocked.mutable_form_control().checkedness=false;
    if(checked_matches(blocked)) return 25;
    blocked.id_attribute="destination";
    if(!webscene_native::css::target_matches(blocked,"#destination") ||
       webscene_native::css::target_matches(blocked,"#") ||
       webscene_native::css::target_matches(blocked,"#elsewhere")) return 26;
    const auto tag_match=[](const webscene_native::dom_node& n,
        const webscene_native::css::compiled_css_compound& c,const webscene_native::dom_node*) {
        return c.valid && c.tag==n.tag;
    };
    const auto matches=[&](const webscene_native::dom_node& n,std::string_view text) {
        const auto prepared=webscene_native::css::compile_selector(text);
        return !prepared.compounds.empty() && webscene_native::css::selector_matches(document,n,
            prepared,prepared.compounds.size()-1,nullptr,tag_match);
    };
    if(!matches(exempt,"fieldset button") || !matches(exempt,"legend > button") ||
       matches(exempt,"fieldset > button") || !matches(blocked,"legend + input") ||
       !matches(blocked,"legend ~ input") || matches(exempt,"input + button")) return 27;
    webscene_native::css::query_host host(document,1);
    host.set_interaction(&exempt,&exempt,true);
    host.set_target_hash("#destination");
    exempt.class_name="primary";
    if(!host.css_selector_matches(exempt,"fieldset > legend > button.primary:focus:hover") ||
       host.css_selector_matches(exempt,"button:disabled") ||
       !host.css_selector_matches(blocked,"input:disabled:target") ||
       !host.css_selector_matches(fieldset,"fieldset:has(> legend):focus-within") ||
       !host.css_selector_matches(exempt,"button:not(.secondary):is(.primary, .other)")) return 28;
    if(host.query_selector_node(fieldset,":scope > legend > button")!=&exempt) return 29;
    exempt.class_name="secondary";
    if(host.css_selector_matches(exempt,"button.primary") ||
       !host.css_selector_matches(exempt,"button.secondary")) return 30;
    host.set_interaction(nullptr,nullptr,false);
    if(host.css_selector_matches(exempt,"button:focus")) return 31;
    std::vector<webscene_native::css::css_rule> rules;
    const auto add_rule=[&](std::string selector,uint32_t specificity,std::string value,bool important) {
        auto payload=std::make_shared<webscene_native::css::css_rule_payload>();
        payload->selector=std::move(selector);payload->specificity=specificity;
        payload->declarations.push_back({"--theme",std::move(value),important});
        rules.push_back({payload});
    };
    add_rule(":root",10,"red",true);
    add_rule("html",1,"blue",false);
    add_rule(":root",10,"green",true);
    std::vector<size_t> candidates{2,0,1};
    webscene_native::css::sort_candidates(rules,candidates);
    if(candidates!=std::vector<size_t>{1,0,2}) return 32;
    std::unordered_map<std::string,std::string> variables;
    std::unordered_set<std::string> important;
    webscene_native::css::rebuild_root_variables(rules,variables,important);
    if(variables["--theme"]!="green" || !important.contains("--theme")) return 33;
    rules[2].media_matches=false;
    webscene_native::css::rebuild_root_variables(rules,variables,important);
    if(variables["--theme"]!="red") return 34;
    rules[0].shadow_scope_root_id=1;
    webscene_native::css::rebuild_root_variables(rules,variables,important);
    if(variables["--theme"]!="blue" || important.contains("--theme")) return 35;
    using webscene_native::css::resolve_value;
    variables["--size"]="12px";
    fieldset.style.mutable_custom_properties().values["--size"]="20px";
    if(resolve_value(exempt,"var(--size)",variables)!="20px" ||
       resolve_value(select,"var(--size)",variables)!="12px" ||
       resolve_value(exempt,"var(--absent, var(--size))",variables)!="20px") return 36;
    legend.style.mutable_custom_properties().values["--size"]="8px";
    if(resolve_value(exempt,"calc(var(--size) + var(--size))",variables)!="calc(8px + 8px)") return 37;
    legend.style.mutable_custom_properties().values["--size"]="9px";
    if(resolve_value(exempt,"var(--size)",variables)!="9px") return 38;
    using webscene_native::css::seed_inline_custom_properties;
    using webscene_native::css::apply_custom_property;
    exempt.mutable_authored_style().declarations["--size"]="11px";
    seed_inline_custom_properties(exempt);
    if(apply_custom_property(exempt,{"--size","20px",false}) ||
       resolve_value(exempt,"var(--size)",variables)!="11px") return 39;
    if(!apply_custom_property(exempt,{"--size","22px",true}) ||
       resolve_value(exempt,"var(--size)",variables)!="22px") return 40;
    exempt.mutable_authored_style().important_declarations.insert("--size");
    seed_inline_custom_properties(exempt);
    if(apply_custom_property(exempt,{"--size","30px",true}) ||
       resolve_value(exempt,"var(--size)",variables)!="11px") return 41;
    exempt.mutable_authored_style().declarations.erase("--size");
    exempt.mutable_authored_style().important_declarations.erase("--size");
    seed_inline_custom_properties(exempt);
    if(!apply_custom_property(exempt,{"--size","33px",false}) ||
       resolve_value(exempt,"var(--size)",variables)!="33px") return 42;
    webscene_native::node_style box;
    webscene_native::css::apply_margin_declaration(box,"margin","1px 2px 3px auto");
    if(box.margin_top.value!=1 || box.margin_right.value!=2 || box.margin_bottom.value!=3 || !box.margin_left_auto) return 43;
    webscene_native::css::apply_padding_declaration(box,"padding","2px 4px");
    if(box.padding_top.value!=2 || box.padding_left.value!=4 || box.padding_bottom.value!=2) return 44;
    exempt.mutable_authored_style().declarations["margin-left"]="7px";
    webscene_native::css::apply_margin_declaration(exempt.style,"margin-left","7px");
    webscene_native::css::apply_margin(exempt,{"margin","3px",false},"3px");
    if(exempt.style.margin_left.value!=7 || exempt.style.margin_right.value!=3) return 45;
    webscene_native::css::apply_margin(exempt,{"margin","9px",true},"9px");
    webscene_native::css::apply_margin(exempt,{"margin-right","1px",false},"1px");
    if(exempt.style.margin_left.value!=9 || exempt.style.margin_right.value!=9) return 46;
    webscene_native::css::apply_inset_declaration(box,"inset","1px 2px auto 4px");
    if(box.top.value!=1 || box.right.value!=2 || box.left.value!=4 ||
       box.bottom.unit!=webscene_native::length_unit::automatic) return 47;
    webscene_native::css::apply_border_declaration(box,"border","2px solid #123456");
    if(box.border_left_width.value!=2 || box.border_top_rgba!=0x123456ffu || box.border_top_current_color) return 48;
    webscene_native::css::apply_border_declaration(box,"border-left-color","currentColor");
    if(!box.border_left_current_color || box.border_top_current_color) return 49;
    webscene_native::css::apply_border_declaration(box,"border-width","1px 2px 3px 4px");
    if(box.border_top_width.value!=1 || box.border_left_width.value!=4) return 50;
    webscene_native::css::apply_border_declaration(box,"border","none");
    if(box.border_top_width.value!=0 || box.border_left_width.value!=0) return 51;
    using webscene_native::css::apply_corner_radius_declaration;
    apply_corner_radius_declaration("border-radius","1px 2px 3px 4px / 5px 6px 7px 8px",box);
    if(box.border_top_left_radius.value!=1 || box.border_bottom_left_radius.value!=4 ||
       box.border_top_left_radius_y().value!=5 || box.border_bottom_left_radius_y().value!=8) return 52;
    apply_corner_radius_declaration("border-top-left-radius","9px 10px",box);
    if(box.border_top_left_radius.value!=9 || box.border_top_left_radius_y().value!=10) return 53;
    webscene_native::node_style::pseudo_element pseudo;
    apply_corner_radius_declaration("border-radius","2px / 4px",pseudo);
    if(!pseudo.elliptical_border_radius || pseudo.border_top_right_radius_y.value!=4) return 54;
    apply_corner_radius_declaration("border-radius","3px",pseudo);
    if(pseudo.elliptical_border_radius || pseudo.border_top_left_radius.value!=3) return 55;
    using webscene_native::css::apply_transition_shorthand;
    apply_transition_shorthand(box,"opacity .2s ease-in 50ms, left 100ms linear -20ms");
    if(box.animations().opacity_transition.duration_ms!=200 ||
       box.animations().opacity_transition.delay_ms!=50 ||
       box.animations().left_transition.duration_ms!=100 ||
       box.animations().left_transition.delay_ms!=-20 ||
       box.animations().left_transition.x1!=0) return 56;
    apply_transition_shorthand(box,"none");
    if(box.animations().opacity_transition.duration_ms!=0 ||
       box.animations().left_transition.duration_ms!=0) return 57;
    using webscene_native::css::apply_grid_placement_declaration;
    apply_grid_placement_declaration(box,"grid-area","1 / 2 / 3 / 4");
    if(box.grid().row_start_value!="1" || box.grid().column_start_value!="2" ||
       box.grid().row_end_value!="3" || box.grid().column_end_value!="4") return 58;
    apply_grid_placement_declaration(box,"grid-column","2");
    if(box.grid().column_value!="2" || box.grid().column_end_value!="auto" || box.grid().span_all) return 59;
    webscene_native::css::apply_animation_shorthand(box,"progress 1.2s infinite ease-in-out");
    if(box.animations().animation_name_value!="progress" ||
       box.animations().animation_duration_value!="1.2s" ||
       box.animations().animation_iteration_count_value!="infinite") return 60;
    webscene_native::css::apply_animation_shorthand(box,"none");
    if(box.animations().animation_name_value!="none" ||
       box.animations().animation_duration_value!="0s") return 61;
    if(webscene_native::css::is_css_time("progress") || webscene_native::css::is_css_time("NaNs") ||
       webscene_native::css::is_css_time("1junkms") || !webscene_native::css::is_css_time("+.15s") ||
       webscene_native::css::parse_css_time_ms("1e-1s")!=100) return 62;
    webscene_native::native_document animated_document;
    auto& animated=animated_document.create_element("div");
    animated_document.append_child(animated_document.body(),animated);
    animated.style.width={20,webscene_native::length_unit::pixels};
    animated.style.height={20,webscene_native::length_unit::pixels};
    std::unordered_map<std::string,webscene_native::css::css_opacity_keyframes> definitions;
    definitions["progress"].opacity_stops={{0,0},{1,1}};
    webscene_native::css::apply_animation_shorthand(animated.style,"progress 1s linear infinite");
    webscene_native::css::configure_keyframes(animated.style,definitions);
    animated_document.layout(100,100);
    animated_document.signal_animation_frame(0);
    animated_document.update_style_animations(animated);
    animated_document.advance_animations();
    animated_document.signal_animation_frame(500);
    animated_document.advance_animations();
    if(std::abs(animated.painted_opacity_value()-.5f)>.02f) return 63;
    webscene_native::css::apply_animation_shorthand(animated.style,"none");
    webscene_native::css::configure_keyframes(animated.style,definitions);
    animated_document.update_style_animations(animated);
    if(animated.animation_runtime() && animated.animation_runtime()->opacity_keyframe_animation_active) return 64;
    webscene_native::node_style::pseudo_element generated;
    const auto apply_generated=[&](const std::string& name,const std::string& value) {
        return webscene_native::css::apply_pseudo_value(generated,0x123456FF,false,name,value);
    };
    apply_generated("content",R"("\2192 next")");
    apply_generated("display","inline-block");
    apply_generated("padding-inline","4px 8px");
    apply_generated("border","2px solid currentColor");
    apply_generated("border-radius","4px / 8px");
    if(!generated.generated || generated.content!="\u2192next" ||
       generated.display!=webscene_native::display_mode::inline_block ||
       generated.padding_left.value!=4 || generated.padding_right.value!=8 ||
       generated.border_left_width.value!=2 || !generated.border_left_current_color ||
       !generated.elliptical_border_radius || generated.border_top_left_radius.value!=4 ||
       generated.border_top_left_radius_y.value!=8) return 65;
    if(apply_generated("unknown-property","x").classification!="unsupported" ||
       apply_generated("line-height","inherit").classification!="supported" ||
       generated.line_height!=-1) return 66;
    if(apply_generated("line-height","1").classification!="supported" ||
       generated.line_height!=-4 ||
       apply_generated("line-height","5").classification!="supported" ||
       generated.line_height!=-8 ||
       apply_generated("line-height","2px").classification!="supported" ||
       generated.line_height!=2) return 168;
    apply_generated("content","none");
    apply_generated("border","none");
    if(generated.generated || !generated.content.empty() || generated.border_left_width.value!=0) return 67;
    webscene_native::css::apply_pseudo_value(generated,0x123456FF,true,"box-sizing","inherit");
    if(!generated.border_box) return 166;
    webscene_native::css::apply_pseudo_value(generated,0x123456FF,false,"box-sizing","inherit");
    if(generated.border_box) return 167;
    stylesheet_test_host stylesheet_host;
    const std::string stylesheet_address="embedded:styles.css";
    webscene_native::css::stylesheet_sink stylesheet_sink(stylesheet_host,stylesheet_address,1024);
    const auto stylesheet_parsed=webscene_native::stream_css_syntax_stylesheet(R"CSS(
      .base { color: red; }
      @media (min-width: 600px) {
        @media (orientation: landscape) { .wide { color: blue !important; } }
      }
      @supports selector(:focus-visible) { .focus { outline: 2px solid; } }
      @container card (width > 10px) { .excluded { color: green; } }
      @keyframes pulse { from { opacity: 0; } to { opacity: 1; } }
      .last { content: "a;b"; }
      .modal, .panel {
        position: fixed;
        .resizable { position: absolute; }
        &.wide { width: 80%; }
        > .content { min-width: 0; }
      }
    )CSS",stylesheet_sink);
    if(!stylesheet_parsed || !stylesheet_sink.complete() || stylesheet_host.rules.size()!=10 ||
       stylesheet_host.rules[1].media.size()!=2 || !stylesheet_host.rules[1].declarations[0].important ||
       stylesheet_host.rules[3].selector!=".excluded" || stylesheet_host.rules[3].media.size()!=1 ||
       !webscene_native::css::is_container_query(stylesheet_host.rules[3].media[0]) ||
       webscene_native::css::container_query_text(stylesheet_host.rules[3].media[0])!="card (width > 10px)" ||
       stylesheet_host.rules[4].selector!=".last" || stylesheet_host.rules[4].address!=stylesheet_address ||
       stylesheet_host.rules[5].selector!=".modal" || stylesheet_host.rules[6].selector!=".panel" ||
       stylesheet_host.rules[7].selector!=":is(.modal, .panel) .resizable" ||
       stylesheet_host.rules[8].selector!=":is(.modal, .panel).wide" ||
       stylesheet_host.rules[9].selector!=":is(.modal, .panel) > .content" ||
       stylesheet_sink.keyframes().size()!=1 || stylesheet_sink.keyframes()[0].second.opacity_stops.size()!=2 ||
       stylesheet_sink.keyframes()[0].first!="pulse" || !stylesheet_host.unsupported.empty()) return 68;
    std::unordered_map<std::string,webscene_native::css::css_opacity_keyframes> parsed_keyframes;
    for(auto& [name,definition]:stylesheet_sink.keyframes()) {
        webscene_native::css::finish_keyframes(parsed_keyframes,name,std::move(definition));
    }
    webscene_native::css::apply_animation_shorthand(animated.style,"pulse 1s linear infinite");
    webscene_native::css::configure_keyframes(animated.style,parsed_keyframes);
    animated_document.signal_animation_frame(1000);
    animated_document.update_style_animations(animated);
    animated_document.advance_animations();
    animated_document.signal_animation_frame(1250);
    animated_document.advance_animations();
    if(std::abs(animated.painted_opacity_value()-.25f)>.02f) return 69;
    webscene_native::css::css_opacity_keyframes rotation_definition;
    webscene_native::css::append_keyframe(rotation_definition,"to",{{"transform","rotate(.5turn)",false}});
    webscene_native::css::finish_keyframes(parsed_keyframes,"spin",std::move(rotation_definition));
    if(parsed_keyframes["spin"].rotation_stops.size()!=2 ||
       parsed_keyframes["spin"].rotation_stops[0].degrees!=0 ||
       parsed_keyframes["spin"].rotation_stops[1].degrees!=180) return 70;
    webscene_native::css::rule_payload_cache payload_cache;
    std::mutex payload_mutex;
    const auto payload_for=[&](const std::string& selector,const std::string& color) {
        return webscene_native::css::intern_rule_payload(payload_mutex,payload_cache,
            [](std::string_view value) {
                return webscene_native::css::compile_selector(value, nullptr);
            },selector,{{"color",color,false}},{});
    };
    auto red_payload=payload_for(".base","red");
    if(red_payload!=payload_for(".base","red") || red_payload==payload_for(".base","blue") ||
       red_payload->compiled_selector.compiled_compounds.size()!=1 || red_payload->specificity==0) return 71;
    std::weak_ptr<const webscene_native::css::css_rule_payload> released=red_payload;
    red_payload.reset();
    if(!released.expired()) return 72;
    red_payload=payload_for(".base","red");
    if(!red_payload || red_payload->declarations[0].value!="red") return 73;
    // Classification belongs to the interned selector, not to each match.
    const std::pair<const char*,uint8_t> classified_selectors[] = {
        {".base",0},{".base::before",1},{".base:before",1},
        {".base::after",2},{".base:after",2},
        {".base::-webkit-scrollbar",3},{".base::-webkit-scrollbar-thumb",4},
        {".base::-webkit-scrollbar-track",5},{".base::-webkit-scrollbar-corner",6},
        {".base::backdrop",7},{".base[data-label='::before']",0}
    };
    for(const auto& [selector,kind]:classified_selectors) {
        const auto payload=payload_for(selector,"red");
        if(payload->pseudo_kind!=kind || payload->host_selector ||
           (kind!=0 && payload->compiled_pseudo_origin.compounds!=std::vector<std::string>{".base"}) ||
           (kind==0 && !payload->compiled_pseudo_origin.compounds.empty()) ||
           payload_for(selector,"red")!=payload) return 166;
    }
    const auto host_payload=payload_for(" :host ","red");
    if(!host_payload->host_selector || host_payload->pseudo_kind!=0 ||
       payload_for(":host(.base)","red")->host_selector) return 167;
    size_t compile_count=0;
    const auto counted_payload=[&] {
        return webscene_native::css::intern_rule_payload(payload_mutex,payload_cache,
            [&](const auto& selector) { ++compile_count; return webscene_native::css::compile_selector(selector); },
            ".compiled-once::before",{{"content","'x'",false}},{});
    };
    const auto compiled_once=counted_payload();
    for(int iteration=0;iteration<128;++iteration)
        if(counted_payload()!=compiled_once) return 168;
    if(compile_count!=2) return 169; // full selector and pseudo origin, only once
    const std::string css_base="asset://kestrel/css/theme/main.css";
    const auto resolved_css=webscene_native::css::resolve_resource_urls(
        R"CSS(url('../../images/grid.png?v=2'), url("../fonts/ui.woff2"), url(#mask))CSS",css_base);
    if(resolved_css!=R"CSS(url("asset://kestrel/images/grid.png?v=2"), url("asset://kestrel/css/fonts/ui.woff2"), url(#mask))CSS") return 74;
    if(webscene_native::resources::resolve_url("data:image/png;base64,AA",css_base)!="data:image/png;base64,AA" ||
       webscene_native::resources::resolve_url("/icons/tool.svg",css_base)!="asset://kestrel/icons/tool.svg" ||
       webscene_native::resources::resolve_url("//cdn.test/a.png","https://example.test/css/main.css")!="https://cdn.test/a.png") return 75;
    if(webscene_native::css::resolve_resource_urls("url('unterminated",css_base)!="url('unterminated" ||
       webscene_native::css::resolve_resource_urls("url(icon.png)","")!="url(icon.png)") return 76;
    const auto old_rule_count=stylesheet_host.rules.size();
    stylesheet_host.append_parsed_css_style_rule(".one, :is(.two, .three)",
        {{"background-image","url(../../images/grid.png)",false}}, {},css_base);
    if(stylesheet_host.rules.size()!=old_rule_count+2 ||
       stylesheet_host.rules.back().declarations[0].value!="url(\"asset://kestrel/images/grid.png\")") return 77;
    stylesheet_host.append_parsed_css_style_rule("div > > span",{{"color","red",false}}, {},css_base);
    if(stylesheet_host.rules.size()!=old_rule_count+2) return 78;
    auto prepared=webscene_native::css::prepare_stylesheet(R"CSS(
        .panel { background-image: url(../../images/grid.png); }
        @media (min-width: 600px) { .panel { width: 50%; } }
        @keyframes fade { from { opacity: 0; } to { opacity: 1; } }
    )CSS",css_base,[](const std::string&) { return false; });
    if(!prepared || prepared->rules.size()!=2 || prepared->source_address!=css_base ||
       prepared->rules[0]->declarations[0].value!="url(\"asset://kestrel/images/grid.png\")" ||
       prepared->rules[1]->media_queries.size()!=1 || prepared->keyframes["fade"].opacity_stops.size()!=2 ||
       prepared->diagnostics.size()!=2) return 79;
    // Rule data outlives the preparation host and its temporary interning cache.
    auto retained_rule=prepared->rules[0];
    prepared.reset();
    if(retained_rule->selector!=".panel" || retained_rule->compiled_selector.compiled_compounds.empty()) return 80;
    const auto rejected=webscene_native::css::prepare_stylesheet(
        "div > > span {color:red} @font-face {font-family: Test;src:url(test.woff2)}",
        css_base,[](const auto&) { return true; });
    if(!rejected || !rejected->rules.empty() || rejected->diagnostics.size()!=2) return 81;
    using webscene_native::css::media_matches;
    if(!media_matches("(max-width:1250px)",{1250,800}) ||
       media_matches("(max-width:1250px)",{1251,800}) ||
       !media_matches("(max-height:700px)",{1400,700}) ||
       media_matches("(max-height:700px)",{1400,701}) ||
       !media_matches("(prefers-color-scheme:dark)",{1400,800,true}) ||
       media_matches("print",{1400,800}) ||
       !media_matches("print, (min-width:1000px)",{1400,800})) return 84;
    const auto live_sheet=webscene_native::css::prepare_stylesheet(
        ".panel {opacity:1} .panel:hover {opacity:.5} .panel::before {content:'x'}",
        "",[](const auto&) { return true; });
    if(!live_sheet || live_sheet->rules.size()!=3) return 85;
    webscene_native::css::query_host live_query(animated_document,0);
    animated.class_name="panel";
    if(!live_query.matches_prepared(animated,live_sheet->rules[0]->compiled_selector) ||
       live_query.matches_prepared(animated,live_sheet->rules[1]->compiled_selector) ||
       live_query.matches_prepared(animated,live_sheet->rules[2]->compiled_selector)) return 86;
    live_query.set_interaction(&animated,nullptr,false);
    if(!live_query.matches_prepared(animated,live_sheet->rules[1]->compiled_selector)) return 87;
    animated.class_name="other";
    if(live_query.matches_prepared(animated,live_sheet->rules[0]->compiled_selector) ||
       live_query.matches_prepared(animated,live_sheet->rules[1]->compiled_selector)) return 88;
    using webscene_native::css::property_mask;
    if(property_mask("background")!=(property_mask("background-color")|property_mask("background-image")) ||
       property_mask("flex")!=(property_mask("flex-grow")|property_mask("flex-shrink")|property_mask("flex-basis")) ||
       property_mask("transition")!=(property_mask("transition-property")|property_mask("transition-duration")|
           property_mask("transition-delay")|property_mask("transition-timing-function")) ||
       property_mask("inset-inline-start")!=property_mask("left") ||
       property_mask("borderTopColor")!=property_mask("border") ||
       property_mask("--custom")!=0 || property_mask("text-anchor")<=0xFFFFFFFFULL) return 89;
    const auto& property_catalog=webscene_native::css::effective_property_metadata_catalog;
    for(size_t index=0;index<property_catalog.size();++index) {
        const auto& metadata=property_catalog[index];
        if(metadata.mask==0 || property_mask(metadata.name)!=metadata.mask ||
           (index>0 && property_catalog[index-1].name>=metadata.name)) return 179;
        for(uint8_t longhand=0;longhand<metadata.longhand_count;++longhand) {
            const auto* target=webscene_native::css::find_effective_property_metadata(
                metadata.longhands[longhand]);
            if(target==nullptr || target->longhand_count!=0U) return 179;
        }
    }
    std::vector<webscene_native::css::css_declaration> effective_declarations;
    webscene_native::css::for_each_effective_declaration(
        {"border-inline-color","red blue",false},
        [&](const auto& declaration,std::string_view) {
            effective_declarations.push_back(declaration);
        });
    webscene_native::css::for_each_effective_declaration(
        {"inset-block","3px 7px",true},
        [&](const auto& declaration,std::string_view) {
            effective_declarations.push_back(declaration);
        });
    if(effective_declarations.size()!=4 ||
       effective_declarations[0].name!="border-left-color" ||
       effective_declarations[0].value!="red" ||
       effective_declarations[1].name!="border-right-color" ||
       effective_declarations[1].value!="blue" ||
       effective_declarations[2].name!="top" ||
       effective_declarations[2].value!="3px" ||
       !effective_declarations[2].important ||
       effective_declarations[3].name!="bottom" ||
       effective_declarations[3].value!="7px") return 180;
    auto& reset_node=animated_document.create_element("button");
    reset_node.style.width={90,webscene_native::length_unit::pixels};
    reset_node.style.height={70,webscene_native::length_unit::pixels};
    reset_node.style.inline_property_mask=property_mask("width");
    reset_node.style.important_property_mask=property_mask("color");
    reset_node.style.foreground_rgba=0x123456FF;
    reset_node.style.opacity=.4f;
    reset_node.style.mutable_custom_properties().values["--color"]="red";
    reset_node.style.mutable_before_pseudo().content="retained";
    webscene_native::css::apply_all_unset(reset_node);
    if(reset_node.style.width.value!=90 || reset_node.style.height.value==70 ||
       reset_node.style.opacity!=1 || reset_node.style.foreground_rgba!=0x123456FF ||
       reset_node.style.custom_properties().values.at("--color")!="red" ||
       reset_node.style.mutable_before_pseudo().content!="retained") return 90;
    animated_document.append_child(animated_document.body(),reset_node);
    animated_document.body().style.width={640,webscene_native::length_unit::pixels};
    const auto apply_box=[&](const std::string& name,const std::string& value,uint64_t protected_mask=0) {
        return webscene_native::css::apply_box_metrics(reset_node,name,value,
            [&](uint64_t property) { return (protected_mask&property)!=0; });
    };
    apply_box("width","200px",property_mask("width"));
    if(reset_node.style.width.value!=90) return 91;
    if(!apply_box("width","inherit") || reset_node.style.width.value!=640) return 92;
    apply_box("inset","1px 2px 3px 4px",property_mask("left"));
    apply_box("padding-inline","5px 9px");
    apply_box("gap","7px 11px");
    if(reset_node.style.left.value==4 || reset_node.style.top.value!=1 ||
       reset_node.style.padding_left.value!=5 || reset_node.style.padding_right.value!=9 ||
       reset_node.style.row_gap.value!=7 || reset_node.style.column_gap.value!=11) return 93;
    apply_box("width","unset");
    if(reset_node.style.width.value==640 || apply_box("unknown-property","1px")) return 94;
    webscene_native::native_document grid_document;
    auto& grid_root=grid_document.body();
    grid_root.style.display=webscene_native::display_mode::grid;
    webscene_native::css::property_result grid_result;
    if(!webscene_native::css::apply_grid_value(grid_root,"grid-template-columns",
        "100px minmax(0,1fr)",grid_result,[](uint64_t) { return false; })) return 95;
    auto& fixed_column=grid_document.create_element("div");
    auto& flexible_column=grid_document.create_element("div");
    grid_document.append_child(grid_root,fixed_column);
    grid_document.append_child(grid_root,flexible_column);
    grid_document.layout(500,100);
    if(std::abs(fixed_column.layout.width-100)>.1f ||
       std::abs(flexible_column.layout.width-400)>.1f) return 96;
    grid_document.layout(700,100);
    if(std::abs(fixed_column.layout.width-100)>.1f ||
       std::abs(flexible_column.layout.width-600)>.1f) return 97;
    grid_root.style.display=webscene_native::display_mode::flex;
    const auto unprotected=[](uint64_t) { return false; };
    webscene_native::css::apply_flex_value(grid_root,"flex-flow","row nowrap",unprotected);
    fixed_column.style.width={100,webscene_native::length_unit::pixels};
    webscene_native::css::apply_flex_value(fixed_column,"flex","none",unprotected);
    webscene_native::css::apply_flex_value(flexible_column,"flex","1 1 0px",unprotected);
    grid_document.layout(500,100);
    if(std::abs(fixed_column.layout.width-100)>.1f ||
       std::abs(flexible_column.layout.width-400)>.1f) return 98;
    grid_document.layout(700,100);
    if(std::abs(flexible_column.layout.width-600)>.1f) return 99;
    const auto structure=[&](webscene_native::dom_node& node,const std::string& name,const std::string& value) {
        return webscene_native::css::apply_structure_value(grid_document,node,name,value,grid_result,unprotected);
    };
    structure(fixed_column,"display","none");
    grid_document.layout(700,100);
    if(std::abs(flexible_column.layout.width-700)>.1f) return 100;
    grid_root.style.position=webscene_native::position_mode::fixed;
    grid_root.style.z_index=12;
    grid_root.style.z_index_auto=false;
    structure(flexible_column,"position","inherit");
    structure(flexible_column,"z-index","inherit");
    if(flexible_column.style.position!=webscene_native::position_mode::fixed ||
       flexible_column.style.z_index!=12 || flexible_column.style.z_index_auto) return 101;
    structure(flexible_column,"z-index","auto");
    structure(flexible_column,"border-spacing","3px 7px");
    structure(flexible_column,"border-collapse","collapse");
    if(!flexible_column.style.z_index_auto || !flexible_column.style.table().border_collapsed ||
       flexible_column.style.table().border_spacing_vertical.value!=7) return 102;
    size_t image_loads=0;
    const auto paint=[&](const std::string& name,const std::string& value) {
        return webscene_native::css::apply_paint_value(flexible_column,name,value,grid_result,unprotected,
            [&](const std::string& url,std::string& markup,std::string& resolved,std::string& view_box) {
                ++image_loads;
                if(url!="asset://icon.svg") return false;
                markup="<svg viewBox='0 0 20 10'></svg>";
                resolved=url;view_box="0 0 20 10";return true;
            });
    };
    paint("box-shadow","inset 1px 2px 3px currentColor");
    if(!flexible_column.style.box_shadow_present || !flexible_column.style.box_shadow_inset ||
       !flexible_column.style.box_shadow_current_color) return 103;
    paint("background-image","url(asset://icon.svg)");
    paint("background-position","bottom right");
    paint("background-size","20px 30px");
    paint("background-size","contain");
    if(image_loads!=1 || flexible_column.style.background_image().image_view_box!="0 0 20 10" ||
       flexible_column.style.background_image().position_y!="bottom" ||
       flexible_column.style.background_image().size_y!="contain") return 104;
    paint("background-size","20px");
    if(flexible_column.style.background_image().size_y!="auto") return 107;
    paint("background-image","linear-gradient(red, blue)");
    if(image_loads!=1 || !flexible_column.style.background_image().image_markup.empty()) return 105;
    paint("background-image","url(missing.svg)");
    if(grid_result.classification!="unsupported" || !flexible_column.style.background_image().image_markup.empty()) return 106;
    webscene_native::native_document hit_document;
    auto& hit_node=hit_document.create_element("div");
    hit_document.append_child(hit_document.body(),hit_node);
    hit_node.style.width={100,webscene_native::length_unit::pixels};
    hit_node.style.height={100,webscene_native::length_unit::pixels};
    const auto visibility=[&](const std::string& name,const std::string& value) {
        return webscene_native::css::apply_visibility_value(hit_document,hit_node,name,value,unprotected);
    };
    hit_document.layout(200,200);
    if(hit_document.hit_test(hit_document.body(),10,10)!=&hit_node) return 108;
    visibility("pointer-events","none");
    if(hit_document.hit_test(hit_document.body(),10,10)==&hit_node) return 109;
    visibility("pointer-events","auto");
    visibility("visibility","hidden");
    if(hit_document.hit_test(hit_document.body(),10,10)==&hit_node) return 110;
    visibility("visibility","visible");
    visibility("overflow","visible scroll");
    if(!hit_node.style.clip || !hit_node.style.scroll_x_enabled || !hit_node.style.scroll_y_enabled) return 111;
    visibility("overflow","clip");
    visibility("contain","paint");
    visibility("opacity","2");
    if(hit_node.style.scroll_x_enabled || hit_node.style.scroll_y_enabled ||
       !hit_node.style.contain_stacking_context || hit_node.style.opacity!=1) return 112;
    const auto text_value=[&](const std::string& name,const std::string& value) {
        return webscene_native::css::apply_text_value(hit_node,name,value,grid_result,unprotected);
    };
    hit_document.body().style.font_size=20;
    text_value("font-size","150%");
    text_value("letter-spacing",".5em");
    text_value("line-height","1.5");
    if(hit_node.style.font_size!=30 || hit_node.style.letter_spacing!=15 ||
       hit_node.style.line_height!=-4.5f) return 113;
    text_value("font","700 16px/1.5 sans-serif");
    if(hit_node.style.font_size!=16 || hit_node.style.font_weight!=700 ||
       hit_node.style.textual().font_family!="sans-serif") return 114;
    const auto decoration=[&](const std::string& name,const std::string& value) {
        return webscene_native::css::apply_decoration_value(hit_node,name,value,grid_result,unprotected);
    };
    decoration("border","3px solid currentColor");
    if(hit_node.style.border_left_width.value!=3 || !hit_node.style.border_left_current_color) return 115;
    decoration("border-color","red");
    if(hit_node.style.border_left_rgba!=0xFF0000FF || hit_node.style.border_left_current_color) return 116;
    decoration("transform","translateX(10px)");
    if(!hit_node.style.transform_specified || hit_node.style.transform_translate_x.value!=10) return 117;
    decoration("transform","none");
    decoration("transition","opacity 500ms linear");
    decoration("border-style","none");
    if(hit_node.style.transform_stacking_context || hit_node.style.border_left_width.value!=0 ||
       hit_node.style.animations().transition_duration_value!="500ms") return 118;
    webscene_native::native_document styled_document;
    auto& view=styled_document.create_element("div");view.class_name="view";
    auto& child_a=styled_document.create_element("div");child_a.class_name="child";
    auto& child_b=styled_document.create_element("div");child_b.class_name="child";
    styled_document.append_child(styled_document.body(),view);
    styled_document.append_child(view,child_a);styled_document.append_child(view,child_b);
    const auto sheet=webscene_native::css::prepare_stylesheet(
        ".view {display:flex;width:300px;height:100px} .child {flex:1}","",
        [](const auto&) { return true; });
    if(!sheet) return 119;
    webscene_native::css::query_host styled_query(styled_document);
    for(auto* node:{&view,&child_a,&child_b}) {
        for(const auto& rule:sheet->rules) {
            if(!styled_query.matches_prepared(*node,rule->compiled_selector)) continue;
            for(const auto& declaration:rule->declarations) {
                webscene_native::css::property_result result;
                webscene_native::css::apply_resolved_declaration(styled_document,*node,
                    declaration,declaration.value,false,result,
                    [](const auto&,auto&,auto&,auto&) { return false; });
                if(result.classification=="unsupported") return 120;
            }
        }
    }
    styled_document.layout(500,200);
    if(std::abs(view.layout.width-300)>.1f || std::abs(child_a.layout.width-150)>.1f ||
       std::abs(child_b.layout.width-150)>.1f) return 121;
    std::unordered_map<std::string,std::string> variable_root;
    const auto declare=[&](const std::string& name,const std::string& value,bool important=false) {
        webscene_native::css::property_result result;
        webscene_native::css::apply_declaration(styled_document,view,{name,value,important},variable_root,
            false,result,[](const auto&,auto&,auto&,auto&) { return false; },[](bool) {});
        return result;
    };
    declare("--size","420px");
    declare("width","var(--size)");
    if(view.style.width.value!=420) return 122;
    if(declare("width","var(--missing)").classification!="invalid-authoring" || view.style.width.value!=420) return 123;
    declare("width","var(--missing, 320px)");
    declare("grid-gap","8px");
    declare("height","40px",true);
    declare("height","90px");
    styled_document.layout(500,200);
    if(view.layout.width!=320 || view.style.row_gap.value!=8 || view.style.height.value!=40) return 124;
    view.class_name.clear();
    view.style.visibility_hidden=true;
    view.style.mutable_before_pseudo().content="stale";
    webscene_native::css::reset_cascaded_style(view,variable_root);
    styled_document.layout(500,200);
    if(view.layout.width!=500 || view.style.visibility_hidden || view.style.important_property_mask!=0 ||
       view.style.custom_properties().values.contains("--size") ||
       !view.style.mutable_before_pseudo().content.empty()) return 125;
    view.style.width={123,webscene_native::length_unit::pixels};
    view.style.inline_property_mask=property_mask("width");
    webscene_native::css::reset_cascaded_style(view,variable_root);
    styled_document.layout(500,200);
    if(view.layout.width!=123) return 126;
    webscene_native::native_document ordered_document;
    auto& ordered_node=ordered_document.create_element("div");ordered_node.class_name="view small";
    ordered_document.append_child(ordered_document.body(),ordered_node);
    ordered_node.mutable_authored_style().declarations["height"]="var(--height)";
    ordered_node.style.inline_property_mask=property_mask("height");
    const auto ordered_sheet=webscene_native::css::prepare_stylesheet(
        ".view {width:var(--size)} .small {--size:120px;--height:40px}","",
        [](const auto&) { return true; });
    if(!ordered_sheet) return 127;
    std::vector<webscene_native::css::css_rule> ordered_rules;
    for(const auto& payload:ordered_sheet->rules) ordered_rules.push_back({payload,0,0,true});
    std::vector<const webscene_native::css::css_rule*> ordered_matches;
    for(const auto& rule:ordered_rules) ordered_matches.push_back(&rule);
    webscene_native::css::reset_cascaded_style(ordered_node,variable_root);
    webscene_native::css::apply_matched_declarations(ordered_node,ordered_matches,
        [&](const webscene_native::css::css_declaration& declaration,bool inline_origin) {
            webscene_native::css::property_result result;
            webscene_native::css::apply_declaration(ordered_document,ordered_node,declaration,variable_root,
                inline_origin,result,[](const auto&,auto&,auto&,auto&) { return false; },[](bool) {});
        });
    ordered_document.layout(500,200);
    if(ordered_node.layout.width!=120 || ordered_node.layout.height!=40 ||
       ordered_node.style.inline_property_mask!=property_mask("height")) return 128;
    std::string pseudo_origin;
    if(webscene_native::css::split_pseudo_element_selector(".view::before",pseudo_origin)!=1 ||
       pseudo_origin!=".view") return 129;
    ordered_node.style.mutable_custom_properties().values["--tone"]="red";
    webscene_native::css::property_result pseudo_result;
    auto& before=ordered_node.style.mutable_before_pseudo();
    webscene_native::css::apply_pseudo_declaration(ordered_node,before,{"content","'label'",false},
        variable_root,pseudo_result,[](bool) {});
    webscene_native::css::apply_pseudo_declaration(ordered_node,before,{"color","var(--tone)",false},
        variable_root,pseudo_result,[](bool) {});
    if(!before.generated || before.content!="label" || before.foreground_rgba!=0xFF0000FF) return 130;
    webscene_native::css::apply_scrollbar_declaration(ordered_node,3,{"display","none",true},variable_root);
    webscene_native::css::apply_scrollbar_declaration(ordered_node,3,{"display","block",false},variable_root);
    if(!ordered_node.style.scrollbar_hidden || ordered_node.style.display==webscene_native::display_mode::none) return 131;
    const auto match_sheet=webscene_native::css::prepare_stylesheet(
        ".view {width:10px} #panel {width:20px} .view::before {content:'x'}", "",
        [](const auto&) { return true; });
    if(!match_sheet || match_sheet->rules.size()!=3) return 132;
    std::vector<webscene_native::css::css_rule> match_rules;
    for(const auto& payload:match_sheet->rules) match_rules.push_back({payload,0,0,true});
    std::vector<size_t> match_indices{2,1,0};
    // Matching receives discovery order, not cascade order. It must return
    // only the matches, ordered by specificity and original source position.
    ordered_node.id_attribute="panel";
    webscene_native::css::query_host match_query(ordered_document);
    const auto collect=[&] {
        return webscene_native::css::match_candidates(ordered_document,ordered_node,match_rules,match_indices,
            [&](const auto& node,const auto&,const auto& selector) { return match_query.matches_prepared(node,selector); },
            [&](const auto& node,const auto& rule) { return match_query.matches_prepared(node,rule.compiled_selector()); });
    };
    auto collected_matches=collect();
    if(collected_matches.ordinary.size()!=2 || collected_matches.ordinary.back()->selector()!="#panel" ||
       collected_matches.pseudo.size()!=1 || collected_matches.pseudo[0].first!=1) return 133;
    match_rules[1].media_matches=false;
    collected_matches=collect();
    if(collected_matches.ordinary.size()!=1) return 134;
    ordered_node.class_name.clear();
    collected_matches=collect();
    if(!collected_matches.ordinary.empty() || !collected_matches.pseudo.empty()) return 135;
    auto font_payload=std::make_shared<webscene_native::css::css_rule_payload>();
    font_payload->declarations={{"line-height","2em",false},{"font-size","30px",false}};
    webscene_native::css::css_rule font_rule{font_payload,0,0,true};
    std::vector<const webscene_native::css::css_rule*> font_rules{&font_rule};
    ordered_node.style.font_size=30;
    ordered_node.style.line_height=20;
    webscene_native::css::recompute_cascaded_line_height(ordered_node,font_rules,variable_root);
    if(ordered_node.style.line_height!=60) return 136;
    auto geometry_style=ordered_node.style;
    geometry_style.background_rgba=0xFFFFFFFF;
    if(!webscene_native::css::computed_layout_style_equal(ordered_node.style,geometry_style)) return 137;
    geometry_style.width={999,webscene_native::length_unit::pixels};
    if(webscene_native::css::computed_layout_style_equal(ordered_node.style,geometry_style)) return 138;
    geometry_style=ordered_node.style;
    geometry_style.mutable_before_pseudo().padding_left={9,webscene_native::length_unit::pixels};
    if(webscene_native::css::computed_layout_style_equal(ordered_node.style,geometry_style)) return 139;
    webscene_native::css::css_cascade_state indexed;
    const auto index_selector_test=[&](size_t index,const std::string& selector) {
        webscene_native::css::index_selector(index,selector,webscene_native::css::compile_selector(selector),indexed.rules_by_id,
            indexed.rules_by_class,indexed.rules_by_tag,indexed.rules_by_attribute,
            indexed.focus_rules,indexed.unindexed_rules,indexed.descendant_attribute_dependencies);
    };
    index_selector_test(0,"[data-state] > .item:hover");
    index_selector_test(1,"#panel::before");
    index_selector_test(2,"button:disabled");
    index_selector_test(3,"[title]");
    index_selector_test(4,":focus");
    index_selector_test(5,":root");
    index_selector_test(6,"*");
    if(indexed.rules_by_class["item"]!=std::vector<size_t>{0} ||
       indexed.rules_by_id["panel"]!=std::vector<size_t>{1} ||
       indexed.rules_by_tag["button"]!=std::vector<size_t>{2} ||
       indexed.rules_by_attribute["title"]!=std::vector<size_t>{3} ||
       indexed.focus_rules!=std::vector<size_t>{4} ||
       indexed.rules_by_tag["html"]!=std::vector<size_t>{5} ||
       indexed.unindexed_rules!=std::vector<size_t>{6} ||
       !indexed.descendant_attribute_dependencies.contains("data-state") ||
       indexed.descendant_attribute_dependencies.contains("title")) return 140;
    ordered_node.tag="button";
    ordered_node.id_attribute="panel";
    ordered_node.class_name=" item  item\t";
    ordered_node.attributes["title"]="example";
    auto candidate_indices=webscene_native::css::collect_candidates(ordered_node,true,
        indexed.rules_by_tag,indexed.rules_by_id,indexed.rules_by_attribute,
        indexed.focus_rules,indexed.unindexed_rules,
        [&](auto& result,std::string_view name) {
            auto found=indexed.rules_by_class.find(std::string(name));
            if(found!=indexed.rules_by_class.end())
                result.insert(result.end(),found->second.begin(),found->second.end());
        });
    std::sort(candidate_indices.begin(),candidate_indices.end());
    if(candidate_indices!=std::vector<size_t>{0,0,1,2,3,4,6}) return 141;
    index_selector_test(7,":not(.skip).after-pseudo");
    index_selector_test(8,".escaped\\:token");
    index_selector_test(9,".other#index\\:id");
    index_selector_test(10,"Widget");
    index_selector_test(11,"[DaTa-Thing]");
    index_selector_test(12,":is(.a,.b)");
    index_selector_test(13,":not(.excluded)");
    if(indexed.rules_by_class["after-pseudo"]!=std::vector<size_t>{7} ||
       indexed.rules_by_class["escaped:token"]!=std::vector<size_t>{8} ||
       indexed.rules_by_id["index:id"]!=std::vector<size_t>{9} ||
       indexed.rules_by_tag["Widget"]!=std::vector<size_t>{10} ||
       indexed.rules_by_tag["widget"]!=std::vector<size_t>{10} ||
       indexed.rules_by_attribute["DaTa-Thing"]!=std::vector<size_t>{11} ||
       indexed.rules_by_attribute["data-thing"]!=std::vector<size_t>{11} ||
       indexed.unindexed_rules!=std::vector<size_t>{6,12,13}) return 171;
    // Folded name buckets must not change XML's case-sensitive matching.
    auto name_sheet=webscene_native::css::prepare_stylesheet(
        "Widget {width:23px} [DaTa-Thing] {height:17px}","",[](const auto&) {return true;});
    if(!name_sheet) return 172;
    webscene_native::css::stylesheet_owner name_sheets;
    name_sheets.replace(1,std::move(*name_sheet));
    for(const auto xml:{false,true}) for(const auto mixed_case:{false,true}) {
        webscene_native::native_document names_document;
        auto& node=names_document.create_element("div");
        node.xml_mode=xml;
        node.tag=mixed_case?"Widget":"widget";
        node.attributes[mixed_case?"DaTa-Thing":"data-thing"]="yes";
        names_document.append_child(names_document.body(),node);
        webscene_native::css::query_host query(names_document);
        auto indices=name_sheets.candidates(node);
        std::vector<size_t> indexed_matches,full_matches;
        const auto& rules=name_sheets.state().rules;
        for(const auto index:indices)
            if(query.matches_prepared(node,rules[index].compiled_selector())) indexed_matches.push_back(index);
        for(size_t index=0;index<rules.size();++index)
            if(query.matches_prepared(node,rules[index].compiled_selector())) full_matches.push_back(index);
        if(indexed_matches!=full_matches) return 173;
        if(xml && full_matches.size()!=(mixed_case?2U:0U)) return 174;
    }
    webscene_native::css::stylesheet_owner sheets;
    auto responsive=std::make_shared<webscene_native::css::css_rule_payload>();
    responsive->selector=".item";
    responsive->media_queries={"(min-width: 600px)"};
    sheets.replace(1,{"asset://app/ui.css",{responsive},{},{}});
    if(sheets.state().rules[0].media_matches || sheets.candidates(ordered_node).size()!=1) return 142;
    if(!sheets.set_environment({800,600,false}) || !sheets.state().rules[0].media_matches ||
       sheets.set_environment({900,600,false})) return 143;
    sheets.replace(2,{"asset://app/override.css",{responsive},{},{}});
    sheets.replace(1,{"asset://app/ui.css",{responsive},{},{}});
    if(sheets.state().rules.size()!=2 || sheets.state().rules[0].stylesheet_owner_id!=1 ||
       sheets.state().rules[1].stylesheet_owner_id!=2 || sheets.candidates(ordered_node).size()!=2) return 144;
    if(!sheets.remove(1) || sheets.remove(1) || sheets.state().rules.size()!=1 ||
       sheets.candidates(ordered_node)!=std::vector<size_t>{0}) return 145;
    sheets.remove(2);
    if(!sheets.candidates(ordered_node).empty()) return 146;
    auto native_sheet=webscene_native::css::prepare_stylesheet(
        ".item { width: 140px; color: red } .item::before { content: 'ready'; padding: 3px }",
        "asset://app/native.css",[](const auto&) { return true; });
    if(!native_sheet) return 147;
    sheets.replace(7,std::move(*native_sheet));
    const auto recascade_native=[&] {
        return webscene_native::css::apply_native_cascade(ordered_document,ordered_node,sheets,match_query,
            variable_root,false,[](const auto&,auto&,auto&,auto&) { return false; },
            [](const auto&,const auto&) {});
    };
    recascade_native();
    if(ordered_node.style.width.value!=140 || !ordered_node.style.before_pseudo().generated ||
       ordered_node.style.before_pseudo().content!="ready") return 148;
    auto native_replacement=webscene_native::css::prepare_stylesheet(
        ".item { width: 210px; color: blue }","asset://app/native.css",[](const auto&) { return true; });
    if(!native_replacement) return 149;
    sheets.replace(7,std::move(*native_replacement));
    if(!recascade_native() || ordered_node.style.width.value!=210 ||
       ordered_node.style.before_pseudo().generated) return 150;
    sheets.remove(7);
    recascade_native();
    if(ordered_node.style.width.value==210) return 151;
    auto layered_sheet=webscene_native::css::prepare_stylesheet(R"CSS(
        @layer reset, components;
        @layer reset {
          :root { --native-layer-size: 37px !important; }
          #layered-native {
            width: var(--native-layer-size);
            margin-left: 11px !important;
            font-size: 10px;
            line-height: 2 !important;
          }
          #layered-native::before {
            content: "";
            display: block;
            height: 7px !important;
          }
        }
        @layer components {
          :root { --native-layer-size: 57px !important; }
          #layered-native {
            margin-left: 21px !important;
            line-height: 3 !important;
          }
          #layered-native::before { height: 17px !important; }
        }
    )CSS","asset://app/layered.css",[](const auto&) {return true;});
    if(!layered_sheet || layered_sheet->cascade_layers!=std::vector<std::string>{"reset","components"})
        return 176;
    for(const auto& rule:layered_sheet->rules)
        if(rule->cascade_layer_index==0U || rule->cascade_layer_index>2U) return 177;
    webscene_native::native_document layered_document;
    auto& layered_node=layered_document.create_element("div");
    layered_node.id_attribute="layered-native";
    layered_document.append_child(layered_document.body(),layered_node);
    webscene_native::css::query_host layered_query(layered_document);
    webscene_native::css::stylesheet_owner layered_sheets;
    layered_sheets.replace(1,std::move(*layered_sheet));
    webscene_native::css::apply_native_document_cascade(
        layered_document,layered_sheets,layered_query,
        [](const auto&,auto&,auto&,auto&) {return false;},
        [](const auto&,const auto&) {});
    if(layered_node.style.width.value!=37
        || layered_node.style.margin_left.value!=11
        // Unitless line-height is retained as -(3 + multiplier) so it
        // inherits as a multiplier; -5 therefore represents the winning 2.
        || layered_node.style.line_height!=-5
        || layered_node.style.before_pseudo().height.value!=7) {
        std::cerr<<"layered-cascade width="<<layered_node.style.width.value
            <<" margin-left="<<layered_node.style.margin_left.value
            <<" line-height="<<layered_node.style.line_height
            <<" before-height="<<layered_node.style.before_pseudo().height.value<<'\n';
        return 178;
    }
    webscene_native::native_document tree_document;
    auto& tree_parent=tree_document.create_element("div");tree_parent.class_name="parent";
    auto& tree_child=tree_document.create_element("div");tree_child.class_name="child";
    tree_document.append_child(tree_document.body(),tree_parent);
    tree_document.append_child(tree_parent,tree_child);
    webscene_native::css::query_host tree_query(tree_document);
    webscene_native::css::stylesheet_owner tree_sheets;
    const auto tree_update=[&](const std::string& css) {
        auto prepared=webscene_native::css::prepare_stylesheet(css,"asset://app/tree.css",[](const auto&) {return true;});
        if(!prepared) return false;
        tree_sheets.replace(1,std::move(*prepared));
        webscene_native::css::apply_native_document_cascade(tree_document,tree_sheets,tree_query,
            [](const auto&,auto&,auto&,auto&) {return false;},[](const auto&,const auto&) {});
        return true;
    };
    if(!tree_update(":root {--size: 80px} .parent {--local: var(--size)} .child {width: var(--local)}") ||
       tree_child.style.width.value!=80) return 152;
    if(!tree_update(":root {--size: 120px} .parent {--local: var(--size)} .child {width: var(--local)}") ||
       tree_child.style.width.value!=120) return 153;
    if(!tree_update(".child {width: var(--local, 25px)}") || tree_child.style.width.value!=25) return 154;
    if(!tree_update(".child {width: 30px} @media (min-width: 600px) {.child {width: 90px}}") ||
       tree_child.style.width.value!=30) return 155;
    if(!tree_sheets.set_environment({800,600,false})) return 156;
    webscene_native::css::apply_native_document_cascade(tree_document,tree_sheets,tree_query,
        [](const auto&,auto&,auto&,auto&) {return false;},[](const auto&,const auto&) {});
    if(tree_child.style.width.value!=90) return 157;
    webscene_native::css::native_style_session session(tree_document);
    auto session_sheet=webscene_native::css::prepare_stylesheet(
        ".child {width: 40px} .child:focus {width: 70px} .changed {width: 100px}",
        "asset://app/session.css",[](const auto&) {return true;});
    if(!session_sheet) return 158;
    session.replace(1,std::move(*session_sheet));
    const auto session_flush=[&] {
        return session.flush([](const auto&,auto&,auto&,auto&) {return false;},[](const auto&,const auto&) {});
    };
    if(!session_flush() || session.pending() || session_flush() || tree_child.style.width.value!=40) return 159;
    session.set_interaction(nullptr,&tree_child,true);
    if(!session_flush() || tree_child.style.width.value!=70) return 160;
    session.set_interaction(nullptr,&tree_child,true);
    if(session_flush()) return 161;
    session.set_interaction(nullptr,nullptr,false);
    tree_child.class_name="changed";session.invalidate();session.invalidate();
    if(!session_flush() || session_flush() || tree_child.style.width.value!=100) return 162;
    session.set_environment({1000,600,false});
    if(!session_flush()) return 163;
    session.set_environment({1000,600,false});
    if(session_flush()) return 164;
    if(!session.remove(1) || !session_flush() || tree_child.style.width.value==100) return 165;
    std::cout<<"V8-free shared CSS declaration service passed\n";
}
