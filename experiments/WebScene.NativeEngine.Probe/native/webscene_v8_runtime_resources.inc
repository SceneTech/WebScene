    static std::string resolve_resource_url(std::string value,const std::string& base)
    { return resources::resolve_url(std::move(value),base); }

    static std::string resource_leaf_name(std::string value)
    {
        if (const auto suffix = value.find_first_of("?#"); suffix != std::string::npos) {
            value.resize(suffix);
        }
        const auto slash = value.find_last_of("/\\");
        return slash == std::string::npos ? value : value.substr(slash + 1U);
    }

    static bool raster_image_dimensions(
        std::string_view bytes, float& width, float& height)
    {
        const auto byte = [&](size_t index) {
            return static_cast<uint8_t>(bytes[index]);
        };
        const auto be16 = [&](size_t index) {
            return static_cast<uint32_t>((byte(index) << 8U) | byte(index + 1U));
        };
        const auto be32 = [&](size_t index) {
            return (static_cast<uint32_t>(byte(index)) << 24U)
                | (static_cast<uint32_t>(byte(index + 1U)) << 16U)
                | (static_cast<uint32_t>(byte(index + 2U)) << 8U)
                | static_cast<uint32_t>(byte(index + 3U));
        };
        const auto accept = [&](uint32_t candidate_width, uint32_t candidate_height) {
            if (candidate_width == 0U || candidate_height == 0U
                || candidate_width > 1'000'000U || candidate_height > 1'000'000U) {
                return false;
            }
            width = static_cast<float>(candidate_width);
            height = static_cast<float>(candidate_height);
            return true;
        };

        if (bytes.size() >= 24U
            && bytes.substr(0U, 8U) == std::string_view{"\x89PNG\r\n\x1a\n", 8U}) {
            return accept(be32(16U), be32(20U));
        }
        if (bytes.size() >= 10U
            && (bytes.starts_with("GIF87a") || bytes.starts_with("GIF89a"))) {
            return accept(
                static_cast<uint32_t>(byte(6U) | (byte(7U) << 8U)),
                static_cast<uint32_t>(byte(8U) | (byte(9U) << 8U)));
        }
        if (bytes.size() >= 30U && bytes.starts_with("RIFF")
            && bytes.substr(8U, 4U) == "WEBP") {
            if (bytes.substr(12U, 4U) == "VP8X") {
                return accept(
                    1U + byte(24U) + (byte(25U) << 8U) + (byte(26U) << 16U),
                    1U + byte(27U) + (byte(28U) << 8U) + (byte(29U) << 16U));
            }
            if (bytes.substr(12U, 4U) == "VP8 "
                && bytes.size() >= 30U
                && byte(23U) == 0x9dU && byte(24U) == 0x01U && byte(25U) == 0x2aU) {
                return accept(
                    (byte(26U) | (byte(27U) << 8U)) & 0x3fffU,
                    (byte(28U) | (byte(29U) << 8U)) & 0x3fffU);
            }
            if (bytes.substr(12U, 4U) == "VP8L" && byte(20U) == 0x2fU) {
                return accept(
                    1U + ((byte(21U) | (byte(22U) << 8U)) & 0x3fffU),
                    1U + (((byte(22U) >> 6U) | (byte(23U) << 2U)
                        | (byte(24U) << 10U)) & 0x3fffU));
            }
        }
        if (bytes.size() >= 4U && byte(0U) == 0xffU && byte(1U) == 0xd8U) {
            size_t cursor = 2U;
            while (cursor + 3U < bytes.size()) {
                while (cursor < bytes.size() && byte(cursor) == 0xffU) ++cursor;
                if (cursor >= bytes.size()) break;
                const auto marker = byte(cursor++);
                if (marker == 0xd8U || marker == 0xd9U || (marker >= 0xd0U && marker <= 0xd7U)) {
                    continue;
                }
                if (cursor + 1U >= bytes.size()) break;
                const auto length = be16(cursor);
                if (length < 2U || cursor + length > bytes.size()) break;
                const auto start_of_frame = (marker >= 0xc0U && marker <= 0xc3U)
                    || (marker >= 0xc5U && marker <= 0xc7U)
                    || (marker >= 0xc9U && marker <= 0xcbU)
                    || (marker >= 0xcdU && marker <= 0xcfU);
                if (start_of_frame && length >= 7U) {
                    return accept(be16(cursor + 5U), be16(cursor + 3U));
                }
                cursor += length;
            }
        }
        return false;
    }

    static bool decode_base64_data_url(std::string_view address, std::string& bytes)
    {
        if (!address.starts_with("data:") || address.size() > 32U * 1024U * 1024U) {
            return false;
        }
        const auto separator = address.find(',');
        if (separator == std::string_view::npos
            || address.substr(5U, separator - 5U).find(";base64") == std::string_view::npos) {
            return false;
        }
        const auto encoded = address.substr(separator + 1U);
        const auto decode = [](unsigned char character) -> int {
            if (character >= 'A' && character <= 'Z') return character - 'A';
            if (character >= 'a' && character <= 'z') return character - 'a' + 26;
            if (character >= '0' && character <= '9') return character - '0' + 52;
            if (character == '+') return 62;
            if (character == '/') return 63;
            return -1;
        };
        bytes.clear();
        bytes.reserve(encoded.size() * 3U / 4U);
        uint32_t accumulator = 0U;
        unsigned bits = 0U;
        for (const auto character : encoded) {
            if (character == '=') break;
            if (std::isspace(static_cast<unsigned char>(character))) continue;
            const auto value = decode(static_cast<unsigned char>(character));
            if (value < 0) return false;
            accumulator = (accumulator << 6U) | static_cast<uint32_t>(value);
            bits += 6U;
            if (bits >= 8U) {
                bits -= 8U;
                bytes.push_back(static_cast<char>((accumulator >> bits) & 0xffU));
            }
        }
        return !bytes.empty();
    }

    static std::string resource_kind_name(uint32_t kind)
    {
        switch (kind) {
        case WEBSCENE_RESOURCE_DOCUMENT: return "document";
        case WEBSCENE_RESOURCE_SCRIPT: return "script";
        case WEBSCENE_RESOURCE_STYLESHEET: return "stylesheet";
        case WEBSCENE_RESOURCE_IMAGE: return "image";
        case WEBSCENE_RESOURCE_DATA: return "data";
        default: return "kind-" + std::to_string(kind);
        }
    }

    static std::string resource_scheme(const std::string& address)
    {
        const auto separator = address.find(':');
        return separator == std::string::npos
            ? "filesystem"
            : lower_html_name(address.substr(0U, separator));
    }

    static std::string resource_origin(const std::string& address)
    {
        const auto scheme_end = address.find("://");
        if (scheme_end == std::string::npos) return {};
        const auto authority_end = address.find_first_of("/?#", scheme_end + 3U);
        const auto authority = address.substr(
            scheme_end + 3U,
            authority_end == std::string::npos
                ? std::string::npos
                : authority_end - scheme_end - 3U);
        if (authority.empty()) return {};
        return lower_html_name(address.substr(0U, scheme_end)) + "://" + authority;
    }

    struct cookie_url_parts final {
        std::string scheme;
        std::string host;
        std::string path{"/"};
        bool valid{false};
    };

    static cookie_url_parts parse_cookie_url(std::string address)
    {
        cookie_url_parts result;
        const auto scheme_end = address.find("://");
        if (scheme_end == std::string::npos) return result;
        result.scheme = lower_html_name(address.substr(0U, scheme_end));
        if (result.scheme == "ws") result.scheme = "http";
        if (result.scheme == "wss") result.scheme = "https";
        const auto authority_start = scheme_end + 3U;
        const auto authority_end = address.find_first_of("/?#", authority_start);
        auto authority = address.substr(
            authority_start,
            authority_end == std::string::npos ? std::string::npos
                : authority_end - authority_start);
        if (const auto at = authority.rfind('@'); at != std::string::npos) {
            authority.erase(0U, at + 1U);
        }
        if (!authority.empty() && authority.front() == '[') {
            const auto bracket = authority.find(']');
            if (bracket == std::string::npos) return result;
            result.host = authority.substr(0U, bracket + 1U);
        } else {
            const auto colon = authority.rfind(':');
            result.host = authority.substr(0U, colon == std::string::npos
                ? std::string::npos : colon);
        }
        result.host = lower_html_name(result.host);
        if (authority_end != std::string::npos && address[authority_end] == '/') {
            const auto path_end = address.find_first_of("?#", authority_end);
            result.path = address.substr(authority_end,
                path_end == std::string::npos ? std::string::npos
                    : path_end - authority_end);
        }
        result.valid = (result.scheme == "http" || result.scheme == "https")
            && !result.host.empty();
        return result;
    }

    static bool cookie_domain_matches(
        const std::string& host, const std::string& domain)
    {
        return host == domain || (host.size() > domain.size()
            && host.ends_with(domain)
            && host[host.size() - domain.size() - 1U] == '.');
    }

    static std::string default_cookie_path(const std::string& path)
    {
        if (path.empty() || path.front() != '/') return "/";
        const auto slash = path.rfind('/');
        return slash == 0U ? "/" : path.substr(0U, slash);
    }

    static std::optional<std::chrono::system_clock::time_point>
    parse_cookie_expiry(const std::string& value)
    {
        std::tm parsed{};
        std::istringstream stream(value);
        stream.imbue(std::locale::classic());
        stream >> std::get_time(&parsed, "%a, %d %b %Y %H:%M:%S GMT");
        if (stream.fail()) return std::nullopt;
#if defined(_WIN32)
        const auto seconds = _mkgmtime(&parsed);
#else
        const auto seconds = timegm(&parsed);
#endif
        if (seconds < 0) return std::nullopt;
        return std::chrono::system_clock::from_time_t(seconds);
    }

    void store_cookie(
        const std::string& response_url,
        const std::string& line,
        bool from_http)
    {
        if (line.empty() || line.size() > maximum_cookie_bytes) return;
        const auto source = parse_cookie_url(response_url);
        if (!source.valid) return;
        const auto trim = [](std::string value) {
            const auto first = value.find_first_not_of(" \t");
            if (first == std::string::npos) return std::string{};
            const auto last = value.find_last_not_of(" \t");
            return value.substr(first, last - first + 1U);
        };
        const auto first_separator = line.find(';');
        const auto pair = line.substr(0U, first_separator);
        const auto equals = pair.find('=');
        if (equals == std::string::npos) return;
        stored_cookie cookie;
        cookie.name = trim(pair.substr(0U, equals));
        cookie.value = trim(pair.substr(equals + 1U));
        cookie.domain = source.host;
        cookie.path = default_cookie_path(source.path);
        if (cookie.name.empty() || cookie.name.find_first_of("=;\r\n\0") != std::string::npos
            || cookie.value.find_first_of(";\r\n\0") != std::string::npos) return;
        std::optional<int64_t> maximum_age;
        auto cursor = first_separator;
        while (cursor != std::string::npos) {
            const auto next = line.find(';', cursor + 1U);
            auto attribute = trim(line.substr(cursor + 1U,
                next == std::string::npos ? std::string::npos : next - cursor - 1U));
            const auto attribute_equals = attribute.find('=');
            auto name = lower_html_name(trim(attribute.substr(0U, attribute_equals)));
            auto value = attribute_equals == std::string::npos
                ? std::string{} : trim(attribute.substr(attribute_equals + 1U));
            if (name == "domain" && !value.empty()) {
                value = lower_html_name(value);
                while (!value.empty() && value.front() == '.') value.erase(value.begin());
                const auto numeric_host = !source.host.empty() && std::all_of(
                    source.host.begin(), source.host.end(), [](unsigned char character) {
                        return std::isdigit(character) || character == '.' || character == ':'
                            || character == '[' || character == ']';
                    });
                if (!cookie_domain_matches(source.host, value)
                    || (numeric_host && value != source.host)) return;
                cookie.domain = std::move(value);
                cookie.host_only = false;
            } else if (name == "path" && !value.empty() && value.front() == '/') {
                cookie.path = std::move(value);
            } else if (name == "secure") {
                cookie.secure = true;
            } else if (name == "httponly" && from_http) {
                cookie.http_only = true;
            } else if (name == "samesite") {
                value = lower_html_name(value);
                if (value == "strict") cookie.same_site = cookie_same_site::strict;
                else if (value == "none") cookie.same_site = cookie_same_site::none;
                else cookie.same_site = cookie_same_site::lax;
            } else if (name == "max-age") {
                int64_t seconds = 0;
                const auto parsed = std::from_chars(
                    value.data(), value.data() + value.size(), seconds);
                if (parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size()) {
                    maximum_age = seconds;
                }
            } else if (name == "expires") {
                if (const auto expiry = parse_cookie_expiry(value); expiry.has_value()) {
                    cookie.expires = *expiry;
                    cookie.persistent = true;
                }
            }
            cursor = next;
        }
        if (cookie.secure && source.scheme != "https") return;
        if (cookie.same_site == cookie_same_site::none && !cookie.secure) return;
        if (cookie.name.starts_with("__Secure-") && !cookie.secure) return;
        if (cookie.name.starts_with("__Host-")
            && (!cookie.secure || !cookie.host_only || cookie.path != "/")) return;
        if (maximum_age.has_value()) {
            cookie.persistent = true;
            cookie.expires = maximum_age.value() <= 0
                ? std::chrono::system_clock::time_point{}
                : std::chrono::system_clock::now()
                    + std::chrono::seconds(std::min<int64_t>(
                        maximum_age.value(), 400LL * 24LL * 60LL * 60LL));
        }
        std::lock_guard lock(cookie_jar->mutex);
        const auto same_cookie = [&](const stored_cookie& known) {
            return known.name == cookie.name && known.domain == cookie.domain
                && known.path == cookie.path;
        };
        auto known = std::find_if(cookie_jar->cookies.begin(), cookie_jar->cookies.end(), same_cookie);
        if (!from_http && known != cookie_jar->cookies.end() && known->http_only) return;
        if (cookie.persistent && cookie.expires <= std::chrono::system_clock::now()) {
            if (known != cookie_jar->cookies.end()) cookie_jar->cookies.erase(known);
            return;
        }
        if (known != cookie_jar->cookies.end()) {
            cookie.creation_order = known->creation_order;
            *known = std::move(cookie);
        } else {
            cookie.creation_order = cookie_jar->next_creation_order++;
            if (cookie_jar->cookies.size() >= maximum_cookie_count) {
                const auto oldest = std::min_element(
                    cookie_jar->cookies.begin(), cookie_jar->cookies.end(),
                    [](const auto& left, const auto& right) {
                        return left.creation_order < right.creation_order;
                    });
                cookie_jar->cookies.erase(oldest);
            }
            cookie_jar->cookies.push_back(std::move(cookie));
        }
    }

    std::string cookie_header_for(
        const std::string& target_url,
        const resource_request_context& context,
        bool script_visible = false)
    {
        const auto target = parse_cookie_url(target_url);
        if (!target.valid || context.credentials == WEBSCENE_FETCH_CREDENTIALS_OMIT) return {};
        auto origin_url = target_url;
        if (origin_url.starts_with("ws:")) origin_url.replace(0U, 3U, "http:");
        else if (origin_url.starts_with("wss:")) origin_url.replace(0U, 4U, "https:");
        const auto target_origin = resource_origin(origin_url);
        if (context.credentials == WEBSCENE_FETCH_CREDENTIALS_SAME_ORIGIN
            && !context.origin.empty()
            && target_origin != context.origin) return {};
        const auto same_site = context.origin.empty() || target_origin == context.origin;
        const auto safe_navigation = context.initiator == WEBSCENE_RESOURCE_INITIATOR_NAVIGATION
            && (context.method.empty() || context.method == "GET" || context.method == "HEAD");
        std::vector<const stored_cookie*> matches;
        const auto now = std::chrono::system_clock::now();
        std::lock_guard lock(cookie_jar->mutex);
        std::erase_if(cookie_jar->cookies, [now](const auto& cookie) {
            return cookie.persistent && cookie.expires <= now;
        });
        for (const auto& cookie : cookie_jar->cookies) {
            if (script_visible && cookie.http_only) continue;
            if (cookie.secure && target.scheme != "https") continue;
            if (cookie.host_only ? cookie.domain != target.host
                    : !cookie_domain_matches(target.host, cookie.domain)) continue;
            if (!target.path.starts_with(cookie.path)
                || (target.path.size() > cookie.path.size()
                    && !cookie.path.ends_with('/')
                    && target.path[cookie.path.size()] != '/')) continue;
            if (!same_site && (cookie.same_site == cookie_same_site::strict
                || (cookie.same_site == cookie_same_site::lax && !safe_navigation))) continue;
            matches.push_back(&cookie);
        }
        std::stable_sort(matches.begin(), matches.end(), [](const auto* left, const auto* right) {
            return left->path.size() != right->path.size()
                ? left->path.size() > right->path.size()
                : left->creation_order < right->creation_order;
        });
        std::string result;
        for (const auto* cookie : matches) {
            if (!result.empty()) result += "; ";
            if (result.size() + cookie->name.size() + cookie->value.size() + 1U > 16U * 1024U) break;
            result += cookie->name + "=" + cookie->value;
        }
        return result;
    }

    void accept_response_cookies(
        const std::string& request_url,
        const resource_request_context& context,
        const resource_response& response)
    {
        if (context.credentials == WEBSCENE_FETCH_CREDENTIALS_OMIT) return;
        const auto final_url = response.final_url.empty() ? request_url : response.final_url;
        if (context.credentials == WEBSCENE_FETCH_CREDENTIALS_SAME_ORIGIN
            && !context.origin.empty()
            && resource_origin(final_url) != context.origin) return;
        for (const auto& [name, value] : response.headers) {
            if (name == "set-cookie") store_cookie(final_url, value, true);
        }
    }

    std::shared_ptr<const resource_response> load_resource_single_flight(
        uint32_t kind,
        const std::string& resolved_name,
        const resource_request_context& request_context,
        const std::string& entity_tag,
        int64_t last_modified_unix_seconds,
        bool& loaded)
    {
        loaded = false;
        if (!load_resource_callback || resolved_name.empty()) return {};
        auto effective_context = request_context;
        if (effective_context.cookie.empty()) {
            effective_context.cookie = cookie_header_for(resolved_name, effective_context);
        }

        const auto key = compilation_cache_directory
            + '\n' + std::to_string(kind)
            + '\n' + resolved_name
            + '\n' + effective_context.origin
            + '\n' + effective_context.cookie
            + (effective_context.credentials == WEBSCENE_FETCH_CREDENTIALS_OMIT
                ? std::string{} : "\nengine:" + std::to_string(
                    reinterpret_cast<uintptr_t>(this)))
            + '\n' + entity_tag
            + '\n' + std::to_string(last_modified_unix_seconds);
        std::shared_ptr<process_resource_load_entry> entry;
        auto producer = false;
        {
            std::lock_guard lock(process_resource_load_mutex);
            const auto now = std::chrono::steady_clock::now();
            std::erase_if(process_resource_loads, [now](const auto& item) {
                return item.second->ready.load(std::memory_order_acquire)
                    && item.second->expires_at <= now;
            });
            if (const auto known = process_resource_loads.find(key);
                known != process_resource_loads.end()) {
                entry = known->second;
                entry->access = ++process_resource_load_access;
            } else {
                entry = std::make_shared<process_resource_load_entry>();
                entry->access = ++process_resource_load_access;
                process_resource_loads.emplace(key, entry);
                producer = true;
            }
        }

        if (!producer) {
            std::unique_lock lock(entry->mutex);
            if (!entry->ready.load(std::memory_order_acquire)) {
                ++process_resource_load_waiter_count;
                entry->condition.wait(lock, [&entry] {
                    return entry->ready.load(std::memory_order_acquire);
                });
            }
            loaded = entry->loaded;
            if (loaded && entry->response != nullptr) {
                ++process_resource_memory_hit_count;
                process_resource_shared_byte_count += entry->response->content.size();
            }
            return entry->response;
        }

        ++process_resource_load_leader_count;
        resource_response response;
        try {
            loaded = load_resource_callback(
                kind,
                resolved_name,
                effective_context,
                entity_tag,
                last_modified_unix_seconds,
                response);
        } catch (...) {
            loaded = false;
        }
        if (loaded) accept_response_cookies(resolved_name, effective_context, response);
        auto shared_response = loaded
            ? std::make_shared<const resource_response>(std::move(response))
            : std::shared_ptr<const resource_response>{};
        {
            std::lock_guard lock(entry->mutex);
            entry->loaded = loaded;
            entry->response = shared_response;
            entry->expires_at = std::chrono::steady_clock::now()
                + (loaded && shared_response != nullptr && shared_response->cacheable
                    ? std::chrono::milliseconds(250)
                    : std::chrono::milliseconds(0));
            entry->ready.store(true, std::memory_order_release);
        }
        entry->condition.notify_all();

        {
            std::lock_guard lock(process_resource_load_mutex);
            while (process_resource_loads.size() > maximum_process_resource_load_entries) {
                auto oldest = process_resource_loads.end();
                for (auto candidate = process_resource_loads.begin();
                    candidate != process_resource_loads.end();
                    ++candidate) {
                    if (!candidate->second->ready.load(std::memory_order_acquire)
                        || candidate->second.get() == entry.get()) {
                        continue;
                    }
                    if (oldest == process_resource_loads.end()
                        || candidate->second->access < oldest->second->access) {
                        oldest = candidate;
                    }
                }
                if (oldest == process_resource_loads.end()) break;
                process_resource_loads.erase(oldest);
            }
        }
        return shared_response;
    }

    bool has_ready_fetch_task() const noexcept
    {
        return std::any_of(
            pending_fetches.begin(),
            pending_fetches.end(),
            [](const auto& task) {
                return task.future.valid()
                    && task.future.wait_for(std::chrono::milliseconds::zero())
                        == std::future_status::ready;
            });
    }

    bool drain_fetch_task()
    {
        const auto ready = std::find_if(
            pending_fetches.begin(),
            pending_fetches.end(),
            [](const auto& task) {
                return task.future.valid()
                    && task.future.wait_for(std::chrono::milliseconds::zero())
                        == std::future_status::ready;
            });
        if (ready == pending_fetches.end()) return true;
        auto task = std::move(*ready);
        pending_fetches.erase(ready);
        auto task_context = task.context.Get(isolate);
        auto resolver = task.resolver.Get(isolate);
        if (task_context.IsEmpty() || resolver.IsEmpty()) return true;
        activate_css_cascade(task_context);
        v8::Context::Scope task_context_scope(task_context);
        style_recascade_batch_scope style_batch(*this, batch_style_recascades);
        async_fetch_result result;
        try {
            result = task.future.get();
        } catch (const std::exception& error) {
            result.error = error.what();
        } catch (...) {
            result.error = "WebScene fetch worker failed";
        }
        bool settled = false;
        if (result.loaded) {
            auto value = v8::Object::New(isolate);
            auto bytes=v8::ArrayBuffer::New(isolate,result.body.size());
            if(!result.body.empty())std::memcpy(bytes->GetBackingStore()->Data(),result.body.data(),result.body.size());
            value->Set(task_context,js_string(isolate,"body"),bytes).Check();
            value->Set(
                task_context,
                js_string(isolate, "url"),
                js_dom_string(isolate, result.resolved_url)).Check();
            value->Set(task_context, js_string(isolate, "status"),
                v8::Integer::NewFromUnsigned(isolate, result.status)).Check();
            value->Set(task_context, js_string(isolate, "statusText"),
                js_dom_string(isolate, result.status_text)).Check();
            value->Set(task_context, js_string(isolate, "redirected"),
                v8::Boolean::New(isolate, result.redirected)).Check();
            value->Set(task_context, js_string(isolate, "type"),
                js_dom_string(isolate, result.response_type)).Check();
            auto headers = v8::Array::New(isolate, static_cast<int>(result.headers.size()));
            for (uint32_t index = 0; index < result.headers.size(); ++index) {
                auto pair = v8::Array::New(isolate, 2);
                pair->Set(task_context, 0,
                    js_dom_string(isolate, result.headers[index].first)).Check();
                pair->Set(task_context, 1,
                    js_dom_string(isolate, result.headers[index].second)).Check();
                headers->Set(task_context, index, pair).Check();
            }
            value->Set(task_context, js_string(isolate, "headers"), headers).Check();
            settled = resolver->Resolve(task_context, value).FromMaybe(false);
        } else {
            const auto message = result.error.empty()
                ? std::string{"Unable to fetch WebScene resource"}
                : result.error;
            settled = resolver->Reject(
                task_context,
                v8::Exception::Error(js_string(isolate, message.c_str())))
                    .FromMaybe(false);
        }
        perform_microtask_checkpoint();
        style_batch.finish();
        return settled;
    }

    bool load_text_resource(
        const std::string& value,
        const std::string& base,
        uint32_t kind,
        std::string& content,
        std::string& resolved_name,
        std::shared_ptr<const immutable_text_source>* immutable_content = nullptr,
        const resource_request_context& request_context = {})
    {
        if (immutable_content != nullptr) immutable_content->reset();
        const auto assign_content = [&](std::shared_ptr<const immutable_text_source> value) {
            if (value == nullptr) {
                content.clear();
                return;
            }
            if (immutable_content != nullptr) {
                *immutable_content = std::move(value);
                content.clear();
            } else {
                content.assign(value->data(), value->size());
            }
        };
        resolved_name = resolve_resource_url(value, base);
        auto effective_request_context = request_context;
        if (effective_request_context.origin.empty()) {
            effective_request_context.initiator = kind == WEBSCENE_RESOURCE_DOCUMENT
                && base.empty() ? WEBSCENE_RESOURCE_INITIATOR_NAVIGATION
                : WEBSCENE_RESOURCE_INITIATOR_SUBRESOURCE;
            effective_request_context.origin = resource_origin(
                base.empty() ? resolved_name : base);
            effective_request_context.referrer = base;
            effective_request_context.method = "GET";
            effective_request_context.credentials = WEBSCENE_FETCH_CREDENTIALS_INCLUDE;
        }
        effective_request_context.cookie = cookie_header_for(
            resolved_name, effective_request_context);
        record_feature(
            "resource",
            "type:" + resource_kind_name(kind),
            "supported",
            {},
            "resource-loader");
        record_feature(
            "resource",
            "scheme:" + resource_scheme(resolved_name),
            "supported",
            {},
            "resource-loader");
        ++resource_cache_request_count;
        const auto representation_name = effective_request_context.origin.empty()
            ? resolved_name
            : resolved_name + "\norigin:" + effective_request_context.origin
                + "\ncookie:" + effective_request_context.cookie;
        const auto cache_key = resource_key_digest(kind, representation_name);
        cached_text_resource cached;
        auto has_cached = false;
        auto has_prefetched = false;
        prefetched_resource prefetched;
        const auto prefetch_key = resource_prefetch_key(
            kind,
            resolved_name,
            effective_request_context.origin,
            effective_request_context.cookie);
        std::shared_future<prefetched_resource> prefetch_future;
        {
            std::lock_guard lock(resource_prefetch_mutex);
            if (auto known = prefetched_resources.find(prefetch_key);
                known != prefetched_resources.end()) {
                prefetch_future = known->second;
                prefetched_resources.erase(known);
            }
        }
        if (prefetch_future.valid()) {
            prefetched = prefetch_future.get();
            cached = std::move(prefetched.cached);
            has_cached = prefetched.has_cached;
            has_prefetched = true;
            if (prefetched.from_fresh_cache) {
                record_feature(
                    "resource", "cache:fresh-prefetch", "supported", {}, "resource-loader");
                assign_content(std::move(cached.content));
                ++resource_cache_hit_count;
                resource_cache_bytes_read_count += immutable_content != nullptr
                    && *immutable_content != nullptr
                    ? (*immutable_content)->size()
                    : content.size();
                return true;
            }
        } else {
            has_cached = read_resource_cache(cache_key, kind, cached);
        }
        const auto now_unix_seconds = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (has_cached && cached.fresh_until_unix_seconds > now_unix_seconds) {
            record_feature(
                "resource", "cache:fresh", "supported", {}, "resource-loader");
            assign_content(std::move(cached.content));
            ++resource_cache_hit_count;
            resource_cache_bytes_read_count += immutable_content != nullptr
                && *immutable_content != nullptr
                ? (*immutable_content)->size()
                : content.size();
            return true;
        }
        if (load_resource_callback && !resolved_name.empty()) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            binding_callback_timer timer(profile_startup ? &startup_resource_read : nullptr);
#endif
            auto loaded = false;
            std::shared_ptr<const resource_response> response;
            if (has_prefetched) {
                response = std::move(prefetched.response);
                loaded = prefetched.loaded;
            } else {
                response = load_resource_single_flight(
                    kind,
                    resolved_name,
                    effective_request_context,
                    has_cached ? cached.entity_tag : std::string{},
                    has_cached ? cached.last_modified_unix_seconds : 0,
                    loaded);
            }
            if (loaded && response != nullptr) {
                if (!response->final_url.empty()) resolved_name = response->final_url;
                if (response->not_modified && has_cached) {
                    record_feature(
                        "resource", "cache:revalidated-304", "supported", {}, "resource-loader");
                    auto shared_content = std::move(cached.content);
                    assign_content(shared_content);
                    ++resource_cache_hit_count;
                    resource_cache_bytes_read_count += shared_content != nullptr
                        ? shared_content->size()
                        : 0U;
                    if (response->cacheable) {
                        write_resource_cache(
                            cache_key,
                            kind,
                            shared_content,
                            response->entity_tag.empty() ? cached.entity_tag : response->entity_tag,
                            response->last_modified_unix_seconds == 0
                                ? cached.last_modified_unix_seconds
                                : response->last_modified_unix_seconds,
                            response->fresh_until_unix_seconds);
                    } else {
                        remove_resource_cache(cache_key);
                    }
                    return true;
                }
                auto shared_content = immutable_text_source::from_string(
                    std::shared_ptr<const std::string>(
                        response,
                        &response->content));
                assign_content(shared_content);
                record_feature(
                    "resource",
                    response->cacheable ? "network:cacheable-200" : "network:uncacheable-200",
                    "supported",
                    {},
                    "resource-loader");
                ++resource_cache_miss_count;
                if (response->cacheable) {
                    write_resource_cache(
                        cache_key,
                        kind,
                        shared_content,
                        response->entity_tag,
                        response->last_modified_unix_seconds,
                        response->fresh_until_unix_seconds);
                } else {
                    remove_resource_cache(cache_key);
                }
                return true;
            }
            if (has_cached) {
                record_feature(
                    "resource", "cache:stale-fallback", "supported", {}, "resource-loader");
                auto shared_content = std::move(cached.content);
                assign_content(shared_content);
                ++resource_cache_hit_count;
                resource_cache_bytes_read_count += shared_content != nullptr
                    ? shared_content->size()
                    : 0U;
                // Back off briefly after a failed revalidation. Without this,
                // a permanently missing optional asset is retried on every
                // engine generation even though its permitted stale body is
                // already serving successfully.
                write_resource_cache(
                    cache_key,
                    kind,
                    shared_content,
                    cached.entity_tag,
                    cached.last_modified_unix_seconds,
                    now_unix_seconds + 300);
                return true;
            }
        }
        ++resource_cache_miss_count;
        const auto path = resolve_resource_path(value);
        if (path.empty() || !profiled_read_text_file(path, content)) {
            record_feature(
                "resource", "load:failed", "unsupported", {}, "resource-loader");
            return false;
        }
        record_feature(
            "resource", "filesystem:read", "supported", {}, "resource-loader");
        resolved_name = path.string();
        auto shared_content = immutable_text_source::from_string(
            std::make_shared<const std::string>(std::move(content)));
        write_resource_cache(cache_key, kind, shared_content, {}, 0, 0);
        assign_content(std::move(shared_content));
        return true;
    }

    static std::string resource_prefetch_key(
        uint32_t kind,
        const std::string& resolved_name,
        const std::string& origin = {},
        const std::string& cookie = {})
    {
        return std::to_string(kind) + ':' + resolved_name + "\norigin:" + origin
            + "\ncookie:" + cookie;
    }

    void start_resource_prefetch(
        const std::string& value,
        const std::string& base,
        uint32_t kind)
    {
        if (!load_resource_callback) return;
        const auto resolved_name = resolve_resource_url(value, base);
        if (resolved_name.empty()) return;
        resource_request_context request_context{
            WEBSCENE_RESOURCE_INITIATOR_SUBRESOURCE,
            resource_origin(base),
            base,
            WEBSCENE_FETCH_MODE_NONE,
            kind == WEBSCENE_RESOURCE_SCRIPT ? WEBSCENE_REQUEST_DESTINATION_SCRIPT
                : kind == WEBSCENE_RESOURCE_STYLESHEET ? WEBSCENE_REQUEST_DESTINATION_STYLE
                : kind == WEBSCENE_RESOURCE_IMAGE ? WEBSCENE_REQUEST_DESTINATION_IMAGE
                : WEBSCENE_REQUEST_DESTINATION_NONE,
            "GET", {}, {}, WEBSCENE_FETCH_CREDENTIALS_INCLUDE, {}};
        request_context.cookie = cookie_header_for(resolved_name, request_context);
        const auto key = resource_prefetch_key(
            kind, resolved_name, request_context.origin, request_context.cookie);
        {
            std::lock_guard lock(resource_prefetch_mutex);
            if (prefetched_resources.size() >= maximum_resource_prefetches
                || prefetched_resources.contains(key)) return;
        }

        cached_text_resource cached;
        const auto cache_key = resource_key_digest(kind, resolved_name);
        const auto has_cached = read_resource_cache(cache_key, kind, cached);
        const auto now_unix_seconds = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (has_cached && cached.fresh_until_unix_seconds > now_unix_seconds) {
            prefetched_resource result;
            result.cached = std::move(cached);
            result.has_cached = true;
            result.from_fresh_cache = true;
            std::promise<prefetched_resource> promise;
            auto future = promise.get_future().share();
            promise.set_value(std::move(result));
            std::lock_guard lock(resource_prefetch_mutex);
            if (prefetched_resources.size() < maximum_resource_prefetches
                && !prefetched_resources.contains(key)) {
                prefetched_resources.emplace(key, std::move(future));
            }
            return;
        }

        std::lock_guard lock(resource_prefetch_mutex);
        if (prefetched_resources.size() >= maximum_resource_prefetches
            || prefetched_resources.contains(key)) return;
        auto entity_tag = has_cached ? cached.entity_tag : std::string{};
        const auto last_modified = has_cached ? cached.last_modified_unix_seconds : 0;
        prefetched_resources.emplace(
            key,
            std::async(
                std::launch::async,
                [this, kind, resolved_name, request_context = std::move(request_context),
                    entity_tag = std::move(entity_tag), last_modified,
                    cached = std::move(cached), has_cached]() mutable {
                    prefetched_resource result;
                    result.cached = std::move(cached);
                    result.has_cached = has_cached;
                    result.response = load_resource_single_flight(
                        kind,
                        resolved_name,
                        request_context,
                        entity_tag,
                        last_modified,
                        result.loaded);
                    return result;
                }).share());
    }

    void prefetch_connected_resources()
    {
        if (connected_resources.empty()) return;
        for (auto& task : connected_resources) {
            if (task.node == nullptr) continue;
            auto task_context = task.context.Get(isolate);
            if (task_context.IsEmpty()) continue;
            activate_css_cascade(task_context);
            v8::Context::Scope task_context_scope(task_context);
            if (begin_connected_resource_interception(task)) continue;
            if (!load_resource_callback) continue;
            const auto& base = current_base_address();
            if (task.node->tag == "link") {
                const auto href = task.node->attributes.find("href");
                if (href != task.node->attributes.end()
                    && !object_urls.contains(href->second)) {
                    start_resource_prefetch(
                        href->second,
                        base,
                        WEBSCENE_RESOURCE_STYLESHEET);
                }
            } else if (task.node->tag == "script") {
                const auto source = task.node->attributes.find("src");
                if (source != task.node->attributes.end() && !source->second.empty()) {
                    start_resource_prefetch(
                        source->second,
                        base,
                        WEBSCENE_RESOURCE_SCRIPT);
                }
            } else if (task.node->tag == "img") {
                const auto source = task.node->attributes.find("src");
                if (source != task.node->attributes.end() && !source->second.empty()) {
                    start_resource_prefetch(
                        source->second,
                        base,
                        WEBSCENE_RESOURCE_IMAGE);
                }
            }
        }
    }

    std::filesystem::path resolve_resource_path(std::string value) const
    {
        if (resource_root.empty() || value.empty()) return {};
        if (const auto suffix = value.find_first_of("?#"); suffix != std::string::npos) {
            value.resize(suffix);
        }
        std::replace(value.begin(), value.end(), '\\', '/');
        if (const auto scheme = value.find("://"); scheme != std::string::npos) {
            const auto path_start = value.find('/', scheme + 3U);
            value = path_start == std::string::npos ? std::string{} : value.substr(path_start + 1U);
        } else {
            while (value.starts_with('/')) value.erase(value.begin());
            while (value.starts_with("./")) value.erase(0U, 2U);
        }
        const auto root_name = resource_root.filename().generic_string();
        if (!root_name.empty()) {
            const auto marker = root_name + '/';
            if (const auto offset = value.find(marker); offset != std::string::npos) {
                value.erase(0U, offset + marker.size());
            }
        }
        const auto relative = std::filesystem::path(value).lexically_normal();
        if (relative.empty() || relative.is_absolute()) return {};
        for (const auto& part : relative) {
            if (part == "..") return {};
        }
        return resource_root / relative;
    }

    static uint32_t connected_resource_kind(const dom_node& node)
    {
        return node.tag == "script" ? WEBSCENE_RESOURCE_SCRIPT
            : node.tag == "link" ? WEBSCENE_RESOURCE_STYLESHEET
            : node.tag == "img" ? WEBSCENE_RESOURCE_IMAGE
            : WEBSCENE_RESOURCE_DATA;
    }

    static uint32_t connected_resource_destination(const dom_node& node)
    {
        return node.tag == "script" ? WEBSCENE_REQUEST_DESTINATION_SCRIPT
            : node.tag == "link" ? WEBSCENE_REQUEST_DESTINATION_STYLE
            : node.tag == "img" ? WEBSCENE_REQUEST_DESTINATION_IMAGE
            : WEBSCENE_REQUEST_DESTINATION_NONE;
    }

    bool begin_connected_resource_interception(connected_resource_task& task)
    {
        if (task.interception_started) return task.controlled_fetch.valid();
        task.interception_started = true;
        if (task.node == nullptr) return false;
        const auto attribute_name = task.node->tag == "link" ? "href" : "src";
        const auto authored = task.node->attributes.find(attribute_name);
        if (authored == task.node->attributes.end() || authored->second.empty()
            || authored->second.starts_with("data:")
            || authored->second.starts_with("blob:")
            || object_urls.contains(authored->second)) {
            return false;
        }
        const auto& base = current_base_address();
        task.controlled_url = resolve_resource_url(authored->second, base);
        resource_request_context request_context{
            WEBSCENE_RESOURCE_INITIATOR_SUBRESOURCE,
            current_security_origin(),
            base,
            WEBSCENE_FETCH_MODE_NONE,
            connected_resource_destination(*task.node),
            "GET", {}, {}, WEBSCENE_FETCH_CREDENTIALS_INCLUDE, {}};
        request_context.cookie = cookie_header_for(
            task.controlled_url, request_context);
        task.controlled_fetch = enqueue_controlled_service_worker_fetch(
            task.controlled_url, request_context);
        if (task.controlled_fetch.valid()) {
            task.controlled_deadline = std::chrono::steady_clock::now()
                + std::chrono::seconds(30);
        }
        return task.controlled_fetch.valid();
    }

    bool has_ready_connected_resource_task() const
    {
        if (connected_resources.empty()) return false;
        const auto now = std::chrono::steady_clock::now();
        const auto& task = connected_resources.front();
        return !task.interception_started || !task.controlled_fetch.valid()
            || task.controlled_fetch.wait_for(std::chrono::seconds(0))
                == std::future_status::ready
            || (task.controlled_deadline != std::chrono::steady_clock::time_point{}
                && now >= task.controlled_deadline);
    }

    bool load_connected_text_resource(
        const std::string& value,
        const std::string& base,
        uint32_t kind,
        std::string& content,
        std::string& resolved_name,
        std::shared_ptr<const immutable_text_source>* immutable_content,
        const service_worker_fetch_result* controlled)
    {
        if (controlled == nullptr || !controlled->handled) {
            return load_text_resource(
                value, base, kind, content, resolved_name, immutable_content);
        }
        if (!controlled->error.empty() || controlled->status < 200U
            || controlled->status >= 300U) {
            return false;
        }
        if (immutable_content != nullptr) immutable_content->reset();
        content = controlled->body;
        resolved_name = controlled->url.empty()
            ? resolve_resource_url(value, base)
            : controlled->url;
        record_feature(
            "resource", "service-worker:intercepted-" + resource_kind_name(kind),
            "supported", {}, "service-worker-fetch");
        return true;
    }

    bool load_connected_resource(
        dom_node& node,
        v8::Local<v8::Object> wrapper,
        const service_worker_fetch_result* controlled = nullptr)
    {
        auto local_context = isolate->GetCurrentContext();
        const auto dispatch_resource_event = [&](const char* type) {
            v8::Local<v8::Value> dispatcher;
            if (!wrapper->Get(
                    local_context,
                    js_string(isolate, "dispatchEvent")).ToLocal(&dispatcher)
                || !dispatcher->IsFunction()) {
                return true;
            }
            auto event = create_event_instance(local_context);
            event->Set(
                local_context,
                js_string(isolate, "type"),
                js_string(isolate, type)).Check();
            event->Set(
                local_context,
                js_string(isolate, "bubbles"),
                v8::False(isolate)).Check();
            event->Set(
                local_context,
                js_string(isolate, "cancelable"),
                v8::False(isolate)).Check();
            v8::TryCatch try_catch(isolate);
            v8::Local<v8::Value> arguments[]{event};
            if (!dispatcher.As<v8::Function>()->Call(
                    local_context,
                    wrapper,
                    1,
                    arguments).IsEmpty()) {
                return true;
            }
            frame_last_error_value = std::string("Connected resource ") + type
                + " event failed: " + describe_reported_exception(try_catch, local_context);
            ++frame_script_error_count;
            return false;
        };
        if (node.tag == "link") {
            const auto href_iterator = node.attributes.find("href");
            if (href_iterator != node.attributes.end()) {
                std::string stylesheet;
                std::string stylesheet_name;
                const auto& base = current_base_address();
                const auto object_url = object_urls.find(href_iterator->second);
                const auto loaded = object_url != object_urls.end()
                    ? (stylesheet = object_url->second,
                        stylesheet_name = href_iterator->second,
                        true)
                    : load_connected_text_resource(
                        href_iterator->second,
                        base,
                        WEBSCENE_RESOURCE_STYLESHEET,
                        stylesheet,
                        stylesheet_name,
                        nullptr,
                        controlled);
                if (loaded) {
                    const auto first_new_rule = add_stylesheet(
                        std::move(stylesheet),
                        stylesheet_name);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                    binding_callback_timer recascade_timer(
                        profile_startup ? &startup_stylesheet_recascade : nullptr);
#endif
                    const auto variable_recascade =
                        make_css_variable_recascade_plan(first_new_rule);
                    const auto apply_to_existing = [&](
                        const auto& recurse,
                        dom_node& existing) -> void {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                        if (profile_startup) ++startup_stylesheet_recascade_nodes;
#endif
                        auto applied = false;
                        if (!filter_stylesheet_candidates
                            || has_appended_css_rule_candidate(
                                existing,
                                first_new_rule)) {
                            applied = apply_appended_css_rules(
                                existing,
                                first_new_rule);
                        }
                        if (!variable_recascade.variables.empty()) {
                            applied |= reapply_css_variable_dependent_rules(
                                existing,
                                variable_recascade);
                        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                        if (profile_startup && applied) {
                            ++startup_stylesheet_candidate_nodes;
                        }
#else
                        static_cast<void>(applied);
#endif
                        for (auto* child : existing.children) {
                            if (child != nullptr) recurse(recurse, *child);
                        }
                    };
                    apply_to_existing(apply_to_existing, document.body());
                } else {
                    frame_last_error_value = "Unable to load connected stylesheet: " + href_iterator->second;
                    ++frame_script_error_count;
                    const auto dispatched = dispatch_resource_event("error");
                    if (dispatched) frame_last_error_value.clear();
                    return dispatched;
                }
            }
        } else if (node.tag == "script") {
            const auto source_iterator = node.attributes.find("src");
            if (source_iterator == node.attributes.end() || source_iterator->second.empty()) return true;
            std::string source;
            std::shared_ptr<const immutable_text_source> immutable_source;
            std::string script_name;
            const auto& base = current_base_address();
            if (!load_connected_text_resource(
                    source_iterator->second,
                    base,
                    WEBSCENE_RESOURCE_SCRIPT,
                    source,
                    script_name,
                    &immutable_source,
                    controlled)) {
                frame_last_error_value = "Unable to load connected script: " + source_iterator->second;
                ++frame_script_error_count;
                const auto dispatched = dispatch_resource_event("error");
                if (dispatched) frame_last_error_value.clear();
                return dispatched;
            }
            std::string error;
            // A dynamically inserted external script and its load event are one
            // browser task. Promise reactions must not run between evaluating
            // the script and dispatching load, otherwise Webpack can re-enter a
            // module graph while an export is still in its TDZ.
            const auto script_source = immutable_source != nullptr
                ? immutable_source->view()
                : std::string_view(source);
            const auto type = node.attributes.find("type");
            const bool module = type != node.attributes.end() && lower_html_name(type->second) == "module";
            if (!(module ? execute_module(local_context,script_source,script_name,error)
                : execute_in_context(local_context,script_source,script_name,error,false,std::move(immutable_source)))) {
                frame_last_error_value = "Connected script failed: " + error;
                ++frame_script_error_count;
                return dispatch_resource_event("error");
            }
            loaded_resource_names.push_back(resource_leaf_name(script_name));
            ++frame_script_execution_count;
        } else if (node.tag == "img") {
            const auto source = node.attributes.find("src");
            if (source == node.attributes.end() || source->second.empty()) {
                node.clear_replaced_image();
                return true;
            }
            auto& image = node.mutable_replaced_image();
            if (!image.complete || image.source != source->second) {
                const auto advance_thumbnail = [&]() {
                    const auto marker = node.attributes.find("data-webscene-thumbnail");
                    if (marker == node.attributes.end() || marker->second != "youtube") return false;
                    const auto next = webscene::embed_fallback::next_thumbnail(source->second);
                    if (!next) return false;
                    source->second = *next;
                    // hqdefault is a 4:3 image containing a letterboxed 16:9
                    // poster. Crop its bars inside the existing clipped link.
                    if (next->ends_with("/hqdefault.jpg")) {
                        for (const css_declaration declaration : {
                                css_declaration{"top", "-16.666667%"},
                                css_declaration{"height", "133.333333%"}}) {
                            if (store_inline_declaration(node, declaration))
                                apply_css_declaration(node, declaration, true);
                        }
                    }
                    node.clear_replaced_image();
                    enqueue_connected_resource_if_needed(node, wrapper, local_context);
                    document.mark_dirty();
                    return true;
                };
                const auto mark_broken_image = [&]() {
                    if (advance_thumbnail()) return true;
                    image.source = source->second;
                    image.resolved_source.clear();
                    image.markup.clear();
                    image.view_box.clear();
                    image.natural_width = 0;
                    image.natural_height = 0;
                    image.encoded_raster = false;
                    image.complete = true;
                    wrapper->Set(
                        local_context,
                        js_string(isolate, "complete"),
                        v8::True(isolate)).Check();
                    wrapper->Set(
                        local_context,
                        js_string(isolate, "currentSrc"),
                        js_string(isolate, source->second.c_str())).Check();
                    wrapper->Set(
                        local_context,
                        js_string(isolate, "naturalWidth"),
                        v8::Integer::New(isolate, 0)).Check();
                    wrapper->Set(
                        local_context,
                        js_string(isolate, "naturalHeight"),
                        v8::Integer::New(isolate, 0)).Check();
                    document.mark_dirty();
                    return dispatch_resource_event("error");
                };
                std::string markup;
                std::string resolved_source;
                const auto& base = current_base_address();
                const auto object_url = object_urls.find(source->second);
                const auto loaded = object_url != object_urls.end()
                    ? (markup = object_url->second,
                        resolved_source = source->second,
                        true)
                    : decode_base64_data_url(source->second, markup)
                    ? (resolved_source = source->second, true)
                    : load_connected_text_resource(
                        source->second,
                        base,
                        WEBSCENE_RESOURCE_IMAGE,
                        markup,
                        resolved_source,
                        nullptr,
                        controlled);
                if (!loaded) {
                    return mark_broken_image();
                }
                const auto view_box = svg_view_box(markup);
                float view_width = 0;
                float view_height = 0;
                const auto encoded_raster = view_box.empty()
                    && raster_image_dimensions(markup, view_width, view_height);
                if (view_box.empty() && !encoded_raster) {
                    return mark_broken_image();
                }
                if (!encoded_raster) {
                    float view_x = 0;
                    float view_y = 0;
                    std::istringstream view_box_stream(view_box);
                    view_box_stream
                        >> view_x >> view_y >> view_width >> view_height;
                }
                if (view_width < 640 && advance_thumbnail()) return true;
                image.source = source->second;
                image.resolved_source = std::move(resolved_source);
                image.markup = std::move(markup);
                image.view_box = view_box;
                image.encoded_raster = encoded_raster;
                image.natural_width = std::max(0.0F, view_width);
                image.natural_height = std::max(0.0F, view_height);
                image.complete = true;
                wrapper->Set(
                    local_context,
                    js_string(isolate, "complete"),
                    v8::True(isolate)).Check();
                wrapper->Set(
                    local_context,
                    js_string(isolate, "currentSrc"),
                    js_string(isolate, image.resolved_source.c_str())).Check();
                wrapper->Set(
                    local_context,
                    js_string(isolate, "naturalWidth"),
                    v8::Number::New(isolate, image.natural_width)).Check();
                wrapper->Set(
                    local_context,
                    js_string(isolate, "naturalHeight"),
                    v8::Number::New(isolate, image.natural_height)).Check();
                document.mark_dirty();
            }
        }
        return dispatch_resource_event("load");
    }
