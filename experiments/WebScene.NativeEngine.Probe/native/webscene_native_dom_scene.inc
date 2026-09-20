void native_document::build_scene(
    std::vector<webscene_scene_command>& commands,
    std::vector<webscene_scene_string>& strings,
    std::vector<char>& string_bytes, bool ordered_canvas, bool capture_gpu_outputs) const
{
    commands.clear();
    commands.reserve(nodes_.size());
    strings.clear();
    string_bytes.clear();
    append_scene(*body_, commands, strings, string_bytes, false, true, false, nullptr, nullptr, ordered_canvas, false, capture_gpu_outputs);
    std::vector<dom_node*> fixed;
    for (auto* child : composed_children(*body_)) {
        collect_outermost_fixed_positioned_nodes(*this, *child, fixed);
    }
    std::stable_sort(
        fixed.begin(),
        fixed.end(),
        [](const auto* left, const auto* right) {
            return left->paint_z_index < right->paint_z_index;
        });
    for (const auto* fixed_node : fixed) {
        if (is_in_modal_layer(*fixed_node) || !display_tree_allows_render(*this, *fixed_node)) continue;
        auto inherited_visibility_hidden = false;
        std::vector<const dom_node*> ancestors;
        for (auto* ancestor = composed_parent(*fixed_node);
             ancestor != nullptr;
             ancestor = composed_parent(*ancestor)) {
            ancestors.push_back(ancestor);
        }
        for (auto iterator = ancestors.rbegin(); iterator != ancestors.rend(); ++iterator) {
            if ((*iterator)->style.visibility_specified) {
                inherited_visibility_hidden = (*iterator)->style.visibility_hidden;
            }
        }
        append_scene(
            *fixed_node,
            commands,
            strings,
            string_bytes,
            inherited_visibility_hidden,
            false, false, nullptr, nullptr, ordered_canvas, false, capture_gpu_outputs);
    }
    // Modal roots leave their DOM stacking context and are emitted exactly
    // once, in registration order, after ordinary/fixed document content.
    for (const auto& entry : modal_dialogs()) {
        if (entry.dialog_id > native_id_index_.size()) continue;
        const auto* modal = native_id_index_[entry.dialog_id - 1U];
        if (modal == nullptr || !display_tree_allows_render(*this, *modal)) continue;
        if(modal->dialog_state && (modal->dialog_state->backdrop_rgba&0xffU))
            commands.push_back(webscene_scene_command{9U,0U,0,0,viewport_width_,viewport_height_,
                modal->dialog_state->backdrop_rgba,modal->id,0,0,0,0,0});
        bool hidden = false;
        std::vector<const dom_node*> ancestors;
        for (auto* ancestor = composed_parent(*modal); ancestor != nullptr;
             ancestor = composed_parent(*ancestor)) ancestors.push_back(ancestor);
        for (auto ancestor = ancestors.rbegin(); ancestor != ancestors.rend(); ++ancestor) {
            if ((*ancestor)->style.visibility_specified) hidden = (*ancestor)->style.visibility_hidden;
        }
        append_scene(*modal, commands, strings, string_bytes, hidden,
            false, false, nullptr, nullptr, ordered_canvas, true, capture_gpu_outputs);
    }
    // Interactive constraint feedback is engine-owned and therefore never
    // enters the author DOM. Emit one retained overlay after all author and
    // modal content. Its geometry is recomputed only when a scene is already
    // being published, so scrolling/layout naturally repositions it without
    // animation-frame demand or another cascade pass.
    if (auxiliary_nodes_ && auxiliary_nodes_->validation_message) {
        const auto& state = *auxiliary_nodes_->validation_message;
        const auto* anchor = state.anchor_id == 0U
                || state.anchor_id > native_id_index_.size()
            ? nullptr : native_id_index_[state.anchor_id - 1U];
        const auto* target = state.target_id == 0U
                || state.target_id > native_id_index_.size()
            ? nullptr : native_id_index_[state.target_id - 1U];
        if (anchor != nullptr && target != nullptr
            && is_connected(*anchor) && is_connected(*target)
            && viewport_width_ > 0.0F && viewport_height_ > 0.0F) {
            constexpr float padding = 9.0F;
            constexpr float font_size = 13.0F;
            constexpr float line_height = 18.0F;
            const auto text_width = measure_text_width(state.message, *anchor);
            const auto bubble_width = std::clamp(
                text_width + padding * 2.0F,
                std::min(160.0F, viewport_width_),
                std::min(360.0F, viewport_width_));
            const auto content_width = std::max(
                0.0F, bubble_width - padding * 2.0F);
            constexpr size_t maximum_lines = 4U;
            std::vector<std::string> lines;
            lines.reserve(maximum_lines);
            std::istringstream words(state.message);
            std::string line;
            std::string word;
            bool truncated = false;
            while(words>>word) {
                auto candidate=line.empty()?word:line+" "+word;
                if(line.empty()
                    || measure_text_width(candidate,*anchor)<=content_width) {
                    line=std::move(candidate);
                    continue;
                }
                lines.push_back(std::move(line));
                line=std::move(word);
                if(lines.size()==maximum_lines) {
                    truncated=true;
                    break;
                }
            }
            if(!line.empty() && lines.size()<maximum_lines)
                lines.push_back(std::move(line));
            if(lines.empty()) lines.emplace_back();
            if(words>>word) truncated=true;
            if(truncated
                || measure_text_width(lines.back(),*anchor)>content_width) {
                constexpr std::string_view ellipsis="\xe2\x80\xa6";
                auto& last=lines.back();
                while(!last.empty()
                    && measure_text_width(last+std::string(ellipsis),*anchor)
                        >content_width) {
                    auto cut=last.size()-1U;
                    while(cut>0U
                        && (static_cast<unsigned char>(last[cut])&0xc0U)==0x80U)
                        --cut;
                    last.resize(cut);
                }
                last+=ellipsis;
            }
            const auto bubble_height = line_height
                * static_cast<float>(lines.size()) + padding * 2.0F;
            const auto x = std::clamp(
                anchor->layout.x, 0.0F, std::max(0.0F, viewport_width_ - bubble_width));
            const auto below = anchor->layout.y + anchor->layout.height + 6.0F;
            const auto above = anchor->layout.y - bubble_height - 6.0F;
            const auto y = below + bubble_height <= viewport_height_
                ? below : std::max(0.0F, above);
            commands.push_back(webscene_scene_command{
                10U, 0U, x, y, bubble_width, bubble_height,
                0x202124F2U, anchor->id, 5, 5, 5, 5, 0});
            commands.push_back(webscene_scene_command{
                11U, 0U, x, y, bubble_width, bubble_height,
                0xFFFFFFFFU, anchor->id, 5, 5, 5, 5, 1});
            commands.push_back(webscene_scene_command{
                12U, 0U, x + padding, y + padding,
                content_width,
                line_height * static_cast<float>(lines.size()),
                0U, anchor->id});
            for(size_t index=0U;index<lines.size();++index) {
                const auto resource = std::to_string(font_size) + '\t'
                    + std::to_string(line_height)
                    + "\t600\tstart\tsans-serif\tauto\t" + lines[index];
                commands.push_back(webscene_scene_command{
                    3U, append_scene_string(resource, strings, string_bytes),
                    x + padding,
                    y + padding + static_cast<float>(index) * line_height,
                    content_width, line_height,
                    0xFFFFFFFFU, anchor->id, 0, 0, 0, 0, 0});
            }
            commands.push_back(webscene_scene_command{
                13U, 0U, 0, 0, 0, 0, 0U, anchor->id});
        }
    }
}

void native_document::publish_gpu_canvas_image(dom_node& node,
    std::shared_ptr<const webscene_gpu_image_lease_v3> image)
{
    if ((node.tag!="canvas" && node.tag!="video") || find_by_native_id(node.id)!=&node)
        throw std::invalid_argument("GPU canvas belongs to another document or is not a canvas");
    auto& canvas=node.mutable_canvas();
    const bool changed=canvas.gpu_image!=image;
    canvas.publish_gpu_image(std::move(image));
    if (changed) {
        // Submission already invalidated the document and captured this output.
        // Completion resolves that dependency; it does not create new content.
        // Keep ordinary completed-image publication invalidating as before.
        if(canvas.gpu_snapshot && canvas.gpu_snapshot->state()!=webscene_gpu_image_snapshot::status::failed) {
            const auto captured=canvas.gpu_snapshot->describe();
            const auto completed=canvas.gpu_image->value.describe();
            if(captured.canvas==completed.canvas && captured.allocation==completed.allocation
                && captured.allocation_generation==completed.allocation_generation
                && captured.content_serial==completed.content_serial)return;
        }
        mark_scene_changed();
    }
}

bool native_document::validate_gpu_canvas_binding(const gpu_canvas_scene_binding& binding) const
{
    const auto* node=binding.node_id==0 || binding.node_id>native_id_index_.size()
        ? nullptr : native_id_index_[binding.node_id-1];
    if(!node || (node->tag!="canvas" && node->tag!="video") || !is_connected(*node))return false;
    const auto& backing=node->canvas().backing;
    if(binding.presentation_generation) {
        return binding.completed && !binding.pending
            && node->canvas().gpu_presentation_image==binding.completed
            && backing.identity()==binding.metadata.canvas
            && backing.allocation_generation()==binding.presentation_generation;
    }
    return backing.identity()==binding.metadata.canvas
        && backing.allocation_generation()==binding.metadata.allocation_generation
        && backing.accepts_completed_content(binding.metadata.content_serial)
        && backing.width()==binding.metadata.width && backing.height()==binding.metadata.height;
}

void native_document::build_gpu_canvas_bindings(
    std::vector<gpu_canvas_scene_binding>& bindings) const
{
    bindings.clear();
    for(const auto& node:nodes_) {
        if((node->tag!="canvas" && node->tag!="video") || !is_connected(*node) || !node->visible
            || !display_tree_allows_render(*this,*node))continue;
        const auto& canvas=node->canvas();
        if(!canvas.gpu_snapshot&&!canvas.gpu_image) {
            if(canvas.gpu_presentation_image) {
                const auto metadata=canvas.gpu_presentation_image->value.describe();
                if(metadata.canvas==canvas.backing.identity())
                    bindings.push_back({node->id,metadata,nullptr,canvas.gpu_presentation_image,
                        canvas.backing.allocation_generation()});
            }
            continue;
        }
        const auto metadata=canvas.gpu_snapshot ? canvas.gpu_snapshot->describe()
            : canvas.gpu_image->value.describe();
        if(metadata.canvas!=canvas.backing.identity()
            || metadata.allocation_generation!=canvas.backing.allocation_generation()
            || !canvas.backing.accepts_completed_content(metadata.content_serial))continue;
        bindings.push_back({node->id,metadata,canvas.gpu_snapshot,
            canvas.gpu_snapshot ? nullptr : canvas.gpu_image});
    }
}

void native_document::build_gpu_canvas_images(
    std::vector<std::shared_ptr<const webscene_gpu_image_lease_v3>>& images) const
{
    images.clear();
    for (const auto& node : nodes_) {
        if ((node->tag!="canvas" && node->tag!="video") || !is_connected(*node) || !node->visible
            || !display_tree_allows_render(*this,*node)) continue;
        const auto& canvas=node->canvas();
        if (!canvas.gpu_image) continue;
        const auto m=canvas.gpu_image->value.describe();
        // A bitmap reset invalidates the presented content. Retained scenes
        // still own the previous frame until their consumers finish.
        if (m.allocation_generation!=canvas.backing.allocation_generation()
            || !canvas.backing.accepts_completed_content(m.content_serial)) continue;
        images.push_back(canvas.gpu_image);
    }
}

void native_document::build_canvas_layouts(
    std::vector<webscene_canvas_layout>& layouts) const
{
    layouts.clear();
    for (const auto& node : nodes_) {
        if (node->tag != "canvas"
            || !is_connected(*node)
            || !node->visible
            || !display_tree_allows_render(*this, *node)) {
            continue;
        }
        const auto width = node->attributes.find("width");
        const auto height = node->attributes.find("height");
        layouts.push_back(webscene_canvas_layout{
            node->id,
            0U,
            node->layout.x,
            node->layout.y,
            node->layout.width,
            node->layout.height,
            static_cast<uint32_t>(std::max(
                0.0F,
                width == node->attributes.end() ? 0.0F : parse_number(width->second))),
            static_cast<uint32_t>(std::max(
                0.0F,
                height == node->attributes.end() ? 0.0F : parse_number(height->second)))});
    }
}

void native_document::build_canvas_display_lists(
    std::vector<webscene_canvas_layer>& layers,
    std::vector<webscene_canvas_command>& canvas_commands,
    std::vector<webscene_scene_string>& strings,
    std::vector<char>& string_bytes) const
{
    constexpr uint32_t offscreen_canvas_layer = 1U << 31U;
    layers.clear();
    canvas_commands.clear();
    strings.clear();
    string_bytes.clear();

    std::unordered_set<uint32_t> retained_canvas_ids;
    for (const auto& node : nodes_) {
        if (node->tag != "canvas" || node->canvas().commands.empty()) continue;
        if ((is_connected(*node)
                && node->visible
                && display_tree_allows_render(*this, *node))
            || node->id == retained_export_canvas_id_) {
            retained_canvas_ids.insert(node->id);
        }
    }
    // An exported canvas can be a composition of otherwise detached source
    // canvases. Retain the complete drawImage(canvas) dependency graph.
    auto added_dependency = true;
    while (added_dependency) {
        added_dependency = false;
        for (const auto& node : nodes_) {
            if (!retained_canvas_ids.contains(node->id)) continue;
            const auto& canvas = node->canvas();
            if (canvas.dependency_generation != canvas.generation
                || canvas.dependency_command_count > canvas.commands.size()) {
                canvas.canvas_dependencies.clear();
                canvas.dependency_command_count = 0;
                canvas.dependency_generation = canvas.generation;
            }
            for (auto index = canvas.dependency_command_count; index < canvas.commands.size(); ++index) {
                const auto& command = canvas.commands[index];
                if (command.kind == 27U) canvas.canvas_dependencies.insert(command.resource_id);
            }
            canvas.dependency_command_count = canvas.commands.size();
            for (const auto dependency : canvas.canvas_dependencies) {
                if (retained_canvas_ids.insert(dependency).second) {
                    added_dependency = true;
                }
            }
        }
    }

    uint32_t z_order = 0;
    for (const auto& node : nodes_) {
        const auto& canvas = node->canvas();
        if (node->tag != "canvas"
            || canvas.commands.empty()
            || !retained_canvas_ids.contains(node->id)) {
            continue;
        }

        // Browser canvas bitmaps exist independently of DOM attachment and CSS
        // visibility. TradingView composes screenshots in a detached canvas and
        // then exports it. Retain those display lists as non-viewport layers so
        // drawImage(canvas) and host-side canvas export can still resolve them.
        const auto offscreen = !is_connected(*node)
            || !node->visible
            || !display_tree_allows_render(*this, *node);

        const auto width = node->attributes.find("width");
        const auto height = node->attributes.find("height");
        const auto command_offset = static_cast<uint32_t>(canvas_commands.size());
        const auto string_offset = static_cast<uint32_t>(strings.size());
        canvas_commands.insert(
            canvas_commands.end(),
            canvas.commands.begin(),
            canvas.commands.end());
        for (const auto& value : canvas.strings) {
            const auto byte_offset = static_cast<uint32_t>(string_bytes.size());
            string_bytes.insert(string_bytes.end(), value.begin(), value.end());
            strings.push_back(webscene_scene_string{
                byte_offset,
                static_cast<uint32_t>(value.size())});
        }

        layers.push_back(webscene_canvas_layer{
            node->id,
            0U,
            command_offset,
            static_cast<uint32_t>(canvas.commands.size()),
            string_offset,
            static_cast<uint32_t>(canvas.strings.size()),
            z_order++ | (offscreen ? offscreen_canvas_layer : 0U),
            offscreen ? 0.0F : node->layout.x,
            offscreen ? 0.0F : node->layout.y,
            offscreen ? 0.0F : node->layout.width,
            offscreen ? 0.0F : node->layout.height,
            static_cast<uint32_t>(std::max(
                0.0F,
                width == node->attributes.end() ? 300.0F : parse_number(width->second, 300.0F))),
            static_cast<uint32_t>(std::max(
                0.0F,
                height == node->attributes.end() ? 150.0F : parse_number(height->second, 150.0F))),
            canvas.generation});
    }
}

void native_document::retain_canvas_for_export(dom_node& node) noexcept
{
    if (node.tag != "canvas") return;
    retained_export_canvas_id_ = node.id;
    mark_scene_changed();
}

bool native_document::has_canvas_references(uint32_t node_id) const
{
    for (const auto& node : nodes_) {
        if (node->tag != "canvas") continue;
        for (const auto& command : node->canvas().commands)
            if (command.kind == 27U && command.resource_id == node_id) return true;
    }
    return false;
}

bool native_document::release_canvas_export(uint32_t node_id) noexcept
{
    if (node_id == 0U || retained_export_canvas_id_ != node_id) return false;
    retained_export_canvas_id_ = 0U;
    mark_scene_changed();
    return true;
}

bool native_document::parse_foreground_filters(
    const dom_node& node,
    std::string_view value,
    std::vector<retained_filter_function>& result) const
{
    result.clear();
    const auto trim = [](std::string_view input) {
        while (!input.empty()
            && std::isspace(static_cast<unsigned char>(input.front()))) {
            input.remove_prefix(1U);
        }
        while (!input.empty()
            && std::isspace(static_cast<unsigned char>(input.back()))) {
            input.remove_suffix(1U);
        }
        return input;
    };
    const auto equals_ascii_case_insensitive = [](std::string_view left,
        std::string_view right) {
        if (left.size() != right.size()) return false;
        for (size_t index = 0; index < left.size(); ++index) {
            const auto character = static_cast<unsigned char>(left[index]);
            const auto lower = character >= 'A' && character <= 'Z'
                ? static_cast<char>(character - 'A' + 'a')
                : static_cast<char>(character);
            if (lower != right[index]) return false;
        }
        return true;
    };
    auto remaining = trim(value);
    if (equals_ascii_case_insensitive(remaining, "none")) return true;
    constexpr size_t maximum_filter_functions = 16U;
    while (!remaining.empty()) {
        if (result.size() >= maximum_filter_functions) {
            result.clear();
            return false;
        }
        const auto opening = remaining.find('(');
        auto closing = std::string_view::npos;
        if (opening != std::string_view::npos) {
            auto depth = 1U;
            for (auto index = opening + 1U; index < remaining.size(); ++index) {
                if (remaining[index] == '(') {
                    ++depth;
                } else if (remaining[index] == ')' && --depth == 0U) {
                    closing = index;
                    break;
                }
            }
        }
        if (opening == 0U || opening == std::string_view::npos
            || closing == std::string_view::npos) {
            result.clear();
            return false;
        }
        const auto name = trim(remaining.substr(0U, opening));
        const auto argument = trim(
            remaining.substr(opening + 1U, closing - opening - 1U));
        uint32_t flags = 0U;
        float amount = -1.0F;
        float maximum = 0.0F;
        if (equals_ascii_case_insensitive(name, "blur")) {
            flags = 1U << 28U;
            maximum = 256.0F;
            const auto length = parse_length(std::string{argument});
            switch (length.unit) {
            case length_unit::pixels:
            case length_unit::em:
            case length_unit::rem:
            case length_unit::viewport_width:
            case length_unit::viewport_height:
            case length_unit::viewport_width_capped:
            case length_unit::viewport_height_capped:
            case length_unit::viewport_width_floored:
            case length_unit::viewport_height_floored:
            case length_unit::container_inline:
            case length_unit::container_block:
            case length_unit::container_min:
            case length_unit::container_max:
                amount = resolve_length(node, length, 0.0F, -1.0F);
                break;
            default:
                break;
            }
        } else {
            const auto parsed = css::parse_ascii_number_prefix(argument);
            if (!parsed.has_value() || parsed->value < 0.0F) {
                result.clear();
                return false;
            }
            const auto suffix = argument.substr(parsed->consumed);
            if (equals_ascii_case_insensitive(name, "brightness")) {
                flags = 1U << 31U;
                maximum = 10.0F;
            } else if (equals_ascii_case_insensitive(name, "grayscale")) {
                flags = 1U << 30U;
                maximum = 1.0F;
            } else if (equals_ascii_case_insensitive(name, "contrast")) {
                flags = 1U << 29U;
                maximum = 10.0F;
            } else if (equals_ascii_case_insensitive(name, "saturate")) {
                flags = 1U << 27U;
                maximum = 10.0F;
            } else {
                result.clear();
                return false;
            }
            amount = suffix.empty()
                ? parsed->value
                : suffix == "%" ? parsed->value / 100.0F : -1.0F;
        }
        if (!std::isfinite(amount) || amount < 0.0F || amount > maximum) {
            result.clear();
            return false;
        }
        result.push_back(retained_filter_function{flags, amount});
        remaining.remove_prefix(closing + 1U);
        if (!remaining.empty()
            && !std::isspace(static_cast<unsigned char>(remaining.front()))) {
            result.clear();
            return false;
        }
        remaining = trim(remaining);
    }
    return true;
}

