    static void dom_parser_parse(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        const auto xml = info.Length() > 0 ? to_utf8(info.GetIsolate(), info[0]) : std::string{};
        const auto mime_type = info.Length() > 1 ? to_utf8(info.GetIsolate(), info[1]) : std::string{};
        if (mime_type == "text/html") {
            if (!self->require_runtime_html()) return;
#if defined(WEBSCENE_NATIVE_ENGINE_HTML5EVER)
            auto& document_root = self->document.create_node(
                dom_node_kind::internal, "#document-root");
            document_root.visible = false;
            const auto parsed = parse_html_document(
                self->document, document_root, xml);
            self->html_parse_duration_ns += parsed.duration_ns;
            self->html_parse_error_count += parsed.parse_error_count;
            self->html_parse_callback_count += parsed.callback_count;
            self->html_parse_element_count += parsed.element_count;
            self->html_parse_text_append_count += parsed.text_append_count;
            self->html_parse_comment_count += parsed.comment_count;
            self->html_parse_doctype_count += parsed.doctype_count;
            self->html_parse_rust_allocation_count += parsed.rust_allocation_count;
            self->html_parse_rust_peak_bytes = std::max(
                self->html_parse_rust_peak_bytes, parsed.rust_peak_bytes);
            self->html_parse_rust_retained_bytes += parsed.rust_retained_bytes;
            auto* html = static_cast<dom_node*>(nullptr);
            for (auto* child : document_root.children) {
                if (child != nullptr && child->tag == "html") {
                    html = child;
                    break;
                }
            }
            if (!parsed || html == nullptr) {
                info.GetIsolate()->ThrowException(v8::Exception::Error(
                    js_string(info.GetIsolate(), "DOMParser HTML tree construction failed")));
                return;
            }
            self->activate_html5ever_subtree(*html, false);
            auto local_context = info.GetIsolate()->GetCurrentContext();
            self->detached_document_roots.insert(html->id);
            info.GetReturnValue().Set(wrap_detached_document(
                *self, *html, local_context, "about:blank"));
#else
            auto& html = self->document.create_element("html");
            auto& head = self->document.create_element("head");
            auto& body = self->document.create_element("body");
            self->document.append_child(html, head);
            self->document.append_child(html, body);
            self->parse_inner_html(body, xml);
            auto local_context = info.GetIsolate()->GetCurrentContext();
            self->detached_document_roots.insert(html.id);
            info.GetReturnValue().Set(wrap_detached_document(
                *self, html, local_context, "about:blank"));
#endif
            return;
        }
        auto& root = self->document.create_element("#document-root");
        root.visible = false;
        root.xml_mode = true;
        std::vector<dom_node*> stack{&root};
        size_t cursor = 0;
        size_t error_position = std::string::npos;
        std::string error_message;
        size_t root_element_count = 0;
        const auto skip_space = [&](size_t& position, size_t limit) {
            while (position < limit
                && std::isspace(static_cast<unsigned char>(xml[position]))) ++position;
        };
        const auto valid_name_start = [](unsigned char value) {
            return std::isalpha(value) || value == '_' || value == ':';
        };
        const auto valid_name_part = [&](unsigned char value) {
            return valid_name_start(value) || std::isdigit(value)
                || value == '-' || value == '.';
        };
        const auto fail = [&](size_t position, std::string message) {
            if (error_position == std::string::npos) {
                error_position = position;
                error_message = std::move(message);
            }
        };
        const auto append_text = [&](std::string value, bool cdata = false) {
            if (value.empty() || stack.empty()) return;
            if (stack.size() == 1U) {
                if (!std::all_of(value.begin(), value.end(), [](unsigned char character) {
                        return std::isspace(character);
                    })) {
                    fail(cursor, "text is not allowed outside the document element");
                }
                return;
            }
            auto& text_node = self->document.create_node(
                dom_node_kind::text, "#text");
            text_node.xml_mode = true;
            text_node.text_content = cdata
                ? std::move(value)
                : decode_html_text(std::move(value));
            self->document.append_child(*stack.back(), text_node);
        };
        while (cursor < xml.size() && error_position == std::string::npos) {
            const auto open = xml.find('<', cursor);
            if (open == std::string::npos) {
                append_text(xml.substr(cursor));
                cursor = xml.size();
                break;
            }
            if (open > cursor) append_text(xml.substr(cursor, open - cursor));
            if (error_position != std::string::npos) break;
            if (xml.compare(open, 4, "<!--") == 0) {
                const auto end = xml.find("-->", open + 4U);
                if (end == std::string::npos) {
                    fail(open, "unterminated comment");
                    break;
                }
                cursor = end + 3U;
                continue;
            }
            if (xml.compare(open, 9, "<![CDATA[") == 0) {
                if (stack.size() == 1U) {
                    fail(open, "CDATA is not allowed outside the document element");
                    break;
                }
                const auto end = xml.find("]]>", open + 9U);
                if (end == std::string::npos) {
                    fail(open, "unterminated CDATA section");
                    break;
                }
                append_text(xml.substr(open + 9U, end - open - 9U), true);
                cursor = end + 3U;
                continue;
            }
            if (xml.compare(open, 2, "<?") == 0) {
                const auto end = xml.find("?>", open + 2U);
                if (end == std::string::npos) {
                    fail(open, "unterminated processing instruction");
                    break;
                }
                cursor = end + 2U;
                continue;
            }
            const auto end = xml.find('>', open + 1U);
            if (end == std::string::npos) {
                fail(open, "unterminated markup");
                break;
            }
            if (xml.compare(open, 2, "<!") == 0) {
                cursor = end + 1U;
                continue;
            }
            if (xml.compare(open, 2, "</") == 0) {
                auto position = open + 2U;
                skip_space(position, end);
                const auto name_start = position;
                while (position < end
                    && valid_name_part(static_cast<unsigned char>(xml[position]))) ++position;
                const auto tag = xml.substr(name_start, position - name_start);
                skip_space(position, end);
                if (tag.empty() || position != end || stack.size() <= 1U
                    || stack.back()->tag != tag) {
                    fail(name_start, "mismatched closing tag");
                    break;
                }
                stack.pop_back();
                cursor = end + 1U;
                continue;
            }
            auto position = open + 1U;
            if (position >= end
                || !valid_name_start(static_cast<unsigned char>(xml[position]))) {
                fail(position, "invalid element name");
                break;
            }
            const auto name_start = position++;
            while (position < end
                && valid_name_part(static_cast<unsigned char>(xml[position]))) ++position;
            const auto tag = xml.substr(name_start, position - name_start);
            if (stack.size() == 1U && ++root_element_count > 1U) {
                fail(name_start, "multiple document elements");
                break;
            }
            auto& child = self->document.create_element(tag);
            child.xml_mode = true;
            child.set_namespace("");
            auto self_closing = false;
            while (position < end && error_position == std::string::npos) {
                skip_space(position, end);
                if (position >= end) break;
                if (xml[position] == '/') {
                    self_closing = true;
                    ++position;
                    skip_space(position, end);
                    if (position != end) fail(position, "unexpected content after '/'");
                    break;
                }
                if (!valid_name_start(static_cast<unsigned char>(xml[position]))) {
                    fail(position, "invalid attribute name");
                    break;
                }
                const auto attribute_start = position++;
                while (position < end
                    && valid_name_part(static_cast<unsigned char>(xml[position]))) ++position;
                const auto attribute_name = xml.substr(
                    attribute_start, position - attribute_start);
                skip_space(position, end);
                if (position >= end || xml[position] != '=') {
                    fail(position, "attribute value is required");
                    break;
                }
                ++position;
                skip_space(position, end);
                if (position >= end || (xml[position] != '\'' && xml[position] != '"')) {
                    fail(position, "attribute value must be quoted");
                    break;
                }
                const auto quote = xml[position++];
                const auto value_start = position;
                while (position < end && xml[position] != quote) ++position;
                if (position >= end) {
                    fail(value_start, "unterminated attribute value");
                    break;
                }
                auto attribute_value = decode_html_text(
                    xml.substr(value_start, position - value_start));
                ++position;
                child.attributes[attribute_name] = attribute_value;
                if (attribute_name == "xmlns") child.set_namespace(attribute_value);
            }
            if (error_position != std::string::npos) break;
            if (const auto id = child.attributes.find("id"); id != child.attributes.end()) {
                child.id_attribute = id->second;
            }
            if (const auto classes = child.attributes.find("class"); classes != child.attributes.end()) {
                child.class_name = classes->second;
            }
            self->document.append_child(*stack.back(), child);
            if (!self_closing) stack.push_back(&child);
            cursor = end + 1U;
        }
        if (error_position == std::string::npos && stack.size() != 1U) {
            fail(xml.size(), "unclosed element");
        }
        if (error_position == std::string::npos && root_element_count != 1U) {
            fail(0U, "document element is required");
        }
        auto* result_root = &root;
        if (error_position != std::string::npos) {
            auto& parser_root = self->document.create_element("#document-root");
            parser_root.visible = false;
            parser_root.xml_mode = true;
            auto& parser_error = self->document.create_element("parsererror");
            parser_error.xml_mode = true;
            parser_error.set_namespace("");
            auto& message = self->document.create_node(dom_node_kind::text, "#text");
            message.xml_mode = true;
            const auto line = static_cast<size_t>(std::count(
                xml.begin(), xml.begin() + std::min(error_position, xml.size()), '\n')) + 1U;
            const auto previous_newline = error_position == 0U
                ? std::string::npos
                : xml.rfind('\n', std::min(error_position, xml.size()) - 1U);
            const auto column = error_position
                - (previous_newline == std::string::npos ? 0U : previous_newline + 1U) + 1U;
            message.text_content = "XML Parsing Error: " + error_message
                + " at line " + std::to_string(line)
                + " column " + std::to_string(column);
            self->document.append_child(parser_error, message);
            self->document.append_child(parser_root, parser_error);
            result_root = &parser_root;
        }
        auto local_context = info.GetIsolate()->GetCurrentContext();
        self->detached_document_roots.insert(result_root->id);
        info.GetReturnValue().Set(wrap_detached_document(
            *self, *result_root, local_context, "about:blank"));
    }

    static void xml_serializer_serialize(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        if (self == nullptr || info.Length() < 1 || !info[0]->IsObject()) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(),
                    "XMLSerializer.serializeToString requires a Node")));
            return;
        }
        auto* node = unwrap_node(info[0].As<v8::Object>());
        if (node == nullptr) {
            info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                js_string(info.GetIsolate(),
                    "XMLSerializer.serializeToString requires a Node")));
            return;
        }
        self->record_feature(
            "dom",
            "XMLSerializer.serializeToString",
            "partially-supported",
            "detached XML/SVG document and element serialization",
            "native-binding");
        std::string serialized;
        if (node->tag.empty() || node->tag.front() == '#') {
            for (const auto* child : node->children) {
                if (child != nullptr) append_serialized_html(*child, serialized);
            }
        } else {
            append_serialized_html(*node, serialized);
        }
        info.GetReturnValue().Set(js_dom_string(info.GetIsolate(), serialized));
    }

    static void parsed_document_get_elements(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = info.Data()->IsExternal()
            ? static_cast<dom_node*>(info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault))
            : nullptr;
        if (root == nullptr) return;
        const auto tag = info.Length() > 0 ? to_utf8(info.GetIsolate(), info[0]) : std::string{};
        info.GetReturnValue().Set(self->selector_results(*root, tag));
    }

    static void parsed_document_query_selector_all(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = info.Data()->IsExternal()
            ? static_cast<dom_node*>(info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault))
            : nullptr;
        if (root == nullptr) return;
        const auto selector = info.Length() > 0 ? to_utf8(info.GetIsolate(), info[0]) : std::string{};
        info.GetReturnValue().Set(self->selector_results(*root, selector));
    }

    static void parsed_document_query_selector(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = info.Data()->IsExternal()
            ? static_cast<dom_node*>(info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault))
            : nullptr;
        if (root == nullptr) return;
        const auto selector = info.Length() > 0 ? to_utf8(info.GetIsolate(), info[0]) : std::string{};
        auto* match = self->query_selector_node(*root, selector, true);
        info.GetReturnValue().Set(match == nullptr
            ? v8::Local<v8::Value>(v8::Null(info.GetIsolate()))
            : v8::Local<v8::Value>(self->wrap_node(*match)));
    }

    static void return_this(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        info.GetReturnValue().Set(info.This());
    }

    static void set_dom_matrix_values(
        v8::Isolate* isolate,
        v8::Local<v8::Context> context,
        v8::Local<v8::Object> object,
        double a,
        double b,
        double c,
        double d,
        double e,
        double f)
    {
        const std::array<std::pair<const char*, double>, 12> values{{
            {"a", a}, {"b", b}, {"c", c}, {"d", d}, {"e", e}, {"f", f},
            {"m11", a}, {"m12", b}, {"m21", c}, {"m22", d},
            {"m41", e}, {"m42", f}}};
        for (const auto& [name, value] : values) {
            set_object_number(isolate, context, object, name, value);
        }
        const auto identity = std::abs(a - 1) < 0.0000001
            && std::abs(b) < 0.0000001 && std::abs(c) < 0.0000001
            && std::abs(d - 1) < 0.0000001
            && std::abs(e) < 0.0000001 && std::abs(f) < 0.0000001;
        object->Set(
            context,
            js_string(isolate, "isIdentity"),
            v8::Boolean::New(isolate, identity)).Check();
    }

    static v8::Local<v8::Object> clone_dom_matrix(
        v8::Isolate* isolate,
        v8::Local<v8::Context> context,
        v8::Local<v8::Object> source)
    {
        auto result = source->Clone(isolate);
        set_dom_matrix_values(
            isolate, context, result,
            object_number(isolate, context, source, "a", 1),
            object_number(isolate, context, source, "b", 0),
            object_number(isolate, context, source, "c", 0),
            object_number(isolate, context, source, "d", 1),
            object_number(isolate, context, source, "e", 0),
            object_number(isolate, context, source, "f", 0));
        result->Set(context, js_string(isolate, "is2D"), v8::True(isolate)).Check();
        return result;
    }

    static void translate_dom_matrix(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        v8::Local<v8::Object> target)
    {
        const auto isolate = info.GetIsolate();
        const auto context = isolate->GetCurrentContext();
        const auto x = info.Length() > 0
            ? info[0]->NumberValue(context).FromMaybe(0) : 0;
        const auto y = info.Length() > 1
            ? info[1]->NumberValue(context).FromMaybe(0) : 0;
        const auto a = object_number(isolate, context, target, "a", 1);
        const auto b = object_number(isolate, context, target, "b", 0);
        const auto c = object_number(isolate, context, target, "c", 0);
        const auto d = object_number(isolate, context, target, "d", 1);
        const auto e = object_number(isolate, context, target, "e", 0);
        const auto f = object_number(isolate, context, target, "f", 0);
        set_dom_matrix_values(
            isolate, context, target, a, b, c, d,
            a * x + c * y + e,
            b * x + d * y + f);
    }

    static void dom_matrix_translate(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto result = clone_dom_matrix(
            info.GetIsolate(), info.GetIsolate()->GetCurrentContext(), info.This());
        translate_dom_matrix(info, result);
        info.GetReturnValue().Set(result);
    }

    static void dom_matrix_translate_self(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        translate_dom_matrix(info, info.This());
        info.GetReturnValue().Set(info.This());
    }

    static void scale_dom_matrix(
        const v8::FunctionCallbackInfo<v8::Value>& info,
        v8::Local<v8::Object> target)
    {
        const auto isolate = info.GetIsolate();
        const auto context = isolate->GetCurrentContext();
        const auto scale_x = info.Length() > 0
            ? info[0]->NumberValue(context).FromMaybe(1)
            : 1;
        const auto scale_y = info.Length() > 1
            ? info[1]->NumberValue(context).FromMaybe(scale_x)
            : scale_x;
        const auto a = object_number(isolate, context, target, "a", 1) * scale_x;
        const auto b = object_number(isolate, context, target, "b", 0) * scale_x;
        const auto c = object_number(isolate, context, target, "c", 0) * scale_y;
        const auto d = object_number(isolate, context, target, "d", 1) * scale_y;
        set_dom_matrix_values(
            isolate, context, target, a, b, c, d,
            object_number(isolate, context, target, "e", 0),
            object_number(isolate, context, target, "f", 0));
    }

    static void dom_matrix_scale(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto result = clone_dom_matrix(
            info.GetIsolate(), info.GetIsolate()->GetCurrentContext(), info.This());
        scale_dom_matrix(info, result);
        info.GetReturnValue().Set(result);
    }

    static void dom_matrix_scale_self(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        scale_dom_matrix(info, info.This());
        info.GetReturnValue().Set(info.This());
    }

    static void install_dom_matrix_methods(
        v8::Isolate* isolate,
        v8::Local<v8::Context> context,
        v8::Local<v8::Object> object)
    {
        const char* passthrough_methods[] = {
            "multiply", "preMultiplySelf", "multiplySelf", "scale3d",
            "rotate", "rotateAxisAngle", "rotateSelf", "skewX", "skewY",
            "invertSelf", "inverse"};
        for (const auto* name : passthrough_methods) {
            object->Set(
                context,
                js_string(isolate, name),
                v8::Function::New(context, return_this).ToLocalChecked()).Check();
        }
        object->Set(context, js_string(isolate, "translate"),
            v8::Function::New(context, dom_matrix_translate).ToLocalChecked()).Check();
        object->Set(context, js_string(isolate, "translateSelf"),
            v8::Function::New(context, dom_matrix_translate_self).ToLocalChecked()).Check();
        object->Set(context, js_string(isolate, "scale"),
            v8::Function::New(context, dom_matrix_scale).ToLocalChecked()).Check();
        object->Set(context, js_string(isolate, "scaleSelf"),
            v8::Function::New(context, dom_matrix_scale_self).ToLocalChecked()).Check();
    }

    static void dom_matrix_constructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        auto result = info.IsConstructCall() ? info.This() : v8::Object::New(isolate);
        const char* ones[] = {"a", "d", "m11", "m22", "m33", "m44"};
        for (const auto* name : ones) {
            result->Set(local_context, js_string(isolate, name), v8::Number::New(isolate, 1)).Check();
        }
        const char* zeros[] = {"b", "c", "e", "f", "m12", "m13", "m14", "m21", "m23", "m24", "m31", "m32", "m34", "m41", "m42", "m43"};
        for (const auto* name : zeros) {
            result->Set(local_context, js_string(isolate, name), v8::Number::New(isolate, 0)).Check();
        }
        result->Set(local_context, js_string(isolate, "is2D"), v8::True(isolate)).Check();
        result->Set(local_context, js_string(isolate, "isIdentity"), v8::True(isolate)).Check();
        if (info.Length() > 0 && info[0]->IsArray()) {
            const auto values = info[0].As<v8::Array>();
            if (values->Length() >= 6) {
                std::array<double, 6> matrix{{1, 0, 0, 1, 0, 0}};
                for (uint32_t index = 0; index < matrix.size(); ++index) {
                    v8::Local<v8::Value> value;
                    if (values->Get(local_context, index).ToLocal(&value)) {
                        matrix[index] = value->NumberValue(local_context).FromMaybe(matrix[index]);
                    }
                }
                set_dom_matrix_values(
                    isolate, local_context, result,
                    matrix[0], matrix[1], matrix[2], matrix[3], matrix[4], matrix[5]);
            }
        } else if (info.Length() > 0 && info[0]->IsObject()) {
            const auto source = info[0].As<v8::Object>();
            set_dom_matrix_values(
                isolate, local_context, result,
                object_number(isolate, local_context, source, "a", 1),
                object_number(isolate, local_context, source, "b", 0),
                object_number(isolate, local_context, source, "c", 0),
                object_number(isolate, local_context, source, "d", 1),
                object_number(isolate, local_context, source, "e", 0),
                object_number(isolate, local_context, source, "f", 0));
        }
        install_dom_matrix_methods(isolate, local_context, result);
        info.GetReturnValue().Set(result);
    }

    static void dom_rect_constructor(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto isolate = info.GetIsolate();
        auto context = isolate->GetCurrentContext();
        auto result = info.IsConstructCall() ? info.This() : v8::Object::New(isolate);
        const auto number = [&](int index) {
            return index < info.Length() && info[index]->IsNumber()
                ? info[index]->NumberValue(context).FromMaybe(0.0)
                : 0.0;
        };
        const auto x = number(0), y = number(1), width = number(2), height = number(3);
        const auto set = [&](const char* name, double value) {
            result->Set(context, js_string(isolate, name),
                v8::Number::New(isolate, value)).Check();
        };
        set("x", x); set("y", y); set("width", width); set("height", height);
        set("left", std::min(x, x + width));
        set("right", std::max(x, x + width));
        set("top", std::min(y, y + height));
        set("bottom", std::max(y, y + height));
        info.GetReturnValue().Set(result);
    }

    static layout_rect transformed_client_bounds(
        const native_document& document,
        const dom_node& node)
    {
        struct point final { double x; double y; };
        std::array<point, 4> corners{{
            {node.layout.x, node.layout.y},
            {node.layout.x + node.layout.width, node.layout.y},
            {node.layout.x + node.layout.width, node.layout.y + node.layout.height},
            {node.layout.x, node.layout.y + node.layout.height}}};
        const auto resolve_origin = [](css_length value, float available) {
            if (value.unit == length_unit::percent) {
                return static_cast<double>(available) * value.value / 100.0
                    + value.pixel_offset;
            }
            return static_cast<double>(value.value);
        };
        constexpr double degrees_to_radians = 0.017453292519943295;
        for (auto* current = &node; current != nullptr;
             current = document.composed_parent(*current)) {
            const auto origin_x = static_cast<double>(current->layout.x)
                + resolve_origin(current->style.transform_origin_x, current->layout.width);
            const auto origin_y = static_cast<double>(current->layout.y)
                + resolve_origin(current->style.transform_origin_y, current->layout.height);
            const auto radians = static_cast<double>(
                current->painted_transform_rotation_value()) * degrees_to_radians;
            const auto cosine = std::cos(radians);
            const auto sine = std::sin(radians);
            for (auto& corner : corners) {
                const auto scaled_x = (corner.x - origin_x)
                    * current->painted_transform_scale_x_value();
                const auto scaled_y = (corner.y - origin_y)
                    * current->painted_transform_scale_y_value();
                corner.x = origin_x + scaled_x * cosine - scaled_y * sine;
                corner.y = origin_y + scaled_x * sine + scaled_y * cosine;
            }
        }
        auto left = corners[0].x;
        auto right = corners[0].x;
        auto top = corners[0].y;
        auto bottom = corners[0].y;
        for (const auto& corner : corners) {
            left = std::min(left, corner.x);
            right = std::max(right, corner.x);
            top = std::min(top, corner.y);
            bottom = std::max(bottom, corner.y);
        }
        return {
            static_cast<float>(left),
            static_cast<float>(top),
            static_cast<float>(right - left),
            static_cast<float>(bottom - top)};
    }

    static void get_bounding_client_rect(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(
            self->profile(binding_category::dom_geometry),
            self->profile_named("Element.getBoundingClientRect"));
        auto* node = unwrap_node(info.This());
        self->ensure_layout(node);
        if (node == nullptr) return;
        const auto has_no_box = !self->is_connected(*node) || [self](const dom_node* candidate) {
            for (auto* current = candidate; current != nullptr;
                 current = self->document.composed_parent(*current)) {
                if (current->style.display == display_mode::none) return true;
            }
            return false;
        }(node);
        // The native tree keeps the live HTML document element beneath the
        // viewport-bearing BODY root.  Geometry exposed to script must match
        // the browser box for that live document element, not the internal
        // zero-height wrapper used by the collapsed native document model.
        if (node->tag == "html" && node->parent == &self->document.body()) {
            node = &self->document.body();
        }
        auto local_context = info.GetIsolate()->GetCurrentContext();
        auto result = v8::Object::New(info.GetIsolate());
        const auto set = [&](const char* name, double value) {
            result->Set(local_context, js_string(info.GetIsolate(), name), v8::Number::New(info.GetIsolate(), value)).Check();
        };
        const auto bounds = has_no_box
            ? layout_rect{}
            : transformed_client_bounds(self->document, *node);
        set("x", bounds.x);
        set("y", bounds.y);
        set("left", bounds.x);
        set("top", bounds.y);
        set("width", bounds.width);
        set("height", bounds.height);
        set("right", bounds.x + bounds.width);
        set("bottom", bounds.y + bounds.height);
        info.GetReturnValue().Set(result);
    }

    static void get_client_rects(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(
            self->profile(binding_category::dom_geometry),
            self->profile_named("Element.getClientRects"));
        auto* node = unwrap_node(info.This());
        self->ensure_layout(node);
        if (node == nullptr) return;
        if (!self->is_connected(*node)) {
            info.GetReturnValue().Set(v8::Array::New(info.GetIsolate()));
            return;
        }
        for (auto* current = node; current != nullptr;
             current = self->document.composed_parent(*current)) {
            if (current->style.display != display_mode::none) continue;
            info.GetReturnValue().Set(v8::Array::New(info.GetIsolate()));
            return;
        }
        if (node->tag == "html" && node->parent == &self->document.body()) {
            node = &self->document.body();
        }
        auto local_context = info.GetIsolate()->GetCurrentContext();
        auto rect = v8::Object::New(info.GetIsolate());
        const auto set = [&](const char* name, double value) {
            rect->Set(local_context, js_string(info.GetIsolate(), name), v8::Number::New(info.GetIsolate(), value)).Check();
        };
        const auto bounds = transformed_client_bounds(self->document, *node);
        set("x", bounds.x);
        set("y", bounds.y);
        set("left", bounds.x);
        set("top", bounds.y);
        set("width", bounds.width);
        set("height", bounds.height);
        set("right", bounds.x + bounds.width);
        set("bottom", bounds.y + bounds.height);
        auto result = v8::Array::New(info.GetIsolate(), 1);
        result->Set(local_context, 0, rect).Check();
        info.GetReturnValue().Set(result);
    }

    static std::string normalized_dom_attribute_name(
        const dom_node& node,
        std::string raw_name)
    {
        // The DOM lowercases attribute names only for HTML elements in an
        // HTML document. Foreign SVG attributes remain case-sensitive, so
        // canonical names such as viewBox must survive both lookup and
        // mutation.
        return !node.xml_mode
                && node.namespace_uri() == dom_node::html_namespace_uri
            ? lower_html_name(raw_name)
            : std::move(raw_name);
    }

    static std::pair<std::string, std::string> split_qualified_attribute_name(
        std::string qualified_name)
    {
        const auto colon = qualified_name.find(':');
        auto local_name = colon == std::string::npos
            ? qualified_name
            : qualified_name.substr(colon + 1U);
        return {std::move(qualified_name), std::move(local_name)};
    }

    static std::optional<std::string> input_live_value_before_attribute_change(
        dom_node& node, std::string_view name)
    {
        if (node.tag != "input" || name != "value") return std::nullopt;
        forms::ensure_text_value(node);
        return node.form_control().value;
    }

    void finish_input_value_attribute_change(
        dom_node& node,
        const std::optional<std::string>& previous_live_value,
        const std::optional<std::string>& current_default_value)
    {
        if (!previous_live_value.has_value()
            || node.form_control().dirty_value) return;
        auto& control = node.mutable_form_control();
        control.value = forms::sanitize_programmatic_numeric_value(
            node,current_default_value.value_or(std::string{}));
        control.value_initialized = true;
        control.selection_start = control.value.size();
        control.selection_end = control.value.size();
        control.selection_start_utf16_suboffset = 0U;
        control.selection_end_utf16_suboffset = 0U;
        control.selection_direction = text_selection_direction::none;
        if (is_connected(node)) {
            recascade_form_value_selector_transition(
                node, *previous_live_value, control.value);
        }
    }

    static void set_attribute(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(
            self->profile(binding_category::dom_mutation),
            self->profile_named("Element.setAttribute"));
        auto* node = unwrap_node(info.This());
        if (node == nullptr || info.Length() < 2) return;
        const auto raw_name = to_utf8(info.GetIsolate(), info[0]);
        const auto name = normalized_dom_attribute_name(*node, raw_name);
        const auto value = to_wtf8(info.GetIsolate(), info[1]);
        const auto previous = node->attributes.find(name);
        const std::optional<std::string> old_value = previous == node->attributes.end()
            ? std::nullopt
            : std::optional<std::string>(previous->second);
        const auto radio_group_state=
            self->radio_group_attribute_may_change_membership(*node,name)
            ? self->capture_radio_group_selector_state()
            : std::vector<radio_group_selector_state>{};
        const auto previous_live_value = input_live_value_before_attribute_change(
            *node, name);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (self->resize_profile_active) {
            ++self->last_resize_attribute_counts[name];
        }
#endif
        self->record_html_attribute_feature(name, "setAttribute");
        if (name == "class"
            && node->class_name == value
            && node->attributes.contains("class")) {
            self->notify_custom_element_attribute(*node, name, old_value, value);
            return;
        }
        if (name == "id"
            && node->id_attribute == value
            && node->attributes.contains("id")) {
            self->notify_custom_element_attribute(*node, name, old_value, value);
            return;
        }
        if ((name.starts_with("data-") || name.starts_with("aria-"))
            && previous != node->attributes.end()
            && previous->second == value) {
            self->notify_custom_element_attribute(*node, name, old_value, value);
            return;
        }
        node->attributes[name] = value;
        auto iframe_navigation_allowed = true;
        if (name == "src" && node->tag == "iframe") {
            iframe_navigation_allowed = self->begin_iframe_navigation(*node);
        }
        if (node->tag == "base" && name == "href") {
            self->invalidate_document_base(*node);
        }
        if ((name == "width" || name == "height")) reset_canvas_backing_store(*self, *node);
        if (name == "id") node->id_attribute = value;
        else if (name == "class") node->class_name = value;
        else if (name == "style") {
            node->clear_authored_style();
            node->style.clear_grid();
            node->style.clear_table();
            node->style.inline_property_mask = 0U;
            node->style.important_property_mask = 0U;
            uint64_t inline_property_mask = 0U;
            for (const auto& declaration : parse_css_declarations(value, true)) {
                if (!store_inline_declaration(*node, declaration)) continue;
                self->apply_css_declaration(*node, declaration, true);
                inline_property_mask |= inline_style_mask(declaration.name);
            }
            node->style.inline_property_mask |= inline_property_mask;
        }
        else if (name == "src" && node->tag == "iframe") {
            if (iframe_navigation_allowed) {
                self->enqueue_iframe_hydration_if_needed(*node);
            }
        }
        else if (name == "src" && node->tag == "img") {
            node->clear_replaced_image();
            self->enqueue_connected_resource_if_needed(
                *node,
                info.This(),
                info.GetIsolate()->GetCurrentContext());
        }
        if (self->attribute_requires_recascade(name)) {
            if (name == "class") {
                self->recascade_class_selector_transition(
                    *node, old_value, value);
            } else {
                const auto trace_reason =
                    std::getenv("WEBSCENE_TRACE_RECASCADE_ROOTS") != nullptr
                    ? std::string{"setAttribute:"} + name
                    : std::string{"setAttribute"};
                // Attribute selectors in the rightmost compound affect only
                // the mutated subject. Keep a subtree recascade for selectors
                // which use this attribute on an ancestor/sibling, and for
                // attributes whose semantics can inherit independently of a
                // selector. OverlayScrollbars updates its viewport data
                // attribute on every resize; treating that subject-only state
                // as a whole content-tree invalidation dominated the frame.
                if (name == "style" || name == "dir"
                    || name == "lang") {
                    self->recascade_connected_subtree(*node, trace_reason);
                } else {
                    self->recascade_attribute_selector_transition(
                        *node, name, old_value, value);
                }
            }
        } else {
            self->document.mark_dirty();
        }
        self->finish_input_value_attribute_change(
            *node, previous_live_value, value);
        self->recascade_radio_group_selector_state_changes(radio_group_state);
        self->recascade_form_attribute_dependencies(
            *node, name, old_value, value);
        if(node->tag=="option" && (name=="selected" || name=="value"))
            self->recascade_containing_select_validity(*node);
        if(validation_constraint_attribute(name))
            self->dismiss_validation_message_after_constraint_mutation(*node);
        else self->reconcile_validation_message_eligibility(*node);
        self->notify_custom_element_attribute(*node, name, old_value, value);
    }

    static void set_attribute_ns(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (info.Length() < 3) return;
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.This());
        if (node == nullptr) return;
        const auto raw_name = to_utf8(info.GetIsolate(), info[1]);
        auto [qualified_name, name] = split_qualified_attribute_name(
            normalized_dom_attribute_name(*node, raw_name));
        const std::optional<std::string> namespace_uri =
            info[0]->IsNullOrUndefined()
                ? std::nullopt
                : std::optional<std::string>(to_wtf8(info.GetIsolate(), info[0]));
        const auto value = to_wtf8(info.GetIsolate(), info[2]);
        const auto previous = node->attributes.find_expanded(
            namespace_uri.value_or(std::string{}), name);
        const std::optional<std::string> old_value = previous == node->attributes.end()
            ? std::nullopt
            : std::optional<std::string>(previous->second);
        const auto radio_group_state=(!namespace_uri || namespace_uri->empty())
            && self->radio_group_attribute_may_change_membership(*node,name)
            ? self->capture_radio_group_selector_state()
            : std::vector<radio_group_selector_state>{};
        const auto previous_live_value = (!namespace_uri || namespace_uri->empty())
            ? input_live_value_before_attribute_change(*node, name)
            : std::nullopt;
        self->record_html_attribute_feature(name, "setAttributeNS");
        node->attributes.set_namespaced(
            std::move(qualified_name), name,
            namespace_uri.value_or(std::string{}), value);
        if ((!namespace_uri || namespace_uri->empty())
            && node->tag == "base" && name == "href") {
            self->invalidate_document_base(*node);
        }
        if ((!namespace_uri || namespace_uri->empty()) && (name == "width" || name == "height")) reset_canvas_backing_store(*self, *node);
        if ((!namespace_uri || namespace_uri->empty()) && name == "id") {
            node->id_attribute = value;
        } else if ((!namespace_uri || namespace_uri->empty()) && name == "class") {
            node->class_name = value;
        }
        if (self->attribute_requires_recascade(name)) {
            if ((!namespace_uri || namespace_uri->empty()) && name == "class") {
                self->recascade_class_selector_transition(*node, old_value, value);
            } else if ((!namespace_uri || namespace_uri->empty()) && name == "id") {
                self->recascade_attribute_selector_transition(*node, name, old_value, value);
            } else {
                self->recascade_connected_subtree(*node, "style.removeProperty-custom");
            }
        } else {
            self->document.mark_dirty();
        }
        self->finish_input_value_attribute_change(
            *node, previous_live_value, value);
        self->recascade_radio_group_selector_state_changes(radio_group_state);
        if(!namespace_uri || namespace_uri->empty())
            self->recascade_form_attribute_dependencies(
                *node, name, old_value, value);
        if((!namespace_uri || namespace_uri->empty()) && node->tag=="option"
            && (name=="selected" || name=="value"))
            self->recascade_containing_select_validity(*node);
        if(!namespace_uri || namespace_uri->empty()) {
            if(validation_constraint_attribute(name))
                self->dismiss_validation_message_after_constraint_mutation(*node);
            else self->reconcile_validation_message_eligibility(*node);
        }
        self->notify_custom_element_attribute(
            *node, name, old_value, value, namespace_uri);
    }

    static void remove_attribute(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.This());
        if (node == nullptr || info.Length() < 1) return;
        const auto raw_name = to_utf8(info.GetIsolate(), info[0]);
        const auto name = normalized_dom_attribute_name(*node, raw_name);
        self->record_html_attribute_feature(name, "removeAttribute");
        if (!node->attributes.contains(name)) {
            return;
        }
        const auto old_value = std::optional<std::string>(node->attributes.at(name));
        const auto radio_group_state=
            self->radio_group_attribute_may_change_membership(*node,name)
            ? self->capture_radio_group_selector_state()
            : std::vector<radio_group_selector_state>{};
        const auto previous_live_value = input_live_value_before_attribute_change(
            *node, name);
        self->detach_attr(*node, name, *old_value);
        node->attributes.erase(name);
        if (node->tag == "base" && name == "href") {
            self->invalidate_document_base(*node);
        }
        if ((name == "width" || name == "height")) reset_canvas_backing_store(*self, *node);
        if (name == "id") node->id_attribute.clear();
        else if (name == "class") node->class_name.clear();
        else if (name == "src" && node->tag == "img") {
            node->clear_replaced_image();
        }
        else if (name == "style") {
            node->clear_authored_style();
            node->style.inline_property_mask = 0U;
            node->style.important_property_mask = 0U;
        }
        if (self->attribute_requires_recascade(name)) {
            if (name == "class") {
                self->recascade_class_selector_transition(*node, old_value, std::nullopt);
            } else if (name == "style" || name == "dir"
                || name == "lang") {
                self->recascade_connected_subtree(*node, "removeAttribute");
            } else {
                self->recascade_attribute_selector_transition(
                    *node, name, old_value, std::nullopt);
            }
        } else {
            self->document.mark_dirty();
        }
        self->finish_input_value_attribute_change(
            *node, previous_live_value, std::nullopt);
        self->recascade_radio_group_selector_state_changes(radio_group_state);
        self->recascade_form_attribute_dependencies(
            *node, name, old_value, std::nullopt);
        if(node->tag=="option" && (name=="selected" || name=="value"))
            self->recascade_containing_select_validity(*node);
        if(validation_constraint_attribute(name))
            self->dismiss_validation_message_after_constraint_mutation(*node);
        else self->reconcile_validation_message_eligibility(*node);
        self->notify_custom_element_attribute(
            *node, name, old_value, std::nullopt);
    }

    static void get_attribute(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_property));
        auto* node = unwrap_node(info.This());
        if (node == nullptr || info.Length() < 1) return;
        const auto raw_name = to_utf8(info.GetIsolate(), info[0]);
        const auto name = normalized_dom_attribute_name(*node, raw_name);
        const auto match = node->attributes.find(name);
        if (match == node->attributes.end()) {
            info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
        } else {
            info.GetReturnValue().Set(js_dom_string(info.GetIsolate(), match->second));
        }
    }

    static void get_attribute_ns(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_property));
        auto* node = unwrap_node(info.This());
        if (node == nullptr || info.Length() < 2) return;
        const auto raw_name = to_utf8(info.GetIsolate(), info[1]);
        const auto name = normalized_dom_attribute_name(*node, raw_name);
        const auto namespace_uri = info[0]->IsNullOrUndefined()
            ? std::string{}
            : to_wtf8(info.GetIsolate(), info[0]);
        const auto match = node->attributes.find_expanded(namespace_uri, name);
        if (match == node->attributes.end()) {
            info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
        } else {
            info.GetReturnValue().Set(js_dom_string(info.GetIsolate(), match->second));
        }
    }

    static void remove_attribute_ns(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.This());
        if (node == nullptr || info.Length() < 2) return;
        const auto raw_name = to_utf8(info.GetIsolate(), info[1]);
        const auto name = normalized_dom_attribute_name(*node, raw_name);
        const std::optional<std::string> namespace_uri =
            info[0]->IsNullOrUndefined()
                ? std::nullopt
                : std::optional<std::string>(to_wtf8(info.GetIsolate(), info[0]));
        self->record_html_attribute_feature(name, "removeAttributeNS");
        const auto match = node->attributes.find_expanded(
            namespace_uri.value_or(std::string{}), name);
        if (match == node->attributes.end()) return;
        const auto qualified_name = match->first;
        const auto old_value = std::optional<std::string>(match->second);
        const auto radio_group_state=(!namespace_uri || namespace_uri->empty())
            && self->radio_group_attribute_may_change_membership(*node,name)
            ? self->capture_radio_group_selector_state()
            : std::vector<radio_group_selector_state>{};
        const auto previous_live_value = (!namespace_uri || namespace_uri->empty())
            ? input_live_value_before_attribute_change(*node, name)
            : std::nullopt;
        self->detach_attr(*node, qualified_name, *old_value);
        node->attributes.erase(qualified_name);
        if ((!namespace_uri || namespace_uri->empty())
            && node->tag == "base" && name == "href") {
            self->invalidate_document_base(*node);
        }
        if ((!namespace_uri || namespace_uri->empty()) && (name == "width" || name == "height")) reset_canvas_backing_store(*self, *node);
        if ((!namespace_uri || namespace_uri->empty()) && name == "id") {
            node->id_attribute.clear();
        } else if ((!namespace_uri || namespace_uri->empty()) && name == "class") {
            node->class_name.clear();
        } else if ((!namespace_uri || namespace_uri->empty())
            && name == "src" && node->tag == "img") node->clear_replaced_image();
        else if ((!namespace_uri || namespace_uri->empty()) && name == "style") {
            node->clear_authored_style();
            node->style.inline_property_mask = 0U;
            node->style.important_property_mask = 0U;
        }
        if (self->attribute_requires_recascade(name)) {
            if ((!namespace_uri || namespace_uri->empty()) && name == "class") {
                self->recascade_class_selector_transition(*node, old_value, std::nullopt);
            } else if ((!namespace_uri || namespace_uri->empty())
                && (name == "style" || name == "dir"
                    || name == "lang")) {
                self->recascade_connected_subtree(*node, "removeAttributeNS");
            } else {
                self->recascade_attribute_selector_transition(
                    *node, name, old_value, std::nullopt);
            }
        } else {
            self->document.mark_dirty();
        }
        self->finish_input_value_attribute_change(
            *node, previous_live_value, std::nullopt);
        self->recascade_radio_group_selector_state_changes(radio_group_state);
        if(!namespace_uri || namespace_uri->empty())
            self->recascade_form_attribute_dependencies(
                *node,name,old_value,std::nullopt);
        if((!namespace_uri || namespace_uri->empty()) && node->tag=="option"
            && (name=="selected" || name=="value"))
            self->recascade_containing_select_validity(*node);
        if(!namespace_uri || namespace_uri->empty()) {
            if(validation_constraint_attribute(name))
                self->dismiss_validation_message_after_constraint_mutation(*node);
            else self->reconcile_validation_message_eligibility(*node);
        }
        self->notify_custom_element_attribute(
            *node, name, old_value, std::nullopt, namespace_uri);
    }

    static void toggle_attribute(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* node = unwrap_node(info.This());
        if (node == nullptr || info.Length() < 1) return;
        const auto raw_name = to_utf8(info.GetIsolate(), info[0]);
        const auto name = normalized_dom_attribute_name(*node, raw_name);
        const bool present = node->attributes.contains(name);
        const auto old_value = present
            ? std::optional<std::string>(node->attributes.at(name))
            : std::nullopt;
        const auto previous_live_value = input_live_value_before_attribute_change(
            *node, name);
        const auto radio_group_state=
            self->radio_group_attribute_may_change_membership(*node,name)
            ? self->capture_radio_group_selector_state()
            : std::vector<radio_group_selector_state>{};
        const bool force_present = info.Length() < 2 || info[1]->IsUndefined()
            ? !present
            : info[1]->BooleanValue(info.GetIsolate());
        if (force_present == present) {
            info.GetReturnValue().Set(present);
            return;
        }
        if (force_present) {
            node->attributes[name] = {};
            if (name == "width" || name == "height") reset_canvas_backing_store(*self, *node);
            if (name == "id") node->id_attribute.clear();
            else if (name == "class") node->class_name.clear();
        } else {
            node->attributes.erase(name);
            if (name == "width" || name == "height") reset_canvas_backing_store(*self, *node);
            if (name == "id") node->id_attribute.clear();
            else if (name == "class") node->class_name.clear();
        }
        if (self->attribute_requires_recascade(name)) {
            if (name == "class") {
                self->recascade_class_selector_transition(*node, old_value,
                    force_present ? std::optional<std::string>(std::string{}) : std::nullopt);
            } else if (name == "style" || name == "dir"
                || name == "lang") {
                self->recascade_connected_subtree(*node, "toggleAttribute");
            } else {
                self->recascade_attribute_selector_transition(
                    *node,
                    name,
                    old_value,
                    force_present
                        ? std::optional<std::string>(std::string{})
                        : std::nullopt);
            }
        } else {
            self->document.mark_dirty();
        }
        self->finish_input_value_attribute_change(
            *node,
            previous_live_value,
            force_present
                ? std::optional<std::string>(std::string{})
                : std::nullopt);
        self->recascade_radio_group_selector_state_changes(radio_group_state);
        self->recascade_form_attribute_dependencies(
            *node,
            name,
            old_value,
            force_present
                ? std::optional<std::string>(std::string{})
                : std::nullopt);
        if(node->tag=="option" && (name=="selected" || name=="value"))
            self->recascade_containing_select_validity(*node);
        if(validation_constraint_attribute(name))
            self->dismiss_validation_message_after_constraint_mutation(*node);
        else self->reconcile_validation_message_eligibility(*node);
        // Queue invalidation before author callbacks: they may synchronously
        // read styles or replace this attribute with another value.
        self->notify_custom_element_attribute(*node, name, old_value,
            force_present ? std::optional<std::string>(std::string{}) : std::nullopt);
        info.GetReturnValue().Set(force_present);
    }

    static void has_attribute(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        binding_callback_timer binding_timer(self->profile(binding_category::dom_property));
        auto* node = unwrap_node(info.This());
        const auto raw_name = node != nullptr && info.Length() > 0
            ? to_utf8(info.GetIsolate(), info[0])
            : std::string{};
        const auto name = node != nullptr
            ? normalized_dom_attribute_name(*node, raw_name)
            : raw_name;
        info.GetReturnValue().Set(v8::Boolean::New(
            info.GetIsolate(),
            node != nullptr && node->attributes.contains(name)));
    }

    static std::string extract_html_attribute(const std::string& html, const std::string& name)
    {
        const auto marker = name + "=";
        const auto position = html.find(marker);
        if (position == std::string::npos) return {};
        auto value_start = position + marker.size();
        if (value_start >= html.size()) return {};
        const auto quote = html[value_start];
        if (quote != '\'' && quote != '"') return {};
        ++value_start;
        const auto value_end = html.find(quote, value_start);
        return value_end == std::string::npos
            ? std::string{}
            : html.substr(value_start, value_end - value_start);
    }

    static std::vector<frame_script> parse_frame_scripts(const std::string& html)
    {
        std::vector<frame_script> result;
        size_t cursor = 0;
        size_t index = 0;
        while (true) {
            const auto open = html.find("<script", cursor);
            if (open == std::string::npos) break;
            const auto open_end = html.find('>', open);
            if (open_end == std::string::npos) break;
            const auto close = html.find("</script>", open_end + 1U);
            if (close == std::string::npos) break;
            const auto attributes = html.substr(open, open_end - open + 1U);
            auto type = lower_html_name(extract_html_attribute(attributes, "type"));
            const auto first = type.find_first_not_of(" \t\r\n\f");
            const auto last = type.find_last_not_of(" \t\r\n\f");
            type = first == std::string::npos
                ? std::string{}
                : type.substr(first, last - first + 1U);
            const auto executable = type.empty() || type == "module"
                || type == "text/javascript"
                || type == "application/javascript"
                || type == "application/ecmascript"
                || type == "text/ecmascript";
            if (executable) {
                result.push_back(frame_script{
                    extract_html_attribute(attributes, "src"),
                    html.substr(open_end + 1U, close - open_end - 1U),
                    extract_html_attribute(attributes, "nonce"),
                    attributes.find("defer") != std::string::npos || type == "module",
                    index,
                    open,
                    type == "module"});
            }
            ++index;
            cursor = close + 9U;
        }
        return result;
    }

    static frame_script_content_security_policy parse_frame_script_policy(
        const std::string& html)
    {
        frame_script_content_security_policy result;
        const auto normalized_html = lower_html_name(html);
        size_t cursor = 0U;
        while (true) {
            const auto open = normalized_html.find("<meta", cursor);
            if (open == std::string::npos) return result;
            const auto open_end = normalized_html.find('>', open + 5U);
            if (open_end == std::string::npos) return result;
            const auto opening = html.substr(open, open_end - open + 1U);
            std::string http_equiv;
            std::string content;
            for (auto [name, value] : parse_html_attributes(opening, 5U)) {
                name = lower_html_name(name);
                if (name == "http-equiv") http_equiv = lower_html_name(value);
                else if (name == "content") content = std::move(value);
            }
            const auto first = http_equiv.find_first_not_of(" \t\r\n\f");
            const auto last = http_equiv.find_last_not_of(" \t\r\n\f");
            http_equiv = first == std::string::npos
                ? std::string{}
                : http_equiv.substr(first, last - first + 1U);
            if (http_equiv != "content-security-policy") {
                cursor = open_end + 1U;
                continue;
            }

            result.present = true;
            result.effective_offset = open_end + 1U;
            std::vector<std::string> default_sources;
            bool found_default = false;
            bool found_script = false;
            size_t directive_start = 0U;
            while (directive_start <= content.size()) {
                const auto directive_end = content.find(';', directive_start);
                const auto directive = std::string_view(content).substr(
                    directive_start,
                    directive_end == std::string::npos
                        ? std::string::npos
                        : directive_end - directive_start);
                std::vector<std::string> tokens;
                for (size_t index = 0U; index < directive.size();) {
                    while (index < directive.size()
                        && std::isspace(static_cast<unsigned char>(directive[index])) != 0) {
                        ++index;
                    }
                    const auto token_start = index;
                    while (index < directive.size()
                        && std::isspace(static_cast<unsigned char>(directive[index])) == 0) {
                        ++index;
                    }
                    if (token_start != index) tokens.emplace_back(
                        directive.substr(token_start, index - token_start));
                }
                if (!tokens.empty()) {
                    const auto name = lower_html_name(tokens.front());
                    std::vector<std::string> sources(
                        std::next(tokens.begin()), tokens.end());
                    if (!found_script
                        && (name == "script-src-elem" || name == "script-src")) {
                        result.sources = std::move(sources);
                        result.restrict_scripts = true;
                        found_script = true;
                    } else if (!found_default && name == "default-src") {
                        default_sources = std::move(sources);
                        found_default = true;
                    }
                }
                if (directive_end == std::string::npos) break;
                directive_start = directive_end + 1U;
            }
            if (!found_script && found_default) {
                result.sources = std::move(default_sources);
                result.restrict_scripts = true;
            }
            return result;
        }
    }

    bool frame_script_policy_allows(
        const frame_script_content_security_policy& policy,
        const frame_script& script,
        const std::string& base_address)
    {
        if (!policy.present || !policy.restrict_scripts
            || script.document_offset < policy.effective_offset) {
            return true;
        }

        auto has_nonce_or_hash = false;
        auto allows_unsafe_inline = false;
        for (const auto& source : policy.sources) {
            const auto normalized = lower_html_name(source);
            has_nonce_or_hash = has_nonce_or_hash
                || normalized.starts_with("'nonce-")
                || normalized.starts_with("'sha256-")
                || normalized.starts_with("'sha384-")
                || normalized.starts_with("'sha512-");
            allows_unsafe_inline = allows_unsafe_inline
                || normalized == "'unsafe-inline'";
            if (!script.nonce.empty()
                && source == "'nonce-" + script.nonce + "'") {
                return true;
            }
        }
        if (script.source.empty()) {
            return allows_unsafe_inline && !has_nonce_or_hash;
        }

        const auto target = resolve_resource_url(script.source, base_address);
        const auto target_origin = resource_origin(target);
        const auto base_origin = resource_origin(base_address);
        for (const auto& source : policy.sources) {
            const auto normalized = lower_html_name(source);
            if (normalized == "'self'" && target_origin == base_origin) return true;
            if (normalized == "*" && !target.starts_with("data:")) return true;
            if (normalized == "'none'" || normalized.starts_with("'nonce-")
                || normalized.starts_with("'sha")) continue;
            if (normalized.ends_with(":") && normalized.find('/') == std::string::npos
                && target.starts_with(normalized)) return true;
            const auto wildcard = normalized.find("://*.");
            if (wildcard != std::string::npos) {
                const auto scheme = normalized.substr(0U, wildcard + 3U);
                const auto suffix = normalized.substr(wildcard + 4U);
                if (target_origin.starts_with(scheme)) {
                    const auto host = target_origin.substr(scheme.size());
                    if (host.size() > suffix.size()
                        && host.ends_with("." + suffix)) return true;
                }
                continue;
            }
            if (normalized.find("://") != std::string::npos
                && resource_origin(normalized) == target_origin) return true;
        }
        return false;
    }

    void append_script_elements(dom_node& parent, const std::vector<frame_script>& scripts)
    {
        for (const auto& script : scripts) {
            record_feature(
                "html",
                "element:script",
                "supported",
                {},
                "html-element");
            auto& element = document.create_element("script");
            element.script_state = script_execution_state::already_started;
            if (!script.source.empty()) {
                record_html_attribute_feature("src", "html-parser");
                element.attributes["src"] = script.source;
            }
            if (!script.nonce.empty()) element.attributes["nonce"] = script.nonce;
            if (script.defer) {
                record_html_attribute_feature("defer", "html-parser");
                element.attributes["defer"] = std::string{};
            }
            element.text_content = script.code;
            apply_css_rules(element);
            document.append_child(parent, element);
        }
    }

    static std::vector<std::string> parsed_inline_stylesheets(const dom_node& root)
    {
        std::vector<std::string> result;
        const auto visit = [&](const auto& self, const dom_node& node) -> void {
            // Tree construction handles raw text, comments and template contents.
            // Never scan script strings for markup belonging to another document.
            if (node.tag == "style") {
                auto text = node.text_content;
                for (const auto* child : node.children)
                    if (child != nullptr && child->tag == "#text") text += child->text_content;
                result.push_back(std::move(text));
                return;
            }
            if (node.tag == "template") return;
            for (const auto* child : node.children) if (child != nullptr) self(self, *child);
        };
        visit(visit, root);
        return result;
    }

    static std::vector<std::string> parse_inline_stylesheets(const std::string& html)
    {
#if defined(WEBSCENE_NATIVE_ENGINE_HTML5EVER)
        native_document parsed;
        const auto status = parse_html_document(parsed, parsed.body(), html);
        if (!status) return {};
        return parsed_inline_stylesheets(parsed.body());
#else
        std::vector<std::string> result;
        size_t cursor = 0U;
        while (true) {
            const auto open = html.find("<style", cursor);
            if (open == std::string::npos) break;
            const auto open_end = html.find('>', open);
            if (open_end == std::string::npos) break;
            const auto close = html.find("</style>", open_end + 1U);
            if (close == std::string::npos) break;
            result.push_back(html.substr(open_end + 1U, close - open_end - 1U));
            cursor = close + 8U;
        }
        return result;
#endif
    }

    static std::string extract_element_contents(const std::string& html, const std::string& tag)
    {
        const auto open = html.find("<" + tag);
        if (open == std::string::npos) return {};
        const auto open_end = html.find('>', open);
        if (open_end == std::string::npos) return {};
        const auto close = html.find("</" + tag + ">", open_end + 1U);
        return close == std::string::npos
            ? std::string{}
            : html.substr(open_end + 1U, close - open_end - 1U);
    }

    static std::string strip_html_elements(std::string html, const std::string& tag)
    {
        size_t cursor = 0U;
        const auto opening = "<" + tag;
        const auto closing = "</" + tag + ">";
        while (true) {
            const auto open = html.find(opening, cursor);
            if (open == std::string::npos) break;
            const auto close = html.find(closing, open + opening.size());
            if (close == std::string::npos) {
                html.erase(open);
                break;
            }
            html.erase(open, close + closing.size() - open);
            cursor = open;
        }
        return html;
    }

    void install_screen(
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Object> global)
    {
        auto screen_template = v8::FunctionTemplate::New(isolate);
        screen_template->SetClassName(js_string(isolate, "Screen"));
        screen_template->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "width"),
            get_screen_width);
        screen_template->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "height"),
            get_screen_height);
        screen_template->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "availWidth"),
            get_screen_width);
        screen_template->InstanceTemplate()->SetNativeDataProperty(
            js_string(isolate, "availHeight"),
            get_screen_height);
        screen_template->InstanceTemplate()->Set(
            js_string(isolate, "colorDepth"),
            v8::Integer::New(isolate, 24),
            v8::PropertyAttribute::ReadOnly);
        screen_template->InstanceTemplate()->Set(
            js_string(isolate, "pixelDepth"),
            v8::Integer::New(isolate, 24),
            v8::PropertyAttribute::ReadOnly);
        auto screen_constructor =
            screen_template->GetFunction(local_context).ToLocalChecked();
        auto screen = screen_constructor->NewInstance(local_context).ToLocalChecked();
        global->Set(
            local_context,
            js_string(isolate, "Screen"),
            screen_constructor).Check();
        global->Set(
            local_context,
            js_string(isolate, "screen"),
            screen).Check();
    }

    void set_context_location(
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Object> global,
        const std::string& href)
    {
        if (context.IsEmpty() || local_context == context.Get(isolate)) {
            top_level_location_address = href;
        }
        v8::Local<v8::Value> existing_location;
        const bool reuse_location = global->Get(
            local_context,
            js_string(isolate, "location")).ToLocal(&existing_location)
            && existing_location->IsObject();
        auto location = reuse_location
            ? existing_location.As<v8::Object>()
            : v8::Object::New(isolate);
        if (!reuse_location) {
            location->SetNativeDataProperty(
                local_context,
                js_string(isolate, "href"),
                get_location_href,
                set_location_href).Check();
            location->SetNativeDataProperty(
                local_context,
                js_string(isolate, "hash"),
                get_location_hash,
                set_location_hash).Check();
        }
        const auto scheme_end = href.find(':');
        const auto authority_start = href.find("://");
        const auto authority_end = authority_start == std::string::npos
            ? std::string::npos
            : href.find_first_of("/?#", authority_start + 3U);
        const auto host = authority_start == std::string::npos
            ? std::string{}
            : href.substr(
                authority_start + 3U,
                (authority_end == std::string::npos ? href.size() : authority_end)
                    - authority_start - 3U);
        const auto query_start = href.find(
            '?',
            authority_end == std::string::npos ? 0U : authority_end);
        const auto hash_start = href.find(
            '#',
            authority_end == std::string::npos ? 0U : authority_end);
        const auto path_end = std::min(
            query_start == std::string::npos ? href.size() : query_start,
            hash_start == std::string::npos ? href.size() : hash_start);
        const auto path = authority_end == std::string::npos
            || authority_end >= path_end
            || href[authority_end] != '/'
            ? std::string("/")
            : href.substr(authority_end, path_end - authority_end);
        const auto search = query_start == std::string::npos
            ? std::string{}
            : href.substr(
                query_start,
                (hash_start == std::string::npos ? href.size() : hash_start)
                    - query_start);
        location->SetPrivate(
            local_context,
            location_href_key(isolate),
            js_string(isolate, href.c_str())).Check();
        location->Set(
            local_context,
            js_string(isolate, "protocol"),
            js_string(isolate, scheme_end == std::string::npos ? "" : href.substr(0U, scheme_end + 1U).c_str())).Check();
        location->Set(local_context, js_string(isolate, "host"), js_string(isolate, host.c_str())).Check();
        const auto port_separator = host.find_last_of(':');
        const auto has_port = port_separator != std::string::npos
            && host.find(']') == std::string::npos;
        const auto hostname = has_port ? host.substr(0U, port_separator) : host;
        const auto port = has_port ? host.substr(port_separator + 1U) : std::string{};
        location->Set(
            local_context,
            js_string(isolate, "hostname"),
            js_string(isolate, hostname.c_str())).Check();
        location->Set(
            local_context,
            js_string(isolate, "port"),
            js_string(isolate, port.c_str())).Check();
        const auto origin = scheme_end == std::string::npos || host.empty()
            ? std::string("null")
            : href.substr(0U, scheme_end + 1U) + "//" + host;
        location->Set(
            local_context,
            js_string(isolate, "origin"),
            js_string(isolate, origin.c_str())).Check();
        location->Set(local_context, js_string(isolate, "pathname"), js_string(isolate, path.c_str())).Check();
        location->Set(local_context, js_string(isolate, "search"), js_string(isolate, search.c_str())).Check();
        if (!reuse_location) {
            location->Set(
                local_context,
                js_string(isolate, "toString"),
                v8::Function::New(local_context, location_to_string).ToLocalChecked()).Check();
            location->Set(
                local_context,
                js_string(isolate, "assign"),
                v8::Function::New(local_context, location_assign).ToLocalChecked()).Check();
            location->Set(
                local_context,
                js_string(isolate, "replace"),
                v8::Function::New(local_context, location_replace).ToLocalChecked()).Check();
            location->Set(
                local_context,
                js_string(isolate, "reload"),
                v8::Function::New(local_context, location_reload).ToLocalChecked()).Check();
        }
        std::string secure_host = host;
        if (const auto at = secure_host.rfind('@'); at != std::string::npos) secure_host.erase(0, at + 1);
        if (secure_host.starts_with("[")) {
            const auto close = secure_host.find(']');
            secure_host = secure_host.substr(0, close == std::string::npos ? secure_host.size() : close + 1);
        } else if (const auto colon = secure_host.find(':'); colon != std::string::npos) secure_host.resize(colon);
        std::transform(secure_host.begin(), secure_host.end(), secure_host.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        bool loopback = secure_host == "localhost" || secure_host.ends_with(".localhost")
            || secure_host == "[::1]";
        if (secure_host.starts_with("127.")) {
            size_t start = 0, components = 0;
            bool valid = true;
            while (start < secure_host.size()) {
                auto end = secure_host.find('.', start);
                if (end == std::string::npos) end = secure_host.size();
                unsigned value = 0;
                const auto parsed = std::from_chars(secure_host.data() + start, secure_host.data() + end, value);
                if (parsed.ec != std::errc{} || parsed.ptr != secure_host.data() + end || value > 255) valid = false;
                ++components; start = end + 1;
            }
            loopback = valid && components == 4;
        }
        const auto protocol = scheme_end == std::string::npos ? std::string{} : href.substr(0, scheme_end);
        bool secure = protocol == "https" || protocol == "wss" || protocol == "file"
            || ((protocol == "http" || protocol == "ws") && loopback);
        // A trustworthy child cannot escape an insecure top-level ancestor.
        if (!context.IsEmpty() && local_context != context.Get(isolate)) {
            v8::Local<v8::Value> parent_secure;
            secure = secure && context.Get(isolate)->Global()->Get(context.Get(isolate),
                js_string(isolate, "isSecureContext")).ToLocal(&parent_secure) && parent_secure->IsTrue();
        }
        global->DefineOwnProperty(local_context, js_string(isolate, "isSecureContext"),
            v8::Boolean::New(isolate, secure), v8::ReadOnly).Check();
        global->Set(local_context, js_string(isolate, "location"), location).Check();
        install_history(local_context, global);
        global->Set(
            local_context,
            js_string(isolate, "origin"),
            js_string(isolate, origin.c_str())).Check();
        v8::Local<v8::Value> document_value;
        if (global->Get(
                local_context,
                js_string(isolate, "document")).ToLocal(&document_value)
            && document_value->IsObject()) {
            set_document_address(local_context, document_value.As<v8::Object>(), href);
        }
    }

    void set_document_address(
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Object> document_value,
        const std::string& href)
    {
        for (const auto* property : {"URL", "documentURI"}) {
            document_value->Set(
                local_context,
                js_string(isolate, property),
                js_string(isolate, href.c_str())).Check();
        }
        document_value->SetNativeDataProperty(
            local_context,
            js_string(isolate, "baseURI"),
            get_document_base_uri).Check();
        auto* root = document_value->InternalFieldCount() > 0
            ? static_cast<dom_node*>(document_value->GetAlignedPointerFromInternalField(
                0, v8::kEmbedderDataTypeTagDefault))
            : nullptr;
        if (root != nullptr && (!document_base_caches.contains(root)
            || !frame_base_addresses.contains(root))) {
            register_document_base(*root, href, href);
            if (!frame_base_addresses.contains(root)) {
                invalidate_document_base_root(root);
            }
        }
    }

    static void get_document_base_uri(
        v8::Local<v8::Name>,
        const v8::PropertyCallbackInfo<v8::Value>& info)
    {
        auto* self = current(info.GetIsolate());
        auto* root = info.Holder()->InternalFieldCount() > 0
            ? static_cast<dom_node*>(info.Holder()->GetAlignedPointerFromInternalField(
                0, v8::kEmbedderDataTypeTagDefault))
            : nullptr;
        if (self == nullptr) return;
        info.GetReturnValue().Set(js_dom_string(
            info.GetIsolate(),
            self->effective_document_base(root)));
    }

    void set_current_script(
        v8::Local<v8::Context> local_context,
        v8::Local<v8::Object> document_value,
        const std::string& script_url,
        std::string_view nonce = {})
    {
        auto current_script = v8::Object::New(isolate);
        current_script->Set(
            local_context,
            js_string(isolate, "src"),
            js_string(isolate, script_url.c_str())).Check();
        current_script->Set(
            local_context,
            js_string(isolate, "tagName"),
            js_string(isolate, "SCRIPT")).Check();
        current_script->Set(
            local_context,
            js_string(isolate, "nonce"),
            js_dom_string(isolate, nonce)).Check();
        current_script->Set(
            local_context,
            js_string(isolate, "getAttribute"),
            v8::Function::New(local_context, current_script_get_attribute).ToLocalChecked()).Check();
        document_value->Set(local_context, js_string(isolate, "currentScript"), current_script).Check();
    }

    static bool read_text_file(const std::filesystem::path& path, std::string& result)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream) return false;
        const auto end = stream.tellg();
        if (end < 0) return false;
        result.resize(static_cast<size_t>(end));
        stream.seekg(0, std::ios::beg);
        if (!result.empty()) {
            stream.read(result.data(), static_cast<std::streamsize>(result.size()));
        }
        return stream.good() || stream.eof();
    }

    bool profiled_read_text_file(const std::filesystem::path& path, std::string& result)
    {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        binding_callback_timer timer(profile_startup ? &startup_resource_read : nullptr);
#endif
        return read_text_file(path, result);
    }