std::string native_document::serialize_filter_functions(
    std::span<const retained_filter_function> functions)
{
    if (functions.empty()) return "none";
    std::ostringstream value;
    for (size_t index = 0U; index < functions.size(); ++index) {
        if (index != 0U) value << ' ';
        const auto& function = functions[index];
        if (function.flags == (1U << 28U)) value << "blur(" << function.amount << "px)";
        else if (function.flags == (1U << 31U)) value << "brightness(" << function.amount << ')';
        else if (function.flags == (1U << 30U)) value << "grayscale(" << function.amount << ')';
        else if (function.flags == (1U << 29U)) value << "contrast(" << function.amount << ')';
        else if (function.flags == (1U << 27U)) value << "saturate(" << function.amount << ')';
    }
    return value.str();
}

void native_document::append_scene(
    const dom_node& node,
    std::vector<webscene_scene_command>& commands,
    std::vector<webscene_scene_string>& strings,
    std::vector<char>& string_bytes,
    bool inherited_visibility_hidden,
    bool defer_fixed_descendants,
    bool defer_positive_descendants,
    const dom_node* paint_target,
    const node_style::pseudo_element* paint_pseudo_target,
    bool ordered_canvas,
    bool paint_modal_root,
    bool capture_gpu_outputs) const
{
    if (!paint_modal_root && is_modal_dialog(node)) return;
    const auto elevated_target = paint_target == &node;
    const auto pseudo_only = elevated_target && paint_pseudo_target != nullptr;
    if (paint_target == &node) paint_target = nullptr;
    const auto owns_stacking_scope = paint_target == nullptr && !pseudo_only
        && (elevated_target || !defer_positive_descendants
            || style_establishes_atomic_stacking_context(node)
            || node.contains_retained_canvas);
    const auto* animation = node.animation_runtime();
    const auto opacity = std::clamp(
        animation != nullptr && animation->opacity_animation_initialized
            ? animation->painted_opacity
            : node.style.opacity,
        0.0F,
        1.0F);
    if (!node.visible
        || node.style.display == display_mode::none
        || opacity <= 0.001F) {
        return;
    }
    std::vector<retained_filter_function> parsed_foreground_filters;
    auto foreground_filters = node.painted_filter_functions_value();
    if (!node.has_painted_filter_override_value()) {
        const auto& effects = node.style.textual().effect_values;
        const auto known = effects.find("filter");
        if (known != effects.end()
            && parse_foreground_filters(
                node, known->second, parsed_foreground_filters)) {
            foreground_filters = std::span<const retained_filter_function>{
                parsed_foreground_filters};
        }
    }
    struct retained_backdrop_effect final {
        bool present{};
        std::string resource;
        float maximum_blur_sigma{};
    };
    const auto backdrop_effect = [&]() {
        retained_backdrop_effect result{};
        const auto& effects = node.style.textual().effect_values;
        const auto known = effects.find("backdrop-filter");
        if (known == effects.end()) return result;
        auto remaining = std::string_view{known->second};
        const auto trim = [](std::string_view value) {
            while (!value.empty()
                && std::isspace(static_cast<unsigned char>(value.front()))) {
                value.remove_prefix(1U);
            }
            while (!value.empty()
                && std::isspace(static_cast<unsigned char>(value.back()))) {
                value.remove_suffix(1U);
            }
            return value;
        };
        const auto equals_ascii_case_insensitive = [](std::string_view left,
            std::string_view right) {
            if (left.size() != right.size()) return false;
            for (size_t index = 0U; index < left.size(); ++index) {
                const auto character = static_cast<unsigned char>(left[index]);
                const auto lower = character >= 'A' && character <= 'Z'
                    ? static_cast<char>(character - 'A' + 'a')
                    : static_cast<char>(character);
                if (lower != right[index]) return false;
            }
            return true;
        };
        remaining = trim(remaining);
        if (remaining.empty()
            || equals_ascii_case_insensitive(remaining, "none")) {
            return result;
        }
        result.present = true;
        const auto invalid = [&]() {
            result.resource = "webscene-backdrop-invalid-v1";
            result.maximum_blur_sigma = 0.0F;
            return result;
        };
        std::ostringstream resource;
        resource << "webscene-backdrop-v1\t";
        constexpr size_t maximum_filter_functions = 16U;
        size_t function_count = 0U;
        while (!remaining.empty()) {
            if (++function_count > maximum_filter_functions) return invalid();
            const auto opening = remaining.find('(');
            auto closing = std::string_view::npos;
            if (opening != std::string_view::npos) {
                auto depth = 1U;
                for (auto index = opening + 1U; index < remaining.size(); ++index) {
                    if (remaining[index] == '(') ++depth;
                    else if (remaining[index] == ')' && --depth == 0U) {
                        closing = index;
                        break;
                    }
                }
            }
            if (opening == 0U || opening == std::string_view::npos
                || closing == std::string_view::npos) {
                return invalid();
            }
            const auto name = trim(remaining.substr(0U, opening));
            const auto argument = trim(
                remaining.substr(opening + 1U, closing - opening - 1U));
            float amount = -1.0F;
            if (equals_ascii_case_insensitive(name, "blur")) {
                const auto length = parse_length(std::string{argument});
                switch (length.unit) {
                case length_unit::pixels:
                case length_unit::em:
                case length_unit::rem:
                case length_unit::viewport_width:
                case length_unit::viewport_height:
                case length_unit::viewport_width_capped:
                case length_unit::viewport_height_capped:
                case length_unit::viewport_width_floored:
                case length_unit::viewport_height_floored:
                case length_unit::container_inline:
                case length_unit::container_block:
                case length_unit::container_min:
                case length_unit::container_max:
                    amount = resolve_length(node, length, 0.0F, -1.0F);
                    break;
                default:
                    break;
                }
                constexpr auto maximum_backdrop_blur_sigma = 64.0F;
                if (!std::isfinite(amount) || amount < 0.0F
                    || amount > maximum_backdrop_blur_sigma) {
                    return invalid();
                }
                result.maximum_blur_sigma = std::max(
                    result.maximum_blur_sigma, amount);
                resource << (function_count == 1U ? "" : ";")
                    << "blur=" << amount;
            } else if (equals_ascii_case_insensitive(name, "saturate")) {
                const auto parsed = css::parse_ascii_number_prefix(argument);
                if (!parsed.has_value() || parsed->value < 0.0F) return invalid();
                const auto suffix = argument.substr(parsed->consumed);
                amount = suffix.empty()
                    ? parsed->value
                    : suffix == "%" ? parsed->value / 100.0F : -1.0F;
                if (!std::isfinite(amount) || amount < 0.0F || amount > 10.0F) {
                    return invalid();
                }
                resource << (function_count == 1U ? "" : ";")
                    << "saturate=" << amount;
            } else {
                return invalid();
            }
            remaining.remove_prefix(closing + 1U);
            if (!remaining.empty()
                && !std::isspace(static_cast<unsigned char>(remaining.front()))) {
                return invalid();
            }
            remaining = trim(remaining);
        }
        result.resource = resource.str();
        return result;
    }();
    const auto mask_resource = [&]() -> std::optional<uint32_t> {
        const auto& effects = node.style.textual().effect_values;
        const auto known = effects.find("mask-image");
        if (known == effects.end()) return std::nullopt;
        const auto images = css::split_css_image_layers(known->second);
        if (images.size() == 1U && css::ascii_lower(images.front()) == "none") {
            return std::nullopt;
        }
        const auto invalid = [&]() {
            return append_scene_string(
                "webscene-mask-invalid-v1", strings, string_bytes);
        };
        const auto value = [&](std::string_view name, std::string_view fallback) {
            const auto found = effects.find(std::string{name});
            return found == effects.end() ? std::string{fallback} : found->second;
        };
        if (images.empty() || images.size() > 16U) return invalid();
        const auto split_list = [&](std::string_view name, std::string_view fallback) {
            return css::split_css_image_layers(value(name, fallback));
        };
        const auto repeats = split_list("mask-repeat", "repeat");
        const auto positions = split_list("mask-position", "0% 0%");
        const auto sizes = split_list("mask-size", "auto");
        const auto modes = split_list("mask-mode", "match-source");
        const auto composites = split_list("mask-composite", "add");
        if (repeats.empty() || positions.empty() || sizes.empty()
            || modes.empty() || composites.empty()) return invalid();
        const auto layer_value = [](const std::vector<std::string>& values, size_t index) {
            return values[index % values.size()];
        };
        for (size_t index = 0U; index < images.size(); ++index) {
            const auto repeat = css::ascii_lower(layer_value(repeats, index));
            const auto mode = css::ascii_lower(layer_value(modes, index));
            const auto composite = css::ascii_lower(layer_value(composites, index));
            if ((repeat != "repeat" && repeat != "no-repeat"
                    && repeat != "repeat-x" && repeat != "repeat-y")
                || (mode != "match-source" && mode != "alpha")
                || (composite != "add" && composite != "exclude"
                    && composite != "xor")) return invalid();
        }

        const auto first_lower = css::ascii_lower(images.front());
        if (images.size() == 1U && first_lower.starts_with("linear-gradient(")) {
            auto resource = std::string{"webscene-bg-v2\t"};
            resource += images.front();
            resource += '\t';
            resource += layer_value(repeats, 0U);
            resource += '\t';
            resource += layer_value(positions, 0U);
            resource += '\t';
            resource += layer_value(sizes, 0U);
            return append_scene_string(resource, strings, string_bytes);
        }
        if (images.size() == 1U && first_lower.starts_with("url(")) {
            const auto markup = effects.find("-webscene-mask-markup");
            const auto view_box = effects.find("-webscene-mask-view-box");
            if (markup == effects.end() || markup->second.empty()
                || view_box == effects.end() || view_box->second.empty()) {
                return invalid();
            }
            if (!markup->second.starts_with("webscene-raster-v2\t")) {
                auto resource = std::string{"webscene-mask-svg-v1\t"};
                resource += view_box->second;
                resource += '\t';
                resource += layer_value(repeats, 0U);
                resource += '\t';
                resource += layer_value(positions, 0U);
                resource += '\t';
                resource += layer_value(sizes, 0U);
                resource += '\t';
                resource += markup->second;
                return append_scene_string(resource, strings, string_bytes);
            }
        }

        const auto has_exclude = [&]() {
            for (size_t index = 0U; index < images.size(); ++index) {
                const auto composite = css::ascii_lower(layer_value(composites, index));
                if (composite == "exclude" || composite == "xor") return true;
            }
            return false;
        }();
        auto resource = std::string{
            has_exclude ? "webscene-mask-v3\t" : "webscene-mask-v2\t"};
        resource += std::to_string(images.size());
        resource += '\t';
        const auto append_field = [&](std::string_view field) {
            resource += std::to_string(field.size());
            resource += ':';
            resource.append(field);
        };
        for (size_t index = 0U; index < images.size(); ++index) {
            const auto lower = css::ascii_lower(images[index]);
            const auto gradient = lower.starts_with("linear-gradient(")
                || lower.starts_with("radial-gradient(");
            const auto url = lower.starts_with("url(");
            if (!gradient && !url) return invalid();
            if (gradient) {
                const auto open = images[index].find('(');
                if (open == std::string::npos || images[index].back() != ')') {
                    return invalid();
                }
                const auto components = css::split_css_image_layers(
                    std::string_view{images[index]}.substr(
                        open + 1U, images[index].size() - open - 2U));
                if (components.size() < 2U || components.size() > 33U) {
                    return invalid();
                }
            }
            std::string_view view_box;
            std::string_view markup;
            if (url) {
                const auto prefix = "-webscene-mask-layer-" + std::to_string(index);
                const auto view_box_entry = effects.find(prefix + "-view-box");
                const auto markup_entry = effects.find(prefix + "-markup");
                if (view_box_entry == effects.end() || view_box_entry->second.empty()
                    || markup_entry == effects.end() || markup_entry->second.empty()) {
                    return invalid();
                }
                view_box = view_box_entry->second;
                markup = markup_entry->second;
            }
            append_field(images[index]);
            append_field(layer_value(repeats, index));
            append_field(layer_value(positions, index));
            append_field(layer_value(sizes, index));
            append_field(layer_value(modes, index));
            if (has_exclude) {
                const auto composite = css::ascii_lower(layer_value(composites, index));
                append_field(composite == "xor" ? "exclude" : composite);
            }
            append_field(view_box);
            append_field(markup);
            if (resource.size() > 4U * 1024U * 1024U) return invalid();
        }
        return append_scene_string(resource, strings, string_bytes);
    }();
    const auto has_opacity_group = opacity < 0.999F;
    if (has_opacity_group) {
        commands.push_back(webscene_scene_command{
            30U,
            0U,
            node.layout.x,
            node.layout.y,
            node.layout.width,
            node.layout.height,
            static_cast<uint32_t>(std::clamp(
                std::lround(opacity * 255.0F),
                0L,
                255L)),
            node.id});
    }
    if (mask_resource.has_value()) {
        commands.push_back(webscene_scene_command{
            30U,
            1U << 26U,
            node.layout.x,
            node.layout.y,
            node.layout.width,
            node.layout.height,
            255U,
            node.id});
    }
    // SaveLayer effects apply when their groups restore. Open in reverse CSS
    // order so restoration applies the authored function list left-to-right.
    for (size_t filter_index = foreground_filters.size(); filter_index > 0U;
         --filter_index) {
        const auto& filter = foreground_filters[filter_index - 1U];
        commands.push_back(webscene_scene_command{
            30U,
            filter.flags,
            node.layout.x,
            node.layout.y,
            node.layout.width,
            node.layout.height,
            255U,
            node.id,
            0,
            0,
            0,
            0,
            filter.amount});
    }
    const auto resolve_transform_origin = [](css_length value, float available, float fallback) {
        if (value.unit == length_unit::pixels) return value.value;
        if (value.unit == length_unit::percent) {
            return available * value.value / 100.0F + value.pixel_offset;
        }
        return fallback;
    };
    const auto transform_origin_x = node.layout.x + resolve_transform_origin(
        node.style.transform_origin_x,
        node.layout.width,
        node.layout.width * 0.5F);
    const auto transform_origin_y = node.layout.y + resolve_transform_origin(
        node.style.transform_origin_y,
        node.layout.height,
        node.layout.height * 0.5F);
    const auto painted_scale_x =
        animation != nullptr && animation->transform_animation_initialized
        ? animation->painted_transform_scale_x
        : node.style.transform_scale_x;
    const auto painted_scale_y =
        animation != nullptr && animation->transform_animation_initialized
        ? animation->painted_transform_scale_y
        : node.style.transform_scale_y;
    const auto painted_rotation =
        animation != nullptr && animation->transform_animation_initialized
        ? animation->painted_transform_rotate_degrees
        : node.style.transform_rotate_degrees;
    const auto has_scale_transform =
        std::abs(painted_scale_x - 1.0F) > 0.0001F
        || std::abs(painted_scale_y - 1.0F) > 0.0001F;
    if (has_scale_transform) {
        commands.push_back(webscene_scene_command{
            15U,
            0U,
            transform_origin_x,
            transform_origin_y,
            painted_scale_x,
            painted_scale_y,
            0U,
            node.id});
    }
    const auto has_rotate_transform = std::abs(painted_rotation) > 0.0001F;
    if (has_rotate_transform) {
        commands.push_back(webscene_scene_command{
            19U,
            0U,
            transform_origin_x,
            transform_origin_y,
            0,
            0,
            0U,
            node.id,
            0,
            0,
            0,
            0,
            painted_rotation});
    }
    const auto with_opacity = [](uint32_t rgba) { return rgba; };
    // Unlike display:none, visibility:hidden does not prune descendants. A child
    // may explicitly restore visibility:visible; component libraries use that contract
    // to switch between its collapsed and expanded responsive toolbar wrappers.
    const auto visibility_hidden = node.style.visibility_specified
        ? node.style.visibility_hidden
        : inherited_visibility_hidden;
    const auto paint_self = !visibility_hidden && paint_target == nullptr && !pseudo_only;
    const auto ascii_equal = [](char left, char right) {
        const auto lower = [](unsigned char character) {
            return character >= 'A' && character <= 'Z'
                ? static_cast<char>(character - 'A' + 'a')
                : static_cast<char>(character);
        };
        return lower(static_cast<unsigned char>(left))
            == lower(static_cast<unsigned char>(right));
    };
    struct effect_clip_geometry final {
        layout_rect bounds;
        float radius{};
        std::string path;
        bool even_odd{};
        bool relative_path{};
        bool object_bounding_box{};
    };
    const auto resolve_effect_clip = [&]() -> std::optional<effect_clip_geometry> {
        const auto& effects = node.style.textual().effect_values;
        const auto known = effects.find("clip-path");
        if (known == effects.end()) return std::nullopt;
        const std::string_view value = known->second;
        constexpr std::string_view inset_prefix = "inset(";
        constexpr std::string_view circle_prefix = "circle(";
        constexpr std::string_view ellipse_prefix = "ellipse(";
        constexpr std::string_view path_prefix = "path(";
        constexpr std::string_view polygon_prefix = "polygon(";
        constexpr std::string_view url_prefix = "url(";
        if (value.empty() || value.back() != ')') return std::nullopt;
        const auto has_prefix = [&](std::string_view prefix) {
            if (value.size() < prefix.size() + 1U) return false;
            for (size_t index = 0; index < prefix.size(); ++index) {
                if (!ascii_equal(value[index], prefix[index])) return false;
            }
            return true;
        };
        if (has_prefix(circle_prefix)) {
            auto arguments = value.substr(
                circle_prefix.size(), value.size() - circle_prefix.size() - 1U);
            while (!arguments.empty()
                && std::isspace(static_cast<unsigned char>(arguments.front()))) {
                arguments.remove_prefix(1U);
            }
            while (!arguments.empty()
                && std::isspace(static_cast<unsigned char>(arguments.back()))) {
                arguments.remove_suffix(1U);
            }
            const auto parsed = css::parse_ascii_inset_length(arguments);
            if (!parsed.has_value() || parsed->value < 0) return std::nullopt;
            const auto radius = parsed->percent
                ? std::hypot(node.layout.width, node.layout.height)
                    * parsed->value / 141.421356F
                : parsed->value;
            const auto center_x = node.layout.x + node.layout.width * 0.5F;
            const auto center_y = node.layout.y + node.layout.height * 0.5F;
            return effect_clip_geometry{
                layout_rect{
                    center_x - radius,
                    center_y - radius,
                    radius * 2.0F,
                    radius * 2.0F},
                radius};
        }
        if (has_prefix(ellipse_prefix)) {
            auto arguments = value.substr(
                ellipse_prefix.size(), value.size() - ellipse_prefix.size() - 1U);
            const auto trim = [](std::string_view token) {
                while (!token.empty()
                    && std::isspace(static_cast<unsigned char>(token.front())))
                    token.remove_prefix(1U);
                while (!token.empty()
                    && std::isspace(static_cast<unsigned char>(token.back())))
                    token.remove_suffix(1U);
                return token;
            };
            arguments = trim(arguments);
            auto at = std::string_view::npos;
            for (size_t index = 0U; index + 4U <= arguments.size(); ++index) {
                if (std::isspace(static_cast<unsigned char>(arguments[index]))
                    && ascii_equal(arguments[index + 1U], 'a')
                    && ascii_equal(arguments[index + 2U], 't')
                    && std::isspace(static_cast<unsigned char>(arguments[index + 3U]))) {
                    at = index;
                    break;
                }
            }
            const auto radii = trim(arguments.substr(0U, at));
            const auto position = at == std::string_view::npos
                ? std::string_view{} : trim(arguments.substr(at + 4U));
            const auto split_pair = [&](std::string_view pair)
                -> std::optional<std::pair<css::ascii_inset_length, css::ascii_inset_length>> {
                const auto separator = pair.find_first_of(" \t\r\n\f");
                if (separator == std::string_view::npos) return std::nullopt;
                const auto first = css::parse_ascii_inset_length(pair.substr(0U, separator));
                const auto second_token = trim(pair.substr(separator + 1U));
                if (!first.has_value() || second_token.empty()
                    || second_token.find_first_of(" \t\r\n\f") != std::string_view::npos)
                    return std::nullopt;
                const auto second = css::parse_ascii_inset_length(second_token);
                if (!second.has_value()) return std::nullopt;
                return std::pair{*first, *second};
            };
            const auto parsed_radii = split_pair(radii);
            if (!parsed_radii.has_value()
                || parsed_radii->first.value < 0.0F
                || parsed_radii->second.value < 0.0F) return std::nullopt;
            const auto parsed_position = position.empty()
                ? std::optional{std::pair{
                    css::ascii_inset_length{50.0F, true},
                    css::ascii_inset_length{50.0F, true}}}
                : split_pair(position);
            if (!parsed_position.has_value()) return std::nullopt;
            const auto resolve = [](css::ascii_inset_length length, float available) {
                return length.percent ? available * length.value / 100.0F : length.value;
            };
            const auto radius_x = resolve(parsed_radii->first, node.layout.width);
            const auto radius_y = resolve(parsed_radii->second, node.layout.height);
            const auto center_x = node.layout.x
                + resolve(parsed_position->first, node.layout.width);
            const auto center_y = node.layout.y
                + resolve(parsed_position->second, node.layout.height);
            std::ostringstream path;
            path.precision(std::numeric_limits<float>::max_digits10);
            path << "M " << center_x - radius_x << ' ' << center_y
                << " A " << radius_x << ' ' << radius_y << " 0 1 0 "
                << center_x + radius_x << ' ' << center_y
                << " A " << radius_x << ' ' << radius_y << " 0 1 0 "
                << center_x - radius_x << ' ' << center_y << " Z";
            return effect_clip_geometry{
                layout_rect{
                    center_x - radius_x,
                    center_y - radius_y,
                    radius_x * 2.0F,
                    radius_y * 2.0F},
                0.0F,
                path.str()};
        }
        const auto invalid_resource_clip = [&]() {
            return effect_clip_geometry{
                layout_rect{node.layout.x, node.layout.y, node.layout.width, node.layout.height},
                0.0F,
                "M0 0",
                false,
                true};
        };
        if (has_prefix(path_prefix)) {
            auto arguments = value.substr(
                path_prefix.size(), value.size() - path_prefix.size() - 1U);
            const auto trim = [](std::string_view token) {
                while (!token.empty()
                    && std::isspace(static_cast<unsigned char>(token.front()))) {
                    token.remove_prefix(1U);
                }
                while (!token.empty()
                    && std::isspace(static_cast<unsigned char>(token.back()))) {
                    token.remove_suffix(1U);
                }
                return token;
            };
            arguments = trim(arguments);
            auto even_odd = false;
            constexpr auto even_odd_prefix = std::string_view{"evenodd"};
            constexpr auto nonzero_prefix = std::string_view{"nonzero"};
            const auto consume_fill_rule = [&](std::string_view prefix, bool value)
                -> bool {
                if (arguments.size() < prefix.size()) return false;
                for (size_t index = 0U; index < prefix.size(); ++index) {
                    if (!ascii_equal(arguments[index], prefix[index])) return false;
                }
                auto remaining = trim(arguments.substr(prefix.size()));
                if (remaining.empty() || remaining.front() != ',') return false;
                arguments = trim(remaining.substr(1U));
                even_odd = value;
                return true;
            };
            if (!consume_fill_rule(even_odd_prefix, true)) {
                consume_fill_rule(nonzero_prefix, false);
            }
            if (arguments.size() < 2U
                || (arguments.front() != '\'' && arguments.front() != '"')
                || arguments.back() != arguments.front()) {
                return invalid_resource_clip();
            }
            const auto quote = arguments.front();
            auto encoded = arguments.substr(1U, arguments.size() - 2U);
            std::string decoded;
            decoded.reserve(encoded.size());
            for (size_t index = 0U; index < encoded.size(); ++index) {
                const auto character = static_cast<unsigned char>(encoded[index]);
                if (character == static_cast<unsigned char>(quote)
                    || character == '\n' || character == '\r' || character == '\f') {
                    return invalid_resource_clip();
                }
                if (character != '\\') {
                    decoded.push_back(static_cast<char>(character));
                    continue;
                }
                if (++index == encoded.size()) return invalid_resource_clip();
                if (encoded[index] == '\n') continue;
                if (encoded[index] == '\r') {
                    if (index + 1U < encoded.size() && encoded[index + 1U] == '\n') ++index;
                    continue;
                }
                auto hexadecimal = uint32_t{};
                auto digits = size_t{};
                for (; index < encoded.size() && digits < 6U; ++index, ++digits) {
                    const auto byte = static_cast<unsigned char>(encoded[index]);
                    uint32_t nibble = 0U;
                    if (byte >= '0' && byte <= '9') nibble = byte - '0';
                    else if (byte >= 'a' && byte <= 'f') nibble = byte - 'a' + 10U;
                    else if (byte >= 'A' && byte <= 'F') nibble = byte - 'A' + 10U;
                    else break;
                    hexadecimal = hexadecimal * 16U + nibble;
                }
                if (digits != 0U) {
                    if (hexadecimal == 0U || hexadecimal > 0x7FU) {
                        return invalid_resource_clip();
                    }
                    decoded.push_back(static_cast<char>(hexadecimal));
                    if (index < encoded.size()
                        && std::isspace(static_cast<unsigned char>(encoded[index]))) {
                        if (encoded[index] == '\r' && index + 1U < encoded.size()
                            && encoded[index + 1U] == '\n') ++index;
                    } else if (index != 0U) {
                        --index;
                    }
                    continue;
                }
                decoded.push_back(encoded[index]);
            }
            if (decoded.empty() || decoded.size() > 16384U) {
                return invalid_resource_clip();
            }
            return effect_clip_geometry{
                layout_rect{
                    node.layout.x,
                    node.layout.y,
                    node.layout.width,
                    node.layout.height},
                0.0F,
                std::move(decoded),
                even_odd,
                true};
        }
        if (has_prefix(url_prefix)) {
            const auto trim = [](std::string_view token) {
                while (!token.empty()
                    && std::isspace(static_cast<unsigned char>(token.front()))) {
                    token.remove_prefix(1U);
                }
                while (!token.empty()
                    && std::isspace(static_cast<unsigned char>(token.back()))) {
                    token.remove_suffix(1U);
                }
                return token;
            };
            auto reference = trim(value.substr(
                url_prefix.size(), value.size() - url_prefix.size() - 1U));
            if (reference.size() >= 2U
                && (reference.front() == '\'' || reference.front() == '"')
                && reference.back() == reference.front()) {
                reference = trim(reference.substr(1U, reference.size() - 2U));
            }
            if (reference.size() < 2U || reference.size() > 257U
                || reference.front() != '#') {
                return invalid_resource_clip();
            }
            reference.remove_prefix(1U);
            if (reference.empty()
                || std::any_of(reference.begin(), reference.end(), [](unsigned char byte) {
                    return std::isspace(byte) || std::iscntrl(byte);
                })) {
                return invalid_resource_clip();
            }
            const auto definition = std::find_if(
                nodes_.begin(), nodes_.end(), [&](const auto& candidate) {
                    if (candidate->id_attribute != reference) return false;
                    constexpr std::string_view expected = "clippath";
                    if (candidate->tag.size() != expected.size()) return false;
                    for (size_t index = 0U; index < expected.size(); ++index) {
                        if (!ascii_equal(candidate->tag[index], expected[index])) return false;
                    }
                    return true;
                });
            if (definition == nodes_.end()
                || containing_shadow_root(node)
                    != containing_shadow_root(**definition)) {
                return invalid_resource_clip();
            }
            const auto& clip = **definition;
            const auto root = [](const dom_node& candidate) {
                auto* current = &candidate;
                while (current->parent != nullptr) current = current->parent;
                return current;
            };
            if (root(node) != root(clip)) return invalid_resource_clip();
            if (clip.attributes.contains("transform") || clip.children.size() > 64U) {
                return invalid_resource_clip();
            }
            auto object_bounding_box = false;
            auto units = clip.attributes.find("clipPathUnits");
            if (units == clip.attributes.end()) {
                units = clip.attributes.find("clippathunits");
            }
            if (units != clip.attributes.end()) {
                auto normalized = units->second;
                std::transform(
                    normalized.begin(), normalized.end(), normalized.begin(),
                    [](unsigned char byte) { return static_cast<char>(std::tolower(byte)); });
                if (normalized == "objectboundingbox") object_bounding_box = true;
                else if (normalized != "userspaceonuse") return invalid_resource_clip();
            }
            const auto number = [&](const dom_node& shape, std::string_view name,
                                    float fallback) -> std::optional<float> {
                const auto found = shape.attributes.find(std::string{name});
                if (found == shape.attributes.end()) return fallback;
                if (found->second.size() > 64U) return std::nullopt;
                const auto token = trim(found->second);
                auto copy = std::string{token};
                char* end = nullptr;
                const auto parsed = std::strtof(copy.c_str(), &end);
                while (end != nullptr && *end != '\0'
                    && std::isspace(static_cast<unsigned char>(*end))) ++end;
                if (end == copy.c_str() || end == nullptr || *end != '\0'
                    || !std::isfinite(parsed)) return std::nullopt;
                return parsed;
            };
            std::ostringstream path;
            path.precision(std::numeric_limits<float>::max_digits10);
            auto fill_rule = std::optional<bool>{};
            size_t geometry_count = 0U;
            const auto apply_rule = [&](const dom_node& shape) {
                const auto authored = shape.attributes.find("clip-rule");
                if (authored == shape.attributes.end()) return true;
                auto normalized = authored->second;
                std::transform(
                    normalized.begin(), normalized.end(), normalized.begin(),
                    [](unsigned char byte) { return static_cast<char>(std::tolower(byte)); });
                const auto even_odd = normalized == "evenodd";
                if (!even_odd && normalized != "nonzero") return false;
                if (fill_rule.has_value() && *fill_rule != even_odd) return false;
                fill_rule = even_odd;
                return true;
            };
            if (!apply_rule(clip)) return invalid_resource_clip();
            for (const auto* child : clip.children) {
                if (child == nullptr || child->tag == "#text") continue;
                if (child->tag == "title" || child->tag == "desc") continue;
                if (child->attributes.contains("transform") || !apply_rule(*child)) {
                    return invalid_resource_clip();
                }
                if (++geometry_count > 64U) return invalid_resource_clip();
                if (child->tag == "path") {
                    const auto data = child->attributes.find("d");
                    if (data == child->attributes.end() || data->second.empty()
                        || data->second.size() > 16384U) return invalid_resource_clip();
                    path << data->second << ' ';
                } else if (child->tag == "rect") {
                    if (child->attributes.contains("rx") || child->attributes.contains("ry")) {
                        return invalid_resource_clip();
                    }
                    const auto x = number(*child, "x", 0.0F);
                    const auto y = number(*child, "y", 0.0F);
                    const auto width = number(*child, "width", -1.0F);
                    const auto height = number(*child, "height", -1.0F);
                    if (!x || !y || !width || !height || *width < 0.0F || *height < 0.0F) {
                        return invalid_resource_clip();
                    }
                    path << 'M' << *x << ' ' << *y << 'H' << *x + *width
                         << 'V' << *y + *height << 'H' << *x << "Z ";
                } else if (child->tag == "circle" || child->tag == "ellipse") {
                    const auto cx = number(*child, "cx", 0.0F);
                    const auto cy = number(*child, "cy", 0.0F);
                    const auto rx = number(
                        *child, child->tag == "circle" ? "r" : "rx", -1.0F);
                    const auto ry = child->tag == "circle"
                        ? rx : number(*child, "ry", -1.0F);
                    if (!cx || !cy || !rx || !ry || *rx < 0.0F || *ry < 0.0F) {
                        return invalid_resource_clip();
                    }
                    path << 'M' << *cx - *rx << ' ' << *cy
                         << 'A' << *rx << ' ' << *ry << " 0 1 0 "
                         << *cx + *rx << ' ' << *cy
                         << 'A' << *rx << ' ' << *ry << " 0 1 0 "
                         << *cx - *rx << ' ' << *cy << "Z ";
                } else if (child->tag == "polygon") {
                    const auto authored = child->attributes.find("points");
                    if (authored == child->attributes.end()
                        || authored->second.empty() || authored->second.size() > 4096U) {
                        return invalid_resource_clip();
                    }
                    std::vector<float> points;
                    const auto* cursor = authored->second.c_str();
                    const auto* limit = cursor + authored->second.size();
                    while (cursor < limit) {
                        while (cursor < limit
                            && (std::isspace(static_cast<unsigned char>(*cursor))
                                || *cursor == ',')) ++cursor;
                        if (cursor == limit) break;
                        char* end = nullptr;
                        const auto parsed = std::strtof(cursor, &end);
                        if (end == cursor || !std::isfinite(parsed) || points.size() >= 256U) {
                            return invalid_resource_clip();
                        }
                        points.push_back(parsed);
                        cursor = end;
                    }
                    if (points.size() < 4U || (points.size() & 1U) != 0U) {
                        return invalid_resource_clip();
                    }
                    path << 'M' << points[0] << ' ' << points[1];
                    for (size_t index = 2U; index < points.size(); index += 2U) {
                        path << 'L' << points[index] << ' ' << points[index + 1U];
                    }
                    path << "Z ";
                } else {
                    return invalid_resource_clip();
                }
                const auto path_size = path.tellp();
                if (path_size == std::streampos{-1}
                    || static_cast<std::streamoff>(path_size) > 16384) {
                    return invalid_resource_clip();
                }
            }
            auto flattened = path.str();
            if (geometry_count == 0U || flattened.empty() || flattened.size() > 16384U) {
                return invalid_resource_clip();
            }
            return effect_clip_geometry{
                layout_rect{node.layout.x, node.layout.y, node.layout.width, node.layout.height},
                0.0F,
                std::move(flattened),
                fill_rule.value_or(false),
                true,
                object_bounding_box};
        }
        if (has_prefix(polygon_prefix)) {
            auto arguments = value.substr(
                polygon_prefix.size(), value.size() - polygon_prefix.size() - 1U);
            const auto trim = [](std::string_view token) {
                while (!token.empty()
                    && std::isspace(static_cast<unsigned char>(token.front()))) {
                    token.remove_prefix(1U);
                }
                while (!token.empty()
                    && std::isspace(static_cast<unsigned char>(token.back()))) {
                    token.remove_suffix(1U);
                }
                return token;
            };
            auto even_odd = false;
            const auto first_comma = arguments.find(',');
            if (first_comma != std::string_view::npos) {
                const auto possible_rule = trim(arguments.substr(0U, first_comma));
                const auto is_rule = [&](std::string_view expected) {
                    if (possible_rule.size() != expected.size()) return false;
                    for (size_t index = 0U; index < expected.size(); ++index) {
                        if (!ascii_equal(possible_rule[index], expected[index])) return false;
                    }
                    return true;
                };
                if (is_rule("evenodd") || is_rule("nonzero")) {
                    even_odd = is_rule("evenodd");
                    arguments = trim(arguments.substr(first_comma + 1U));
                }
            }
            std::ostringstream path;
            path.precision(std::numeric_limits<float>::max_digits10);
            size_t point_count = 0U;
            size_t cursor = 0U;
            while (cursor <= arguments.size()) {
                const auto comma = arguments.find(',', cursor);
                const auto point = trim(arguments.substr(
                    cursor,
                    comma == std::string_view::npos
                        ? arguments.size() - cursor : comma - cursor));
                const auto separator = point.find_first_of(" \t\r\n\f");
                if (separator == std::string_view::npos || point_count == 64U) {
                    return std::nullopt;
                }
                const auto x_token = point.substr(0U, separator);
                const auto y_token = trim(point.substr(separator + 1U));
                if (y_token.empty()
                    || y_token.find_first_of(" \t\r\n\f") != std::string_view::npos) {
                    return std::nullopt;
                }
                const auto x = css::parse_ascii_inset_length(x_token);
                const auto y = css::parse_ascii_inset_length(y_token);
                if (!x.has_value() || !y.has_value()) return std::nullopt;
                const auto resolved_x = node.layout.x
                    + (x->percent ? node.layout.width * x->value / 100.0F : x->value);
                const auto resolved_y = node.layout.y
                    + (y->percent ? node.layout.height * y->value / 100.0F : y->value);
                path << (point_count == 0U ? "M " : " L ")
                    << resolved_x << ' ' << resolved_y;
                ++point_count;
                if (comma == std::string_view::npos) break;
                cursor = comma + 1U;
            }
            if (point_count < 3U) return std::nullopt;
            path << " Z";
            return effect_clip_geometry{
                layout_rect{
                    node.layout.x,
                    node.layout.y,
                    node.layout.width,
                    node.layout.height},
                0.0F,
                path.str(),
                even_odd};
        }
        if (!has_prefix(inset_prefix)) return std::nullopt;
        const std::string_view arguments(value.data() + 6U, value.size() - 7U);
        std::array<css::ascii_inset_length, 4> parsed{};
        size_t count = 0U;
        size_t cursor = 0U;
        while (cursor < arguments.size()) {
            while (cursor < arguments.size()
                && std::isspace(static_cast<unsigned char>(arguments[cursor]))) ++cursor;
            if (cursor == arguments.size()) break;
            const auto end = arguments.find_first_of(" \t\r\n\f", cursor);
            const auto token = arguments.substr(
                cursor,
                end == std::string_view::npos ? arguments.size() - cursor : end - cursor);
            if (count == parsed.size()) return std::nullopt;
            const auto length = css::parse_ascii_inset_length(token);
            if (!length.has_value()) return std::nullopt;
            parsed[count++] = *length;
            if (end == std::string_view::npos) break;
            cursor = end + 1U;
        }
        if (count == 0U) return std::nullopt;
        const auto top = parsed[0];
        const auto right = count > 1U ? parsed[1] : parsed[0];
        const auto bottom = count > 2U ? parsed[2] : parsed[0];
        const auto left = count > 3U ? parsed[3] : count > 1U ? parsed[1] : parsed[0];
        const auto pixels = [](css::ascii_inset_length length, float available) {
            return length.percent ? available * length.value / 100.0F : length.value;
        };
        const auto top_pixels = pixels(top, node.layout.height);
        const auto right_pixels = pixels(right, node.layout.width);
        const auto bottom_pixels = pixels(bottom, node.layout.height);
        const auto left_pixels = pixels(left, node.layout.width);
        return effect_clip_geometry{
            layout_rect{
                node.layout.x + left_pixels,
                node.layout.y + top_pixels,
                std::max(0.0F, node.layout.width - left_pixels - right_pixels),
                std::max(0.0F, node.layout.height - top_pixels - bottom_pixels)}};
    };
    const auto effect_clip = resolve_effect_clip();
    if (effect_clip.has_value()) {
        constexpr uint32_t polygon_clip_resource = 1U << 31U;
        constexpr uint32_t clip_even_odd = 1U << 30U;
        constexpr uint32_t clip_relative_path = 1U << 29U;
        constexpr uint32_t clip_object_bounding_box = 1U << 28U;
        const auto flags = effect_clip->path.empty()
            ? 0U
            : polygon_clip_resource
                | (effect_clip->even_odd ? clip_even_odd : 0U)
                | (effect_clip->relative_path ? clip_relative_path : 0U)
                | (effect_clip->object_bounding_box ? clip_object_bounding_box : 0U)
                | append_scene_string(effect_clip->path, strings, string_bytes);
        commands.push_back(webscene_scene_command{
            12U,
            flags,
            effect_clip->bounds.x,
            effect_clip->bounds.y,
            effect_clip->bounds.width,
            effect_clip->bounds.height,
            0U,
            node.id,
            effect_clip->radius,
            effect_clip->radius,
            effect_clip->radius,
            effect_clip->radius});
    }
    const auto paint_box_in_foreground = [&] {
        auto fixed_layer = false;
        auto outermost_z_index = 0;
        for (auto* current = &node; current != nullptr; current = composed_parent(*current)) {
            // A fixed popup participates in the elevated overlay layer even
            // when a backdrop child uses z-index:-1 relative to that popup.
            if (current->style.position == position_mode::fixed) fixed_layer = true;
            // A negative descendant stays inside its ancestor's stacking context.
            // Its own level orders it behind siblings in that context; it must not
            // demote an otherwise elevated legend or dialog behind retained canvases.
            if (current->style.z_index != 0) {
                outermost_z_index = current->style.z_index;
            }
            // Fixed-position dialogs and menus form an elevated paint layer in
            // component UI even when its authored z-index is auto. Its DOM
            // backgrounds must be emitted after retained chart canvases, just
            // like their text and SVG foreground commands.
        }
        return backdrop_effect.present || is_in_modal_layer(node)
            || node.paints_after_retained_canvas || fixed_layer
            || outermost_z_index > 0;
    }();
    struct resolved_radii final {
        float top_left;
        float top_right;
        float bottom_right;
        float bottom_left;
        float top_left_y;
        float top_right_y;
        float bottom_right_y;
        float bottom_left_y;

        bool any() const noexcept
        {
            return top_left > 0 || top_right > 0 || bottom_right > 0 || bottom_left > 0
                || top_left_y > 0 || top_right_y > 0
                || bottom_right_y > 0 || bottom_left_y > 0;
        }

        bool elliptical() const noexcept
        {
            return std::abs(top_left - top_left_y) > 0.01F
                || std::abs(top_right - top_right_y) > 0.01F
                || std::abs(bottom_right - bottom_right_y) > 0.01F
                || std::abs(bottom_left - bottom_left_y) > 0.01F;
        }
    };
    const auto resolve_radii = [&](css_length top_left, css_length top_right,
                                   css_length bottom_right, css_length bottom_left,
                                   css_length top_left_y, css_length top_right_y,
                                   css_length bottom_right_y, css_length bottom_left_y,
                                   float width, float height) {
        resolved_radii result{
            std::max(0.0F, resolve_length(node, top_left, width, 0)),
            std::max(0.0F, resolve_length(node, top_right, width, 0)),
            std::max(0.0F, resolve_length(node, bottom_right, width, 0)),
            std::max(0.0F, resolve_length(node, bottom_left, width, 0)),
            std::max(0.0F, resolve_length(node, top_left_y, height, 0)),
            std::max(0.0F, resolve_length(node, top_right_y, height, 0)),
            std::max(0.0F, resolve_length(node, bottom_right_y, height, 0)),
            std::max(0.0F, resolve_length(node, bottom_left_y, height, 0))};
        auto scale = 1.0F;
        const auto constrain = [&](float available, float sum) {
            if (sum > available && sum > 0) scale = std::min(scale, available / sum);
        };
        constrain(width, result.top_left + result.top_right);
        constrain(width, result.bottom_left + result.bottom_right);
        constrain(height, result.top_left_y + result.bottom_left_y);
        constrain(height, result.top_right_y + result.bottom_right_y);
        if (scale < 1.0F) {
            result.top_left *= scale;
            result.top_right *= scale;
            result.bottom_right *= scale;
            result.bottom_left *= scale;
            result.top_left_y *= scale;
            result.top_right_y *= scale;
            result.bottom_right_y *= scale;
            result.bottom_left_y *= scale;
        }
        return result;
    };
    const auto append_ellipse_metadata = [&](const resolved_radii& radii,
                                             float x, float y,
                                             float width, float height) {
        if (!radii.elliptical()) return;
        // Kind 32 is an ABI-preserving companion for the immediately following
        // rounded command. Geometry is duplicated for damage tracking; the four
        // existing radius fields carry vertical radii. Older presenters ignore
        // this command and retain their established circular fallback.
        commands.push_back(webscene_scene_command{
            32U,
            0U,
            x,
            y,
            width,
            height,
            0U,
            node.id,
            radii.top_left_y,
            radii.top_right_y,
            radii.bottom_right_y,
            radii.bottom_left_y,
            0});
    };
    const auto pack_round_rect = [](float packed_radius, float stroke_width) {
        const auto radius_bits = static_cast<uint32_t>(std::clamp(
            std::lround(packed_radius * 100.0F), 0L, 65535L));
        const auto width_bits = static_cast<uint32_t>(std::clamp(
            std::lround(stroke_width * 100.0F), 0L, 65535L));
        return (radius_bits << 16U) | width_bits;
    };
    const auto append_pseudo = [&](const node_style::pseudo_element& pseudo) {
        if (node.style.content_visibility_hidden) return;
        if ((!paint_self && !(pseudo_only && &pseudo == paint_pseudo_target && !visibility_hidden))
            || !pseudo.generated || pseudo.display_none || pseudo.visibility_hidden) return;
        const auto after_retained_canvas = &pseudo == &node.style.after_pseudo()
            && node.contains_retained_canvas
            && !(pseudo.z_index < 0 && pseudo.position != position_mode::normal);
        const auto pseudo_paints_in_foreground = paint_box_in_foreground
            || after_retained_canvas
            || (pseudo.z_index > 0 && pseudo.position != position_mode::normal);
        const dom_node::animation_runtime_data::pseudo_animation_runtime*
            pseudo_animation = nullptr;
        if (animation != nullptr) {
            if (&pseudo == &node.style.before_pseudo()) {
                pseudo_animation = &animation->before_pseudo;
            } else if (&pseudo == &node.style.after_pseudo()) {
                pseudo_animation = &animation->after_pseudo;
            }
        }
        const auto pseudo_opacity = std::clamp(
            pseudo_animation != nullptr && pseudo_animation->opacity_override
                ? pseudo_animation->painted_opacity : pseudo.opacity,
            0.0F,
            1.0F);
        if (pseudo_opacity <= 0.001F) return;
        const auto with_pseudo_opacity = [&](uint32_t rgba) {
            const auto alpha = static_cast<uint32_t>(std::clamp(
                std::lround(static_cast<float>(rgba & 0xFFU) * pseudo_opacity),
                0L,
                255L));
            return with_opacity((rgba & 0xFFFFFF00U) | alpha);
        };
        const auto inherited_font_size = resolved_font_size(node);
        const auto font_size = pseudo.font_size >= 0 ? pseudo.font_size : inherited_font_size;
        const auto line_height = pseudo.line_height <= -3.0F
            ? (-3.0F - pseudo.line_height) * font_size
            : pseudo.line_height == -2.0F
                ? font_size * 1.125F
                : pseudo.line_height >= 0
                    ? pseudo.line_height
                    : resolved_line_height(node, font_size);
        auto x = pseudo.layout.x;
        auto y = pseudo.layout.y;
        const auto width = pseudo.layout.width;
        const auto height = pseudo.layout.height;
        if (pseudo_animation != nullptr && pseudo_animation->transform_override) {
            x += resolve_length(
                node, pseudo_animation->painted_translation_x, width, 0.0F);
            y += resolve_length(
                node, pseudo_animation->painted_translation_y, height, 0.0F);
        }
        const auto animated_filters = pseudo_animation != nullptr
                && pseudo_animation->filter_override
            ? std::span<const retained_filter_function>{
                pseudo_animation->painted_filter_functions}
            : std::span<const retained_filter_function>{};
        for (size_t filter_index = animated_filters.size(); filter_index > 0U;
             --filter_index) {
            const auto& filter = animated_filters[filter_index - 1U];
            commands.push_back(webscene_scene_command{
                30U,
                filter.flags,
                x,
                y,
                width,
                height,
                255U,
                node.id,
                0,
                0,
                0,
                0,
                filter.amount});
        }
        const auto animated_scale = pseudo_animation != nullptr
            && pseudo_animation->transform_override
            && (std::abs(pseudo_animation->painted_scale_x - 1.0F) > 0.0001F
                || std::abs(pseudo_animation->painted_scale_y - 1.0F) > 0.0001F);
        const auto animated_rotation = pseudo_animation != nullptr
            && pseudo_animation->transform_override
            && std::abs(pseudo_animation->painted_rotation_degrees) > 0.0001F;
        const auto transform_origin_x = x + width * 0.5F;
        const auto transform_origin_y = y + height * 0.5F;
        if (animated_scale) {
            commands.push_back(webscene_scene_command{
                15U,
                0U,
                transform_origin_x,
                transform_origin_y,
                pseudo_animation->painted_scale_x,
                pseudo_animation->painted_scale_y,
                0U,
                node.id});
        }
        if (animated_rotation) {
            commands.push_back(webscene_scene_command{
                19U,
                0U,
                transform_origin_x,
                transform_origin_y,
                0,
                0,
                0U,
                node.id,
                0,
                0,
                0,
                0,
                pseudo_animation->painted_rotation_degrees});
        }
        const auto pseudo_background = pseudo.background_current_color
            ? (pseudo.foreground_rgba & 0xFFU) != 0U
                ? pseudo.foreground_rgba
                : resolved_foreground(node)
            : pseudo.background_rgba;
        const auto pseudo_radii = resolve_radii(
            pseudo.border_top_left_radius,
            pseudo.border_top_right_radius,
            pseudo.border_bottom_right_radius,
            pseudo.border_bottom_left_radius,
            pseudo.elliptical_border_radius
                ? pseudo.border_top_left_radius_y : pseudo.border_top_left_radius,
            pseudo.elliptical_border_radius
                ? pseudo.border_top_right_radius_y : pseudo.border_top_right_radius,
            pseudo.elliptical_border_radius
                ? pseudo.border_bottom_right_radius_y : pseudo.border_bottom_right_radius,
            pseudo.elliptical_border_radius
                ? pseudo.border_bottom_left_radius_y : pseudo.border_bottom_left_radius,
            width,
            height);
        if ((pseudo_background & 0xFFU) != 0U && width > 0 && height > 0) {
            append_ellipse_metadata(pseudo_radii, x, y, width, height);
            commands.push_back(webscene_scene_command{
                pseudo_radii.any()
                    ? pseudo_paints_in_foreground ? 10U : 7U
                    : pseudo_paints_in_foreground ? 9U : 1U,
                0U,
                x,
                y,
                width,
                height,
                with_pseudo_opacity(pseudo_background),
                node.id,
                pseudo_radii.top_left,
                pseudo_radii.top_right,
                pseudo_radii.bottom_right,
                pseudo_radii.bottom_left,
                0});
        }
        auto pseudo_background_image_lower = pseudo.background_image.image_value;
        std::transform(
            pseudo_background_image_lower.begin(),
            pseudo_background_image_lower.end(),
            pseudo_background_image_lower.begin(),
            [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
        if ((pseudo_background_image_lower.starts_with("linear-gradient(") || pseudo_background_image_lower.starts_with("radial-gradient("))
            && width > 0 && height > 0) {
            auto gradient_resource = std::string{"webscene-bg-v2\t"};
            gradient_resource += pseudo.background_image.image_value;
            gradient_resource += '\t';
            gradient_resource += pseudo.background_image.repeat;
            gradient_resource += '\t';
            gradient_resource += pseudo.background_image.position_value;
            gradient_resource += '\t';
            gradient_resource += pseudo.background_image.size_value;
            append_ellipse_metadata(pseudo_radii, x, y, width, height);
            commands.push_back(webscene_scene_command{
                pseudo_paints_in_foreground ? 22U : 21U,
                append_scene_string(gradient_resource, strings, string_bytes),
                x,
                y,
                width,
                height,
                0U,
                node.id,
                pseudo_radii.top_left,
                pseudo_radii.top_right,
                pseudo_radii.bottom_right,
                pseudo_radii.bottom_left,
                0});
        }
        if (width > 0 && height > 0) {
            const auto pseudo_foreground =
                (pseudo.foreground_rgba & 0xFFU) != 0U
                ? pseudo.foreground_rgba
                : resolved_foreground(node);
            const auto border_top_rgba = pseudo.border_top_current_color
                ? pseudo_foreground : pseudo.border_top_rgba;
            const auto border_right_rgba = pseudo.border_right_current_color
                ? pseudo_foreground : pseudo.border_right_rgba;
            const auto border_bottom_rgba = pseudo.border_bottom_current_color
                ? pseudo_foreground : pseudo.border_bottom_rgba;
            const auto border_left_rgba = pseudo.border_left_current_color
                ? pseudo_foreground : pseudo.border_left_rgba;
            const auto top = std::clamp(
                resolve_length(node, pseudo.border_top_width, height, 0),
                0.0F,
                height);
            const auto right = std::clamp(
                resolve_length(node, pseudo.border_right_width, width, 0),
                0.0F,
                width);
            const auto bottom = std::clamp(
                resolve_length(node, pseudo.border_bottom_width, height, 0),
                0.0F,
                height);
            const auto left = std::clamp(
                resolve_length(node, pseudo.border_left_width, width, 0),
                0.0F,
                width);
            const auto uniform_width = top > 0
                && std::abs(top - right) < 0.01F
                && std::abs(top - bottom) < 0.01F
                && std::abs(top - left) < 0.01F;
            const auto uniform_color = border_top_rgba == border_right_rgba
                && border_top_rgba == border_bottom_rgba
                && border_top_rgba == border_left_rgba;
            const auto border_radii = resolve_radii(
                pseudo.border_top_left_radius,
                pseudo.border_top_right_radius,
                pseudo.border_bottom_right_radius,
                pseudo.border_bottom_left_radius,
                pseudo.elliptical_border_radius
                    ? pseudo.border_top_left_radius_y : pseudo.border_top_left_radius,
                pseudo.elliptical_border_radius
                    ? pseudo.border_top_right_radius_y : pseudo.border_top_right_radius,
                pseudo.elliptical_border_radius
                    ? pseudo.border_bottom_right_radius_y : pseudo.border_bottom_right_radius,
                pseudo.elliptical_border_radius
                    ? pseudo.border_bottom_left_radius_y : pseudo.border_bottom_left_radius,
                width,
                height);
            if (border_radii.any() && uniform_width && uniform_color
                && (border_top_rgba & 0xFFU) != 0U) {
                const auto inset = top * 0.5F;
                const auto stroke_x = x + inset;
                const auto stroke_y = y + inset;
                const auto stroke_width = std::max(0.0F, width - top);
                const auto stroke_height = std::max(0.0F, height - top);
                const auto stroke_radii = resolved_radii{
                    std::max(0.0F, border_radii.top_left - inset),
                    std::max(0.0F, border_radii.top_right - inset),
                    std::max(0.0F, border_radii.bottom_right - inset),
                    std::max(0.0F, border_radii.bottom_left - inset),
                    std::max(0.0F, border_radii.top_left_y - inset),
                    std::max(0.0F, border_radii.top_right_y - inset),
                    std::max(0.0F, border_radii.bottom_right_y - inset),
                    std::max(0.0F, border_radii.bottom_left_y - inset)};
                append_ellipse_metadata(
                    stroke_radii,
                    stroke_x,
                    stroke_y,
                    stroke_width,
                    stroke_height);
                commands.push_back(webscene_scene_command{
                    pseudo_paints_in_foreground ? 11U : 8U,
                    pack_round_rect(0, top),
                    stroke_x,
                    stroke_y,
                    stroke_width,
                    stroke_height,
                    with_pseudo_opacity(border_top_rgba),
                    node.id,
                    stroke_radii.top_left,
                    stroke_radii.top_right,
                    stroke_radii.bottom_right,
                    stroke_radii.bottom_left,
                    top});
            } else {
                // CSS joins adjacent border sides along the diagonal between
                // the outer and inner corners.  This is observable when a
                // zero-sized generated box uses two wide borders to form a
                // triangle (TradingView's split-colour swatch), but it also
                // matters for ordinary non-uniform side colours.  Preserve the
                // full outer bounds in the command and let the presenter form
                // the appropriate side polygon from the four border widths.
                const auto append_pseudo_border_side = [&] (
                    uint32_t side,
                    uint32_t rgba) {
                    if ((rgba & 0xFFU) == 0U) return;
                    commands.push_back(webscene_scene_command{
                        pseudo_paints_in_foreground ? 34U : 33U,
                        side,
                        x,
                        y,
                        width,
                        height,
                        with_pseudo_opacity(rgba),
                        node.id,
                        top,
                        right,
                        bottom,
                        left,
                        0});
                };
                append_pseudo_border_side(1U, border_top_rgba);
                append_pseudo_border_side(2U, border_right_rgba);
                append_pseudo_border_side(4U, border_bottom_rgba);
                append_pseudo_border_side(8U, border_left_rgba);
            }
        }
        const auto outline = std::max(
            0.0F,
            resolve_length(node, pseudo.outline_width, width, 0));
        if ((pseudo.outline_rgba & 0xFFU) != 0U
            && outline > 0 && width > 0 && height > 0) {
            const auto radii = resolve_radii(
                pseudo.border_top_left_radius,
                pseudo.border_top_right_radius,
                pseudo.border_bottom_right_radius,
                pseudo.border_bottom_left_radius,
                pseudo.elliptical_border_radius
                    ? pseudo.border_top_left_radius_y : pseudo.border_top_left_radius,
                pseudo.elliptical_border_radius
                    ? pseudo.border_top_right_radius_y : pseudo.border_top_right_radius,
                pseudo.elliptical_border_radius
                    ? pseudo.border_bottom_right_radius_y : pseudo.border_bottom_right_radius,
                pseudo.elliptical_border_radius
                    ? pseudo.border_bottom_left_radius_y : pseudo.border_bottom_left_radius,
                width,
                height);
            if (radii.any()) {
                const auto half_outline = outline * 0.5F;
                const auto outline_x = x - half_outline;
                const auto outline_y = y - half_outline;
                const auto outline_width = width + outline;
                const auto outline_height = height + outline;
                const auto outline_radii = resolved_radii{
                    radii.top_left + half_outline,
                    radii.top_right + half_outline,
                    radii.bottom_right + half_outline,
                    radii.bottom_left + half_outline,
                    radii.top_left_y + half_outline,
                    radii.top_right_y + half_outline,
                    radii.bottom_right_y + half_outline,
                    radii.bottom_left_y + half_outline};
                append_ellipse_metadata(
                    outline_radii,
                    outline_x,
                    outline_y,
                    outline_width,
                    outline_height);
                commands.push_back(webscene_scene_command{
                    pseudo_paints_in_foreground ? 11U : 8U,
                    pack_round_rect(0, outline),
                    outline_x,
                    outline_y,
                    outline_width,
                    outline_height,
                    with_pseudo_opacity(pseudo.outline_rgba),
                    node.id,
                    outline_radii.top_left,
                    outline_radii.top_right,
                    outline_radii.bottom_right,
                    outline_radii.bottom_left,
                    outline});
            } else {
                const auto append_outline_rect = [&](float rect_x, float rect_y,
                                                     float rect_width, float rect_height) {
                    if (rect_width <= 0 || rect_height <= 0) return;
                    commands.push_back(webscene_scene_command{
                        pseudo_paints_in_foreground ? 9U : 1U,
                        0U,
                        rect_x,
                        rect_y,
                        rect_width,
                        rect_height,
                        with_pseudo_opacity(pseudo.outline_rgba),
                        node.id});
                };
                append_outline_rect(x - outline, y - outline, width + outline * 2.0F, outline);
                append_outline_rect(x + width, y, outline, height);
                append_outline_rect(x - outline, y + height, width + outline * 2.0F, outline);
                append_outline_rect(x - outline, y, outline, height);
            }
        }
        if (has_visible_text(pseudo.content)) {
            std::ostringstream resource;
            resource << font_size << '\t' << line_height << '\t'
                << resolved_font_weight(node) << '\t' << resolved_text_align(node) << '\t';
            if (pseudo.font_family.empty()) resource << resolved_font_family(node);
            else resource << pseudo.font_family;
            resource << '\t' << resolved_font_smoothing(node) << '\t' << pseudo.content;
            commands.push_back(webscene_scene_command{
                3U,
                append_scene_string(resource.str(), strings, string_bytes),
                x,
                y,
                width,
                height,
                (pseudo.foreground_rgba & 0xFFU) != 0U
                    ? with_pseudo_opacity(pseudo.foreground_rgba)
                    : with_pseudo_opacity(resolved_foreground(node)),
                node.id,
                resolved_letter_spacing(node),
                resolved_word_spacing(node),
                0,
                0,
                0});
        }
        if (animated_rotation) {
            commands.push_back(webscene_scene_command{
                20U, 0U, 0, 0, 0, 0, 0U, node.id});
        }
        if (animated_scale) {
            commands.push_back(webscene_scene_command{
                16U, 0U, 0, 0, 0, 0, 0U, node.id});
        }
        for (size_t filter_index = 0U; filter_index < animated_filters.size();
             ++filter_index) {
            commands.push_back(webscene_scene_command{
                31U, 0U, x, y, width, height, 0U, node.id});
        }
    };
    const auto radii = resolve_radii(
        node.style.border_top_left_radius,
        node.style.border_top_right_radius,
        node.style.border_bottom_right_radius,
        node.style.border_bottom_left_radius,
        node.style.border_top_left_radius_y(),
        node.style.border_top_right_radius_y(),
        node.style.border_bottom_right_radius_y(),
        node.style.border_bottom_left_radius_y(),
        node.layout.width,
        node.layout.height);
    if (paint_self && backdrop_effect.present
        && node.layout.width > 0.0F && node.layout.height > 0.0F) {
        append_ellipse_metadata(
            radii,
            node.layout.x,
            node.layout.y,
            node.layout.width,
            node.layout.height);
        commands.push_back(webscene_scene_command{
            48U,
            append_scene_string(backdrop_effect.resource, strings, string_bytes),
            node.layout.x,
            node.layout.y,
            node.layout.width,
            node.layout.height,
            1U,
            node.id,
            radii.top_left,
            radii.top_right,
            radii.bottom_right,
            radii.bottom_left,
            backdrop_effect.maximum_blur_sigma});
    }
    if (paint_self
        && node.style.box_shadow_present && !node.style.box_shadow_inset
        && node.layout.width > 0
        && node.layout.height > 0) {
        const auto spread = node.style.box_shadow_spread_radius;
        const auto shadow_width = node.layout.width + spread * 2.0F;
        const auto shadow_height = node.layout.height + spread * 2.0F;
        if (shadow_width > 0 && shadow_height > 0) {
            const auto shadow_radii = resolved_radii{
                std::max(0.0F, radii.top_left + spread),
                std::max(0.0F, radii.top_right + spread),
                std::max(0.0F, radii.bottom_right + spread),
                std::max(0.0F, radii.bottom_left + spread),
                std::max(0.0F, radii.top_left_y + spread),
                std::max(0.0F, radii.top_right_y + spread),
                std::max(0.0F, radii.bottom_right_y + spread),
                std::max(0.0F, radii.bottom_left_y + spread)};
            const auto shadow_x = node.layout.x + node.style.box_shadow_offset_x - spread;
            const auto shadow_y = node.layout.y + node.style.box_shadow_offset_y - spread;
            append_ellipse_metadata(
                shadow_radii, shadow_x, shadow_y, shadow_width, shadow_height);
            commands.push_back(webscene_scene_command{
                paint_box_in_foreground ? 18U : 17U,
                0U,
                shadow_x,
                shadow_y,
                shadow_width,
                shadow_height,
                with_opacity(node.style.box_shadow_current_color ? resolved_foreground(node) : node.style.box_shadow_rgba),
                node.id,
                shadow_radii.top_left,
                shadow_radii.top_right,
                shadow_radii.bottom_right,
                shadow_radii.bottom_left,
                node.style.box_shadow_blur_radius});
        }
    }
    const auto background_rgba =
        node.painted_background_value(resolved_foreground(node));
    if (paint_self
        && (background_rgba & 0xFFU) != 0U
        && node.layout.width > 0
        && node.layout.height > 0) {
        append_ellipse_metadata(
            radii,
            node.layout.x,
            node.layout.y,
            node.layout.width,
            node.layout.height);
        commands.push_back(webscene_scene_command{
            radii.any()
                ? paint_box_in_foreground ? 10U : 7U
                : paint_box_in_foreground ? 9U : 1U,
            node.style.clip ? 1U : 0U,
            node.layout.x,
            node.layout.y,
            node.layout.width,
            node.layout.height,
            with_opacity(background_rgba),
            node.id,
            radii.top_left,
            radii.top_right,
            radii.bottom_right,
            radii.bottom_left,
            0});
    }
    const auto& background_image = node.style.background_image();
    auto background_image_value_lower = background_image.image_value;
    std::transform(
        background_image_value_lower.begin(),
        background_image_value_lower.end(),
        background_image_value_lower.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    if (paint_self && background_image.linear_colors.size() >= 2 && node.layout.width > 0 && node.layout.height > 0) {
        commands.push_back(webscene_scene_command{
            paint_box_in_foreground ? 46U : 44U,
            static_cast<uint32_t>(background_image.linear_colors.size()),
            node.layout.x, node.layout.y, node.layout.width, node.layout.height,
            0U, node.id, radii.top_left, radii.top_right, radii.bottom_right,
            radii.bottom_left, background_image.linear_angle});
        for (size_t i=0; i<background_image.linear_colors.size(); ++i) {
            commands.push_back(webscene_scene_command{45U, 0U,
                static_cast<float>(i) / (background_image.linear_colors.size()-1), 0, 0, 0,
                background_image.linear_colors[i], node.id});
        }
    }
    if (paint_self
        && (background_image_value_lower.starts_with("linear-gradient(") || background_image_value_lower.starts_with("radial-gradient("))
        && node.layout.width > 0
        && node.layout.height > 0) {
        auto gradient_resource = std::string{"webscene-bg-v2\t"};
        gradient_resource += background_image.image_value;
        gradient_resource += '\t';
        gradient_resource += background_image.repeat;
        gradient_resource += '\t';
        gradient_resource += background_image.position_value;
        gradient_resource += '\t';
        gradient_resource += background_image.size_value;
        append_ellipse_metadata(
            radii,
            node.layout.x,
            node.layout.y,
            node.layout.width,
            node.layout.height);
        commands.push_back(webscene_scene_command{
            paint_box_in_foreground ? 22U : 21U,
            append_scene_string(gradient_resource, strings, string_bytes),
            node.layout.x,
            node.layout.y,
            node.layout.width,
            node.layout.height,
            0U,
            node.id,
            radii.top_left,
            radii.top_right,
            radii.bottom_right,
            radii.bottom_left,
            0});
    }
    if (paint_self
        && !background_image.image_markup.empty()
        && !background_image.image_view_box.empty()
        && node.layout.width > 0
        && node.layout.height > 0) {
        // A CSS background image owns the element's paint box, not only the
        // first resolved image tile. Preserve its longhands in the retained
        // resource so the backend can clip and repeat the SVG exactly like a
        // browser background brush. Replaced <img> elements continue to use
        // the legacy viewBox/markup resource below.
        auto background_resource = std::string{"webscene-bg-svg-v1\t"};
        background_resource += background_image.image_view_box;
        background_resource += '\t';
        background_resource += background_image.repeat;
        background_resource += '\t';
        background_resource += background_image.position_value;
        background_resource += '\t';
        background_resource += background_image.size_value;
        background_resource += '\t';
        background_resource += background_image.image_markup;
        const auto resource_index = append_scene_string(
            background_resource,
            strings,
            string_bytes);
        append_ellipse_metadata(
            radii,
            node.layout.x,
            node.layout.y,
            node.layout.width,
            node.layout.height);
        commands.push_back(webscene_scene_command{
            6U,
            resource_index,
            node.layout.x,
            node.layout.y,
            node.layout.width,
            node.layout.height,
            0U,
            node.id,
            radii.top_left,
            radii.top_right,
            radii.bottom_right,
            radii.bottom_left,
            0});
    }
    const auto& replaced_image = node.replaced_image();
    if (paint_self
        && node.tag == "img"
        && !replaced_image.markup.empty()
        && (replaced_image.encoded_raster || !replaced_image.view_box.empty())
        && node.layout.width > 0
        && node.layout.height > 0) {
        const auto resource = replaced_image.encoded_raster
            ? std::string{"webscene-raster-v1\t"} + replaced_image.markup
            : replaced_image.view_box + "\t\t" + replaced_image.markup;
        const auto resource_index = append_scene_string(resource, strings, string_bytes);
        const auto border_left = resolve_length(
            node, node.style.border_left_width, node.layout.width, 0);
        const auto border_top = resolve_length(
            node, node.style.border_top_width, node.layout.width, 0);
        const auto border_right = resolve_length(
            node, node.style.border_right_width, node.layout.width, 0);
        const auto border_bottom = resolve_length(
            node, node.style.border_bottom_width, node.layout.width, 0);
        const auto padding_left = resolve_length(
            node, node.style.padding_left, node.layout.width, 0);
        const auto padding_top = resolve_length(
            node, node.style.padding_top, node.layout.width, 0);
        const auto padding_right = resolve_length(
            node, node.style.padding_right, node.layout.width, 0);
        const auto padding_bottom = resolve_length(
            node, node.style.padding_bottom, node.layout.width, 0);
        const auto inset_left = std::max(0.0F, border_left + padding_left);
        const auto inset_top = std::max(0.0F, border_top + padding_top);
        const auto inset_right = std::max(0.0F, border_right + padding_right);
        const auto inset_bottom = std::max(0.0F, border_bottom + padding_bottom);
        const layout_rect content_box{
            node.layout.x + inset_left,
            node.layout.y + inset_top,
            std::max(0.0F, node.layout.width - inset_left - inset_right),
            std::max(0.0F, node.layout.height - inset_top - inset_bottom)};
        auto object_width = content_box.width;
        auto object_height = content_box.height;
        const auto natural_width = replaced_image.natural_width;
        const auto natural_height = replaced_image.natural_height;
        const auto fit = node.style.textual().object_fit.empty()
            ? std::string_view{"fill"}
            : std::string_view{node.style.textual().object_fit};
        if (natural_width > 0 && natural_height > 0 && fit != "fill") {
            auto scale = 1.0F;
            if (fit == "contain" || fit == "cover" || fit == "scale-down") {
                const auto width_scale = content_box.width / natural_width;
                const auto height_scale = content_box.height / natural_height;
                scale = fit == "cover"
                    ? std::max(width_scale, height_scale)
                    : std::min(width_scale, height_scale);
                if (fit == "scale-down") scale = std::min(1.0F, scale);
            }
            object_width = std::max(0.0F, natural_width * scale);
            object_height = std::max(0.0F, natural_height * scale);
        }
        auto horizontal_position = std::string{"50%"};
        auto vertical_position = std::string{"50%"};
        if (!node.style.textual().object_position.empty()) {
            std::istringstream position(node.style.textual().object_position);
            position >> horizontal_position >> vertical_position;
            if (vertical_position.empty()) vertical_position = "50%";
            const auto vertical_keyword = [](std::string_view token) {
                return token == "top" || token == "bottom";
            };
            const auto horizontal_keyword = [](std::string_view token) {
                return token == "left" || token == "right";
            };
            if (vertical_keyword(horizontal_position)
                || horizontal_keyword(vertical_position)) {
                std::swap(horizontal_position, vertical_position);
            }
        }
        const auto resolve_object_position = [&](std::string_view token,
            float free_space) {
            if (token == "left" || token == "top") return 0.0F;
            if (token == "center") return free_space * 0.5F;
            if (token == "right" || token == "bottom") return free_space;
            const auto length = native_document::parse_length(std::string(token));
            return resolve_length(node, length, free_space, free_space * 0.5F);
        };
        const auto object_x = content_box.x + resolve_object_position(
            horizontal_position, content_box.width - object_width);
        const auto object_y = content_box.y + resolve_object_position(
            vertical_position, content_box.height - object_height);
        const auto clip_top_left = std::max(
            0.0F, radii.top_left - std::max(inset_left, inset_top));
        const auto clip_top_right = std::max(
            0.0F, radii.top_right - std::max(inset_right, inset_top));
        const auto clip_bottom_right = std::max(
            0.0F, radii.bottom_right - std::max(inset_right, inset_bottom));
        const auto clip_bottom_left = std::max(
            0.0F, radii.bottom_left - std::max(inset_left, inset_bottom));
        commands.push_back(webscene_scene_command{
            12U, 0U, content_box.x, content_box.y,
            content_box.width, content_box.height, 0U, node.id,
            clip_top_left, clip_top_right, clip_bottom_right, clip_bottom_left});
        commands.push_back(webscene_scene_command{
            6U,
            resource_index,
            object_x,
            object_y,
            object_width,
            object_height,
            0U,
            node.id,
            0,
            0,
            0,
            0});
        commands.push_back(webscene_scene_command{
            13U, 0U, 0, 0, 0, 0, 0U, node.id});
    }
    if (paint_self && node.style.box_shadow_present && node.style.box_shadow_inset
        && node.layout.width > 0 && node.layout.height > 0) {
        const float left = resolve_length(node, node.style.border_left_width, node.layout.width, 0);
        const float right = resolve_length(node, node.style.border_right_width, node.layout.width, 0);
        const float top = resolve_length(node, node.style.border_top_width, node.layout.width, 0);
        const float bottom = resolve_length(node, node.style.border_bottom_width, node.layout.width, 0);
        const layout_rect box{node.layout.x + left, node.layout.y + top,
            std::max(0.0F, node.layout.width - left - right), std::max(0.0F, node.layout.height - top - bottom)};
        const float spread = node.style.box_shadow_spread_radius;
        const auto tl = std::max(0.0F, radii.top_left - std::max(left, top));
        const auto tr = std::max(0.0F, radii.top_right - std::max(right, top));
        const auto br = std::max(0.0F, radii.bottom_right - std::max(right, bottom));
        const auto bl = std::max(0.0F, radii.bottom_left - std::max(left, bottom));
        commands.push_back(webscene_scene_command{12U,0U,box.x,box.y,box.width,box.height,0U,node.id,tl,tr,br,bl});
        // Shadow flag 1 paints the inverse rounded hole inside the preceding clip.
        commands.push_back(webscene_scene_command{paint_box_in_foreground ? 18U : 17U,1U,
            box.x + node.style.box_shadow_offset_x + spread,
            box.y + node.style.box_shadow_offset_y + spread,
            std::max(0.0F, box.width - 2 * spread), std::max(0.0F, box.height - 2 * spread),
            with_opacity(node.style.box_shadow_current_color ? resolved_foreground(node) : node.style.box_shadow_rgba),node.id,
            std::max(0.0F,tl-spread),std::max(0.0F,tr-spread),std::max(0.0F,br-spread),std::max(0.0F,bl-spread),
            node.style.box_shadow_blur_radius});
        commands.push_back(webscene_scene_command{13U,0U,0,0,0,0,0U,node.id});
    }
    const auto append_border_rect = [&](float x, float y, float width, float height, uint32_t rgba) {
        if ((rgba & 0xFFU) == 0U || width <= 0 || height <= 0) return;
        commands.push_back(webscene_scene_command{
            paint_box_in_foreground ? 9U : 1U,
            0U,
            x,
            y,
            width,
            height,
            with_opacity(rgba),
            node.id});
    };
    if (paint_self && node.layout.width > 0 && node.layout.height > 0) {
        const auto foreground_rgba = resolved_foreground(node);
        const auto border_top_rgba = node.style.border_top_current_color
            ? foreground_rgba : node.style.border_top_rgba;
        const auto border_right_rgba = node.style.border_right_current_color
            ? foreground_rgba : node.style.border_right_rgba;
        const auto border_bottom_rgba = node.style.border_bottom_current_color
            ? foreground_rgba : node.style.border_bottom_rgba;
        const auto border_left_rgba = node.style.border_left_current_color
            ? foreground_rgba : node.style.border_left_rgba;
        const auto top = std::clamp(
            resolve_length(node, node.style.border_top_width, node.layout.height, 0),
            0.0F,
            node.layout.height);
        const auto right = std::clamp(
            resolve_length(node, node.style.border_right_width, node.layout.width, 0),
            0.0F,
            node.layout.width);
        const auto bottom = std::clamp(
            resolve_length(node, node.style.border_bottom_width, node.layout.height, 0),
            0.0F,
            node.layout.height);
        const auto left = std::clamp(
            resolve_length(node, node.style.border_left_width, node.layout.width, 0),
            0.0F,
            node.layout.width);
        const auto uniform_width = std::abs(top - right) < 0.01F
            && std::abs(top - bottom) < 0.01F
            && std::abs(top - left) < 0.01F;
        const auto uniform_color = border_top_rgba == border_right_rgba
            && border_top_rgba == border_bottom_rgba
            && border_top_rgba == border_left_rgba;
        const auto rounded_stroke_width = std::max({top, right, bottom, left});
        const auto compatible_rounded_widths = rounded_stroke_width > 0
            && (top <= 0 || std::abs(top - rounded_stroke_width) < 0.01F)
            && (right <= 0 || std::abs(right - rounded_stroke_width) < 0.01F)
            && (bottom <= 0 || std::abs(bottom - rounded_stroke_width) < 0.01F)
            && (left <= 0 || std::abs(left - rounded_stroke_width) < 0.01F);
        const auto &dash = node.style.border_dashed;
        if (std::any_of(dash.begin(), dash.end(), [](bool value){ return value; })) {
            if (uniform_width && uniform_color && std::all_of(dash.begin(), dash.end(), [](bool value){ return value; })) {
                const float inset = top * .5F;
                commands.push_back(webscene_scene_command{paint_box_in_foreground ? 41U : 40U,0U,
                    node.layout.x+inset,node.layout.y+inset,
                    std::max(0.0F,node.layout.width-top),std::max(0.0F,node.layout.height-top),
                    with_opacity(border_top_rgba),node.id,
                    std::max(0.0F,radii.top_left-inset),std::max(0.0F,radii.top_right-inset),
                    std::max(0.0F,radii.bottom_right-inset),std::max(0.0F,radii.bottom_left-inset),top});
            } else {
                const auto edge = [&](float x,float y,float end_x,float end_y,float width,uint32_t color,bool dashed) {
                    if (width <= 0) return;
                    commands.push_back(webscene_scene_command{dashed ? (paint_box_in_foreground ? 43U : 42U) :
                        (paint_box_in_foreground ? 14U : 2U),0U,x,y,end_x,end_y,with_opacity(color),node.id,0,0,0,0,width});
                };
                edge(node.layout.x,node.layout.y+top*.5F,node.layout.x+node.layout.width,node.layout.y+top*.5F,top,border_top_rgba,dash[1]);
                edge(node.layout.x+node.layout.width-right*.5F,node.layout.y,node.layout.x+node.layout.width-right*.5F,node.layout.y+node.layout.height,right,border_right_rgba,dash[2]);
                edge(node.layout.x,node.layout.y+node.layout.height-bottom*.5F,node.layout.x+node.layout.width,node.layout.y+node.layout.height-bottom*.5F,bottom,border_bottom_rgba,dash[3]);
                edge(node.layout.x+left*.5F,node.layout.y,node.layout.x+left*.5F,node.layout.y+node.layout.height,left,border_left_rgba,dash[0]);
            }
        } else if (radii.any() && compatible_rounded_widths) {
            constexpr uint32_t border_top_flag = 1U << 28U;
            constexpr uint32_t border_right_flag = 1U << 29U;
            constexpr uint32_t border_bottom_flag = 1U << 30U;
            constexpr uint32_t border_left_flag = 1U << 31U;
            constexpr uint32_t border_color_partition_flag = 1U << 27U;
            const auto inset = rounded_stroke_width * 0.5F;
            const auto left_inset = left > 0 ? inset : 0.0F;
            const auto right_inset = right > 0 ? inset : 0.0F;
            const auto top_inset = top > 0 ? inset : 0.0F;
            const auto bottom_inset = bottom > 0 ? inset : 0.0F;
            const auto all_sides = top > 0 && right > 0 && bottom > 0 && left > 0;
            const auto partition_colors = all_sides && !uniform_color;
            const auto append_rounded_border = [&](uint32_t color, uint32_t side_flags) {
                if ((color & 0xFFU) == 0U || side_flags == 0U) return;
                const auto border_x = node.layout.x + left_inset;
                const auto border_y = node.layout.y + top_inset;
                const auto border_width = std::max(
                    0.0F, node.layout.width - left_inset - right_inset);
                const auto border_height = std::max(
                    0.0F, node.layout.height - top_inset - bottom_inset);
                const auto border_radii = resolved_radii{
                    std::max(0.0F, radii.top_left - left_inset),
                    std::max(0.0F, radii.top_right - right_inset),
                    std::max(0.0F, radii.bottom_right - right_inset),
                    std::max(0.0F, radii.bottom_left - left_inset),
                    std::max(0.0F, radii.top_left_y - top_inset),
                    std::max(0.0F, radii.top_right_y - top_inset),
                    std::max(0.0F, radii.bottom_right_y - bottom_inset),
                    std::max(0.0F, radii.bottom_left_y - bottom_inset)};
                append_ellipse_metadata(
                    border_radii, border_x, border_y, border_width, border_height);
                commands.push_back(webscene_scene_command{
                    paint_box_in_foreground ? 11U : 8U,
                    pack_round_rect(0, rounded_stroke_width)
                        | (partition_colors ? border_color_partition_flag : 0U)
                        | (all_sides && uniform_width && uniform_color ? 0U : side_flags),
                    border_x,
                    border_y,
                    border_width,
                    border_height,
                    with_opacity(color),
                    node.id,
                    border_radii.top_left,
                    border_radii.top_right,
                    border_radii.bottom_right,
                    border_radii.bottom_left,
                    rounded_stroke_width});
            };
            const std::array<uint32_t, 4> colors{
                border_top_rgba,
                border_right_rgba,
                border_bottom_rgba,
                border_left_rgba};
            for (size_t color_index = 0; color_index < colors.size(); ++color_index) {
                const auto color = colors[color_index];
                auto already_emitted = false;
                for (size_t previous = 0; previous < color_index; ++previous) {
                    already_emitted = already_emitted || colors[previous] == color;
                }
                if (already_emitted) continue;
                const auto side_flags =
                    (top > 0 && border_top_rgba == color ? border_top_flag : 0U)
                    | (right > 0 && border_right_rgba == color ? border_right_flag : 0U)
                    | (bottom > 0 && border_bottom_rgba == color ? border_bottom_flag : 0U)
                    | (left > 0 && border_left_rgba == color ? border_left_flag : 0U);
                append_rounded_border(color, side_flags);
            }
        } else {
            append_border_rect(
                node.layout.x,
                node.layout.y,
                node.layout.width,
                top,
                border_top_rgba);
            append_border_rect(
                node.layout.x + node.layout.width - right,
                node.layout.y,
                right,
                node.layout.height,
                border_right_rgba);
            append_border_rect(
                node.layout.x,
                node.layout.y + node.layout.height - bottom,
                node.layout.width,
                bottom,
                border_bottom_rgba);
            append_border_rect(
                node.layout.x,
                node.layout.y,
                left,
                node.layout.height,
                border_left_rgba);
        }
    }
    const auto outline_rgba = node.style.outline_current_color
        ? resolved_foreground(node) : node.style.outline_rgba;
    if (paint_self && node.layout.width > 0 && node.layout.height > 0
        && (outline_rgba & 0xFFU) != 0U) {
        const auto outline = std::max(
            0.0F,
            resolve_length(node, node.style.outline_width, node.layout.width, 0));
        const auto offset = std::max(-std::min(node.layout.width, node.layout.height) * .5F,
            resolve_length(node, node.style.outline_offset, node.layout.width, 0));
        const layout_rect outline_box{node.layout.x - offset, node.layout.y - offset,
            std::max(0.0F, node.layout.width + 2 * offset),
            std::max(0.0F, node.layout.height + 2 * offset)};
        if (outline > 0 && radii.any()) {
            const auto half_outline = outline * 0.5F;
            const auto outline_x = outline_box.x - half_outline;
            const auto outline_y = outline_box.y - half_outline;
            const auto outline_width = outline_box.width + outline;
            const auto outline_height = outline_box.height + outline;
            const auto outline_radii = resolved_radii{
                std::max(0.0F, radii.top_left + offset + half_outline),
                std::max(0.0F, radii.top_right + offset + half_outline),
                std::max(0.0F, radii.bottom_right + offset + half_outline),
                std::max(0.0F, radii.bottom_left + offset + half_outline),
                std::max(0.0F, radii.top_left_y + offset + half_outline),
                std::max(0.0F, radii.top_right_y + offset + half_outline),
                std::max(0.0F, radii.bottom_right_y + offset + half_outline),
                std::max(0.0F, radii.bottom_left_y + offset + half_outline)};
            append_ellipse_metadata(
                outline_radii,
                outline_x,
                outline_y,
                outline_width,
                outline_height);
            commands.push_back(webscene_scene_command{
                paint_box_in_foreground ? 11U : 8U,
                pack_round_rect(0, outline),
                outline_x,
                outline_y,
                outline_width,
                outline_height,
                with_opacity(outline_rgba),
                node.id,
                outline_radii.top_left,
                outline_radii.top_right,
                outline_radii.bottom_right,
                outline_radii.bottom_left,
                outline});
        } else {
            append_border_rect(
                outline_box.x - outline,
                outline_box.y - outline,
                outline_box.width + outline * 2.0F,
                outline,
                outline_rgba);
            append_border_rect(
                outline_box.x + outline_box.width,
                outline_box.y,
                outline,
                outline_box.height,
                outline_rgba);
            append_border_rect(
                outline_box.x - outline,
                outline_box.y + outline_box.height,
                outline_box.width + outline * 2.0F,
                outline,
                outline_rgba);
            append_border_rect(
                outline_box.x - outline,
                outline_box.y,
                outline,
                outline_box.height,
                outline_rgba);
        }
    }
    // An empty overflow viewport still clips all of its contents. In
    // particular, components commonly hide retained status text by animating
    // the containing box to height: 0 while leaving overflow: hidden. Skipping
    // the clip for that geometry incorrectly paints the retained text.
    const auto clip_contents = node.style.clip;
    if (clip_contents) {
        // Kinds 12/13 bracket the element's pseudo/content/descendant paint.
        // The box background and border intentionally remain outside its own
        // overflow clip, matching CSS background/border painting semantics.
        append_ellipse_metadata(
            radii,
            node.layout.x,
            node.layout.y,
            node.layout.width,
            node.layout.height);
        commands.push_back(webscene_scene_command{
            12U,
            0U,
            node.layout.x,
            node.layout.y,
            node.layout.width,
            node.layout.height,
            0U,
            node.id,
            radii.top_left,
            radii.top_right,
            radii.bottom_right,
            radii.bottom_left,
            0});
    }
    if (node.list_marker_layout.width > 0 || node.list_marker_layout.height > 0) {
        // Keep existing LI marker rasterization stable. A generic list-item's
        // square is geometry so its correctness does not depend on font glyphs.
        if (paint_self
            && node.tag != "li"
            && resolved_list_style(node, false) == "square") {
            const auto font_size = resolved_font_size(node);
            const auto line_height = resolved_line_height(node, font_size);
            const auto marker_size = std::max(3.0F, std::round(font_size * 0.3125F));
            commands.push_back(webscene_scene_command{
                9U,
                0U,
                node.list_marker_layout.x,
                node.list_marker_layout.y
                    + std::max(0.0F, (line_height - marker_size) * 0.5F),
                marker_size,
                marker_size,
                with_opacity(resolved_foreground(node)),
                node.id,
                0,
                0,
                0,
                0,
                0});
        } else {
            node_style::pseudo_element marker{};
            marker.generated = true;
            marker.content = list_marker_text(node);
            marker.layout = node.list_marker_layout;
            append_pseudo(marker);
        }
    }
    const auto before_paints_after_content = node.style.before_pseudo().z_index > 0
        && node.style.before_pseudo().position != position_mode::normal;
    if (!before_paints_after_content) {
        append_pseudo(node.style.before_pseudo());
    }
    // Positioned negative-z pseudo-elements participate behind the element's
    // content. Hosted controls use this for hover/active button surfaces; painting
    // ::after in its ordinary DOM position would cover the button SVG instead.
    const auto after_paints_behind_content = node.style.after_pseudo().z_index < 0
        && node.style.after_pseudo().position != position_mode::normal;
    if (after_paints_behind_content) {
        append_pseudo(node.style.after_pseudo());
    }
    const auto& canvas = node.canvas();
    const auto gpu_metadata=capture_gpu_outputs && canvas.gpu_snapshot
        ? std::optional(canvas.gpu_snapshot->describe())
        : canvas.gpu_image ? std::optional(canvas.gpu_image->value.describe()) : std::nullopt;
    const bool presentation_gpu=capture_gpu_outputs && !gpu_metadata && canvas.gpu_presentation_image
        && canvas.gpu_presentation_image->value.describe().canvas==canvas.backing.identity();
    const bool paints_gpu=presentation_gpu || (gpu_metadata
        && gpu_metadata->allocation_generation==canvas.backing.allocation_generation()
        && canvas.backing.accepts_completed_content(gpu_metadata->content_serial));
    if (paint_self && (node.tag=="canvas" || node.tag=="video") && paints_gpu) {
        // Native DOM output carries the node ID. Publication resolves it to the
        // scene-local image index before exposing the command to consumers.
        commands.push_back(webscene_scene_command{
            WEBSCENE_SCENE_COMMAND_GPU_IMAGE,0U,node.layout.x,node.layout.y,
            node.layout.width,node.layout.height,0U,node.id,0,0,0,0,0});
    }
    if (ordered_canvas && paint_self && !paints_gpu && node.tag=="canvas" && (!canvas.commands.empty() || canvas.externally_composited)) {
        commands.push_back(webscene_scene_command{
            WEBSCENE_SCENE_COMMAND_CANVAS_LAYER,0U,0,0,0,0,0U,node.id,0,0,0,0,0});
    }
    if (paint_self && !paints_gpu
        && node.tag == "canvas"
        && canvas.commands.empty()
        && (!canvas.rects.empty() || !canvas.lines.empty())) {
        const auto width_iterator = node.attributes.find("width");
        const auto height_iterator = node.attributes.find("height");
        const auto bitmap_width = width_iterator == node.attributes.end()
            ? 300.0F
            : std::max(1.0F, parse_number(width_iterator->second, 300.0F));
        const auto bitmap_height = height_iterator == node.attributes.end()
            ? 150.0F
            : std::max(1.0F, parse_number(height_iterator->second, 150.0F));
        const auto display_width = node.layout.width > 0 ? node.layout.width : bitmap_width;
        const auto display_height = node.layout.height > 0 ? node.layout.height : bitmap_height;
        const auto scale_x = display_width / bitmap_width;
        const auto scale_y = display_height / bitmap_height;
        for (const auto& rect : canvas.rects) {
            if ((rect.rgba & 0xFFU) == 0U || rect.width <= 0 || rect.height <= 0) continue;
            commands.push_back(webscene_scene_command{
                1U,
                0U,
                node.layout.x + rect.x * scale_x,
                node.layout.y + rect.y * scale_y,
                rect.width * scale_x,
                rect.height * scale_y,
                with_opacity(rect.rgba),
                node.id});
        }
        for (const auto& line : canvas.lines) {
            if ((line.rgba & 0xFFU) == 0U) continue;
            commands.push_back(webscene_scene_command{
                2U,
                static_cast<uint32_t>(std::max(1.0F, line.line_width * 100.0F)),
                node.layout.x + line.x1 * scale_x,
                node.layout.y + line.y1 * scale_y,
                node.layout.x + line.x2 * scale_x,
                node.layout.y + line.y2 * scale_y,
                with_opacity(line.rgba),
                node.id});
        }
    }

    if (paint_self && is_collapsed_select(node)) {
        const auto font_size = resolved_font_size(node);
        const auto line_height = resolved_line_height(node, font_size);
        const auto padding_left = resolve_length(
            node, node.style.padding_left, node.layout.width, 0);
        const auto padding_right = resolve_length(
            node, node.style.padding_right, node.layout.width, 0);
        const auto affordance_width = std::max(12.0F, font_size * 1.25F);
        const auto text_x = node.layout.x + padding_left;
        const auto text_y = node.layout.y
            + std::max(0.0F, (node.layout.height - line_height) * 0.5F);
        const auto text_width = std::max(
            0.0F,
            node.layout.width - padding_left - padding_right - affordance_width);
        if (const auto* selected = selected_option_for(node); selected != nullptr) {
            // A collapsed SELECT is a replaced control: its selected label is
            // clipped to the principal content slot rather than painting
            // through the disclosure affordance or outside the control. Text
            // commands deliberately live on the DOM foreground plane, so the
            // clip must bracket both retained presenter planes.
            commands.push_back(webscene_scene_command{
                12U,
                0U,
                text_x,
                node.layout.y,
                text_width,
                node.layout.height,
                0U,
                node.id});
            std::ostringstream resource;
            resource << font_size << '\t' << line_height << '\t'
                << resolved_font_weight(node) << '\t' << "start" << '\t'
                << resolved_font_family(node) << '\t'
                << resolved_font_smoothing(node) << '\t' << option_label(*selected);
            commands.push_back(webscene_scene_command{
                3U,
                append_scene_string(resource.str(), strings, string_bytes),
                text_x,
                text_y,
                text_width,
                line_height,
                with_opacity(resolved_foreground(node)),
                node.id,
                resolved_letter_spacing(node),
                resolved_word_spacing(node),
                0,
                0,
                0});
            commands.push_back(webscene_scene_command{
                13U, 0U, 0, 0, 0, 0, 0U, node.id});
        }
        const auto center_x = node.layout.x + node.layout.width
            - padding_right - affordance_width * 0.5F;
        const auto center_y = node.layout.y + node.layout.height * 0.5F;
        const auto arrow_color = with_opacity(resolved_foreground(node));
        // The disclosure affordance is principal foreground content. Kind 2
        // is composited with DOM backgrounds and is hidden by this control's
        // own opaque kind 9/10 background in an elevated stacking context.
        commands.push_back(webscene_scene_command{
            14U, 100U,
            center_x - 3.5F, center_y - 1.5F,
            center_x, center_y + 2.0F,
            arrow_color, node.id});
        commands.push_back(webscene_scene_command{
            14U, 100U,
            center_x, center_y + 2.0F,
            center_x + 3.5F, center_y - 1.5F,
            arrow_color, node.id});
    }

    const auto input_type = node.attributes.find("type");
    const bool color_input = node.tag == "input"
        && input_type != node.attributes.end() && input_type->second == "color";
    const bool checkbox_input = node.tag == "input"
        && input_type != node.attributes.end() && input_type->second == "checkbox";
    if(paint_self && checkbox_input) {
        const auto& control=node.form_control();
        const bool checked=control.checkedness_initialized?control.checkedness:node.attributes.contains("checked");
        if(control.indeterminate) {
            const auto size=std::min(node.layout.width,node.layout.height);
            const auto x=node.layout.x+(node.layout.width-size)*0.5F;
            const auto y=node.layout.y+(node.layout.height-size)*0.5F;
            const auto color=with_opacity(resolved_foreground(node));
            commands.push_back(webscene_scene_command{2U,150U,x+size*.2F,y+size*.5F,x+size*.8F,y+size*.5F,color,node.id});
        } else if(checked) {
            const auto size=std::min(node.layout.width,node.layout.height);
            const auto x=node.layout.x+(node.layout.width-size)*0.5F;
            const auto y=node.layout.y+(node.layout.height-size)*0.5F;
            const auto color=with_opacity(resolved_foreground(node));
            commands.push_back(webscene_scene_command{2U,150U,x+size*.2F,y+size*.5F,x+size*.43F,y+size*.72F,color,node.id});
            commands.push_back(webscene_scene_command{2U,150U,x+size*.43F,y+size*.72F,x+size*.82F,y+size*.25F,color,node.id});
        }
    }
    if (paint_self && color_input) {
        // A color control presents its live value as a swatch, not editable text.
        // Keep the value on the control so native application updates repaint it.
        const auto& value = node.form_control().value;
        const bool valid_color = value.size() == 7 && value.front() == '#'
            && std::all_of(value.begin() + 1, value.end(), [](unsigned char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')
                    || (c >= 'A' && c <= 'F');
            });
        const auto inset = std::min(3.0F, std::min(node.layout.width, node.layout.height) * 0.5F);
        commands.push_back(webscene_scene_command{
            1U, 0U, node.layout.x + inset, node.layout.y + inset,
            std::max(0.0F, node.layout.width - inset * 2),
            std::max(0.0F, node.layout.height - inset * 2),
            with_opacity(valid_color ? parse_color(value) : 0x000000FFU), node.id});
    }
    if (paint_self && !color_input && !checkbox_input && (node.tag == "input" || node.tag == "textarea")) {
        const auto font_size = resolved_font_size(node);
        const auto line_height = resolved_line_height(node, font_size);
        const auto padding_left = resolve_length(
            node, node.style.padding_left, node.layout.width, 0);
        const auto padding_right = resolve_length(
            node, node.style.padding_right, node.layout.width, 0);
        const auto text_x = node.layout.x + padding_left;
        const auto text_y = node.layout.y + std::max(0.0F, (node.layout.height - line_height) * 0.5F);
        const auto text_width = std::max(
            0.0F, node.layout.width - padding_left - padding_right);
        const auto placeholder = node.attributes.find("placeholder");
        const bool placeholder_type = node.tag == "textarea" || input_type == node.attributes.end()
            || input_type->second == "text" || input_type->second == "search"
            || input_type->second == "url" || input_type->second == "tel"
            || input_type->second == "email" || input_type->second == "password"
            || input_type->second == "number";
        const bool show_placeholder = placeholder_type && node.form_control().value.empty()
            && placeholder != node.attributes.end() && !placeholder->second.empty();
        if (!node.form_control().value.empty() || show_placeholder) {
            const auto painted_value = resolved_text_transform(
                node, show_placeholder ? placeholder->second : node.form_control().value);
            auto text_color = resolved_foreground(node);
            if (show_placeholder) {
                const auto& placeholder_style = node.style.placeholder_pseudo();
                if (placeholder_style.foreground_specified)
                    text_color = placeholder_style.foreground_rgba;
                const auto alpha = static_cast<uint32_t>(std::clamp(
                    std::lround(static_cast<float>(text_color & 0xFFU)
                        * placeholder_style.opacity),
                    0L,
                    255L));
                text_color = (text_color & 0xFFFFFF00U) | alpha;
            }
            std::ostringstream resource;
            resource << font_size << '\t' << line_height << '\t'
                << resolved_font_weight(node) << '\t' << "start" << '\t'
                << resolved_font_family(node) << '\t'
                << resolved_font_smoothing(node) << '\t' << painted_value;
            commands.push_back(webscene_scene_command{
                3U,
                append_scene_string(resource.str(), strings, string_bytes),
                text_x,
                text_y,
                text_width,
                line_height,
                with_opacity(text_color),
                node.id,
                resolved_letter_spacing(node),
                resolved_word_spacing(node),
                0,
                0,
                0});
        }
        if (node.form_control().input_focused && node.form_control().caret_visible) {
            const auto caret_index = std::min(
                node.form_control().selection_end,
                node.form_control().value.size());
            const auto caret_x = std::min(
                text_x + measure_text_width(
                    std::string_view(node.form_control().value).substr(0, caret_index),
                    node),
                node.layout.x + node.layout.width - padding_right);
            commands.push_back(webscene_scene_command{
                14U,
                100U,
                caret_x,
                text_y + 1.0F,
                caret_x,
                text_y + std::max(1.0F, line_height - 1.0F),
                with_opacity(resolved_foreground(node)),
                node.id});
        }
    }

    if (paint_self && has_visible_text(node.text_content)) {
        const auto font_size = resolved_font_size(node);
        const auto line_height = resolved_line_height(node, font_size);
        if (!node.text_layout_fragments.empty()) {
            struct highlight_text_segment final {
                size_t start_byte{};
                size_t end_byte{};
                uint32_t foreground_rgba{};
                uint32_t background_rgba{};
                int32_t priority{};
                uint32_t registry_order{};
                bool foreground_specified{false};
                bool background_specified{false};
            };
            std::vector<highlight_text_segment> highlight_segments;
            if (auxiliary_nodes_ != nullptr
                && !auxiliary_nodes_->custom_highlights.empty()) {
                const auto utf8_scalar_extent = [](std::string_view value, size_t index) {
                    if (index >= value.size()) return std::pair<size_t, size_t>{0U, 0U};
                    const auto first = static_cast<unsigned char>(value[index]);
                    size_t bytes = 1U;
                    size_t utf16_units = 1U;
                    if ((first & 0xE0U) == 0xC0U) bytes = 2U;
                    else if ((first & 0xF0U) == 0xE0U) bytes = 3U;
                    else if ((first & 0xF8U) == 0xF0U) {
                        bytes = 4U;
                        utf16_units = 2U;
                    }
                    if (index + bytes > value.size()) return std::pair<size_t, size_t>{1U, 1U};
                    for (size_t offset = 1U; offset < bytes; ++offset) {
                        if ((static_cast<unsigned char>(value[index + offset]) & 0xC0U)
                            != 0x80U) return std::pair<size_t, size_t>{1U, 1U};
                    }
                    return std::pair{bytes, utf16_units};
                };
                const auto utf16_length = [&](std::string_view value) {
                    size_t bytes = 0U;
                    size_t units = 0U;
                    while (bytes < value.size()) {
                        const auto [length, scalar_units] = utf8_scalar_extent(value, bytes);
                        if (length == 0U) break;
                        bytes += length;
                        units += scalar_units;
                    }
                    return units;
                };
                const auto byte_offset_from_utf16 = [&](std::string_view value,
                                                        size_t requested_units) {
                    size_t bytes = 0U;
                    size_t units = 0U;
                    while (bytes < value.size() && units < requested_units) {
                        const auto [length, scalar_units] = utf8_scalar_extent(value, bytes);
                        if (length == 0U || units + scalar_units > requested_units) break;
                        bytes += length;
                        units += scalar_units;
                    }
                    return bytes;
                };
                const auto direct_child_below = [&](const dom_node& ancestor,
                                                    const dom_node& descendant) {
                    auto* child = &descendant;
                    for (auto* parent = dom_parent(*child);
                         parent != nullptr && parent != &ancestor;
                         parent = dom_parent(*child)) child = parent;
                    return dom_parent(*child) == &ancestor ? child : nullptr;
                };
                const auto boundary_compare = [&](const dom_node& left,
                                                  size_t left_offset,
                                                  const dom_node& right,
                                                  size_t right_offset) {
                    if (&left == &right) {
                        return left_offset < right_offset
                            ? -1 : left_offset > right_offset ? 1 : 0;
                    }
                    if (const auto* child = direct_child_below(left, right);
                        child != nullptr) {
                        const auto iterator = std::find(
                            left.children.begin(), left.children.end(), child);
                        const auto index = static_cast<size_t>(
                            std::distance(left.children.begin(), iterator));
                        return left_offset <= index ? -1 : 1;
                    }
                    if (const auto* child = direct_child_below(right, left);
                        child != nullptr) {
                        const auto iterator = std::find(
                            right.children.begin(), right.children.end(), child);
                        const auto index = static_cast<size_t>(
                            std::distance(right.children.begin(), iterator));
                        return index < right_offset ? -1 : 1;
                    }
                    std::vector<const dom_node*> left_path;
                    std::vector<const dom_node*> right_path;
                    for (auto* current = &left; current != nullptr;
                         current = dom_parent(*current)) left_path.push_back(current);
                    for (auto* current = &right; current != nullptr;
                         current = dom_parent(*current)) right_path.push_back(current);
                    std::reverse(left_path.begin(), left_path.end());
                    std::reverse(right_path.begin(), right_path.end());
                    size_t shared = 0U;
                    while (shared < left_path.size() && shared < right_path.size()
                        && left_path[shared] == right_path[shared]) ++shared;
                    if (shared == 0U) return left.id < right.id ? -1 : 1;
                    if (shared == left_path.size()) return -1;
                    if (shared == right_path.size()) return 1;
                    const auto& siblings = left_path[shared - 1U]->children;
                    const auto left_position = std::find(
                        siblings.begin(), siblings.end(), left_path[shared]);
                    const auto right_position = std::find(
                        siblings.begin(), siblings.end(), right_path[shared]);
                    return left_position < right_position ? -1 : 1;
                };
                const auto node_utf16_length = utf16_length(node.text_content);
                for (const auto& highlight : auxiliary_nodes_->custom_highlights) {
                    auto* root = highlight.root_id == 0U
                            || highlight.root_id > native_id_index_.size()
                        ? nullptr : native_id_index_[highlight.root_id - 1U];
                    auto* start = highlight.start_node_id == 0U
                            || highlight.start_node_id > native_id_index_.size()
                        ? nullptr : native_id_index_[highlight.start_node_id - 1U];
                    auto* end = highlight.end_node_id == 0U
                            || highlight.end_node_id > native_id_index_.size()
                        ? nullptr : native_id_index_[highlight.end_node_id - 1U];
                    if (root == nullptr || start == nullptr || end == nullptr) continue;
                    const auto belongs_to_root = [&](const dom_node& candidate) {
                        for (auto* current = &candidate; current != nullptr;
                             current = dom_parent(*current)) {
                            if (current == root) return true;
                        }
                        return false;
                    };
                    if (!belongs_to_root(node) || !belongs_to_root(*start)
                        || !belongs_to_root(*end)) continue;
                    if (boundary_compare(
                            *start, highlight.start_offset, node, node_utf16_length) >= 0
                        || boundary_compare(
                            *end, highlight.end_offset, node, 0U) <= 0) continue;
                    const auto local_start_utf16 = start == &node
                        ? std::min(highlight.start_offset, node_utf16_length) : 0U;
                    const auto local_end_utf16 = end == &node
                        ? std::min(highlight.end_offset, node_utf16_length)
                        : node_utf16_length;
                    if (local_start_utf16 >= local_end_utf16) continue;
                    const auto start_byte = byte_offset_from_utf16(
                        node.text_content, local_start_utf16);
                    const auto end_byte = byte_offset_from_utf16(
                        node.text_content, local_end_utf16);
                    if (start_byte >= end_byte) continue;
                    highlight_segments.push_back({
                        start_byte,
                        end_byte,
                        highlight.foreground_rgba,
                        highlight.background_rgba,
                        highlight.priority,
                        highlight.registry_order,
                        highlight.foreground_specified,
                        highlight.background_specified});
                }
                std::stable_sort(
                    highlight_segments.begin(),
                    highlight_segments.end(),
                    [](const auto& left, const auto& right) {
                        return left.priority < right.priority
                            || (left.priority == right.priority
                                && left.registry_order < right.registry_order);
                    });
            }
            const auto paint_highlight_fragments = [&](bool foreground) {
                for (const auto& segment : highlight_segments) {
                    if (foreground ? !segment.foreground_specified
                                   : !segment.background_specified) continue;
                    const auto color = foreground
                        ? segment.foreground_rgba : segment.background_rgba;
                    if ((color & 0xFFU) == 0U) continue;
                    for (const auto& fragment : node.text_layout_fragments) {
                        const auto overlap_start = std::max(
                            segment.start_byte, fragment.source_byte_start);
                        const auto overlap_end = std::min(
                            segment.end_byte, fragment.source_byte_end);
                        if (overlap_start >= overlap_end) continue;
                        size_t local_start = 0U;
                        size_t local_end = fragment.text.size();
                        const auto source_size = fragment.source_byte_end
                            - fragment.source_byte_start;
                        if (source_size == fragment.text.size()) {
                            local_start = std::min(
                                fragment.text.size(),
                                overlap_start - fragment.source_byte_start);
                            local_end = std::min(
                                fragment.text.size(),
                                overlap_end - fragment.source_byte_start);
                        }
                        if (local_start >= local_end) continue;
                        const auto prefix = std::string_view(fragment.text)
                            .substr(0U, local_start);
                        const auto selected = std::string_view(fragment.text)
                            .substr(local_start, local_end - local_start);
                        const auto selected_x = fragment.x
                            + measure_text_width(prefix, node);
                        const auto selected_width = measure_text_width(selected, node);
                        if (!foreground) {
                            commands.push_back(webscene_scene_command{
                                2U, 0U,
                                selected_x,
                                fragment.y,
                                selected_width,
                                fragment.height,
                                with_opacity(color),
                                node.id});
                            continue;
                        }
                        std::ostringstream resource;
                        resource << font_size << '\t' << fragment.height << '\t'
                            << resolved_font_weight(node) << '\t'
                            << resolved_text_align(node) << '\t'
                            << resolved_font_family(node) << '\t'
                            << resolved_font_smoothing(node) << '\t'
                            << resolved_text_transform(node, std::string(selected));
                        commands.push_back(webscene_scene_command{
                            3U,
                            append_scene_string(resource.str(), strings, string_bytes),
                            selected_x,
                            fragment.y,
                            selected_width,
                            fragment.height,
                            with_opacity(color),
                            node.id,
                            resolved_letter_spacing(node),
                            resolved_word_spacing(node),
                            0, 0, 0});
                    }
                }
            };
            paint_highlight_fragments(false);
            for (const auto& fragment : node.text_layout_fragments) {
                std::ostringstream resource;
                resource << font_size << '\t' << fragment.height << '\t'
                    << resolved_font_weight(node) << '\t' << resolved_text_align(node) << '\t'
                    << resolved_font_family(node) << '\t'
                    << resolved_font_smoothing(node) << '\t'
                    << resolved_text_transform(node, fragment.text);
                commands.push_back(webscene_scene_command{
                    3U,
                    append_scene_string(resource.str(), strings, string_bytes),
                    fragment.x,
                    fragment.y,
                    fragment.width,
                    fragment.height,
                    with_opacity(resolved_foreground(node)),
                    node.id,
                    resolved_letter_spacing(node),
                    resolved_word_spacing(node),
                    0,
                    0,
                    0});
            }
            paint_highlight_fragments(true);
        } else {
        const auto padding_left = resolve_length(
            node, node.style.padding_left, node.layout.width, 0);
        const auto padding_right = resolve_length(
            node, node.style.padding_right, node.layout.width, 0);
        const auto padding_top = resolve_vertical_padding(
            node, node.style.padding_top, node.layout.height, 0);
        const auto padding_bottom = resolve_vertical_padding(
            node, node.style.padding_bottom, node.layout.height, 0);
        auto x = node.layout.x + padding_left;
        auto y = node.layout.y + padding_top;
        auto width = std::max(0.0F, node.layout.width - padding_left - padding_right);
        auto height = std::max(0.0F, node.layout.height - padding_top - padding_bottom);
        if (width <= 0) {
            width = measure_text_width(
                resolved_text_transform(node, node.text_content),
                node);
        }
        if (height <= 0 && node.tag == "#text" && composed_parent(node) != nullptr) {
            y = composed_parent(node)->layout.y;
            height = composed_parent(node)->layout.height;
        }
        if (height <= 0) height = line_height;
        const auto wraps = resolved_white_space_wraps(node);
        const auto transformed_text = resolved_text_transform(node, node.text_content);
        const auto lines = wrap_text_lines(
            transformed_text,
            width,
            node,
            wraps);
        const auto command_width = wraps
            ? width
            : std::max(width, measure_text_width(transformed_text, node));
        for (size_t line_index = 0; line_index < lines.size(); ++line_index) {
            std::ostringstream resource;
            resource << font_size << '\t' << line_height << '\t'
                << resolved_font_weight(node) << '\t' << resolved_text_align(node) << '\t'
                << resolved_font_family(node) << '\t'
                << resolved_font_smoothing(node) << '\t'
                << lines[line_index];
            commands.push_back(webscene_scene_command{
                3U,
                append_scene_string(resource.str(), strings, string_bytes),
                x,
                y + static_cast<float>(line_index) * line_height,
                command_width,
                line_height,
                with_opacity(resolved_foreground(node)),
                node.id,
                resolved_letter_spacing(node),
                resolved_word_spacing(node),
                0,
                0,
                0});
        }
        }
    }

    if (node.tag == "svg" && paint_self) {
        auto box = node.layout;
        if ((box.width <= 0 || box.height <= 0) && composed_parent(node) != nullptr) {
            box = composed_parent(node)->layout;
        }
        if (box.width > 0 && box.height > 0) {
            const auto view_box = node.attributes.contains("viewBox")
                ? node.attributes.at("viewBox")
                : std::string("0 0 ") + std::to_string(std::max(1.0F, box.width))
                    + " " + std::to_string(std::max(1.0F, box.height));
            const auto preserve_aspect_ratio = node.attributes.contains("preserveAspectRatio")
                ? node.attributes.at("preserveAspectRatio")
                : std::string("xMidYMid meet");
            std::string markup;
            markup.reserve(256U);
            serialize_svg_subtree(node, markup, true);
            const auto resource_index = append_scene_string(
                view_box + '\t' + preserve_aspect_ratio + '\t' + markup,
                strings,
                string_bytes);
            commands.push_back(webscene_scene_command{
                6U,
                resource_index,
                box.x,
                box.y,
                box.width,
                box.height,
                with_opacity(resolved_foreground(node)),
                node.id,
                0,
                0,
                0,
                0,
                0});
        }
        if (clip_contents) {
            commands.push_back(webscene_scene_command{13U, 0U, 0, 0, 0, 0, 0U, node.id});
        }
        if (has_rotate_transform) {
            commands.push_back(webscene_scene_command{20U, 0U, 0, 0, 0, 0, 0U, node.id});
        }
        if (has_scale_transform) {
            commands.push_back(webscene_scene_command{16U, 0U, 0, 0, 0, 0, 0U, node.id});
        }
        for (size_t filter_index = 0U;
             filter_index < foreground_filters.size(); ++filter_index) {
            commands.push_back(webscene_scene_command{
                31U, 0U, node.layout.x, node.layout.y,
                node.layout.width, node.layout.height, 0U, node.id});
        }
        if (mask_resource.has_value()) {
            commands.push_back(webscene_scene_command{
                47U, *mask_resource,
                node.layout.x, node.layout.y,
                node.layout.width, node.layout.height,
                0U, node.id});
            commands.push_back(webscene_scene_command{
                31U, 0U, node.layout.x, node.layout.y,
                node.layout.width, node.layout.height, 0U, node.id});
        }
        if (has_opacity_group) {
            commands.push_back(webscene_scene_command{
                31U, 0U, node.layout.x, node.layout.y,
                node.layout.width, node.layout.height, 0U, node.id});
        }
        return;
    }

    if (paint_self && node.tag == "path" && node.attributes.contains("d")) {
        const dom_node* svg = composed_parent(node);
        while (svg != nullptr && svg->tag != "svg") svg = composed_parent(*svg);
        if (svg != nullptr) {
            auto box = svg->layout;
            if ((box.width <= 0 || box.height <= 0) && composed_parent(*svg) != nullptr) {
                box = composed_parent(*svg)->layout;
            }
            const auto view_box = svg->attributes.contains("viewBox")
                ? svg->attributes.at("viewBox")
                : std::string("0 0 ") + std::to_string(std::max(1.0F, box.width))
                    + " " + std::to_string(std::max(1.0F, box.height));
            const auto preserve_aspect_ratio = svg->attributes.contains("preserveAspectRatio")
                ? svg->attributes.at("preserveAspectRatio")
                : std::string("xMidYMid meet");
            const auto transform = node.attributes.contains("transform")
                ? node.attributes.at("transform") : std::string{};
            const auto stroke_width = node.attributes.contains("stroke-width")
                ? node.attributes.at("stroke-width") : std::string("1");
            std::string resource = view_box + '\t' + preserve_aspect_ratio + '\t'
                + stroke_width + '\t'
                + transform + '\t' + node.attributes.at("d");
            const auto resource_index = append_scene_string(resource, strings, string_bytes);
            const auto fill = node.attributes.contains("fill")
                ? node.attributes.at("fill")
                : svg->attributes.contains("fill") ? svg->attributes.at("fill") : "currentColor";
            if (fill != "none") {
                const auto color = fill == "currentColor"
                    ? resolved_foreground(node)
                    : parse_color(fill);
                commands.push_back(webscene_scene_command{
                    4U,
                    resource_index,
                    box.x,
                    box.y,
                    box.width,
                    box.height,
                    with_opacity(color == 0 ? resolved_foreground(node) : color),
                    node.id,
                    0,
                    0,
                    0,
                    0,
                    0});
            }
            const auto stroke = node.attributes.contains("stroke")
                ? node.attributes.at("stroke")
                : svg->attributes.contains("stroke") ? svg->attributes.at("stroke") : std::string{};
            if (!stroke.empty() && stroke != "none") {
                const auto color = stroke == "currentColor"
                    ? resolved_foreground(node)
                    : parse_color(stroke);
                commands.push_back(webscene_scene_command{
                    5U,
                    resource_index,
                    box.x,
                    box.y,
                    box.width,
                    box.height,
                    with_opacity(color == 0 ? resolved_foreground(node) : color),
                    node.id,
                    0,
                    0,
                    0,
                    0,
                    0});
            }
        }
    }
    struct local_paint_entry final {
        const dom_node* child{};
        const node_style::pseudo_element* pseudo{};
        int z_index{};
        size_t source_order{};
    };
    static const std::vector<dom_node*> no_painted_children;
    const auto& children = node.style.content_visibility_hidden
        ? no_painted_children : composed_children(node);
    const auto paint_entry_count = children.size()
        + (before_paints_after_content ? 1U : 0U)
        + (!after_paints_behind_content ? 1U : 0U);
#if defined(WEBSCENE_NATIVE_ENGINE_SCENE_PAINT_ORDER_CONTROL)
    std::vector<local_paint_entry> paint_order;
    paint_order.reserve(paint_entry_count);
    const auto append_paint_entry = [&](local_paint_entry entry) {
        paint_order.push_back(entry);
    };
#else
    constexpr size_t inline_paint_capacity = 8U;
    std::array<local_paint_entry, inline_paint_capacity> inline_paint_order{};
    std::vector<local_paint_entry> spill_paint_order;
    const auto uses_inline_paint_order = paint_entry_count <= inline_paint_capacity;
    auto inline_paint_count = size_t{0U};
    if (!uses_inline_paint_order) spill_paint_order.reserve(paint_entry_count);
    const auto append_paint_entry = [&](local_paint_entry entry) {
        if (uses_inline_paint_order) {
            inline_paint_order[inline_paint_count++] = entry;
        } else {
            spill_paint_order.push_back(entry);
        }
    };
#endif
    if (before_paints_after_content) {
        append_paint_entry(local_paint_entry{
            nullptr,
            &node.style.before_pseudo(),
            node.style.before_pseudo().z_index,
            0U});
    }
    for (size_t index = 0; index < children.size(); ++index) {
        const auto* child = children[index];
        append_paint_entry(local_paint_entry{
            child,
            nullptr,
            child->paint_z_index,
            index + 1U});
    }
    if (!after_paints_behind_content) {
        append_paint_entry(local_paint_entry{
            nullptr,
            &node.style.after_pseudo(),
            node.style.after_pseudo().z_index,
            children.size() + 1U});
    }
#if defined(WEBSCENE_NATIVE_ENGINE_SCENE_PAINT_ORDER_CONTROL)
    auto ordered_paint = std::span<local_paint_entry>(paint_order);
#else
    auto ordered_paint = uses_inline_paint_order
        ? std::span<local_paint_entry>(inline_paint_order.data(), inline_paint_count)
        : std::span<local_paint_entry>(spill_paint_order);
#endif
    std::stable_sort(
        ordered_paint.begin(),
        ordered_paint.end(),
        [](const auto& left, const auto& right) {
            return left.z_index < right.z_index
                || (left.z_index == right.z_index
                    && left.source_order < right.source_order);
        });
    const auto& details_content = node.style.details_content_pseudo();
    const auto details_content_opacity = details_content_used_opacity(node);
    const auto uses_details_content = node.tag == "details"
        && details_content.present
        && !details_content_is_suppressed(node);
    const auto paints_details_content = uses_details_content
        && details_content.layout.width > 0
        && details_content.layout.height > 0;
    if (paints_details_content) {
        const auto border_width = resolve_length(
            node,
            details_content.border_inline_start_width,
            details_content.layout.width,
            0);
        if (border_width > 0.0F && details_content_opacity > 0.001F) {
            const auto border_x = resolved_right_to_left(node)
                ? details_content.layout.x + details_content.layout.width - border_width
                : details_content.layout.x;
            if (!details_content.border_image_value.empty()) {
                if (details_content_opacity < 0.999F) {
                    commands.push_back(webscene_scene_command{
                        30U, 0U,
                        border_x,
                        details_content.layout.y,
                        border_width,
                        details_content.layout.height,
                        static_cast<uint32_t>(std::clamp(
                            std::lround(details_content_opacity * 255.0F),
                            0L,
                            255L)),
                        node.id});
                }
                auto resource = std::string{"webscene-bg-v2\t"};
                resource += details_content.border_image_value;
                resource += "\tno-repeat\t0% 0%\t100% 100%";
                commands.push_back(webscene_scene_command{
                    21U,
                    append_scene_string(resource, strings, string_bytes),
                    border_x,
                    details_content.layout.y,
                    border_width,
                    details_content.layout.height,
                    0U,
                    node.id});
                if (details_content_opacity < 0.999F) {
                    commands.push_back(webscene_scene_command{
                        31U, 0U,
                        border_x,
                        details_content.layout.y,
                        border_width,
                        details_content.layout.height,
                        0U,
                        node.id});
                }
            } else {
            auto border_color = details_content.border_inline_start_current_color
                ? resolved_foreground(node)
                : details_content.border_inline_start_rgba;
            const auto alpha = static_cast<uint32_t>(std::clamp(
                std::lround(static_cast<float>(border_color & 0xFFU)
                    * details_content_opacity),
                0L,
                255L));
            border_color = (border_color & 0xFFFFFF00U) | alpha;
            commands.push_back(webscene_scene_command{
                2U,
                0U,
                border_x,
                details_content.layout.y,
                border_width,
                details_content.layout.height,
                with_opacity(border_color),
                node.id});
            }
        }
    }
    for (const auto& entry : ordered_paint) {
        if (entry.child != nullptr) {
            if (pseudo_only) continue;
            if (defer_fixed_descendants
                && entry.child->style.position == position_mode::fixed) {
                continue;
            }
            if (paint_target != nullptr) {
                auto* branch = paint_target;
                while (branch != nullptr && composed_parent(*branch) != &node) {
                    branch = composed_parent(*branch);
                }
                if (entry.child != branch) continue;
            } else if (entry.child->style.z_index > 0) {
                // Emitted by the nearest stacking scope below, without also
                // repainting the z-index:auto ancestors' own content.
                continue;
            }
            const auto details_content_child = uses_details_content
                && is_details_content_child(node, *entry.child);
            if (details_content_child && details_content_opacity <= 0.001F) continue;
            const auto clips_details_content = details_content_child
                && details_content.overflow_hidden;
            if (clips_details_content) {
                commands.push_back(webscene_scene_command{
                    12U, 0U,
                    details_content.layout.x,
                    details_content.layout.y,
                    details_content.layout.width,
                    details_content.layout.height,
                    0U,
                    node.id});
            }
            const auto details_opacity_group = details_content_child
                && details_content_opacity < 0.999F;
            if (details_opacity_group) {
                commands.push_back(webscene_scene_command{
                    30U,
                    0U,
                    entry.child->layout.x,
                    entry.child->layout.y,
                    entry.child->layout.width,
                    entry.child->layout.height,
                    static_cast<uint32_t>(std::clamp(
                        std::lround(details_content_opacity * 255.0F),
                        0L,
                        255L)),
                    node.id});
            }
            append_scene(
                *entry.child,
                commands,
                strings,
                string_bytes,
                visibility_hidden,
                defer_fixed_descendants,
                true,
                paint_target,
                paint_pseudo_target, ordered_canvas, false, capture_gpu_outputs);
            if (details_opacity_group) {
                commands.push_back(webscene_scene_command{
                    31U, 0U,
                    entry.child->layout.x,
                    entry.child->layout.y,
                    entry.child->layout.width,
                    entry.child->layout.height,
                    0U,
                    node.id});
            }
            if (clips_details_content) {
                commands.push_back(webscene_scene_command{
                    13U, 0U, 0, 0, 0, 0, 0U, node.id});
            }
        } else if (entry.pseudo != nullptr) {
            if (!pseudo_only && entry.z_index > 0) continue;
            append_pseudo(*entry.pseudo);
        }
    }
    if (owns_stacking_scope) {
        std::vector<local_paint_entry> elevated_paint;
        const auto collect = [&](const auto& self, const dom_node& current) -> void {
            if (&current != &node) {
                if (current.style.z_index > 0) {
                    elevated_paint.push_back({&current,nullptr,current.style.z_index,elevated_paint.size()});
                    return;
                }
                if (style_establishes_atomic_stacking_context(current) || current.contains_retained_canvas) return;
            }
            if (current.style.content_visibility_hidden) return;
            const auto add_pseudo = [&](const node_style::pseudo_element& pseudo) {
                if (pseudo.generated && !pseudo.display_none && pseudo.z_index > 0) {
                    elevated_paint.push_back({&current,&pseudo,pseudo.z_index,elevated_paint.size()});
                }
            };
            add_pseudo(current.style.before_pseudo());
            for (const auto* child : composed_children(current)) self(self,*child);
            add_pseudo(current.style.after_pseudo());
        };
        collect(collect,node);
        std::stable_sort(elevated_paint.begin(), elevated_paint.end(), [](const auto& a, const auto& b) {
            return a.z_index < b.z_index;
        });
        for (const auto& entry : elevated_paint) {
            const auto* target = entry.child;
            if (target == &node) { append_pseudo(*entry.pseudo); continue; }
            if (defer_fixed_descendants && target->style.position == position_mode::fixed) continue;
            const auto* branch = target;
            while (branch != nullptr && composed_parent(*branch) != &node) branch = composed_parent(*branch);
            if (branch == nullptr) continue;
            // Walk only this branch. Ancestors retain overflow clips and
            // visibility inheritance but do not duplicate backgrounds or SVGs.
            append_scene(*branch, commands, strings, string_bytes,
                visibility_hidden, defer_fixed_descendants, true, target, entry.pseudo, ordered_canvas, false, capture_gpu_outputs);
        }
    }
    if (clip_contents) {
        commands.push_back(webscene_scene_command{13U, 0U, 0, 0, 0, 0, 0U, node.id});
    }
    // Scroll geometry is a DOM/CSS contract, while the presence of an actual
    // scrollbar is a host-renderer contract. Emit lightweight overlay rails
    // after the overflow clip so they remain visible above the clipped content.
    // They do not consume clientWidth/clientHeight, matching overlay scrollbars
    // on the certified macOS host.
    if (paint_self && !node.style.scrollbar_hidden
        && node.layout.width > 0 && node.layout.height > 0) {
        const auto border_left = resolve_length(
            node, node.style.border_left_width, node.layout.width, 0);
        const auto border_right = resolve_length(
            node, node.style.border_right_width, node.layout.width, 0);
        const auto border_top = resolve_length(
            node, node.style.border_top_width, node.layout.height, 0);
        const auto border_bottom = resolve_length(
            node, node.style.border_bottom_width, node.layout.height, 0);
        const auto padding_left = resolve_length(
            node, node.style.padding_left, node.layout.width, 0);
        const auto padding_right = resolve_length(
            node, node.style.padding_right, node.layout.width, 0);
        const auto padding_top = resolve_vertical_padding(
            node, node.style.padding_top, node.layout.height, 0);
        const auto padding_bottom = resolve_vertical_padding(
            node, node.style.padding_bottom, node.layout.height, 0);
        const auto viewport_x = node.layout.x + border_left + padding_left;
        const auto viewport_y = node.layout.y + border_top + padding_top;
        const auto viewport_width = std::max(
            0.0F,
            node.layout.width - border_left - border_right - padding_left - padding_right);
        const auto viewport_height = std::max(
            0.0F,
            node.layout.height - border_top - border_bottom - padding_top - padding_bottom);
        const auto snap_size_to_css_pixel = [](float size, float origin) {
            return std::round(origin + size) - std::round(origin);
        };
        const auto snapped_viewport_width = snap_size_to_css_pixel(
            node.scroll_viewport_width, viewport_x);
        const auto snapped_viewport_height = snap_size_to_css_pixel(
            node.scroll_viewport_height, viewport_y);
        const auto snapped_content_width = snap_size_to_css_pixel(
            node.scroll_content_width, viewport_x);
        const auto snapped_content_height = snap_size_to_css_pixel(
            node.scroll_content_height, viewport_y);
        const auto maximum_x = std::max(
            0.0F, snapped_content_width - snapped_viewport_width);
        const auto maximum_y = std::max(
            0.0F, snapped_content_height - snapped_viewport_height);
        const auto& scrollbar = node.style.scrollbar();
        const auto append_overlay_rect = [&](float x, float y, float width, float height,
                                             uint32_t rgba, float radius) {
            if (width <= 0 || height <= 0) return;
            commands.push_back(webscene_scene_command{
                radius > 0 ? 10U : 9U,
                0U,
                x,
                y,
                width,
                height,
                rgba,
                node.id,
                radius,
                radius,
                radius,
                radius,
                0});
        };
        const auto root_viewport_scroll_x = node.parent == nullptr
            && node.style.overflow_x == overflow_mode::visible;
        const auto root_viewport_scroll_y = node.parent == nullptr
            && node.style.overflow_y == overflow_mode::visible;
        const auto vertical_thickness = scrollbar.width;
        const auto horizontal_thickness = scrollbar.height;
        const auto inset = scrollbar.overlay_inset;
        const auto paints_vertical =
            (node.style.scroll_y_enabled || root_viewport_scroll_y) && maximum_y > 0.01F
            && vertical_thickness > 0
            && viewport_width >= vertical_thickness + inset * 2
            && viewport_height > 0;
        const auto paints_horizontal =
            (node.style.scroll_x_enabled || root_viewport_scroll_x) && maximum_x > 0.01F
            && horizontal_thickness > 0
            && viewport_height >= horizontal_thickness + inset * 2
            && viewport_width > 0;
        if (paints_vertical) {
            const auto rail_x = viewport_x + viewport_width - vertical_thickness - inset;
            const auto rail_y = viewport_y + inset;
            const auto rail_height = std::max(0.0F, viewport_height - inset * 2);
            const auto thumb_height = std::min(
                rail_height,
                std::max(
                    scrollbar.thumb_min_height,
                    rail_height * snapped_viewport_height
                        / std::max(snapped_viewport_height, snapped_content_height)));
            const auto thumb_travel = std::max(0.0F, rail_height - thumb_height);
            const auto thumb_y = rail_y + thumb_travel
                * std::clamp(node.scroll_top / maximum_y, 0.0F, 1.0F);
            append_overlay_rect(rail_x, rail_y, vertical_thickness, rail_height,
                scrollbar.track_rgba, scrollbar.track_radius);
            const auto border = std::min(scrollbar.thumb_border_width,
                std::min(vertical_thickness, thumb_height) * 0.5F);
            append_overlay_rect(rail_x + border, thumb_y + border,
                vertical_thickness - border * 2, thumb_height - border * 2,
                scrollbar.thumb_rgba, std::max(0.0F, scrollbar.thumb_radius - border));
        }
        if (paints_horizontal) {
            const auto rail_x = viewport_x + inset;
            const auto rail_y = viewport_y + viewport_height - horizontal_thickness - inset;
            const auto rail_width = std::max(0.0F, viewport_width - inset * 2);
            const auto thumb_width = std::min(
                rail_width,
                std::max(
                    18.0F,
                    rail_width * snapped_viewport_width
                        / std::max(snapped_viewport_width, snapped_content_width)));
            const auto thumb_travel = std::max(0.0F, rail_width - thumb_width);
            const auto thumb_x = rail_x + thumb_travel
                * std::clamp(node.scroll_left / maximum_x, 0.0F, 1.0F);
            append_overlay_rect(rail_x, rail_y, rail_width, horizontal_thickness,
                scrollbar.track_rgba, scrollbar.track_radius);
            const auto border = std::min(scrollbar.thumb_border_width,
                std::min(horizontal_thickness, thumb_width) * 0.5F);
            append_overlay_rect(thumb_x + border, rail_y + border,
                thumb_width - border * 2, horizontal_thickness - border * 2,
                scrollbar.thumb_rgba, std::max(0.0F, scrollbar.thumb_radius - border));
        }
    }
    if (effect_clip.has_value()) {
        commands.push_back(webscene_scene_command{
            13U, 0U, 0, 0, 0, 0, 0U, node.id});
    }
    if (has_rotate_transform) {
        commands.push_back(webscene_scene_command{20U, 0U, 0, 0, 0, 0, 0U, node.id});
    }
    if (has_scale_transform) {
        commands.push_back(webscene_scene_command{16U, 0U, 0, 0, 0, 0, 0U, node.id});
    }
    for (size_t filter_index = 0U;
         filter_index < foreground_filters.size(); ++filter_index) {
        commands.push_back(webscene_scene_command{
            31U, 0U, node.layout.x, node.layout.y,
            node.layout.width, node.layout.height, 0U, node.id});
    }
    if (mask_resource.has_value()) {
        commands.push_back(webscene_scene_command{
            47U, *mask_resource,
            node.layout.x, node.layout.y,
            node.layout.width, node.layout.height,
            0U, node.id});
        commands.push_back(webscene_scene_command{
            31U, 0U, node.layout.x, node.layout.y,
            node.layout.width, node.layout.height, 0U, node.id});
    }
    if (has_opacity_group) {
        commands.push_back(webscene_scene_command{
            31U, 0U, node.layout.x, node.layout.y,
            node.layout.width, node.layout.height, 0U, node.id});
    }
}
