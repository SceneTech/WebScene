#include "webscene_v8_runtime_select_popup.inc"
    bool promote_pending_promise_error()
    {
        if (pending_promise_rejections.empty()) return true;
        if (diagnostics != nullptr) {
            for (auto& rejection : pending_promise_rejections) {
                if (rejection.diagnostic.message.empty()) continue;
                rejection.diagnostic.promise_rejection = true;
                diagnostics->publish(std::move(rejection.diagnostic));
            }
        }
        last_error = std::move(pending_promise_rejections.front().error);
        pending_promise_rejections.clear();
        return false;
    }

    uint32_t pump_v8_platform_tasks()
    {
        constexpr uint32_t maximum_tasks = 8U;
        constexpr auto maximum_time = std::chrono::milliseconds(1);
        const auto started = std::chrono::steady_clock::now();
        uint32_t count = 0U;
        while (count < maximum_tasks
            && std::chrono::steady_clock::now() - started < maximum_time
            && v8::platform::PumpMessageLoop(
                v8_platform.get(),
                isolate,
                v8::platform::MessageLoopBehavior::kDoNotWait)) {
            ++count;
        }
        return count;
    }

    void perform_microtask_checkpoint()
    {
        isolate->PerformMicrotaskCheckpoint();
        if (work_metrics_enabled.load(std::memory_order_relaxed)) {
            ++work.microtask_checkpoints;
        }
        if (!dispatching_user_input) flush_primary_selection_publication();
    }

    bool execute_timer_task(
        timer_task task,
        std::chrono::steady_clock::time_point now_time)
    {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (profile_startup) {
            if (task.animation_frame) ++startup_raf_executed;
            else ++startup_timer_executed;
        }
#endif
        auto task_context = task.context.Get(isolate);
        if (task_context.IsEmpty()) {
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
            inspector_async_task_canceled(task.id);
#endif
            return true;
        }
        activate_css_cascade(task_context);
        v8::Context::Scope task_context_scope(task_context);
        auto callback = task.callback.Get(isolate);
        if (callback.IsEmpty()) {
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
            inspector_async_task_canceled(task.id);
#endif
            return true;
        }
        const auto collect_work_metrics =
            work_metrics_enabled.load(std::memory_order_relaxed);
        if (collect_work_metrics && task.animation_frame) {
            ++work.animation_frames_invoked;
        } else if (collect_work_metrics) {
            ++work.timers_fired;
            constexpr auto late_threshold = std::chrono::milliseconds(1);
            if (now_time > task.deadline + late_threshold) {
                ++work.late_timers;
                work.total_timer_lateness_nanoseconds +=
                    static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            now_time - task.deadline).count());
            }
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        auto task_profile_name = std::string{};
        auto task_profile_started = std::chrono::steady_clock::time_point{};
        auto task_phase_before = startup_phase_snapshot{};
        std::array<binding_callback_stats, binding_category_count>
            task_binding_profile_before{};
        if (profile_startup) {
            const auto origin = callback->GetScriptOrigin().ResourceName();
            task_profile_name = origin.IsEmpty() || origin->IsUndefined()
                ? std::string("<anonymous>")
                : resource_leaf_name(to_utf8(isolate, origin));
            auto function_name = to_utf8(isolate, callback->GetName());
            if (function_name.empty()) {
                function_name = to_utf8(isolate, callback->GetInferredName());
            }
            if (!function_name.empty()) task_profile_name += ':' + function_name;
            else {
                const auto line = callback->GetScriptLineNumber();
                const auto column = callback->GetScriptColumnNumber();
                if (line >= 0) {
                    task_profile_name += ':' + std::to_string(line + 1);
                    if (column >= 0) task_profile_name += ':' + std::to_string(column + 1);
                }
            }
            task_profile_started = std::chrono::steady_clock::now();
            task_phase_before = capture_startup_phase_snapshot();
        }
        if (profile_bindings) {
            task_binding_profile_before = binding_totals;
        }
#endif
        v8::TryCatch try_catch(isolate);
        std::vector<v8::Local<v8::Value>> arguments;
        if (task.animation_frame) {
            const auto timestamp = task.animation_timestamp_ms >= 0
                ? task.animation_timestamp_ms
                : std::chrono::duration<double, std::milli>(
                    now_time.time_since_epoch()).count();
            arguments.push_back(v8::Number::New(isolate, timestamp));
        } else {
            arguments.reserve(task.arguments.size());
            for (auto& argument : task.arguments) {
                arguments.push_back(argument.Get(isolate));
            }
        }
        active_timer_id = task.id;
        active_timer_cancelled = false;
        style_recascade_batch_scope style_batch(*this);
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
        inspector_async_task_started(task.id);
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        binding_callback_timer callback_timer(
            profile_startup ? &startup_task_callbacks : nullptr);
#endif
        if (callback->Call(
                task_context,
                task_context->Global(),
                static_cast<int>(arguments.size()),
                arguments.empty() ? nullptr : arguments.data()).IsEmpty()) {
            const auto callback_error = describe_reported_exception(try_catch, task_context);
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
            report_inspector_exception(try_catch, task_context);
#endif
            auto exception = try_catch.Exception();
            auto message = try_catch.Message();
            v8::Local<v8::Value> onerror;
            const auto has_onerror = task_context->Global()->Get(
                    task_context,
                    js_string(isolate, "onerror")).ToLocal(&onerror)
                && onerror->IsFunction();
            if (!has_onerror) {
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
                inspector_async_task_finished(task.id);
                if (task.repeating) inspector_async_task_canceled(task.id);
#endif
                active_timer_id = 0;
                last_error = std::string(task.animation_frame
                        ? "requestAnimationFrame task failed: "
                        : "timer task failed: ")
                    + callback_error;
                return false;
            }

            auto source = std::string{};
            auto line = 0;
            auto column = 0;
            if (!message.IsEmpty()) {
                auto resource_name = message->GetScriptOrigin().ResourceName();
                if (!resource_name.IsEmpty() && !resource_name->IsUndefined()) {
                    source = to_utf8(isolate, resource_name);
                }
                line = message->GetLineNumber(task_context).FromMaybe(0);
                column = message->GetStartColumn(task_context).FromMaybe(-1) + 1;
            }
            std::array<v8::Local<v8::Value>, 5U> error_arguments{
                js_string(isolate, callback_error.c_str()),
                js_string(isolate, source.c_str()),
                v8::Integer::New(isolate, line),
                v8::Integer::New(isolate, column),
                exception.IsEmpty() ? v8::Undefined(isolate) : exception};
            try_catch.Reset();
            v8::TryCatch onerror_try_catch(isolate);
            if (onerror.As<v8::Function>()->Call(
                    task_context,
                    task_context->Global(),
                    static_cast<int>(error_arguments.size()),
                    error_arguments.data()).IsEmpty()) {
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
                report_inspector_exception(onerror_try_catch, task_context);
                inspector_async_task_finished(task.id);
                if (task.repeating) inspector_async_task_canceled(task.id);
#endif
                active_timer_id = 0;
                last_error = "Window.onerror handler failed: "
                    + describe_reported_exception(onerror_try_catch, task_context);
                return false;
            }
        }
        perform_microtask_checkpoint();
        style_batch.finish();
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
        inspector_async_task_finished(task.id);
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (profile_startup) {
            const auto task_elapsed_nanoseconds = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - task_profile_started).count());
            auto& stats = startup_task_profiles[task_profile_name];
            ++stats.calls;
            stats.nanoseconds += task_elapsed_nanoseconds;
            record_startup_nested_phases(
                startup_task_phase_profiles,
                task_profile_name,
                task_phase_before,
                task_elapsed_nanoseconds);
            if (std::getenv("WEBSCENE_PROBE_TRACE_STARTUP_TASKS") != nullptr) {
                std::cerr << "[Native Engine Probe] startup-task t=" << std::fixed
                    << std::setprecision(1)
                    << std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - startup_profile_started).count()
                    << "ms elapsed=" << std::fixed << std::setprecision(3)
                    << std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - task_profile_started).count()
                    << "ms kind=" << (task.animation_frame ? "raf" : "timer")
                    << " delay=" << task.interval.count() << "ms"
                    << " name=" << task_profile_name;
                if (profile_bindings) {
                    std::cerr << " bindings=[";
                    auto wrote_binding = false;
                    for (size_t index = 0;
                        index < binding_category_count;
                        ++index) {
                        const auto calls =
                            binding_totals[index].calls
                                - task_binding_profile_before[index].calls;
                        const auto nanoseconds =
                            binding_totals[index].nanoseconds
                                - task_binding_profile_before[index].nanoseconds;
                        if (calls == 0) continue;
                        if (wrote_binding) std::cerr << ',';
                        wrote_binding = true;
                        std::cerr << binding_category_names[index]
                            << ":c" << calls << "/ms" << std::fixed
                            << std::setprecision(3)
                            << nanoseconds / 1'000'000.0;
                    }
                    std::cerr << ']';
                }
                std::cerr << '\n';
            }
        }
#endif
        const auto cancelled = active_timer_cancelled;
        active_timer_id = 0;
        active_timer_cancelled = false;
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
        if (cancelled && task.repeating) {
            inspector_async_task_canceled(task.id);
        }
#endif
        if (task.repeating && !cancelled) {
            const auto current = std::chrono::steady_clock::now();
            do {
                task.deadline +=
                    std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                        task.interval);
            } while (task.deadline <= current);
            timers.push_back(std::move(task));
        }
        return true;
    }

    bool drain_connected_resource_task()
    {
        if (connected_resources.empty()) return true;
        prefetch_connected_resources();
        prefer_timer_task = true;
        auto& pending = connected_resources.front();
        auto pending_context = pending.context.Get(isolate);
        if (pending.node == nullptr || pending_context.IsEmpty()) {
            connected_resources.erase(connected_resources.begin());
            return true;
        }
        activate_css_cascade(pending_context);
        v8::Context::Scope pending_scope(pending_context);
        if (!pending.interception_started) {
            const auto intercepted = begin_connected_resource_interception(pending);
            if (!intercepted) {
                const auto attribute_name = pending.node->tag == "link" ? "href" : "src";
                if (const auto authored = pending.node->attributes.find(attribute_name);
                    authored != pending.node->attributes.end() && !authored->second.empty()) {
                    start_resource_prefetch(
                        authored->second,
                        current_base_address(),
                        connected_resource_kind(*pending.node));
                }
            }
        }
        if (pending.controlled_fetch.valid()
            && pending.controlled_fetch.wait_for(std::chrono::seconds(0))
                != std::future_status::ready
            && std::chrono::steady_clock::now() < pending.controlled_deadline) {
            return true;
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (profile_startup) ++startup_connected_resources;
#endif
        auto resource = std::move(connected_resources.front());
        connected_resources.erase(connected_resources.begin());
        auto resource_context = resource.context.Get(isolate);
        auto wrapper = resource.wrapper.Get(isolate);
        if (resource.node == nullptr
            || resource_context.IsEmpty()
            || wrapper.IsEmpty()) {
            return true;
        }
        activate_css_cascade(resource_context);
        v8::Context::Scope resource_scope(resource_context);
        // A dynamically loaded script, its load event, and the resulting
        // microtasks form one browser task. Keep style invalidations pending
        // across that entire boundary so Webpack/React DOM construction does
        // not recascade synchronously between those phases.
        style_recascade_batch_scope style_batch(
            *this,
            batch_connected_resource_style_recascades);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto trace_resource = profile_startup
            && std::getenv("WEBSCENE_PROBE_TRACE_STARTUP_TASKS") != nullptr;
#else
        constexpr auto trace_resource = false;
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto resource_started = trace_resource
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
#endif
        auto resource_name = std::string{};
        if (trace_resource) {
            if (resource.css_image) {
                resource_name = resource_leaf_name(resource.controlled_url);
            } else {
                const auto attribute_name =
                    resource.node->tag == "script" ? "src" : "href";
                if (const auto attribute =
                        resource.node->attributes.find(attribute_name);
                    attribute != resource.node->attributes.end()) {
                    resource_name = resource_leaf_name(attribute->second);
                }
            }
        }
        std::optional<service_worker_fetch_result> controlled;
        if (resource.controlled_fetch.valid()) {
            if (resource.controlled_fetch.wait_for(std::chrono::seconds(0))
                == std::future_status::ready) {
                try {
                    controlled = resource.controlled_fetch.get();
                } catch (const std::exception& error) {
                    controlled.emplace();
                    controlled->handled = true;
                    controlled->error = std::string(
                        "Service worker connected resource failed: ") + error.what();
                }
            } else {
                controlled.emplace();
                controlled->handled = true;
                controlled->error = "Service worker connected resource timed out";
            }
        }
        if (resource.css_image) {
            apply_controlled_css_image_resource(
                resource,
                controlled.has_value() ? &*controlled : nullptr);
        } else if (!load_connected_resource(
                       *resource.node,
                       wrapper,
                       controlled.has_value() ? &*controlled : nullptr)) {
            last_error = frame_last_error_value;
            return false;
        }
        if (trace_resource) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            const auto completed = std::chrono::steady_clock::now();
            std::cerr << "[Native Engine Probe] startup-resource t="
                << std::fixed << std::setprecision(1)
                << std::chrono::duration<double, std::milli>(
                    completed - startup_profile_started).count()
                << "ms elapsed="
                << std::chrono::duration<double, std::milli>(
                    completed - resource_started).count()
                << "ms kind=" << resource.node->tag
                << " name=" << resource_name << '\n';
#endif
        }
        perform_microtask_checkpoint();
        style_batch.finish();
        return true;
    }

    bool drain_window_message_task()
    {
        if (pending_window_messages.empty()) return true;
        auto task = std::move(pending_window_messages.front());
        pending_window_messages.pop_front();
        const auto packet_bytes = clone_packet_size(task.packet);
        pending_window_message_bytes = packet_bytes > pending_window_message_bytes
            ? 0U : pending_window_message_bytes - packet_bytes;

        auto target_context = task.target_context.Get(isolate);
        auto source_context = task.source_context.Get(isolate);
        auto source_window = task.source_window.Get(isolate);
        if (target_context.IsEmpty() || source_context.IsEmpty()
            || source_window.IsEmpty()) {
            return true;
        }

        activate_css_cascade(target_context);
        v8::Context::Scope target_scope(target_context);
        // A queued message and the microtasks it schedules are one browser
        // task. Data-driven React commits commonly clear many inline style
        // properties while reconciling a result list; coalesce those style
        // invalidations until a synchronous style/layout read or the task
        // boundary instead of recascading the same subtree per property.
        style_recascade_batch_scope style_batch(*this);
        auto target_global = target_context->Global();
        v8::Local<v8::Value> location_value;
        v8::Local<v8::Value> origin_value;
        auto current_target_origin = std::string("null");
        if (target_global->Get(
                target_context,
                js_string(isolate, "location")).ToLocal(&location_value)
            && location_value->IsObject()
            && location_value.As<v8::Object>()->Get(
                target_context,
                js_string(isolate, "origin")).ToLocal(&origin_value)) {
            current_target_origin = to_utf8(isolate, origin_value);
        }
        if (!task.target_origin.empty()
            && task.target_origin != "*"
            && task.target_origin != current_target_origin) {
            return true;
        }

        std::vector<v8::Local<v8::Object>> transferred_ports;
        v8::Local<v8::Value> data;
        v8::TryCatch decode_error(isolate);
        const auto decoded = deserialize_clone(
            isolate,
            target_context,
            task.packet,
            &transferred_ports).ToLocal(&data);
        if (!decoded) decode_error.Reset();
        auto event = v8::Object::New(isolate);
        event->Set(
            target_context,
            js_string(isolate, "type"),
            js_string(isolate, decoded ? "message" : "messageerror")).Check();
        if (decoded) {
            event->Set(
                target_context,
                js_string(isolate, "data"),
                data).Check();
        }
        event->Set(
            target_context,
            js_string(isolate, "origin"),
            js_string(isolate, task.source_origin.c_str())).Check();
        event->Set(
            target_context,
            js_string(isolate, "source"),
            source_window).Check();
        event->Set(
            target_context,
            js_string(isolate, "lastEventId"),
            js_string(isolate, "")).Check();
        auto ports = v8::Array::New(
            isolate,
            static_cast<int>(transferred_ports.size()));
        for (uint32_t index = 0; index < transferred_ports.size(); ++index) {
            ports->Set(target_context, index, transferred_ports[index]).Check();
        }
        event->Set(target_context, js_string(isolate, "ports"), ports).Check();
        event->Set(
            target_context,
            js_string(isolate, "bubbles"),
            v8::False(isolate)).Check();
        event->Set(
            target_context,
            js_string(isolate, "cancelable"),
            v8::False(isolate)).Check();

        v8::Local<v8::Value> dispatch_value;
        if (!target_global->Get(
                target_context,
                js_string(isolate, "dispatchEvent")).ToLocal(&dispatch_value)
            || !dispatch_value->IsFunction()) {
            return true;
        }
        v8::Local<v8::Value> arguments[] = {event};
        v8::TryCatch try_catch(isolate);
        if (dispatch_value.As<v8::Function>()->Call(
                target_context,
                target_global,
                1,
                arguments).IsEmpty()) {
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
            report_inspector_exception(try_catch, target_context);
#endif
            last_error = "Window.postMessage dispatch failed: "
                + describe_reported_exception(try_catch, target_context);
            ++frame_script_error_count;
            return false;
        }
        perform_microtask_checkpoint();
        style_batch.finish();
        return true;
    }

    bool drain_dialog_close_event_task()
    {
        auto task = std::move(pending_dialog_close_events.front());
        pending_dialog_close_events.erase(pending_dialog_close_events.begin());
        auto local_context = task.context.Get(isolate);
        auto* target = document.find_by_native_id(task.target_id);
        if (target == nullptr || local_context.IsEmpty()) return true;
        v8::Context::Scope context_scope(local_context);
        activate_css_cascade(local_context);
        webscene_input_event input{};
        v8::TryCatch try_catch(isolate);
        if (dispatch_input_event_type(input, "close", *target)) return true;
        last_error = "Dialog close dispatch failed: "
            + describe_reported_exception(try_catch, local_context);
        return false;
    }

    void enqueue_programmatic_scroll_event(dom_node& target)
    {
        auto local_context = isolate->GetCurrentContext();
        const auto already_pending = std::any_of(
            pending_programmatic_scroll_events.begin(),
            pending_programmatic_scroll_events.end(),
            [&](const auto& event) {
                return event.target_id == target.id
                    && event.context.Get(isolate) == local_context;
            });
        if (already_pending) return;
        pending_programmatic_scroll_events.push_back(
            pending_programmatic_scroll_event{
                target.id,
                v8::Global<v8::Context>(isolate, local_context),
                current_input_sequence});
    }

    void enqueue_details_toggle_event(
        dom_node& target,
        bool old_open,
        bool new_open)
    {
        if (target.tag != "details" || old_open == new_open) return;
        auto local_context = context_for_node(target);
        const auto existing = std::find_if(
            pending_details_toggle_events.begin(),
            pending_details_toggle_events.end(),
            [&](const auto& event) {
                return event.target_id == target.id
                    && event.context.Get(isolate) == local_context;
            });
        if (existing != pending_details_toggle_events.end()) {
            existing->new_open = new_open;
            return;
        }
        if (pending_details_toggle_events.size()
            >= maximum_pending_details_toggle_events) return;
        pending_details_toggle_events.push_back(pending_details_toggle_event{
            target.id,
            v8::Global<v8::Context>(isolate, local_context),
            old_open,
            new_open});
    }

    void cancel_discrete_wheel_scroll(const dom_node* target = nullptr)
    {
        if (discrete_wheel_scroll == nullptr) return;
        if (target != nullptr
            && discrete_wheel_scroll->target_id != target->id) return;
        discrete_wheel_scroll.reset();
    }

    std::pair<float, float> discrete_wheel_scroll_base(
        const dom_node& target,
        float delta_x,
        float delta_y) const noexcept
    {
        if (discrete_wheel_scroll == nullptr
            || discrete_wheel_scroll->target_id != target.id) {
            return {target.scroll_left, target.scroll_top};
        }
        const auto remaining_x =
            discrete_wheel_scroll->target_x - target.scroll_left;
        const auto remaining_y =
            discrete_wheel_scroll->target_y - target.scroll_top;
        // Same-direction ticks extend the existing destination. A reversal
        // cancels the old destination and starts from the currently painted
        // position, so the old animation never fights the user's input.
        if (delta_x * remaining_x + delta_y * remaining_y < -0.01F) {
            return {target.scroll_left, target.scroll_top};
        }
        return {
            discrete_wheel_scroll->target_x,
            discrete_wheel_scroll->target_y};
    }

    void begin_discrete_wheel_scroll(
        dom_node& target,
        float target_x,
        float target_y,
        v8::Local<v8::Context> target_context,
        uint64_t input_sequence)
    {
        const auto distance = std::hypot(
            target_x - target.scroll_left,
            target_y - target.scroll_top);
        auto animation = std::make_unique<discrete_wheel_scroll_animation>();
        animation->target_id = target.id;
        animation->context.Reset(isolate, target_context);
        animation->start_x = target.scroll_left;
        animation->start_y = target.scroll_top;
        animation->target_x = target_x;
        animation->target_y = target_y;
        animation->start_timestamp_ms = last_animation_frame_timestamp_ms > 0.0
            ? last_animation_frame_timestamp_ms
            : -1.0;
        // A short distance should feel immediate; a coalesced burst gets a
        // little more time instead of crossing hundreds of pixels in one
        // frame. The bounded curve never becomes free-running momentum.
        animation->duration_ms = std::clamp(
            120.0 + static_cast<double>(distance) * 0.08,
            120.0,
            180.0);
        animation->input_sequence = input_sequence;
        discrete_wheel_scroll = std::move(animation);
    }

    bool advance_discrete_wheel_scroll(double timestamp_ms)
    {
        if (discrete_wheel_scroll == nullptr) return false;
        auto* target = document.find_by_native_id(
            discrete_wheel_scroll->target_id);
        auto target_context = discrete_wheel_scroll->context.Get(isolate);
        if (target == nullptr || !is_connected(*target)
            || target_context.IsEmpty()) {
            discrete_wheel_scroll.reset();
            return false;
        }
        activate_css_cascade(target_context);
        v8::Context::Scope context_scope(target_context);
        auto& animation = *discrete_wheel_scroll;
        const auto maximum_x = std::max(
            0.0F, target->scroll_content_width - target->scroll_viewport_width);
        const auto maximum_y = std::max(
            0.0F, target->scroll_content_height - target->scroll_viewport_height);
        animation.target_x = std::clamp(animation.target_x, 0.0F, maximum_x);
        animation.target_y = std::clamp(animation.target_y, 0.0F, maximum_y);
        if (!std::isfinite(timestamp_ms) || timestamp_ms < 0.0) {
            timestamp_ms = last_animation_frame_timestamp_ms;
        }
        if (animation.start_timestamp_ms < 0.0) {
            // The first requested compositor frame is also the first visual
            // response. Seed one nominal frame of elapsed time so a host that
            // was previously idle does not emit an unchanged warm-up frame.
            animation.start_timestamp_ms = timestamp_ms
                - std::min(1000.0 / 60.0, animation.duration_ms);
        }
        const auto progress = std::clamp(
            (timestamp_ms - animation.start_timestamp_ms)
                / animation.duration_ms,
            0.0,
            1.0);
        const auto remaining = 1.0 - progress;
        const auto eased = 1.0 - remaining * remaining * remaining;
        const auto next_x = progress >= 1.0
            ? animation.target_x
            : animation.start_x
                + (animation.target_x - animation.start_x)
                    * static_cast<float>(eased);
        const auto next_y = progress >= 1.0
            ? animation.target_y
            : animation.start_y
                + (animation.target_y - animation.start_y)
                    * static_cast<float>(eased);
        const auto changed = std::abs(next_x - target->scroll_left) > 0.01F
            || std::abs(next_y - target->scroll_top) > 0.01F;
        if (changed) {
            document.set_scroll_offset(*target, next_x, next_y);
            current_input_sequence = animation.input_sequence;
            enqueue_programmatic_scroll_event(*target);
        }
        if (progress >= 1.0
            || (std::abs(animation.target_x - target->scroll_left) <= 0.01F
                && std::abs(animation.target_y - target->scroll_top) <= 0.01F)) {
            discrete_wheel_scroll.reset();
        }
        return changed;
    }

    bool drain_programmatic_scroll_event_task()
    {
        if (pending_programmatic_scroll_events.empty()) return true;
        auto task = std::move(pending_programmatic_scroll_events.front());
        pending_programmatic_scroll_events.erase(
            pending_programmatic_scroll_events.begin());
        auto task_context = task.context.Get(isolate);
        auto* target = document.find_by_native_id(task.target_id);
        if (target == nullptr || task_context.IsEmpty()) return true;
        activate_css_cascade(task_context);
        v8::Context::Scope task_context_scope(task_context);
        webscene_input_event input{};
        input.sequence = task.input_sequence;
        v8::TryCatch try_catch(isolate);
        if (dispatch_input_event_type(input, "scroll", *target)) return true;
        last_error = "Programmatic scroll dispatch failed: "
            + describe_reported_exception(try_catch, task_context);
        return false;
    }

    bool drain_details_toggle_event_task()
    {
        if (pending_details_toggle_events.empty()) return true;
        auto task = std::move(pending_details_toggle_events.front());
        pending_details_toggle_events.erase(pending_details_toggle_events.begin());
        auto task_context = task.context.Get(isolate);
        auto* target = document.find_by_native_id(task.target_id);
        if (target == nullptr || task_context.IsEmpty()) return true;
        activate_css_cascade(task_context);
        v8::Context::Scope task_context_scope(task_context);
        webscene_input_event input{};
        v8::TryCatch try_catch(isolate);
        if (dispatch_input_event_type(
                input, "toggle", *target, nullptr, nullptr, false,
                task.old_open, task.new_open)) return true;
        last_error = "Details toggle dispatch failed: "
            + describe_reported_exception(try_catch, task_context);
        return false;
    }

    bool drain_frame_hydration_task()
    {
        if (pending_frame_hydrations.empty()) return true;
        auto* frame = pending_frame_hydrations.front();
        pending_frame_hydrations.erase(pending_frame_hydrations.begin());
        if (frame == nullptr) return true;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto started = std::chrono::steady_clock::now();
        if (profile_startup) ++startup_frame_hydration_slices;
#endif
        const auto result = hydrate_frame(*frame);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (profile_startup) {
            const auto elapsed = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - started).count());
            startup_frame_hydration_max_slice_nanoseconds = std::max(
                startup_frame_hydration_max_slice_nanoseconds,
                elapsed);
            if (result == frame_hydration_result::pending) {
                ++startup_frame_hydration_yields;
            }
        }
#endif
        if (result == frame_hydration_result::pending) {
            pending_frame_hydrations.push_back(frame);
            return true;
        }
        if (result == frame_hydration_result::complete) return true;
        frame->attributes["data-webscene-frame-error"] =
            frame_last_error_value;
        last_error = frame_last_error_value;
        return false;
    }

    bool drain_due_timer_task()
    {
        const auto now_time = std::chrono::steady_clock::now();
        const auto next = std::min_element(
            timers.begin(),
            timers.end(),
            [](const auto& left, const auto& right) {
                return left.deadline < right.deadline;
            });
        if (next == timers.end() || next->deadline > now_time) return true;
        auto task = std::move(*next);
        timers.erase(next);
        // Allow one ready task from another source before the next due timer.
        // This keeps a self-refilling timer source from starving transport,
        // worker, resource, and DOM event tasks in the opposite direction.
        prefer_timer_task = false;
        prefer_frame_hydration_task = true;
        return execute_timer_task(std::move(task), now_time);
    }

    bool drain_file_reading_task()
    {
        if (pending_file_reading_tasks.empty()) return true;
        auto task = std::move(pending_file_reading_tasks.front());
        pending_file_reading_tasks.pop_front();
        return execute_timer_task(
            std::move(task), std::chrono::steady_clock::now());
    }

    bool drain_tasks()
    {
        if (message_port_wake->consume_reachability_change()) {
            refresh_message_port_reachability();
            refresh_broadcast_channel_reachability();
            refresh_abort_signal_compositions();
        }
        // Native DOM objects are retained by JavaScript wrappers, so report
        // their fixed object storage at browser task boundaries. V8 can then
        // schedule normal incremental/concurrent work from allocation pressure
        // instead of WebScene manufacturing a low-memory event during input.
        synchronize_external_dom_memory_accounting();
        // V8's default platform uses foreground tasks to finish background
        // compilation and optimization work. An embedder must service that
        // queue; otherwise hot application code remains on its initial tier.
        pump_v8_platform_tasks();
        const auto timer_due = has_due_timer();
        // Browser task-source selection is implementation-defined, but it
        // cannot permanently strand one ready source. Alternate a due timer
        // with all other observable task sources instead of placing timers
        // behind every Worker, ServiceWorker, MessagePort, WebSocket, fetch,
        // crypto, dialog, scroll, frame, resource, and observer task.
        if (timer_due && prefer_timer_task) {
            return drain_due_timer_task();
        }
        const auto alternate_other_task = timer_due && !prefer_timer_task;
        if (alternate_other_task) prefer_timer_task = true;
        if (indexeddb_work_ready.load(std::memory_order_acquire)) {
            return drain_indexeddb_completion();
        }
#if defined(WEBSCENE_NATIVE_ENGINE_ENABLE_GRAPHICS)
        if (graphics && !graphics_delivering && graphics->has_ready_work()) {
            struct delivery_guard {
                bool& active;
                explicit delivery_guard(bool& value) : active(value) { active=true; }
                ~delivery_guard() { active=false; }
            } guard(graphics_delivering);
            // execute() also reaches this path after its script context scope
            // has unwound. Enter the owning context explicitly for delivery.
            auto graphics_context = context.Get(isolate);
            v8::Context::Scope graphics_context_scope(graphics_context);
            // Bound each batch and continue task arbitration to avoid starving DOM.
            if (graphics->pump(graphics_deliver, 64))
                perform_microtask_checkpoint();
        }
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_ENABLE_GRAPHICS) && (defined(__APPLE__) || defined(_WIN32))
        publish_ready_gpu_canvases();
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_ENABLE_MEDIA)
        synchronize_media_elements();
        if(drain_media())return true;
#endif
        // Worker, ServiceWorker, MessagePort, BroadcastChannel, WebSocket
        // messages, and file
        // reading callbacks are independent browser task sources. Rotate the
        // first eligible source so a self-refilling source cannot strand
        // another source behind its backlog. FileReader must not inherit a
        // large application timer backlog merely because its compatibility
        // implementation needs an asynchronous task boundary.
        for (uint8_t offset = 0U; offset < 6U; ++offset) {
            const auto source = static_cast<uint8_t>(
                (next_async_message_task_source + offset) % 6U);
            const auto ready = source == 0U ? has_worker_messages()
                : source == 1U ? has_service_worker_messages()
                : source == 2U ? has_message_port_messages()
                : source == 3U ? has_broadcast_channel_messages()
                : source == 4U ? websocket_transport.has_pending_events()
                : !pending_file_reading_tasks.empty();
            if (!ready) continue;
            next_async_message_task_source = static_cast<uint8_t>((source + 1U) % 6U);
            if (source == 0U) return drain_worker_message();
            if (source == 1U) return drain_service_worker_message();
            if (source == 2U) return drain_message_port_message();
            if (source == 3U) return drain_broadcast_channel_message();
            if (source == 4U) return drain_websocket_event();
            return drain_file_reading_task();
        }
        if (!pending_window_messages.empty()) {
            return drain_window_message_task();
        }
        if (has_ready_fetch_task()) {
            return drain_fetch_task();
        }
        if (has_ready_digest_task()) {
            return drain_digest_task();
        }
        if (has_ready_cipher_task()) {
            return drain_cipher_task();
        }
        if (!pending_dialog_close_events.empty()) {
            return drain_dialog_close_event_task();
        }
        if (!pending_details_toggle_events.empty()) {
            return drain_details_toggle_event_task();
        }
        if (!pending_programmatic_scroll_events.empty()) {
            return drain_programmatic_scroll_event_task();
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto cooperative_frames = cooperative_iframe_hydration;
#else
        constexpr auto cooperative_frames = true;
#endif
        const auto connected_resource_ready = has_ready_connected_resource_task();
        const auto competing_task_ready = timer_due
            || connected_resource_ready
            || resize_observers_pending;
        if (!pending_frame_hydrations.empty()
            && (!cooperative_frames
                || !competing_task_ready
                || prefer_frame_hydration_task)) {
            prefer_frame_hydration_task = false;
            return drain_frame_hydration_task();
        }
        // Dynamically connected resources and zero-delay timers are separate
        // browser task sources. Do not starve React/datafeed timers behind an
        // entire Webpack resource wave; alternate when both are ready.
        if (connected_resource_ready
            && (!timer_due || alternate_other_task)) {
            // Preparing a large warm-cache resource wave can itself take a few
            // milliseconds. Only do that work once this task source has won
            // the fairness decision, so an already-due timer runs promptly.
            prefer_frame_hydration_task = true;
            return drain_connected_resource_task();
        }
        if (resize_observers_pending) {
            prefer_frame_hydration_task = true;
            ensure_layout();
            return deliver_resize_observer_checkpoint();
        }
        if (!has_due_timer()) {
            // Detached DOM reclamation is maintenance work. Run one bounded
            // slice only after all observable browser task sources have had
            // priority, so broadening a search cannot delay its input event or
            // network result task behind a stop-the-world native release.
            collect_detached_dom_if_requested();
            return true;
        }
        return drain_due_timer_task();
    }

    bool has_due_timer() const noexcept
    {
        const auto now = std::chrono::steady_clock::now();
        return std::any_of(
            timers.begin(),
            timers.end(),
            [now](const auto& timer) { return timer.deadline <= now; });
    }

    bool has_due_animation_frame_task() const noexcept
    {
        const auto now = std::chrono::steady_clock::now();
        return std::any_of(
            timers.begin(),
            timers.end(),
            [now](const auto& timer) {
                return timer.animation_frame && timer.deadline <= now;
            });
    }

    bool has_waiting_animation_frame_task() const noexcept
    {
        return std::any_of(
            timers.begin(),
            timers.end(),
            [](const auto& timer) {
                return timer.animation_frame
                    && timer.deadline
                        == std::chrono::steady_clock::time_point::max();
            });
    }

    bool drain_animation_frame_task()
    {
        const auto now = std::chrono::steady_clock::now();
        const auto next = std::min_element(
            timers.begin(),
            timers.end(),
            [now](const auto& left, const auto& right) {
                const auto left_due =
                    left.animation_frame && left.deadline <= now;
                const auto right_due =
                    right.animation_frame && right.deadline <= now;
                if (left_due != right_due) return left_due;
                return left.deadline < right.deadline;
            });
        if (next == timers.end()
            || !next->animation_frame
            || next->deadline > now) {
            return true;
        }
        auto task = std::move(*next);
        timers.erase(next);
        return execute_timer_task(std::move(task), now);
    }

#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
    void summarize_resize_cpu_profile(const v8::CpuProfile& profile)
    {
        std::unordered_map<std::string, uint64_t> hits;
        std::vector<std::string> path;
        const auto visit = [&](const auto& self, const v8::CpuProfileNode* node) -> void {
            if (node == nullptr) return;
            auto name = std::string(node->GetFunctionNameStr());
            if (name.empty()) name = "(anonymous)";
            const auto resource = std::string(node->GetScriptResourceNameStr());
            if (!resource.empty()) {
                const auto leaf = std::filesystem::path(resource).filename().string();
                name += "@" + (leaf.empty() ? resource : leaf);
            }
            name += "#" + std::to_string(node->GetScriptId())
                + ":" + std::to_string(node->GetLineNumber())
                + "/t" + std::to_string(static_cast<int>(node->GetSourceType()));
            path.push_back(std::move(name));
            const auto hit_count = node->GetHitCount();
            if (hit_count != 0) {
                std::ostringstream key;
                const auto begin = path.size() > 5U ? path.size() - 5U : 0U;
                for (size_t index = begin; index < path.size(); ++index) {
                    if (index != begin) key << '>';
                    key << path[index];
                }
                hits[std::move(key).str()] += hit_count;
            }
            for (int index = 0; index < node->GetChildrenCount(); ++index) {
                self(self, node->GetChild(index));
            }
            path.pop_back();
        };
        visit(visit, profile.GetTopDownRoot());
        std::vector<std::pair<std::string, uint64_t>> ordered(hits.begin(), hits.end());
        std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
            return left.second > right.second;
        });
        std::ostringstream result;
        result << "samples=" << profile.GetSamplesCount() << "/top{";
        const auto limit = std::min<size_t>(12U, ordered.size());
        for (size_t index = 0; index < limit; ++index) {
            if (index != 0) result << ';';
            result << ordered[index].first << ':' << ordered[index].second;
        }
        result << '}';
        last_resize_cpu_profile = std::move(result).str();
        if (profile_resize_cpu) {
            std::cerr << "WebScene resize CPU profile: "
                << last_resize_cpu_profile << '\n';
        }
    }
#endif

    v8::Local<v8::Object> create_event_instance(
        v8::Local<v8::Context> local_context,
        const char* constructor_name = "Event")
    {
        v8::Local<v8::Value> constructor_value;
        if (local_context->Global()->Get(
                local_context,
                js_string(isolate, constructor_name)).ToLocal(&constructor_value)
            && constructor_value->IsFunction()) {
            auto constructor = constructor_value.As<v8::Function>();
            if (auto instance = constructor->NewInstance(local_context, 0, nullptr);
                !instance.IsEmpty()) {
                return instance.ToLocalChecked();
            }
        }
        return v8::Object::New(isolate);
    }

    v8::Local<v8::Object> create_window_event(
        v8::Local<v8::Context> local_context,
        std::string_view type)
    {
        auto event = create_event_instance(local_context);
        auto global = local_context->Global();
        event->Set(
            local_context,
            js_string(isolate, "type"),
            js_string(isolate, std::string(type).c_str())).Check();
        event->Set(local_context, js_string(isolate, "target"), global).Check();
        event->Set(local_context, js_string(isolate, "currentTarget"), global).Check();
        event->Set(local_context, js_string(isolate, "srcElement"), global).Check();
        event->Set(local_context, js_string(isolate, "view"), global).Check();
        event->Set(local_context, js_string(isolate, "bubbles"), v8::False(isolate)).Check();
        event->Set(local_context, js_string(isolate, "cancelable"), v8::False(isolate)).Check();
        event->Set(local_context, js_string(isolate, "defaultPrevented"), v8::False(isolate)).Check();
        return event;
    }

    bool dispatch_media_query_list_changes()
    {
        // Authored callbacks can call matchMedia and reallocate the owner vector.
        // Keep only the stable pointee across calls and snapshot the dispatch set.
        const auto query_count = media_query_lists.size();
        for (size_t query_index = 0; query_index < query_count; ++query_index) {
            auto* state = media_query_lists[query_index].get();
            if (state == nullptr) continue;
            const auto matches = media_query_matches(state->query);
            if (matches == state->matches) continue;
            state->matches = matches;
            auto local_context = state->context.Get(isolate);
            auto wrapper = state->wrapper.Get(isolate);
            if (local_context.IsEmpty() || wrapper.IsEmpty()) continue;
            v8::Context::Scope context_scope(local_context);
            wrapper->Set(
                local_context,
                js_string(isolate, "matches"),
                v8::Boolean::New(isolate, matches)).Check();
            auto event = create_event_instance(local_context);
            event->Set(
                local_context,
                js_string(isolate, "type"),
                js_string(isolate, "change")).Check();
            event->Set(local_context, js_string(isolate, "target"), wrapper).Check();
            event->Set(local_context, js_string(isolate, "currentTarget"), wrapper).Check();
            event->Set(local_context, js_string(isolate, "srcElement"), wrapper).Check();
            event->Set(
                local_context,
                js_string(isolate, "matches"),
                v8::Boolean::New(isolate, matches)).Check();
            event->Set(
                local_context,
                js_string(isolate, "media"),
                js_string(isolate, state->query.c_str())).Check();
            event->Set(
                local_context,
                js_string(isolate, "bubbles"),
                v8::False(isolate)).Check();
            event->Set(
                local_context,
                js_string(isolate, "cancelable"),
                v8::False(isolate)).Check();
            v8::Local<v8::Value> arguments[] = {event};
            const auto listener_count = state->listeners.size();
            for (size_t index = 0; index < listener_count; ++index) {
                auto& registration = state->listeners[index];
                auto callback = registration.callback.Get(isolate);
                if (callback.IsEmpty()) continue;
                if (registration.once) registration.callback.Reset();
                v8::TryCatch try_catch(isolate);
                if (call_event_listener(
                        isolate,
                        local_context,
                        callback,
                        wrapper,
                        1,
                        arguments).IsEmpty()) {
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
                    report_inspector_exception(try_catch, local_context);
#endif
                    last_error = describe_reported_exception(try_catch);
                    return false;
                }
            }
            std::erase_if(
                state->listeners,
                [](const auto& listener) { return listener.callback.IsEmpty(); });
            v8::Local<v8::Value> onchange;
            if (!wrapper->Get(
                    local_context,
                    js_string(isolate, "onchange")).ToLocal(&onchange)) {
                return false;
            }
            if (!onchange->IsNullOrUndefined()) {
                v8::TryCatch try_catch(isolate);
                if (call_event_listener(
                        isolate,
                        local_context,
                        onchange,
                        wrapper,
                        1,
                        arguments).IsEmpty()) {
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
                    report_inspector_exception(try_catch, local_context);
#endif
                    last_error = describe_reported_exception(try_catch);
                    return false;
                }
            }
            perform_microtask_checkpoint();
        }
        return true;
    }

    bool dispatch_resize()
    {
        const auto trace_resize_phases =
            std::getenv("WEBSCENE_TRACE_RESIZE_PHASES") != nullptr;
        const auto trace_resize_started = trace_resize_phases
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        record_feature(
            "event", "type:resize", "supported", {}, "event-dispatch");
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        struct resize_profile_scope final {
            implementation& owner;

            explicit resize_profile_scope(implementation& value)
                : owner(value)
            {
                owner.resize_profile_active = owner.profile_bindings;
                if (owner.resize_profile_active) {
                    owner.last_resize_style_property_counts.clear();
                    owner.last_resize_style_target_counts.clear();
                    owner.last_resize_attribute_counts.clear();
                    owner.last_resize_geometry_counts.clear();
                    owner.last_resize_redundant_style_writes = 0;
                }
            }

            ~resize_profile_scope()
            {
                owner.resize_profile_active = false;
            }
        } profile_scope{*this};
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto resize_started = std::chrono::steady_clock::now();
#endif
        auto isolate_locker = lock_shared_isolate();
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        activate_css_cascade(context.Get(isolate));
        // A resize event, its frame listeners and their microtasks are one
        // rendering task. Attribute-heavy libraries such as OverlayScrollbars
        // may update the same selector-bearing nodes repeatedly while measuring
        // the new viewport. Accumulate those invalidations until the synchronous
        // layout barrier instead of recascading intermediate states.
        style_recascade_batch_scope style_batch(*this);
        {
            auto local_context = context.Get(isolate);
            v8::Context::Scope context_scope(local_context);
            auto global = local_context->Global();
            auto event = create_window_event(local_context, "resize");
            v8::Local<v8::Value> arguments[] = {event};
            const auto listener_count = resize_listeners.size();
            for (size_t index = 0; index < listener_count; ++index) {
                auto& listener = resize_listeners[index];
                auto callback = listener.callback.Get(isolate);
                if (callback.IsEmpty()) continue;
                if (!listener.signal.IsEmpty()) {
                    auto signal = listener.signal.Get(isolate);
                    v8::Local<v8::Value> aborted;
                    if (signal->Get(
                            local_context,
                            js_string(isolate, "aborted")).ToLocal(&aborted)
                        && aborted->BooleanValue(isolate)) {
                        clear_event_listener(listener);
                        continue;
                    }
                }
                const auto passive = listener.passive;
                if (listener.once) {
                    clear_event_listener(listener);
                }
                event->Set(
                    local_context,
                    js_string(isolate, "__websceneInPassiveListener"),
                    v8::Boolean::New(isolate, passive)).Check();
                v8::TryCatch try_catch(isolate);
                const auto callback_succeeded =
                    !call_event_listener(
                        isolate,
                        local_context,
                        callback,
                        global,
                        1,
                        arguments).IsEmpty();
                event->Set(
                    local_context,
                    js_string(isolate, "__websceneInPassiveListener"),
                    v8::False(isolate)).Check();
                if (!callback_succeeded) {
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
                    report_inspector_exception(try_catch, local_context);
#endif
                    last_error = describe_reported_exception(try_catch);
                    return false;
                }
            }
            std::erase_if(
                resize_listeners,
                [](const auto& listener) { return listener.callback.IsEmpty(); });
            perform_microtask_checkpoint();
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto outer_listeners_finished = std::chrono::steady_clock::now();
        last_resize_outer_listeners_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                outer_listeners_finished - resize_started).count());
#endif
        const auto trace_outer_listeners_finished = trace_resize_phases
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};

        // Media matches and selector indexes belong to each browsing context.
        // Recompute only contexts whose query truth changed, retaining relative
        // lengths for layout to resolve against the new host viewport.
        const auto refresh_context_styles = [&](v8::Local<v8::Context> css_context) {
            if (css_context.IsEmpty()) return;
            activate_css_cascade(css_context);
            v8::Context::Scope css_context_scope(css_context);
            std::vector<size_t> changed_rules;
            if (!refresh_css_media_matches(&changed_rules)) return;
            rebuild_css_rule_indexes_and_root_variables();
            recascade_media_query_changes(active_root(), changed_rules);
        };
        refresh_context_styles(context.Get(isolate));
        for (auto& [id, persistent_context] : actual_frame_contexts) {
            static_cast<void>(id);
            if (!persistent_context.IsEmpty()) {
                refresh_context_styles(persistent_context.Get(isolate));
            }
        }
        activate_css_cascade(context.Get(isolate));
        if (!dispatch_media_query_list_changes()) return false;
        const auto trace_media_finished = trace_resize_phases
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};

        // The outer document also needs the post-layout observation checkpoint
        // when no iframe exists (including a scale-only host resize).
        if (!frame_context.IsEmpty()) {
            auto local_frame_context = frame_context.Get(isolate);
            activate_css_cascade(local_frame_context);
            v8::Context::Scope frame_scope(local_frame_context);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            v8::Local<v8::String> cpu_profile_title;
            auto cpu_profile_started = false;
            if (cpu_profiler != nullptr
                && (profile_bindings || !resize_cpu_profile_captured)) {
                cpu_profile_title = js_string(isolate, "webscene-resize-listener");
                cpu_profile_started = cpu_profiler->StartProfiling(cpu_profile_title, true)
                    == v8::CpuProfilingStatus::kStarted;
            }
            const auto stop_cpu_profile = [&] {
                if (!cpu_profile_started) return;
                if (auto* profile = cpu_profiler->StopProfiling(cpu_profile_title); profile != nullptr) {
                    summarize_resize_cpu_profile(*profile);
                    profile->Delete();
                }
                cpu_profile_started = false;
                resize_cpu_profile_captured = true;
                // Starting and stopping V8's profiler on every resize is itself
                // expensive enough to halve an otherwise 60 Hz stream. The
                // low-overhead mode deliberately captures one representative
                // callback, then removes the profiler from the live isolate.
                if (!profile_bindings && cpu_profiler != nullptr) {
                    cpu_profiler->Dispose();
                    cpu_profiler = nullptr;
                }
            };
            const auto binding_profile_before = binding_totals;
#endif
            auto listeners = frame_event_listeners.find("resize");
            if (listeners != frame_event_listeners.end()) {
                auto event = create_window_event(local_frame_context, "resize");
                v8::Local<v8::Value> arguments[] = {event};
                ++input_event_dispatch_count;
                auto frame_global = local_frame_context->Global();
                const auto listener_count = listeners->second.size();
                for (size_t index = 0; index < listener_count; ++index) {
                    auto& listener = listeners->second[index];
                    if (listener.target != 0U
                        || listener.context.Get(isolate) != local_frame_context) {
                        continue;
                    }
                    auto callback = listener.callback.Get(isolate);
                    if (callback.IsEmpty()) continue;
                    if (!listener.signal.IsEmpty()) {
                        auto signal = listener.signal.Get(isolate);
                        v8::Local<v8::Value> aborted;
                        if (signal->Get(
                                local_frame_context,
                                js_string(isolate, "aborted")).ToLocal(&aborted)
                            && aborted->BooleanValue(isolate)) {
                            clear_event_listener(listener);
                            continue;
                        }
                    }
                    const auto passive = listener.passive;
                    if (listener.once) {
                        clear_event_listener(listener);
                    }
                    event->Set(
                        local_frame_context,
                        js_string(isolate, "__websceneInPassiveListener"),
                        v8::Boolean::New(isolate, passive)).Check();
                    v8::TryCatch try_catch(isolate);
                    const auto callback_succeeded = !call_event_listener(
                            isolate,
                            local_frame_context,
                            callback,
                            frame_global,
                            1,
                            arguments).IsEmpty();
                    event->Set(
                        local_frame_context,
                        js_string(isolate, "__websceneInPassiveListener"),
                        v8::False(isolate)).Check();
                    if (!callback_succeeded) {
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
                        report_inspector_exception(
                            try_catch,
                            local_frame_context);
#endif
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                        stop_cpu_profile();
#endif
                        last_error = describe_reported_exception(try_catch, local_frame_context);
                        return false;
                    }
                    ++input_callback_invocation_count;
                }
                std::erase_if(
                    listeners->second,
                    [](const auto& listener) { return listener.callback.IsEmpty(); });
            }
            perform_microtask_checkpoint();
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            stop_cpu_profile();
            for (size_t index = 0; index < binding_category_count; ++index) {
                last_resize_binding_profile[index] = binding_callback_stats{
                    binding_totals[index].calls - binding_profile_before[index].calls,
                    binding_totals[index].nanoseconds - binding_profile_before[index].nanoseconds};
            }
#endif
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto frame_listeners_finished = std::chrono::steady_clock::now();
        last_resize_frame_listeners_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                frame_listeners_finished - outer_listeners_finished).count());
#endif
        const auto trace_frame_listeners_finished = trace_resize_phases
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        style_batch.finish();
        const auto trace_batch_finished = trace_resize_phases
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        if (shared_isolate != nullptr) {
            isolate->Exit();
            try {
                {
                    v8::Unlocker isolate_unlocker(isolate);
                    ensure_layout();
                }
            } catch (...) {
                isolate->Enter();
                throw;
            }
            isolate->Enter();
        } else {
            ensure_layout();
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto layout_finished = std::chrono::steady_clock::now();
        last_resize_layout_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                layout_finished - frame_listeners_finished).count());
#endif
        const auto trace_layout_finished = trace_resize_phases
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        const auto result = deliver_resize_observer_checkpoint();
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        last_resize_observers_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - layout_finished).count());
#endif
        if (trace_resize_phases) {
            const auto milliseconds = [](auto from, auto to) {
                return std::chrono::duration<double, std::milli>(to - from).count();
            };
            const auto trace_finished = std::chrono::steady_clock::now();
            std::cerr << "WebScene resize phases: outer-ms="
                << milliseconds(trace_resize_started, trace_outer_listeners_finished)
                << ", media-ms="
                << milliseconds(trace_outer_listeners_finished, trace_media_finished)
                << ", frame-listeners-ms="
                << milliseconds(trace_media_finished, trace_frame_listeners_finished)
                << ", batched-recascade-ms="
                << milliseconds(trace_frame_listeners_finished, trace_batch_finished)
                << ", layout-ms="
                << milliseconds(trace_batch_finished, trace_layout_finished)
                << ", observers-ms="
                << milliseconds(trace_layout_finished, trace_finished)
                << ", total-ms="
                << milliseconds(trace_resize_started, trace_finished) << '\n';
        }
        return result;
    }

    bool deliver_resize_observer_checkpoint()
    {
        // ResizeObserver delivery belongs to the rendering lifecycle after
        // style/layout and before paint. A callback may change the observed
        // geometry, so repeat layout and observation until the sizes settle.
        // This is the bounded equivalent of Blink's resize-observation loop.
        resize_observers_pending = false;
        constexpr auto maximum_depth = size_t{10};
        for (auto depth = size_t{0}; depth < maximum_depth; ++depth) {
            if (!dispatch_resize_observers()) return false;
            if (!document.dirty()) return true;
            ensure_layout();
        }
        return true;
    }

    std::pair<double, double> resize_observer_box_size(
        const dom_node& node, resize_observer_box box, double scale) const
    {
        if (has_display_none_ancestor(node)) return {0, 0};
        if (box == resize_observer_box::border) {
            return {std::max(0.0F, node.layout.width), std::max(0.0F, node.layout.height)};
        }
        const auto content = connected_box_for(node);
        if (box == resize_observer_box::device_pixel_content) {
            // Physical content extents are integral, unlike CSS box sizes.
            // Do not use transformed/client rects or alias the CSS-pixel box:
            // canvas libraries use this value as the bitmap backing size.
            return {std::round(content.content_width * scale),
                std::round(content.content_height * scale)};
        }
        return {content.content_width, content.content_height};
    }

    bool dispatch_resize_observers()
    {
        const auto requested_scale = viewport_provider().device_scale_factor;
        const auto scale = std::isfinite(requested_scale) && requested_scale > 0
            ? requested_scale : 1.0;
        std::vector<resize_observer_state*> pending;
        pending.reserve(resize_observers.size());
        for (const auto& observer : resize_observers) {
            if (observer != nullptr) pending.push_back(observer.get());
        }
        for (auto* observer : pending) {
            if (observer == nullptr || observer->nodes.empty()) continue;
            std::vector<dom_node*> changed_nodes;
            changed_nodes.reserve(observer->nodes.size());
            for (auto* node : observer->nodes) {
                if (node == nullptr) continue;
                const auto size = resize_observer_box_size(
                    *node, observer->observed_boxes.at(node->id), scale);
                const auto previous = observer->delivered_sizes.find(node->id);
                if (previous == observer->delivered_sizes.end() || previous->second != size) {
                    changed_nodes.push_back(node);
                    observer->delivered_sizes[node->id] = size;
                }
            }
            if (changed_nodes.empty()) continue;
            auto observer_context = observer->context.Get(isolate);
            auto callback = observer->callback.Get(isolate);
            if (observer_context.IsEmpty() || callback.IsEmpty()) continue;
            activate_css_cascade(observer_context);
            v8::Context::Scope observer_scope(observer_context);
            auto entries = v8::Array::New(isolate, static_cast<int>(changed_nodes.size()));
            for (uint32_t index = 0; index < changed_nodes.size(); ++index) {
                auto* node = changed_nodes[index];
                if (node == nullptr) continue;
                const auto content = resize_observer_box_size(*node, resize_observer_box::content, scale);
                const auto border = resize_observer_box_size(*node, resize_observer_box::border, scale);
                const auto device_pixels = resize_observer_box_size(*node, resize_observer_box::device_pixel_content, scale);
                const auto padding_width = connected_box_for(*node).padding_width;
                const auto x = resolve_connected_css_length(*node, node->style.padding_left, padding_width);
                const auto y = resolve_connected_css_length(*node, node->style.padding_top, padding_width);
                auto rect = v8::Object::New(isolate);
                const auto set = [&](const char* name, double value) {
                    rect->Set(
                        observer_context,
                        js_string(isolate, name),
                        v8::Number::New(isolate, value)).Check();
                };
                set("x", x);
                set("y", y);
                set("width", content.first);
                set("height", content.second);
                set("top", y);
                set("left", x);
                set("right", x + content.first);
                set("bottom", y + content.second);
                auto entry = v8::Object::New(isolate);
                entry->Set(observer_context, js_string(isolate, "target"), wrap_node(*node)).Check();
                entry->Set(observer_context, js_string(isolate, "contentRect"), rect).Check();
                const auto make_box_array = [&](std::pair<double, double> size) {
                    auto box_size = v8::Object::New(isolate);
                    box_size->Set(
                        observer_context,
                        js_string(isolate, "inlineSize"),
                        v8::Number::New(isolate, size.first)).Check();
                    box_size->Set(
                        observer_context,
                        js_string(isolate, "blockSize"),
                        v8::Number::New(isolate, size.second)).Check();
                    auto values = v8::Array::New(isolate, 1);
                    values->Set(observer_context, 0, box_size).Check();
                    return values;
                };
                entry->Set(
                    observer_context,
                    js_string(isolate, "contentBoxSize"),
                    make_box_array(content)).Check();
                entry->Set(
                    observer_context,
                    js_string(isolate, "borderBoxSize"),
                    make_box_array(border)).Check();
                entry->Set(
                    observer_context,
                    js_string(isolate, "devicePixelContentBoxSize"),
                    make_box_array(device_pixels)).Check();
                entries->Set(observer_context, index, entry).Check();
            }
            v8::Local<v8::Value> arguments[] = {entries, v8::Undefined(isolate)};
            v8::TryCatch try_catch(isolate);
            if (callback->Call(
                    observer_context,
                    observer_context->Global(),
                    2,
                    arguments).IsEmpty()) {
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
                report_inspector_exception(try_catch, observer_context);
#endif
                last_error = describe_reported_exception(try_catch, observer_context);
                return false;
            }
            ++input_callback_invocation_count;
            perform_microtask_checkpoint();
        }
        return true;
    }

    static void event_prevent_default(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto context = info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::Value> passive;
        if (info.This()->Get(
                context,
                js_string(info.GetIsolate(), "__websceneInPassiveListener")).ToLocal(&passive)
            && passive->BooleanValue(info.GetIsolate())) {
            return;
        }
        v8::Local<v8::Value> cancelable;
        if (!info.This()->Get(
                context,
                js_string(info.GetIsolate(), "cancelable")).ToLocal(&cancelable)
            || !cancelable->BooleanValue(info.GetIsolate())) return;
        info.This()->Set(
            context,
            js_string(info.GetIsolate(), "defaultPrevented"),
            v8::True(info.GetIsolate())).Check();
    }

    static void event_return_value_get(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto context = info.GetIsolate()->GetCurrentContext();
        v8::Local<v8::Value> prevented;
        const auto default_prevented = info.This()->Get(
                context,
                js_string(info.GetIsolate(), "defaultPrevented")).ToLocal(&prevented)
            && prevented->BooleanValue(info.GetIsolate());
        info.GetReturnValue().Set(v8::Boolean::New(info.GetIsolate(), !default_prevented));
    }

    static void event_return_value_set(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if (info.Length() == 0 || info[0]->BooleanValue(info.GetIsolate())) return;
        event_prevent_default(info);
    }

    static void event_stop_propagation(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        info.This()->Set(
            info.GetIsolate()->GetCurrentContext(),
            js_string(info.GetIsolate(), "__webscenePropagationStopped"),
            v8::True(info.GetIsolate())).Check();
    }

    static void event_stop_immediate_propagation(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto context = info.GetIsolate()->GetCurrentContext();
        info.This()->Set(
            context,
            js_string(info.GetIsolate(), "__webscenePropagationStopped"),
            v8::True(info.GetIsolate())).Check();
        info.This()->Set(
            context,
            js_string(info.GetIsolate(), "__websceneImmediatePropagationStopped"),
            v8::True(info.GetIsolate())).Check();
    }

    static void event_composed_path(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* isolate = info.GetIsolate();
        auto local_context = isolate->GetCurrentContext();
        v8::Local<v8::Value> stored;
        if (!info.This()->Get(
                local_context,
                js_string(isolate, "__webSceneComposedPath")).ToLocal(&stored)
            || !stored->IsArray()) {
            info.GetReturnValue().Set(v8::Array::New(isolate));
            return;
        }
        auto source = stored.As<v8::Array>();
        auto result = v8::Array::New(isolate, static_cast<int>(source->Length()));
        for (uint32_t index = 0; index < source->Length(); ++index) {
            v8::Local<v8::Value> entry;
            if (source->Get(local_context, index).ToLocal(&entry)) {
                result->Set(local_context, index, entry).Check();
            }
        }
        info.GetReturnValue().Set(result);
    }

    static std::string utf8_scalar(uint32_t value)
    {
        if (value > 0x10FFFFU || (value >= 0xD800U && value <= 0xDFFFU)) return {};
        std::string result;
        if (value <= 0x7FU) {
            result.push_back(static_cast<char>(value));
        } else if (value <= 0x7FFU) {
            result.push_back(static_cast<char>(0xC0U | (value >> 6U)));
            result.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        } else if (value <= 0xFFFFU) {
            result.push_back(static_cast<char>(0xE0U | (value >> 12U)));
            result.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        } else {
            result.push_back(static_cast<char>(0xF0U | (value >> 18U)));
            result.push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
        }
        return result;
    }

    static std::string keyboard_key_name(int key_code)
    {
        switch (key_code) {
        case 8: return "Backspace";
        case 9: return "Tab";
        case 13: return "Enter";
        case 16: return "Shift";
        case 17: return "Control";
        case 18: return "Alt";
        case 27: return "Escape";
        case 32: return " ";
        case 33: return "PageUp";
        case 34: return "PageDown";
        case 35: return "End";
        case 36: return "Home";
        case 37: return "ArrowLeft";
        case 38: return "ArrowUp";
        case 39: return "ArrowRight";
        case 40: return "ArrowDown";
        case 46: return "Delete";
        case 91: return "Meta";
        case 186: return ";";
        case 187: return "=";
        case 188: return ",";
        case 189: return "-";
        case 190: return ".";
        case 191: return "/";
        case 192: return "`";
        case 219: return "[";
        case 220: return "\\";
        case 221: return "]";
        case 222: return "'";
        default:
            if ((key_code >= '0' && key_code <= '9')
                || (key_code >= 'A' && key_code <= 'Z')) {
                return std::string(1, static_cast<char>(key_code));
            }
            return "Unidentified";
        }
    }

    static std::string keyboard_code_name(int key_code)
    {
        if (key_code >= 'A' && key_code <= 'Z') {
            return "Key" + std::string(1, static_cast<char>(key_code));
        }
        if (key_code >= '0' && key_code <= '9') {
            return "Digit" + std::string(1, static_cast<char>(key_code));
        }
        switch (key_code) {
        case 16: return "ShiftLeft";
        case 17: return "ControlLeft";
        case 18: return "AltLeft";
        case 91: return "MetaLeft";
        case 186: return "Semicolon";
        case 187: return "Equal";
        case 188: return "Comma";
        case 189: return "Minus";
        case 190: return "Period";
        case 191: return "Slash";
        case 192: return "Backquote";
        case 219: return "BracketLeft";
        case 220: return "Backslash";
        case 221: return "BracketRight";
        case 222: return "Quote";
        default: break;
        }
        return keyboard_key_name(key_code);
    }

    bool dispatch_input_event_type(
        const webscene_input_event& input,
        const char* type,
        dom_node& target,
        bool* default_prevented = nullptr,
        dom_node* submitter = nullptr,
        std::optional<bool> trusted = std::nullopt,
        std::optional<bool> toggle_old_open = std::nullopt,
        std::optional<bool> toggle_new_open = std::nullopt)
    {
        const auto type_view = std::string_view(type);
        if ((type_view == "input" || type_view == "change")
            && document.validation_message_target_id() == target.id) {
            dismiss_validation_message();
        }
        webscene_frame_trace::scope trace(frame_trace, "event-start", "event-end", input.sequence);
        const auto previous_context = isolate->GetCurrentContext();
        auto local_context = context_for_node(target);
        v8::Context::Scope event_context_scope(local_context);
        activate_css_cascade(local_context);
        const auto restore_cascade = [this, previous_context](void*) {
            activate_css_cascade(previous_context);
        };
        std::unique_ptr<void, decltype(restore_cascade)> cascade_scope(this, restore_cascade);
        // Event listeners and their microtasks form one browser task. A
        // computed-style or geometry getter calls ensure_layout() and remains
        // a synchronous style-recalculation barrier within this scope.
        style_recascade_batch_scope style_batch(*this);
        record_feature(
            "event",
            std::string("type:") + type,
            "supported",
            {},
            "event-dispatch");
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        ++event_dispatch_counts[type];
        ++event_dispatch_target_counts[type][target.id];
#endif
        const auto callback_count_before = input_callback_invocation_count;
        const auto drag_event = type_view == "dragstart" || type_view == "dragend"
            || type_view == "dragenter"
            || type_view == "dragover" || type_view == "dragleave"
            || type_view == "drop";
        auto event = create_event_instance(
            local_context, drag_event ? "DragEvent" : "Event");
        auto target_wrapper = wrap_node(target);
        const auto set_number = [&](const char* name, double value) {
            event->Set(local_context, js_string(isolate, name), v8::Number::New(isolate, value)).Check();
        };
        const auto bubbles = type_view != "beforetoggle"
            && type_view != "toggle"
            && type_view != "close"
            && type_view != "cancel"
            && type_view != "blur"
            && type_view != "focus"
            && type_view != "mouseenter"
            && type_view != "mouseleave"
            && type_view != "pointerenter"
            && type_view != "pointerleave"
            && type_view != "scroll";
        const auto cancelable = type_view != "close"
            && type_view != "toggle"
            && type_view != "blur"
            && type_view != "focus"
            && type_view != "focusin"
            && type_view != "focusout"
            && type_view != "mouseenter"
            && type_view != "mouseleave"
            && type_view != "pointerenter"
            && type_view != "pointerleave"
            && type_view != "input"
            && type_view != "scroll";
        event->Set(local_context, js_string(isolate, "type"), js_string(isolate, type)).Check();
        event->Set(local_context, js_string(isolate, "target"), target_wrapper).Check();
        event->Set(local_context, js_string(isolate, "srcElement"), target_wrapper).Check();
        event->Set(local_context, js_string(isolate, "view"), local_context->Global()).Check();
        event->Set(local_context, js_string(isolate, "bubbles"), v8::Boolean::New(isolate, bubbles)).Check();
        event->Set(local_context, js_string(isolate, "cancelable"), v8::Boolean::New(isolate, cancelable)).Check();
        event->Set(local_context, js_string(isolate, "defaultPrevented"), v8::False(isolate)).Check();
        if (drag_event && active_drag_session.has_value()) {
            event->Set(
                local_context,
                js_string(isolate, "dataTransfer"),
                drag_data_transfer_for(target)).Check();
        } else if (drag_event && active_outbound_drag.has_value()
            && !active_outbound_drag->transfer.IsEmpty()) {
            event->Set(
                local_context,
                js_string(isolate, "dataTransfer"),
                active_outbound_drag->transfer.Get(isolate)).Check();
        }
        event->Set(
            local_context,
            js_string(isolate, "isTrusted"),
            v8::Boolean::New(isolate, trusted.value_or(dispatching_user_input))).Check();
        if (type_view == "beforetoggle") {
            event->Set(local_context, js_string(isolate, "oldState"), js_string(isolate, "closed")).Check();
            event->Set(local_context, js_string(isolate, "newState"), js_string(isolate, "open")).Check();
            event->Set(local_context, js_string(isolate, "source"), v8::Null(isolate)).Check();
        } else if (type_view == "toggle"
            && toggle_old_open.has_value()
            && toggle_new_open.has_value()) {
            event->Set(
                local_context,
                js_string(isolate, "oldState"),
                js_string(isolate, *toggle_old_open ? "open" : "closed")).Check();
            event->Set(
                local_context,
                js_string(isolate, "newState"),
                js_string(isolate, *toggle_new_open ? "open" : "closed")).Check();
        }
        if (type_view == "submit") {
            event->Set(
                local_context,
                js_string(isolate, "submitter"),
                submitter == nullptr
                    ? v8::Local<v8::Value>(v8::Null(isolate))
                    : v8::Local<v8::Value>(wrap_node(*submitter))).Check();
        }
        event->Set(local_context, js_string(isolate, "pointerType"), js_string(isolate, "mouse")).Check();
        event->Set(local_context, js_string(isolate, "isPrimary"), v8::True(isolate)).Check();
        event->Set(
            local_context,
            js_string(isolate, "relatedTarget"),
            current_related_target == nullptr
                ? v8::Local<v8::Value>(v8::Null(isolate))
                : v8::Local<v8::Value>(wrap_node(*current_related_target))).Check();
        auto client_x = input.x;
        auto client_y = input.y;
        auto page_x = input.x;
        auto page_y = input.y;
        const auto pointer_input = input.kind == WEBSCENE_INPUT_POINTER_MOVE
            || input.kind == WEBSCENE_INPUT_POINTER_DOWN
            || input.kind == WEBSCENE_INPUT_POINTER_UP
            || input.kind == WEBSCENE_INPUT_POINTER_LEAVE
            || input.kind == WEBSCENE_INPUT_WHEEL;
        if (pointer_input) {
            // The native host reports coordinates in the top-level viewport.
            // A pointer event dispatched in a child browsing context exposes
            // client coordinates in that context's viewport instead. Layout
            // remains host-relative for hit testing and retained painting, so
            // project only the script-visible event fields at this boundary.
            for (auto* ancestor = target.parent; ancestor != nullptr;
                 ancestor = ancestor->parent) {
                if (ancestor->tag != "iframe") continue;
                const auto border_left = document.resolve_used_length(
                    *ancestor,
                    ancestor->style.border_left_width,
                    ancestor->layout.width,
                    0);
                const auto border_top = document.resolve_used_length(
                    *ancestor,
                    ancestor->style.border_top_width,
                    ancestor->layout.height,
                    0);
                const auto padding_left = document.resolve_used_length(
                    *ancestor,
                    ancestor->style.padding_left,
                    ancestor->layout.width,
                    0);
                const auto padding_top = document.resolve_used_length(
                    *ancestor,
                    ancestor->style.padding_top,
                    ancestor->layout.height,
                    0);
                client_x -= ancestor->layout.x + border_left + padding_left;
                client_y -= ancestor->layout.y + border_top + padding_top;
                page_x = client_x;
                page_y = client_y;
                if (const auto scroll = frame_window_scroll_offsets.find(ancestor->id);
                    scroll != frame_window_scroll_offsets.end()) {
                    page_x += scroll->second.first;
                    page_y += scroll->second.second;
                } else {
                    auto* frame_root = &target;
                    while (frame_root->parent != nullptr
                        && frame_root->parent != ancestor) {
                        frame_root = frame_root->parent;
                    }
                    if (frame_root->parent == ancestor) {
                        page_x += frame_root->scroll_left;
                        page_y += frame_root->scroll_top;
                    }
                }
                // The nearest owner defines the target event's Window. Its
                // layout position already includes every outer frame offset.
                break;
            }
        }
        set_number("clientX", client_x);
        set_number("clientY", client_y);
        set_number("pageX", page_x);
        set_number("pageY", page_y);
        set_number("screenX", input.x);
        set_number("screenY", input.y);
        set_number("offsetX", input.x - target.layout.x);
        set_number("offsetY", input.y - target.layout.y);
        set_number("movementX", current_movement_x);
        set_number("movementY", current_movement_y);
        set_number("deltaX", input.delta_x);
        set_number("deltaY", input.delta_y);
        set_number("deltaMode", 0);
        const auto encoded_button = static_cast<int>((input.flags >> 8U) & 0xffU);
        const auto changed_button = encoded_button == 0 ? 0 : encoded_button - 1;
        const auto buttons = static_cast<int>(input.flags & 0x1fU);
        set_number("button", changed_button);
        set_number("buttons", buttons);
        set_number("pointerId", 1);
        const auto mouse_click_sequence_event = std::string_view(type) == "mousedown"
            || std::string_view(type) == "mouseup"
            || std::string_view(type) == "click"
            || std::string_view(type) == "auxclick";
        set_number("detail", mouse_click_sequence_event ? 1 : 0);
        // MouseEvent.which identifies the changed button and remains 1 for a
        // primary-button mouseup/click even though `buttons` has returned to 0.
        // Release and long-press paths in component libraries still consult this legacy
        // field; reporting 0 made a genuine release look like a hover event.
        const auto mouse_button_event = std::string_view(type) == "pointerdown"
            || std::string_view(type) == "mousedown"
            || std::string_view(type) == "pointerup"
            || std::string_view(type) == "mouseup"
            || std::string_view(type) == "click"
            || std::string_view(type) == "auxclick"
            || std::string_view(type) == "contextmenu";
        // Legacy MouseEvent.which also reports the currently held primary
        // button on mousemove. Drag and long-press normalization
        // still reads it in addition to `buttons`; reporting zero here makes
        // an ordered pressed move look like a hover on that path.
        const auto primary_button_held = (buttons & 1) != 0;
        const auto legacy_changed_button = changed_button == 1
            ? 2
            : changed_button == 2 ? 3 : 1;
        set_number(
            "which",
            mouse_button_event ? legacy_changed_button : (primary_button_held ? 1 : 0));
        const auto keyboard_input = input.kind == WEBSCENE_INPUT_KEY_DOWN
            || input.kind == WEBSCENE_INPUT_KEY_UP;
        const auto text_input = input.kind == WEBSCENE_INPUT_TEXT;
        event->Set(local_context, js_string(isolate, "altKey"), v8::Boolean::New(
            isolate, (keyboard_input && (input.flags & WEBSCENE_INPUT_MODIFIER_ALT) != 0U)
                || (pointer_input && (input.flags & WEBSCENE_INPUT_POINTER_MODIFIER_ALT) != 0U))).Check();
        event->Set(local_context, js_string(isolate, "ctrlKey"), v8::Boolean::New(
            isolate, (keyboard_input && (input.flags & WEBSCENE_INPUT_MODIFIER_CONTROL) != 0U)
                || (pointer_input && (input.flags & WEBSCENE_INPUT_POINTER_MODIFIER_CONTROL) != 0U))).Check();
        event->Set(local_context, js_string(isolate, "metaKey"), v8::Boolean::New(
            isolate, (keyboard_input && (input.flags & WEBSCENE_INPUT_MODIFIER_META) != 0U)
                || (pointer_input && (input.flags & WEBSCENE_INPUT_POINTER_MODIFIER_META) != 0U))).Check();
        event->Set(local_context, js_string(isolate, "shiftKey"), v8::Boolean::New(
            isolate, (keyboard_input && (input.flags & WEBSCENE_INPUT_MODIFIER_SHIFT) != 0U)
                || (pointer_input && (input.flags & WEBSCENE_INPUT_POINTER_MODIFIER_SHIFT) != 0U))).Check();
        if (keyboard_input) {
            const auto key_code = static_cast<int>(input.x);
            const auto key = keyboard_key_name(key_code);
            const auto code = keyboard_code_name(key_code);
            event->Set(local_context, js_string(isolate, "key"), js_string(isolate, key.c_str())).Check();
            event->Set(local_context, js_string(isolate, "code"), js_string(isolate, code.c_str())).Check();
            set_number("keyCode", key_code);
            set_number("charCode", type_view == "keypress" ? key_code : 0);
            set_number("which", key_code);
            set_number("location", 0);
            event->Set(local_context, js_string(isolate, "repeat"), v8::Boolean::New(
                isolate, (input.flags & WEBSCENE_INPUT_KEY_REPEAT) != 0U)).Check();
        }
        if (text_input || std::string_view(type) == "beforeinput"
            || std::string_view(type) == "input") {
            const auto deleting = input.kind == WEBSCENE_INPUT_KEY_DOWN
                && (static_cast<int>(input.x) == 8 || static_cast<int>(input.x) == 46);
            const auto inserting_line_break = input.kind == WEBSCENE_INPUT_KEY_DOWN
                && static_cast<int>(input.x) == 13;
            const auto data = text_input ? utf8_scalar(static_cast<uint32_t>(input.x)) : std::string{};
            event->Set(
                local_context,
                js_string(isolate, "data"),
                deleting || inserting_line_break
                    ? v8::Local<v8::Value>(v8::Null(isolate))
                    : v8::Local<v8::Value>(js_string(isolate, data.c_str()))).Check();
            event->Set(
                local_context,
                js_string(isolate, "inputType"),
                js_string(isolate, deleting
                    ? static_cast<int>(input.x) == 8
                        ? "deleteContentBackward"
                        : "deleteContentForward"
                    : inserting_line_break
                        ? "insertLineBreak"
                    : "insertText")).Check();
            event->Set(local_context, js_string(isolate, "isComposing"), v8::False(isolate)).Check();
        }
        set_number("timeStamp", std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        set_number("sequence", static_cast<double>(input.sequence));
        event->Set(
            local_context,
            js_string(isolate, "__webscenePropagationStopped"),
            v8::False(isolate)).Check();
        event->Set(
            local_context,
            js_string(isolate, "__websceneImmediatePropagationStopped"),
            v8::False(isolate)).Check();
        const auto read_event_boolean = [&](const char* name) {
            v8::Local<v8::Value> value;
            return event->Get(local_context, js_string(isolate, name)).ToLocal(&value)
                && value->BooleanValue(isolate);
        };
        const auto propagation_stopped = [&]() {
            return read_event_boolean("__webscenePropagationStopped");
        };
        const auto immediate_propagation_stopped = [&]() {
            return read_event_boolean("__websceneImmediatePropagationStopped");
        };

        auto* event_root = static_cast<dom_node*>(
            local_context->GetAlignedPointerFromEmbedderData(
                1,
                v8::kEmbedderDataTypeTagDefault));
        if (event_root == nullptr) event_root = &document.body();
        std::vector<dom_node*> ancestry;
        for (auto* node = &target; node != nullptr; node = document.event_parent(*node)) {
            ancestry.push_back(node);
            // Iframe documents share the retained native layout tree with
            // their owner, while DOM event paths terminate at the child
            // Document/Window boundary.
            if (node == event_root) break;
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (std::string_view(type) == "mousemove") {
            std::ostringstream path;
            for (size_t index = 0; index < ancestry.size(); ++index) {
                if (index != 0) path << '>';
                path << ancestry[index]->id;
            }
            last_mousemove_ancestry = path.str();
        }
#endif
        ++input_event_dispatch_count;
        v8::Local<v8::Value> arguments[] = {event};
        const auto listeners_at_dispatch_start = frame_event_listeners.find(type);
        const auto listener_count_at_dispatch_start =
            listeners_at_dispatch_start == frame_event_listeners.end()
                ? 0U
                : listeners_at_dispatch_start->second.size();
        const auto retargeted_node = [&](const dom_node* current_target) {
            const auto* root = document.containing_shadow_root(target);
            if (root == nullptr) return &target;
            if (current_target != nullptr
                && document.containing_shadow_root(*current_target) == root) {
                return &target;
            }
            const auto* state = document.shadow_dom(*root);
            return state == nullptr || state->host == nullptr ? &target : state->host;
        };
        const auto set_retargeted_target = [&](const dom_node* current_target) {
            auto wrapper = wrap_node(*retargeted_node(current_target));
            event->Set(local_context, js_string(isolate, "target"), wrapper).Check();
            event->Set(local_context, js_string(isolate, "srcElement"), wrapper).Check();
        };
        const auto invoke_listeners = [&](
            uint32_t registered_target,
            v8::Local<v8::Object> current_target,
            bool capture,
            int event_phase) {
            auto listeners = frame_event_listeners.find(type);
            if (listeners == frame_event_listeners.end()) return true;
            set_retargeted_target(document.find_by_native_id(registered_target));
            event->Set(local_context, js_string(isolate, "currentTarget"), current_target).Check();
            set_number("eventPhase", event_phase);
            // The DOM dispatch algorithm snapshots listener eligibility when
            // dispatch starts. An overlay commonly installs its document-level
            // dismissal listener while handling the anchor click; that listener
            // must begin with the next event, not a later phase of this one.
            const auto listener_count = std::min(
                listener_count_at_dispatch_start,
                listeners->second.size());
            for (size_t index = 0; index < listener_count; ++index) {
                auto& listener = listeners->second[index];
                if (listener.context.Get(isolate) != local_context
                    || listener.target != registered_target
                    || listener.capture != capture) continue;
                auto callback = listener.callback.Get(isolate);
                if (callback.IsEmpty()) continue;
                if (!listener.signal.IsEmpty()) {
                    auto signal = listener.signal.Get(isolate);
                    v8::Local<v8::Value> aborted;
                    if (signal->Get(
                            local_context,
                            js_string(isolate, "aborted")).ToLocal(&aborted)
                        && aborted->BooleanValue(isolate)) {
                        clear_event_listener(listener);
                        continue;
                    }
                }
                const auto passive = listener.passive;
                if (listener.once) {
                    clear_event_listener(listener);
                }
                event->Set(
                    local_context,
                    js_string(isolate, "__websceneInPassiveListener"),
                    v8::Boolean::New(isolate, passive)).Check();
                const auto callback_succeeded =
                    !call_event_listener(
                        isolate,
                        local_context,
                        callback,
                        current_target,
                        1,
                        arguments).IsEmpty();
                event->Set(
                    local_context,
                    js_string(isolate, "__websceneInPassiveListener"),
                    v8::False(isolate)).Check();
                if (!callback_succeeded) return false;
                ++input_callback_invocation_count;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
                ++event_callback_target_counts[type][registered_target];
                ++event_callback_index_counts[type][index];
                if (std::getenv("WEBSCENE_PROBE_PROFILE_INPUT") != nullptr
                    && (std::string_view(type) == "pointerdown"
                        || std::string_view(type) == "mousedown"
                        || std::string_view(type) == "pointermove"
                        || std::string_view(type) == "mousemove"
                        || std::string_view(type) == "pointerup"
                        || std::string_view(type) == "mouseup")) {
                    std::cerr << "[Native Engine Probe] input listener"
                        << ", type=" << type
                        << ", target=" << registered_target
                        << ", capture=" << (capture ? 1 : 0)
                        << ", phase=" << event_phase
                        << ", listener=" << index
                        << ", stopped=" << (propagation_stopped() ? 1 : 0)
                        << ", immediate="
                        << (immediate_propagation_stopped() ? 1 : 0)
                        << '\n';
                }
#endif
                if (immediate_propagation_stopped()) break;
            }
            return true;
        };

        const auto property_name = std::string("on") + type;
        const auto invoke_property = [&](dom_node& node, int event_phase) {
            auto wrapper = wrap_node(node);
            v8::Local<v8::Value> handler;
            if (!wrapper->Get(local_context, js_string(isolate, property_name.c_str())).ToLocal(&handler)
                || !handler->IsFunction()) return true;
            set_retargeted_target(&node);
            event->Set(local_context, js_string(isolate, "currentTarget"), wrapper).Check();
            set_number("eventPhase", event_phase);
            if (handler.As<v8::Function>()->Call(local_context, wrapper, 1, arguments).IsEmpty()) {
                return false;
            }
            ++input_callback_invocation_count;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            ++event_callback_target_counts[type][node.id];
#endif
            return true;
        };

        // Preserve browser listener phase/ancestry order and propagation stops.
        // A native mouseup may have several gesture recognizers registered on
        // overlapping chart ancestors; continuing after stopPropagation can end
        // the same scale transaction twice.
        auto global = local_context->Global();
        auto document_target = global;
        v8::Local<v8::Value> document_value;
        if (global->Get(local_context, js_string(isolate, "document")).ToLocal(&document_value)
            && document_value->IsObject()) {
            document_target = document_value.As<v8::Object>();
        }
        auto composed_path = v8::Array::New(
            isolate,
            static_cast<int>(ancestry.size() + 2U));
        for (size_t index = 0; index < ancestry.size(); ++index) {
            composed_path->Set(
                local_context,
                static_cast<uint32_t>(index),
                wrap_node(*ancestry[index])).Check();
        }
        composed_path->Set(
            local_context,
            static_cast<uint32_t>(ancestry.size()),
            document_target).Check();
        composed_path->Set(
            local_context,
            static_cast<uint32_t>(ancestry.size() + 1U),
            global).Check();
        event->Set(
            local_context,
            js_string(isolate, "__webSceneComposedPath"),
            composed_path).Check();
        event->Set(
            local_context,
            js_string(isolate, "composed"),
            v8::True(isolate)).Check();
        if (!invoke_listeners(0U, global, true, 1)) return false;
        if (!propagation_stopped() && !invoke_listeners(
                std::numeric_limits<uint32_t>::max(),
                document_target,
                true,
                1)) return false;
        for (size_t cursor = ancestry.size();
             !propagation_stopped() && cursor-- > 1;) {
            auto* node = ancestry[cursor];
            if (!invoke_listeners(node->id, wrap_node(*node), true, 1)) return false;
        }
        if (!propagation_stopped()) {
            auto target_wrapper = wrap_node(target);
            if (!invoke_listeners(target.id, target_wrapper, true, 2)) return false;
            if (!immediate_propagation_stopped()
                && !invoke_listeners(target.id, target_wrapper, false, 2)) return false;
            if (!immediate_propagation_stopped()
                && !invoke_property(target, 2)) return false;
        }
        if (bubbles && !propagation_stopped()) {
            for (size_t cursor = 1;
                cursor < ancestry.size() && !propagation_stopped();
                 ++cursor) {
                auto* node = ancestry[cursor];
                auto wrapper = wrap_node(*node);
                if (!invoke_listeners(node->id, wrapper, false, 3)
                    || (!immediate_propagation_stopped()
                        && !invoke_property(*node, 3))) return false;
            }
            if (!propagation_stopped()
                && !invoke_listeners(
                    std::numeric_limits<uint32_t>::max(),
                    document_target,
                    false,
                    3)) return false;
            if (!propagation_stopped()
                && !invoke_listeners(0U, global, false, 3)) return false;
        }
        set_number("eventPhase", 0);
        event->Set(local_context, js_string(isolate, "currentTarget"), v8::Null(isolate)).Check();
        perform_microtask_checkpoint();
        style_batch.finish();

#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        event_callback_counts[type] +=
            input_callback_invocation_count - callback_count_before;
#else
        static_cast<void>(callback_count_before);
#endif
        if (default_prevented != nullptr) {
            v8::Local<v8::Value> prevented;
            *default_prevented = event->Get(
                    local_context,
                    js_string(isolate, "defaultPrevented"))
                .ToLocal(&prevented)
                && prevented->BooleanValue(isolate);
        }

        // removeEventListener is commonly called by component code from inside
        // its root mouseup callback. Removal tombstones the callback so the
        // active dispatch remains stable; compact tombstones only after every
        // callback for this event has completed.
        if (auto listeners = frame_event_listeners.find(type);
            listeners != frame_event_listeners.end()) {
            std::erase_if(
                listeners->second,
                [](const auto& listener) { return listener.callback.IsEmpty(); });
        }
        return true;
    }

    static bool is_text_control(const dom_node* node) { return forms::is_text_control(node); }

    bool is_writable_text_control(const dom_node* node) const
    {
        return is_text_control(node)
            && !node->attributes.contains("readonly")
            && !document.is_inert(*node);
    }

    dom_node* form_owner(dom_node& control)
    {
        return const_cast<dom_node*>(forms::form_owner(document,control));
    }

    static bool is_submit_button(const dom_node* node)
    {
        if (node == nullptr || node->attributes.contains("disabled")) return false;
        const auto authored_type = node->attributes.find("type");
        const auto type = authored_type == node->attributes.end()
            ? std::string{}
            : lower_html_name(authored_type->second);
        if (node->tag == "button") return type.empty() || type == "submit";
        return node->tag == "input" && (type == "submit" || type == "image");
    }

    void collect_form_controls(dom_node& node, dom_node& form, std::vector<dom_node*>& controls)
    {
        for (auto& child : node.children) {
            if (child->kind != dom_node_kind::element) continue;
            if (form_owner(*child) == &form) controls.push_back(child);
            collect_form_controls(*child, form, controls);
        }
    }

    dom_node* default_submitter(dom_node& form)
    {
        std::vector<dom_node*> controls;
        collect_form_controls(active_root(), form, controls);
        const auto match = std::find_if(controls.begin(), controls.end(), is_submit_button);
        return match == controls.end() ? nullptr : *match;
    }

    bool construct_form_data(dom_node& form,dom_node* submitter)
    {
        auto local_context=context_for_node(form);
        v8::Context::Scope context_scope(local_context);
        v8::Local<v8::Value> constructor_value;
        if(!local_context->Global()->Get(
                local_context,js_string(isolate,"FormData"))
                .ToLocal(&constructor_value)
            || !constructor_value->IsFunction()) return true;
        v8::Local<v8::Value> arguments[]={
            wrap_node(form),
            submitter==nullptr
                ? v8::Local<v8::Value>(v8::Null(isolate))
                : v8::Local<v8::Value>(wrap_node(*submitter))};
        v8::Local<v8::Object> form_data;
        return constructor_value.As<v8::Function>()->NewInstance(
            local_context,2,arguments).ToLocal(&form_data);
    }

    bool dispatch_form_submit(
        const webscene_input_event& input,
        dom_node& form,
        dom_node* submitter)
    {
        const auto controls=forms::form_validation_controls(document,form);
        for(const auto* candidate:controls) {
            if(candidate==nullptr || (candidate->tag!="input"
                && candidate->tag!="select" && candidate->tag!="textarea"
                && !candidate->form_control().form_associated_custom_element))
                continue;
            auto& control=*const_cast<dom_node*>(candidate);
            if(control.form_control().user_validity_interacted) continue;
            const auto value=forms::text_control_value(control);
            if(is_connected(control))
                recascade_form_value_selector_transition(
                    control,value,value,true,std::nullopt);
            else control.mutable_form_control().user_validity_interacted=true;
        }
        const auto skip_validation=form.attributes.contains("novalidate")
            || (submitter!=nullptr
                && submitter->attributes.contains("formnovalidate"));
        if(!skip_validation && !validate_form_constraints(form)) return true;
        bool submit_prevented=false;
        if(!dispatch_input_event_type(
                input,"submit",form,&submit_prevented,submitter)) return false;
        return submit_prevented || construct_form_data(form,submitter);
    }

    bool perform_implicit_submission(
        const webscene_input_event& input,
        dom_node& control)
    {
        auto* form = form_owner(control);
        if (form == nullptr) return true;

        if (auto* submitter = default_submitter(*form); submitter != nullptr) {
            bool click_prevented = false;
            if (!dispatch_input_event_type(
                    input,
                    "click",
                    *submitter,
                    &click_prevented)) {
                return false;
            }
            return click_prevented || dispatch_form_submit(input, *form, submitter);
        }

        // HTML suppresses implicit submission for a form with more than one
        // blocking text control when it has no submit button. TradingView's
        // interval form contains exactly one input and therefore takes this path.
        std::vector<dom_node*> controls;
        collect_form_controls(active_root(), *form, controls);
        const auto blocking_controls = std::count_if(
            controls.begin(),
            controls.end(),
            [](const auto* candidate) { return is_text_control(candidate); });
        return blocking_controls > 1
            || dispatch_form_submit(input, *form, nullptr);
    }

    static bool is_natively_focusable(const dom_node* node)
    {
        if (node == nullptr || node->attributes.contains("disabled")) return false;
        if (node->tag == "button" || node->tag == "select" || node->tag == "textarea") {
            return true;
        }
        if (node->tag == "summary" && node->parent != nullptr
            && first_details_summary(*node->parent) == node) return true;
        if (node->tag == "a") return node->attributes.contains("href");
        if (node->tag != "input") return false;
        const auto type = node->attributes.find("type");
        return type == node->attributes.end() || type->second != "hidden";
    }

    bool is_effectively_visible_for_focus(const dom_node* node)
    {
        if (node != nullptr && document.is_inert(*node)) return false;
        auto visibility_resolved = false;
        auto visibility_hidden = false;
        for (auto* current_node = node; current_node != nullptr;
             current_node = document.composed_parent(*current_node)) {
            if (current_node->style.display == display_mode::none
                || current_node->style.content_visibility_hidden) return false;
            if (!visibility_resolved && current_node->style.visibility_specified) {
                visibility_hidden = current_node->style.visibility_hidden;
                visibility_resolved = true;
            }
        }
        return !visibility_hidden;
    }

    static bool try_tab_index(const dom_node* node, int& value)
    {
        if (node == nullptr) return false;
        const auto attribute = node->attributes.find("tabindex");
        if (attribute == node->attributes.end() || attribute->second.empty()) return false;
        char* end = nullptr;
        const auto parsed = std::strtol(attribute->second.c_str(), &end, 10);
        if (end == attribute->second.c_str() || *end != '\0') return false;
        value = static_cast<int>(parsed);
        return true;
    }

    bool is_programmatically_focusable(const dom_node* node)
    {
        int tab_index = 0;
        return node != nullptr
            && !node->attributes.contains("disabled")
            && is_effectively_visible_for_focus(node)
            && (try_tab_index(node, tab_index) || is_natively_focusable(node));
    }

    bool is_pointer_focusable(const dom_node* node)
    {
        return is_programmatically_focusable(node);
    }

    dom_node* nearest_pointer_focusable(dom_node* node)
    {
        for (auto* current_node = node; current_node != nullptr;
            current_node = document.composed_parent(*current_node)) {
            if (is_pointer_focusable(current_node)) return current_node;
        }
        return nullptr;
    }

    std::vector<dom_node*> sequential_focus_order()
    {
        struct candidate final {
            dom_node* node;
            int tab_index;
            size_t order;
        };
        std::vector<candidate> candidates;
        size_t order = 0;
        const std::function<void(dom_node&)> visit = [&](dom_node& node) {
            if (!node.tag.starts_with('#') && !node.attributes.contains("disabled")
                && is_effectively_visible_for_focus(&node)) {
                int tab_index = 0;
                const auto explicit_tab_index = try_tab_index(&node, tab_index);
                if ((explicit_tab_index && tab_index >= 0)
                    || (!explicit_tab_index && is_natively_focusable(&node))) {
                    candidates.push_back(candidate{&node, tab_index, order});
                }
                ++order;
            }
            for (auto* child : document.composed_children(node)) visit(*child);
        };
        // Sequential navigation spans browsing-context boundaries. The native
        // DOM stores child document roots below their iframe owners, so the
        // top-level tree provides the browser order for entering and leaving
        // a frame without a second focus stop on the owner element.
        visit(document.body());
        std::stable_sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
            const auto left_positive = left.tab_index > 0;
            const auto right_positive = right.tab_index > 0;
            if (left_positive != right_positive) return left_positive;
            if (left_positive && left.tab_index != right.tab_index) {
                return left.tab_index < right.tab_index;
            }
            return left.order < right.order;
        });
        std::vector<dom_node*> result;
        result.reserve(candidates.size());
        for (const auto& entry : candidates) result.push_back(entry.node);
        return result;
    }

    dom_node* nearest_text_control(dom_node* node)
    {
        for (auto* current_node = node; current_node != nullptr;
            current_node = document.composed_parent(*current_node)) {
            if (is_text_control(current_node)) return current_node;
        }
        return nullptr;
    }

    uint32_t focus_document_owner(const dom_node* node) const
    {
        for (auto* ancestor = node; ancestor != nullptr; ancestor = ancestor->parent) {
            if (ancestor->tag == "iframe") return ancestor->id;
            for (const auto& [frame_id, body] : provisional_frame_bodies) {
                if (ancestor == body) return frame_id;
            }
        }
        return 0;
    }

    static dom_node* find_node_by_native_id(dom_node& root, uint32_t id)
    {
        if (root.id == id) return &root;
        for (auto* child : root.children) {
            if (child == nullptr) continue;
            if (auto* match = find_node_by_native_id(*child, id); match != nullptr) return match;
        }
        return nullptr;
    }

    void recascade_dynamic_state_subject(dom_node& subject, std::string_view reason)
    {
        // Recompute the ancestor chain for dynamic selector inputs such as
        // focus, checkedness, and class tokens. A class mutation can change a
        // following sibling through `+`/`~`, while ancestor state can change
        // through :has(). Keep the work inside the subject's own browsing
        // context: same-origin callers can mutate a frame node while another
        // CSS cascade is active.
        auto* context_root = css_cascade_root_for_node(subject);
        std::vector<dom_node*> ancestors;
        for (auto* ancestor = document.dom_parent(subject);
            ancestor != nullptr;
            ancestor = document.dom_parent(*ancestor)) {
            ancestors.push_back(ancestor);
            if (ancestor == context_root) break;
        }
        for (auto iterator = ancestors.rbegin(); iterator != ancestors.rend(); ++iterator) {
            if (*iterator == ancestors.front()) continue;
            recascade_connected_node(**iterator);
        }
        auto* local_root = ancestors.empty() ? &subject : ancestors.front();
        recascade_connected_subtree(*local_root, reason);
    }

    void recascade_focus_subject(dom_node& subject, std::string_view reason)
    {
        recascade_dynamic_state_subject(subject, reason);
    }

    bool set_active_element(dom_node* next, const webscene_input_event& input)
    {
      const auto validation_target=document.validation_message_target_id();
      const auto validation_anchor=document.validation_message_anchor_id();
      if(validation_target!=0U && (next==nullptr
          || (next->id!=validation_target && next->id!=validation_anchor)))
        dismiss_validation_message();
      const auto next_owner = next == nullptr ? focused_document_owner_id
                                              : focus_document_owner(next);
      if (active_element == next && focused_document_owner_id == next_owner)
        return true;
      auto *previous = active_element;
      auto *related_target =
          previous != nullptr && next != nullptr &&
                  focus_document_owner(previous) == focus_document_owner(next)
              ? next
              : nullptr;
      if (previous != nullptr) {
        previous->mutable_form_control().input_focused = false;
        previous->mutable_form_control().caret_visible = false;
        // The document regains focus before blur/focusout listeners run.
        // This also allows a listener to focus another element without
        // recursively blurring the element whose transition is in flight.
        active_element = nullptr;
        recascade_focus_subject(*previous, "focus-before-blur");
        current_related_target = related_target;
        if (!dispatch_input_event_type(input, "blur", *previous)) {
          current_related_target = nullptr;
          return false;
        }
        current_related_target = related_target;
        if (!dispatch_input_event_type(input, "focusout", *previous)) {
          current_related_target = nullptr;
          return false;
        }
        // A focus/blur listener may have completed a newer focus change.
        // Preserve that result instead of overwriting it with this request.
        if (active_element != nullptr) {
          current_related_target = nullptr;
          document.mark_dirty();
          return true;
        }
        }
        if (next != nullptr && !set_focus_document_owner(next_owner)) {
          current_related_target = nullptr;
          return false;
        }
        if (active_element != nullptr) {
          current_related_target = nullptr;
          document.mark_dirty();
          return true;
        }
        active_element = next;
        if (next != nullptr && is_text_control(next)) {
            ensure_form_value(*next);
            if (!next->form_control().selection_explicitly_set) {
                next->mutable_form_control().selection_start =
                    next->form_control().value.size();
                next->mutable_form_control().selection_end =
                    next->form_control().value.size();
                next->mutable_form_control().selection_start_utf16_suboffset = 0U;
                next->mutable_form_control().selection_end_utf16_suboffset = 0U;
                next->mutable_form_control().selection_direction =
                    text_selection_direction::none;
                next->mutable_form_control().selection_explicitly_set = true;
            }
            mark_primary_selection_publication_dirty(next);
        }
        // Focus styles are observable synchronously from focus listeners.
        // Recompute them before dispatching focus/focusin, not only after the
        // event sequence has completed. When `next` is null, the transition to
        // an unfocused document was already recomputed before blur above.
        if (next != nullptr) {
            recascade_focus_subject(*next, "focus-before-focus");
        }
        if (next != nullptr) {
            next->mutable_form_control().input_focused = is_text_control(next);
            next->mutable_form_control().caret_visible = next->mutable_form_control().input_focused;
            caret_blink_epoch_ms = last_animation_frame_timestamp_ms;
            related_target = previous != nullptr
                    && focus_document_owner(previous) == focus_document_owner(next)
                ? previous
                : nullptr;
            current_related_target = related_target;
            if (!dispatch_input_event_type(input, "focus", *next)) {
                current_related_target = nullptr;
                return false;
            }
            current_related_target = related_target;
            if (!dispatch_input_event_type(input, "focusin", *next)) {
                current_related_target = nullptr;
                return false;
            }
        }
        current_related_target = nullptr;
        // The active element has not changed since the pre-event recascade.
        // DOM/class/style writes performed by focus listeners invalidate their
        // own affected subtrees, so a duplicate whole-document pass here only
        // delays publication without changing computed style.
        document.mark_dirty();
        return true;
    }

    bool subtree_contains(const dom_node& root, const dom_node* candidate)
    {
        for (auto* current_node = candidate; current_node != nullptr;
             current_node = document.event_parent(*current_node)) {
            if (current_node == &root) return true;
        }
        return false;
    }

    bool blur_active_element_before_detach(dom_node& detached_root)
    {
        const auto validation_target=document.find_by_native_id(
            document.validation_message_target_id());
        const auto validation_anchor=document.find_by_native_id(
            document.validation_message_anchor_id());
        if(subtree_contains(detached_root,validation_target)
            || subtree_contains(detached_root,validation_anchor))
            dismiss_validation_message();
        document.unregister_modal_subtree(detached_root);
        auto *focused_owner =
            focused_document_owner_id == 0U
                ? nullptr
                : document.find_by_native_id(focused_document_owner_id);
        const auto removes_focused_document =
            focused_owner != nullptr &&
            subtree_contains(detached_root, focused_owner);
        if (!subtree_contains(detached_root, active_element) &&
            !removes_focused_document)
          return true;
        webscene_input_event synthetic{};
        if (subtree_contains(detached_root, active_element) &&
            !set_active_element(nullptr, synthetic))
          return false;

        // A blur/focusout listener can synchronously focus another node. Preserve
        // focus outside the subtree, but never retain a node that is about to be
        // detached as document.activeElement.
        if (subtree_contains(detached_root, active_element)) {
            auto* detached_active_element = active_element;
            detached_active_element->mutable_form_control().input_focused = false;
            detached_active_element->mutable_form_control().caret_visible = false;
            active_element = nullptr;
            recascade_focus_subject(
                *detached_active_element,
                "focus-detached-reentrant");
            document.mark_dirty();
        }
        if (removes_focused_document && focused_document_owner_id != 0U &&
            !set_focus_document_owner(0U))
          return false;
        return true;
    }

    bool blur_active_descendant_before_replacing_children(dom_node& parent)
    {
        for (auto* child : parent.children) {
            if (child != nullptr && subtree_contains(*child, active_element)) {
                return blur_active_element_before_detach(*child);
            }
        }
        return true;
    }

    void ensure_form_value(dom_node& node)
    {
        webscene_native::forms::ensure_text_value(node);
    }

    bool dispatch_clipboard_shortcut(
        dom_node& target,
        const char* type,
        uint32_t read_flags = 0U)
    {
        auto local_context = context_for_node(target);
        auto global = local_context->Global();
        v8::Local<v8::Value> helper;
        if (!global->Get(
                local_context,
                js_string(isolate, "__webSceneClipboardShortcut")).ToLocal(&helper)
            || !helper->IsFunction()) {
            return false;
        }
        v8::Local<v8::Value> arguments[] = {
            js_string(isolate, type),
            wrap_node(target),
            v8::Integer::NewFromUnsigned(isolate, read_flags)};
        v8::Local<v8::Value> result;
        if (!helper.As<v8::Function>()->Call(
                local_context, global, 3, arguments).ToLocal(&result)) {
            return false;
        }
        perform_microtask_checkpoint();
        return result->BooleanValue(isolate);
    }

    static size_t previous_utf8_boundary(const std::string& value, size_t index)
    {
        return webscene_native::forms::previous_utf8_boundary(value,index);
    }

    static size_t next_utf8_boundary(const std::string& value, size_t index)
    {
        return webscene_native::forms::next_utf8_boundary(value,index);
    }

    bool dispatch_keyboard_or_text(const webscene_input_event& input)
    {
        if (active_element != nullptr && document.is_inert(*active_element)) {
            if (!set_active_element(nullptr, input)) return false;
        }
        auto* target = active_element == nullptr ? &active_root() : active_element;
        if (input.kind == WEBSCENE_INPUT_KEY_UP) {
            pending_text_input_from_keydown = false;
            pending_text_input_target_id = 0U;
            return dispatch_input_event_type(input, "keyup", *target);
        }

        if (input.kind == WEBSCENE_INPUT_TEXT) {
            if (pending_text_input_from_keydown) {
                const auto target_id = pending_text_input_target_id;
                pending_text_input_from_keydown = false;
                pending_text_input_target_id = 0U;
                // Native hosts report the committed character separately from
                // keydown. Keep it associated with the editing host that owned
                // the keydown: a chart key handler may focus and seed a search
                // input, but Chrome does not retarget that same character into
                // the newly focused field a second time.
                if (target_id == 0U) return true;
                target = find_node_by_native_id(active_root(), target_id);
                if (target == nullptr) return true;
            }
            if (!is_text_control(target) || document.is_inert(*target)) return true;
            bool prevented = false;
            if (!dispatch_input_event_type(input, "beforeinput", *target, &prevented)) return false;
            if (prevented) return true;
            ensure_form_value(*target);
            const auto previous_value = target->form_control().value;
            const auto text = utf8_scalar(static_cast<uint32_t>(input.x));
            const forms::utf8_selection_offset start{
                std::min(target->mutable_form_control().selection_start,
                    target->mutable_form_control().value.size()),
                target->mutable_form_control().selection_start_utf16_suboffset};
            const forms::utf8_selection_offset end{
                std::min(target->mutable_form_control().selection_end,
                    target->mutable_form_control().value.size()),
                target->mutable_form_control().selection_end_utf16_suboffset};
            auto units=forms::utf16_units_from_utf8(
                target->mutable_form_control().value);
            const auto start_units=forms::utf16_offset_from_utf8_selection(
                target->mutable_form_control().value,start);
            const auto end_units=forms::utf16_offset_from_utf8_selection(
                target->mutable_form_control().value,end);
            const auto inserted_units=forms::utf16_units_from_utf8(text);
            units.erase(units.begin()+static_cast<std::ptrdiff_t>(start_units),
                units.begin()+static_cast<std::ptrdiff_t>(end_units));
            units.insert(units.begin()+static_cast<std::ptrdiff_t>(start_units),
                inserted_units.begin(),inserted_units.end());
            target->mutable_form_control().value=forms::wtf8_from_utf16_units(units);
            target->mutable_form_control().dirty_value = true;
            const auto caret=forms::utf8_selection_from_utf16_offset(
                target->mutable_form_control().value,
                start_units+inserted_units.size());
            target->mutable_form_control().selection_start = caret.byte_offset;
            target->mutable_form_control().selection_end = caret.byte_offset;
            target->mutable_form_control().selection_start_utf16_suboffset =
                caret.utf16_suboffset;
            target->mutable_form_control().selection_end_utf16_suboffset =
                caret.utf16_suboffset;
            target->mutable_form_control().selection_direction = text_selection_direction::none;
            target->mutable_form_control().selection_explicitly_set = true;
            mark_primary_selection_publication_dirty(target);
            target->mutable_form_control().caret_visible = true;
            caret_blink_epoch_ms = last_animation_frame_timestamp_ms;
            recascade_form_value_selector_transition(
                *target, previous_value, target->form_control().value, true, true);
            return dispatch_input_event_type(input, "input", *target);
        }

        pending_text_input_from_keydown = false;
        pending_text_input_target_id = 0U;
        const auto key_code = static_cast<int>(input.x);
        const auto command_modifier = (input.flags
            & (WEBSCENE_INPUT_MODIFIER_CONTROL
                | WEBSCENE_INPUT_MODIFIER_ALT
                | WEBSCENE_INPUT_MODIFIER_META)) != 0U;
        if (!command_modifier && key_code >= 32 && key_code <= 126) {
            pending_text_input_from_keydown = true;
            pending_text_input_target_id = is_text_control(target)
                ? target->id
                : 0U;
        }
        bool prevented = false;
        if (!dispatch_input_event_type(input, "keydown", *target, &prevented)) return false;
        const auto emits_legacy_keypress = key_code == 13
            || (!command_modifier && key_code >= 32 && key_code <= 126);
        if (!prevented && emits_legacy_keypress) {
            bool keypress_prevented = false;
            if (!dispatch_input_event_type(input, "keypress", *target, &keypress_prevented)) {
                return false;
            }
            prevented = keypress_prevented;
        }
        if (!prevented && target->tag == "select"
            && select_popup_is_candidate(*target)) {
            if (select_popup_is_open(*target)) {
                if (!select_popup_snapshot_valid(*target)) {
                    close_select_popup(*target, false);
                } else if (key_code == 27) {
                    close_select_popup(*target, false);
                    return true;
                } else if (key_code == 13 || key_code == 32) {
                    return select_popup_commit(*target, input);
                } else if (key_code == 36 || key_code == 35
                    || key_code == 38 || key_code == 40) {
                    return select_popup_move_highlight(
                        *target,
                        key_code == 38 ? -1 : key_code == 40 ? 1 : 0,
                        key_code == 36,
                        key_code == 35);
                } else if (key_code == 33 || key_code == 34) {
                    for (auto step = 0; step < 5; ++step) {
                        select_popup_move_highlight(
                            *target, key_code == 33 ? -1 : 1);
                    }
                    return true;
                } else if (!command_modifier
                    && ((key_code >= '0' && key_code <= '9')
                        || (key_code >= 'A' && key_code <= 'Z'))) {
                    return select_popup_typeahead(
                        *target, static_cast<char>(key_code));
                } else if (key_code == 9) {
                    // Tab accepts no pending value; close and let the normal
                    // sequential focus algorithm continue below.
                    close_select_popup(*target, false);
                }
            } else if (key_code == 13 || key_code == 32
                || (key_code == 40
                    && (input.flags & WEBSCENE_INPUT_MODIFIER_ALT) != 0U)) {
                static_cast<void>(open_select_popup(*target));
                return true;
            }
        }
        if (!prevented && key_code == 9) {
            const auto focus_order = sequential_focus_order();
            if (!focus_order.empty()) {
                const auto current = std::find(focus_order.begin(), focus_order.end(), active_element);
                const auto backwards = (input.flags & WEBSCENE_INPUT_MODIFIER_SHIFT) != 0U;
                dom_node* next = nullptr;
                if (current == focus_order.end()) {
                    // Window.focus() enters a child document with BODY active,
                    // represented by a null native active_element. Forward Tab
                    // begins at that document's first focusable descendant;
                    // reverse Tab exits to the preceding outer control.
                    auto* focused_owner = focused_document_owner_id == 0U
                        ? nullptr
                        : find_node_by_native_id(
                            document.body(), focused_document_owner_id);
                    const auto first_in_focused_frame = focused_owner == nullptr
                        ? focus_order.end()
                        : std::find_if(
                            focus_order.begin(),
                            focus_order.end(),
                            [&](const dom_node* candidate) {
                                return subtree_contains(*focused_owner, candidate);
                            });
                    if (first_in_focused_frame == focus_order.end()) {
                        next = backwards ? focus_order.back() : focus_order.front();
                    } else if (!backwards) {
                        next = *first_in_focused_frame;
                    } else {
                        next = first_in_focused_frame == focus_order.begin()
                            ? focus_order.back()
                            : *(first_in_focused_frame - 1);
                    }
                } else if (backwards) {
                    next = current == focus_order.begin() ? focus_order.back() : *(current - 1);
                } else {
                    next = current + 1 == focus_order.end() ? focus_order.front() : *(current + 1);
                }
                focus_visible = true;
                return set_active_element(next, input);
            }
        }
        if (!prevented && key_code == 13 && target->tag == "a"
            && target->attributes.contains("href") && is_connected(*target)) {
            bool click_prevented = false;
            if (!dispatch_input_event_type(input, "click", *target, &click_prevented)) return false;
            return click_prevented || queue_external_navigation(*target);
        }
        if (!prevented && key_code == 13 && target->tag == "input") {
            return perform_implicit_submission(input, *target);
        }
        if (!prevented && target->tag == "select"
            && !target->attributes.contains("multiple")
            && (key_code == 35 || key_code == 36
                || key_code == 38 || key_code == 40)) {
            const auto changed = move_single_select_selection(
                *target,
                key_code == 38 ? -1 : key_code == 40 ? 1 : 0,
                key_code == 36,
                key_code == 35,
                false);
            if (changed) {
                document.mark_dirty();
                if (!dispatch_input_event_type(input, "input", *target)
                    || !dispatch_input_event_type(input, "change", *target)) {
                    return false;
                }
            }
            return true;
        }
        const auto clipboard_modifier = (input.flags
            & (WEBSCENE_INPUT_MODIFIER_CONTROL | WEBSCENE_INPUT_MODIFIER_META)) != 0U
            && (input.flags & WEBSCENE_INPUT_MODIFIER_ALT) == 0U;
        if (!prevented && clipboard_modifier
            && (key_code == 'A' || key_code == 'a')
            && is_text_control(target)) {
            ensure_form_value(*target);
            target->mutable_form_control().selection_start = 0U;
            target->mutable_form_control().selection_end =
                target->mutable_form_control().value.size();
            target->mutable_form_control().selection_start_utf16_suboffset = 0U;
            target->mutable_form_control().selection_end_utf16_suboffset = 0U;
            target->mutable_form_control().selection_direction =
                text_selection_direction::none;
            target->mutable_form_control().selection_explicitly_set = true;
            mark_primary_selection_publication_dirty(target);
            target->mutable_form_control().caret_visible = true;
            caret_blink_epoch_ms = last_animation_frame_timestamp_ms;
            document.mark_dirty();
            return true;
        }
        if (!prevented && clipboard_modifier) {
            if (key_code == 'C' || key_code == 'c')
                return dispatch_clipboard_shortcut(*target, "copy");
            if (key_code == 'X' || key_code == 'x')
                return dispatch_clipboard_shortcut(*target, "cut");
            if (key_code == 'V' || key_code == 'v')
                return dispatch_clipboard_shortcut(*target, "paste");
        }
        if (prevented || !is_text_control(target)) return true;
        ensure_form_value(*target);
        auto start = std::min(target->mutable_form_control().selection_start, target->mutable_form_control().value.size());
        auto end = std::min(
            std::max(target->mutable_form_control().selection_start, target->mutable_form_control().selection_end),
            target->mutable_form_control().value.size());
        bool changed = false;
        bool inserting_line_break = false;
        const auto shift = (input.flags & WEBSCENE_INPUT_MODIFIER_SHIFT) != 0U;
        if (key_code == 8) {
            if (start == end) start = previous_utf8_boundary(target->mutable_form_control().value, start);
            changed = end > start;
        } else if (key_code == 46) {
            if (start == end) end = next_utf8_boundary(target->mutable_form_control().value, end);
            changed = end > start;
        } else if (key_code == 13 && target->tag == "textarea") {
            changed = true;
            inserting_line_break = true;
        } else if (key_code == 37 || key_code == 38
            || key_code == 39 || key_code == 40
            || key_code == 36 || key_code == 35) {
            const auto moving_backward = key_code == 37 || key_code == 38 || key_code == 36;
            const auto boundary = key_code == 36 || key_code == 38
                ? size_t{0}
                : key_code == 35 || key_code == 40
                    ? target->mutable_form_control().value.size()
                    : moving_backward
                        ? previous_utf8_boundary(target->mutable_form_control().value, start)
                        : next_utf8_boundary(target->mutable_form_control().value, end);
            if (!shift) {
                const auto position = start != end && (key_code == 37 || key_code == 38)
                    ? start
                    : start != end && (key_code == 39 || key_code == 40)
                        ? end
                        : boundary;
                target->mutable_form_control().selection_start = target->mutable_form_control().selection_end = position;
                target->mutable_form_control().selection_start_utf16_suboffset = 0U;
                target->mutable_form_control().selection_end_utf16_suboffset = 0U;
                target->mutable_form_control().selection_direction = text_selection_direction::none;
            } else {
                const auto anchor =
                    target->mutable_form_control().selection_direction == text_selection_direction::backward
                    ? end
                    : start;
                const auto focus =
                    target->mutable_form_control().selection_direction == text_selection_direction::backward
                    ? start
                    : end;
                const auto next_focus = key_code == 36 || key_code == 38
                    ? size_t{0}
                    : key_code == 35 || key_code == 40
                        ? target->mutable_form_control().value.size()
                        : moving_backward
                            ? previous_utf8_boundary(target->mutable_form_control().value, focus)
                            : next_utf8_boundary(target->mutable_form_control().value, focus);
                target->mutable_form_control().selection_start = std::min(anchor, next_focus);
                target->mutable_form_control().selection_end = std::max(anchor, next_focus);
                target->mutable_form_control().selection_start_utf16_suboffset = 0U;
                target->mutable_form_control().selection_end_utf16_suboffset = 0U;
                target->mutable_form_control().selection_direction = next_focus < anchor
                    ? text_selection_direction::backward
                    : next_focus > anchor
                        ? text_selection_direction::forward
                        : text_selection_direction::none;
            }
        }
        if (changed) {
            bool before_input_prevented = false;
            if (!dispatch_input_event_type(input, "beforeinput", *target, &before_input_prevented)) {
                return false;
            }
            if (!before_input_prevented) {
                const auto previous_value = target->form_control().value;
                if (inserting_line_break) {
                    target->mutable_form_control().value.replace(start, end - start, "\n");
                    ++start;
                } else {
                    target->mutable_form_control().value.erase(start, end - start);
                }
                target->mutable_form_control().dirty_value = true;
                target->mutable_form_control().selection_start =
                    target->mutable_form_control().selection_end = start;
                target->mutable_form_control().selection_start_utf16_suboffset = 0U;
                target->mutable_form_control().selection_end_utf16_suboffset = 0U;
                target->mutable_form_control().selection_direction = text_selection_direction::none;
                target->mutable_form_control().selection_explicitly_set = true;
                recascade_form_value_selector_transition(
                    *target, previous_value, target->form_control().value, true, true);
                if (!dispatch_input_event_type(input, "input", *target)) return false;
            }
        }
        if (key_code == 37 || key_code == 38 || key_code == 39 || key_code == 40
            || key_code == 36 || key_code == 35) {
            target->mutable_form_control().selection_explicitly_set = true;
            mark_primary_selection_publication_dirty(target);
        }
        target->mutable_form_control().caret_visible = true;
        caret_blink_epoch_ms = last_animation_frame_timestamp_ms;
        document.mark_dirty();
        return true;
    }

    v8::Local<v8::Object> drag_data_transfer_for(dom_node& target)
    {
        auto local_context = context_for_node(target);
        auto* root = css_cascade_root_for_node(target);
        const auto root_id = root == nullptr ? document.body().id : root->id;
        if (active_drag_session.has_value()) {
            if (const auto known = active_drag_session->transfers.find(root_id);
                known != active_drag_session->transfers.end()
                && !known->second.IsEmpty()) return known->second.Get(isolate);
        }
        auto records = v8::Array::New(
            isolate,
            active_drag_session.has_value()
                ? static_cast<int>(active_drag_session->items.size()) : 0);
        if (active_drag_session.has_value()) {
            for (uint32_t index = 0U;
                 index < active_drag_session->items.size(); ++index) {
                const auto& item = active_drag_session->items[index];
                auto record = v8::Object::New(isolate);
                record->Set(local_context, js_string(isolate, "kind"),
                    v8::Integer::NewFromUnsigned(isolate, item.kind)).Check();
                record->Set(local_context, js_string(isolate, "type"),
                    js_string(isolate, item.mime_type.c_str())).Check();
                record->Set(local_context, js_string(isolate, "name"),
                    js_string(isolate, item.name.c_str())).Check();
                record->Set(local_context, js_string(isolate, "path"),
                    js_string(isolate, item.relative_path.c_str())).Check();
                auto buffer = v8::ArrayBuffer::New(isolate, item.bytes.size());
                if (!item.bytes.empty()) {
                    std::memcpy(buffer->GetBackingStore()->Data(),
                        item.bytes.data(), item.bytes.size());
                }
                record->Set(local_context, js_string(isolate, "bytes"),
                    v8::Uint8Array::New(buffer, 0U, item.bytes.size())).Check();
                records->Set(local_context, index, record).Check();
            }
        }
        v8::Local<v8::Value> factory;
        v8::Local<v8::Value> result;
        v8::Local<v8::Value> arguments[]{records};
        if (!local_context->Global()->Get(local_context,
                js_string(isolate, "__webSceneCreateDragDataTransfer"))
                .ToLocal(&factory)
            || !factory->IsFunction()
            || !factory.As<v8::Function>()->Call(
                local_context, local_context->Global(), 1, arguments).ToLocal(&result)
            || !result->IsObject()) return v8::Object::New(isolate);
        auto transfer = result.As<v8::Object>();
        active_drag_session->transfers.emplace(
            root_id, v8::Global<v8::Object>(isolate, transfer));
        return transfer;
    }

    bool retire_drag_session(bool dispatch_leave, const webscene_input_event& input)
    {
        if (!active_drag_session.has_value()) return true;
        auto* target = document.find_by_native_id(
            active_drag_session->target_node_id);
        auto succeeded = true;
        if (dispatch_leave && target != nullptr && is_connected(*target)) {
            succeeded = dispatch_input_event_type(input, "dragleave", *target);
        }
        active_drag_session.reset();
        return succeeded;
    }

    bool dispatch_drag(native_drag_event& drag)
    {
        struct drag_dispatch_scope final {
            bool& dispatching;
            bool previous;
            explicit drag_dispatch_scope(bool& value)
                : dispatching(value), previous(value) { dispatching = true; }
            ~drag_dispatch_scope() { dispatching = previous; }
        } dispatch_scope{dispatching_user_input};
        auto isolate_locker = lock_shared_isolate();
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        auto outer_context = context.Get(isolate);
        v8::Context::Scope context_scope(outer_context);
        activate_css_cascade(outer_context);
        ensure_layout();
        webscene_input_event input{
            WEBSCENE_INPUT_POINTER_MOVE,
            drag.flags,
            drag.sequence,
            drag.x,
            drag.y,
            0.0,
            0.0};

        if (drag.action == WEBSCENE_DRAG_ENTER_V1) {
            if (!retire_drag_session(true, input)) return false;
            active_drag_session.emplace();
            active_drag_session->session_id = drag.session_id;
            active_drag_session->last_sequence = drag.sequence;
            active_drag_session->document_generation =
                document_generation.load(std::memory_order_acquire);
            active_drag_session->items = std::move(drag.items);
        } else if (!active_drag_session.has_value()
            || active_drag_session->session_id != drag.session_id) {
            return true;
        } else if (drag.sequence <= active_drag_session->last_sequence) {
            return true;
        } else {
            active_drag_session->last_sequence = drag.sequence;
        }
        if (active_drag_session->document_generation
            != document_generation.load(std::memory_order_acquire)) {
            retire_drag_session(false, input);
            return true;
        }
        if (active_drag_session->frame_owner_id != 0U
            && active_drag_session->frame_generation
                != current_iframe_navigation_generation(
                    active_drag_session->frame_owner_id)) {
            retire_drag_session(false, input);
            return true;
        }

        if (drag.action == WEBSCENE_DRAG_CANCEL_V1
            || drag.action == WEBSCENE_DRAG_LEAVE_V1) {
            return retire_drag_session(true, input);
        }

        auto* target = document.hit_test(
            document.body(), static_cast<float>(drag.x), static_cast<float>(drag.y));
        if (target == nullptr) target = &document.body();
        const auto target_id = target->id;
        if (active_drag_session->target_node_id != target_id) {
            auto* previous = document.find_by_native_id(
                active_drag_session->target_node_id);
            current_related_target = target;
            if (previous != nullptr && is_connected(*previous)
                && !dispatch_input_event_type(input, "dragleave", *previous)) {
                current_related_target = nullptr;
                retire_drag_session(false, input);
                return false;
            }
            current_related_target = previous;
            active_drag_session->target_node_id = target_id;
            active_drag_session->drop_admitted = false;
            if (!dispatch_input_event_type(input, "dragenter", *target)) {
                current_related_target = nullptr;
                retire_drag_session(false, input);
                return false;
            }
            current_related_target = nullptr;
            active_drag_session->frame_owner_id = 0U;
            active_drag_session->frame_generation = 0U;
            for (auto* ancestor = target->parent; ancestor != nullptr;
                 ancestor = ancestor->parent) {
                if (ancestor->tag != "iframe") continue;
                active_drag_session->frame_owner_id = ancestor->id;
                active_drag_session->frame_generation =
                    current_iframe_navigation_generation(ancestor->id);
                break;
            }
        }

        if (drag.action == WEBSCENE_DRAG_OVER_V1) {
            bool prevented = false;
            if (!dispatch_input_event_type(
                    input, "dragover", *target, &prevented)) {
                static_cast<void>(retire_drag_session(false, input));
                return false;
            }
            active_drag_session->drop_admitted = prevented;
            return true;
        }
        if (drag.action == WEBSCENE_DRAG_DROP_V1) {
            if (active_drag_session->drop_admitted
                && !dispatch_input_event_type(input, "drop", *target)) {
                retire_drag_session(false, input);
                return false;
            }
            retire_drag_session(false, input);
        }
        return true;
    }

    bool move_single_select_selection(
        dom_node& select,
        int offset,
        bool first,
        bool last,
        bool wrap)
    {
        const auto previous_value_missing=forms::select_value_missing(select);
        std::vector<dom_node*> options;
        const auto collect = [&](const auto& self, dom_node& node) -> void {
            for (auto* child : node.children) {
                if (child == nullptr) continue;
                if (child->tag == "option" && !child->attributes.contains("disabled")) {
                    options.push_back(child);
                }
                self(self, *child);
            }
        };
        collect(collect, select);
        if (options.empty()) return false;

        auto selected = options.end();
        selected = std::find_if(options.begin(), options.end(), [](const auto* option) {
            return option->form_control().selectedness_initialized
                && option->form_control().selectedness;
        });
        if (selected == options.end()
            && !select.form_control().selection_explicitly_empty) {
            selected = std::find_if(options.begin(), options.end(), [](const auto* option) {
                return option->attributes.contains("selected");
            });
        }
        if (selected == options.end()
            && !select.form_control().selection_explicitly_empty) {
            selected = options.begin();
        }
        auto next = selected;
        if (first) next = options.begin();
        else if (last) next = options.end() - 1;
        else if (selected == options.end()) next = options.begin();
        else if (offset > 0) {
            if (selected + 1 != options.end()) next = selected + 1;
            else if (wrap) next = options.begin();
        } else if (offset < 0) {
            if (selected != options.begin()) next = selected - 1;
            else if (wrap) next = options.end() - 1;
        }
        if (next == options.end() || next == selected) return false;
        select.mutable_form_control().selection_explicitly_empty = false;
        for (auto* option : options) {
            option->mutable_form_control().selectedness_initialized = true;
            option->mutable_form_control().selectedness = option == *next;
        }
        recascade_select_validity_selector_change(
            select,previous_value_missing);
        return true;
    }

    struct checkable_click_activation final {
        dom_node* target{nullptr};
        dom_node* previously_checked_radio{nullptr};
        bool target_was_checked{false};
        bool target_was_indeterminate{false};
        bool changed{false};
        bool active{false};
    };

    bool native_checkedness(const dom_node& target) const
    {
        return forms::input_checked(target);
    }

    std::vector<dom_node*> radio_group_members(dom_node& target)
    {
        const auto members=forms::radio_group_members(document,target);
        std::vector<dom_node*> result;
        result.reserve(members.size());
        for(const auto* member:members)
            result.push_back(const_cast<dom_node*>(member));
        return result;
    }

    bool set_native_checkedness(dom_node& target, bool checked)
    {
        const auto previous = native_checkedness(target);
        const auto group=radio_group_members(target);
        target.mutable_form_control().checkedness_initialized = true;
        target.mutable_form_control().checkedness = checked;
        if (previous != checked) {
            dismiss_validation_message_after_constraint_mutation(target);
            if (is_connected(target)) {
                recascade_form_checkedness_selector_transition(
                    target,previous,checked);
                // Group validity and radio :indeterminate both consume the
                // changed member's checkedness. Revisit only this radio group
                // through their compiled dependency.
                for(auto* member:group)
                    if(member!=&target)
                        recascade_radio_group_checkedness_selector_transition(
                            *member,target,previous,checked);
            } else {
                document.mark_dirty();
            }
        }
        return previous != checked;
    }

    bool begin_checkable_click_activation(
        dom_node& target,
        checkable_click_activation& activation)
    {
        if (target.tag != "input" || target.attributes.contains("disabled")) return false;
        const auto type_iterator = target.attributes.find("type");
        auto type = type_iterator == target.attributes.end()
            ? std::string("text")
            : type_iterator->second;
        std::transform(type.begin(), type.end(), type.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
        if (type != "checkbox" && type != "radio") return false;

        activation.target = &target;
        activation.target_was_checked = native_checkedness(target);
        activation.target_was_indeterminate = target.form_control().indeterminate;
        activation.active = true;
        if (type == "checkbox") {
            if(activation.target_was_indeterminate) {
                target.mutable_form_control().indeterminate=false;
                recascade_dynamic_state_subject(target,"checkbox-activation-indeterminate");
            }
            activation.changed = set_native_checkedness(
                target, !activation.target_was_checked);
            return true;
        }

        for (auto* candidate : radio_group_members(target)) {
            if (candidate == &target || !native_checkedness(*candidate)) continue;
            if (activation.previously_checked_radio == nullptr) {
                activation.previously_checked_radio = candidate;
            }
            set_native_checkedness(*candidate, false);
        }
        activation.changed = set_native_checkedness(target, true);
        return true;
    }

    void restore_checkable_click_activation(
        const checkable_click_activation& activation)
    {
        if (!activation.active || activation.target == nullptr) return;
        set_native_checkedness(*activation.target, activation.target_was_checked);
        if(activation.target->form_control().indeterminate
            != activation.target_was_indeterminate) {
            activation.target->mutable_form_control().indeterminate
                = activation.target_was_indeterminate;
            recascade_dynamic_state_subject(
                *activation.target,"checkbox-activation-restore-indeterminate");
        }
        if (activation.previously_checked_radio != nullptr) {
            set_native_checkedness(*activation.previously_checked_radio, true);
        }
    }

    bool apply_non_checkable_form_control_click_default(dom_node& target)
    {
        if (target.attributes.contains("disabled")) return false;
        // Collapsed selects have a real picker default action. Opening it is
        // deferred until after the click event so preventDefault() can suppress
        // the picker exactly like any other cancelable activation default.
        if (select_popup_is_candidate(target)) return false;
        return false;
    }

    bool apply_details_summary_click_default(
        dom_node& target,
        const webscene_input_event&)
    {
        auto* summary = &target;
        while (summary != nullptr && summary->tag != "summary") {
            const auto owns_activation = summary->tag == "button"
                || summary->tag == "input"
                || summary->tag == "select"
                || summary->tag == "textarea"
                || summary->tag == "label"
                || summary->tag == "option"
                || ((summary->tag == "a" || summary->tag == "area")
                    && summary->attributes.contains("href"))
                || ((summary->tag == "audio" || summary->tag == "video")
                    && summary->attributes.contains("controls"));
            if (owns_activation) return true;
            summary = summary->parent;
        }
        if (summary == nullptr || summary->parent == nullptr
            || summary->parent->tag != "details"
            || first_details_summary(*summary->parent) != summary) return true;
        auto& details = *summary->parent;
        if (!is_connected(details)) return true;
        const auto old_value = details.attributes.contains("open")
            ? std::optional<std::string>{details.attributes.at("open")}
            : std::optional<std::string>{};
        if (old_value.has_value()) details.attributes.erase("open");
        else details.attributes["open"] = {};
        const auto new_value = details.attributes.contains("open")
            ? std::optional<std::string>{details.attributes.at("open")}
            : std::optional<std::string>{};
        recascade_attribute_selector_transition(
            details, "open", old_value, new_value);
        document.mark_dirty();
        enqueue_details_toggle_event(
            details, old_value.has_value(), new_value.has_value());
        return true;
    }

    dom_node* label_control_for_click(dom_node& target)
    {
        if (is_labelable_control(target)) return &target;
        auto* label = &target;
        while (label != nullptr && label->tag != "label") label = label->parent;
        if (label == nullptr) return nullptr;

        const auto explicit_control = label->attributes.find("for");
        if (explicit_control != label->attributes.end()) {
            if (explicit_control->second.empty()) return nullptr;
            ensure_explicit_form_validity_dependents();
            auto first = static_cast<dom_node*>(nullptr);
            const auto candidates = label_targets_by_id.find(
                explicit_control->second);
            if (candidates == label_targets_by_id.end()) return nullptr;
            const auto* root = forms::tree_root(document, *label);
            for (auto* candidate : candidates->second) {
                if (candidate == nullptr || !is_connected(*candidate)
                    || forms::tree_root(document, *candidate) != root) continue;
                if (first == nullptr || tree_order_before(*candidate, *first))
                    first = candidate;
            }
            return first != nullptr && is_labelable_control(*first) ? first : nullptr;
        }
        return first_labelable_descendant(*label);
    }

    struct scrollbar_thumb_hit final {
        dom_node* target{nullptr};
        bool vertical{false};
        float pointer_origin{0};
        float scroll_origin{0};
        float maximum{0};
        float thumb_travel{0};
    };

    std::optional<scrollbar_thumb_hit> hit_test_scrollbar_thumb(
        dom_node& hit_target,
        float x,
        float y)
    {
        for (auto* node = &hit_target; node != nullptr; node = node->parent) {
            if (node->style.scrollbar_hidden
                || node->layout.width <= 0 || node->layout.height <= 0) continue;
            const auto border_left = document.resolve_used_length(
                *node, node->style.border_left_width, node->layout.width, 0);
            const auto border_right = document.resolve_used_length(
                *node, node->style.border_right_width, node->layout.width, 0);
            const auto border_top = document.resolve_used_length(
                *node, node->style.border_top_width, node->layout.height, 0);
            const auto border_bottom = document.resolve_used_length(
                *node, node->style.border_bottom_width, node->layout.height, 0);
            const auto padding_left = document.resolve_used_length(
                *node, node->style.padding_left, node->layout.width, 0);
            const auto padding_right = document.resolve_used_length(
                *node, node->style.padding_right, node->layout.width, 0);
            const auto padding_top = document.resolve_used_length(
                *node, node->style.padding_top, node->layout.height, 0);
            const auto padding_bottom = document.resolve_used_length(
                *node, node->style.padding_bottom, node->layout.height, 0);
            const auto viewport_x = node->layout.x + border_left + padding_left;
            const auto viewport_y = node->layout.y + border_top + padding_top;
            const auto viewport_width = std::max(
                0.0F, node->layout.width - border_left - border_right
                    - padding_left - padding_right);
            const auto viewport_height = std::max(
                0.0F, node->layout.height - border_top - border_bottom
                    - padding_top - padding_bottom);
            const auto maximum_x = std::max(
                0.0F, node->scroll_content_width - node->scroll_viewport_width);
            const auto maximum_y = std::max(
                0.0F, node->scroll_content_height - node->scroll_viewport_height);
            const auto root_scroll_x = node->parent == nullptr
                && node->style.overflow_x == overflow_mode::visible;
            const auto root_scroll_y = node->parent == nullptr
                && node->style.overflow_y == overflow_mode::visible;
            const auto& scrollbar = node->style.scrollbar();
            const auto inset = scrollbar.overlay_inset;
            if ((node->style.scroll_y_enabled || root_scroll_y)
                && maximum_y > 0.01F && scrollbar.width > 0) {
                const auto rail_x = viewport_x + viewport_width - scrollbar.width - inset;
                const auto rail_y = viewport_y + inset;
                const auto rail_height = std::max(0.0F, viewport_height - inset * 2);
                const auto thumb_height = std::min(rail_height, std::max(
                    scrollbar.thumb_min_height,
                    rail_height * node->scroll_viewport_height
                        / std::max(node->scroll_viewport_height, node->scroll_content_height)));
                const auto travel = std::max(0.0F, rail_height - thumb_height);
                const auto thumb_y = rail_y + travel
                    * std::clamp(node->scroll_top / maximum_y, 0.0F, 1.0F);
                if (x >= rail_x && x <= rail_x + scrollbar.width
                    && y >= thumb_y && y <= thumb_y + thumb_height) {
                    return scrollbar_thumb_hit{
                        node, true, y, node->scroll_top, maximum_y, travel};
                }
            }
            if ((node->style.scroll_x_enabled || root_scroll_x)
                && maximum_x > 0.01F && scrollbar.height > 0) {
                const auto rail_x = viewport_x + inset;
                const auto rail_y = viewport_y + viewport_height - scrollbar.height - inset;
                const auto rail_width = std::max(0.0F, viewport_width - inset * 2);
                const auto thumb_width = std::min(rail_width, std::max(
                    18.0F,
                    rail_width * node->scroll_viewport_width
                        / std::max(node->scroll_viewport_width, node->scroll_content_width)));
                const auto travel = std::max(0.0F, rail_width - thumb_width);
                const auto thumb_x = rail_x + travel
                    * std::clamp(node->scroll_left / maximum_x, 0.0F, 1.0F);
                if (x >= thumb_x && x <= thumb_x + thumb_width
                    && y >= rail_y && y <= rail_y + scrollbar.height) {
                    return scrollbar_thumb_hit{
                        node, false, x, node->scroll_left, maximum_x, travel};
                }
            }
        }
        return std::nullopt;
    }

    void update_scrollbar_thumb_interaction(
        dom_node* next_hover,
        dom_node* next_active)
    {
        auto* previous_hover = scrollbar_hover_target;
        auto* previous_active = scrollbar_drag_target;
        scrollbar_hover_target = next_hover;
        std::array<dom_node*, 4> affected{
            previous_hover, previous_active, next_hover, next_active};
        for (size_t index = 0; index < affected.size(); ++index) {
            auto* node = affected[index];
            if (node == nullptr || !is_connected(*node)) continue;
            if (std::find(affected.begin(), affected.begin() + index, node)
                != affected.begin() + index) continue;
            const auto hovered = node == next_hover;
            const auto active = node == next_active;
            if (node->scrollbar_thumb_hovered == hovered
                && node->scrollbar_thumb_active == active) continue;
            node->scrollbar_thumb_hovered = hovered;
            node->scrollbar_thumb_active = active;
            // State selectors are folded into the existing thumb cascade, so
            // one affected-node recascade preserves specificity, layers,
            // source order and !important without scanning the document.
            apply_css_rules(*node);
        }
    }

    std::pair<std::string_view, dom_node*> used_user_select(dom_node& target)
    {
        for (auto* node = target.kind == dom_node_kind::text
                ? target.parent : &target;
            node != nullptr; node = node->parent) {
            const auto& value = node->style.textual().user_select;
            if (value == "none" || value == "text" || value == "all") {
                return {value, node};
            }
        }
        return {"text", target.kind == dom_node_kind::text
            ? target.parent : &target};
    }

    dom_node* pointer_text_node_at(dom_node& hit, float x, float y)
    {
        if (hit.kind == dom_node_kind::text && !hit.text_content.empty()) {
            return &hit;
        }
        dom_node* fallback = nullptr;
        const auto find = [&](const auto& recurse, dom_node& node) -> dom_node* {
            if (!node.visible || node.style.display == display_mode::none) return nullptr;
            if (node.kind == dom_node_kind::text && !node.text_content.empty()) {
                if (fallback == nullptr) fallback = &node;
                const auto& box = node.layout;
                if (x >= box.x && x <= box.x + box.width
                    && y >= box.y && y <= box.y + box.height) return &node;
                for (const auto& fragment : node.text_layout_fragments) {
                    if (x >= fragment.x && x <= fragment.x + fragment.width
                        && y >= fragment.y && y <= fragment.y + fragment.height) {
                        return &node;
                    }
                }
                return nullptr;
            }
            for (auto* child : node.children) {
                if (child == nullptr) continue;
                if (auto* result = recurse(recurse, *child); result != nullptr) {
                    return result;
                }
            }
            return nullptr;
        };
        if (auto* exact = find(find, hit); exact != nullptr) return exact;
        return fallback;
    }

    bool begin_pointer_text_selection(
        const webscene_input_event& input, dom_node& target)
    {
        auto [policy, boundary] = used_user_select(target);
        if (policy == "none" || boundary == nullptr) return true;
        for (auto* node = &target; node != nullptr; node = node->parent) {
            if (node->tag == "input" || node->tag == "textarea"
                || node->tag == "select" || node->tag == "button") return true;
            if (const auto draggable = node->attributes.find("draggable");
                draggable != node->attributes.end() && draggable->second == "true") {
                return true;
            }
        }
        bool prevented = false;
        if (!dispatch_input_event_type(input, "selectstart", target, &prevented)) {
            return false;
        }
        if (prevented) return true;
        auto* root = css_cascade_root_for_node(target);
        if (root == nullptr) return true;
        auto& selection = window_selections.try_emplace(root->id).first->second;
        selection.root_id = root->id;
        refresh_selection_text_nodes(selection, *root);
        if (policy == "all") {
            std::vector<dom_node*> selected;
            const auto collect = [&](const auto& recurse, dom_node& node) -> void {
                if (!node.visible || node.style.display == display_mode::none) return;
                if (node.kind == dom_node_kind::text && !node.text_content.empty()) {
                    selected.push_back(&node);
                }
                for (auto* child : node.children) {
                    if (child != nullptr) recurse(recurse, *child);
                }
            };
            collect(collect, *boundary);
            if (selected.empty()) return true;
            selection.has_range = true;
            selection.anchor_node_id = selected.front()->id;
            selection.anchor_offset = 0U;
            selection.focus_node_id = selected.back()->id;
            selection.focus_offset = decode_wtf8_to_utf16(
                selected.back()->text_content).size();
            pointer_text_selection_active = false;
            pointer_selection_root_id = 0U;
            selection_paint_changed();
            return true;
        }
        auto* text = pointer_text_node_at(
            target, static_cast<float>(input.x), static_cast<float>(input.y));
        if (text == nullptr) return true;
        const auto offset = document.text_caret_offset_at_x(
            *text, static_cast<float>(input.x));
        selection.has_range = true;
        selection.anchor_node_id = text->id;
        selection.focus_node_id = text->id;
        selection.anchor_offset = offset;
        selection.focus_offset = offset;
        pointer_selection_root_id = root->id;
        pointer_text_selection_active = true;
        selection_paint_changed();
        return true;
    }

    void update_pointer_text_selection(
        const webscene_input_event& input, dom_node& hit)
    {
        if (!pointer_text_selection_active) return;
        const auto known = window_selections.find(pointer_selection_root_id);
        if (known == window_selections.end()) return;
        auto* text = pointer_text_node_at(
            hit, static_cast<float>(input.x), static_cast<float>(input.y));
        if (text == nullptr || css_cascade_root_for_node(*text) == nullptr
            || css_cascade_root_for_node(*text)->id != pointer_selection_root_id
            || used_user_select(*text).first == "none") return;
        auto& selection = known->second;
        const auto offset = document.text_caret_offset_at_x(
            *text, static_cast<float>(input.x));
        if (selection.focus_node_id == text->id && selection.focus_offset == offset) return;
        selection.focus_node_id = text->id;
        selection.focus_offset = offset;
        selection_paint_changed();
    }

    bool dispatch_input(const webscene_input_event& input, bool defer_cursor_update = false)
    {
        struct user_input_dispatch_scope {
            bool& active;
            bool previous;
            explicit user_input_dispatch_scope(bool& value)
                : active(value), previous(value) { active = true; }
            ~user_input_dispatch_scope() { active = previous; }
        } dispatch_scope{dispatching_user_input};
        current_input_sequence = input.sequence;
        if (input.kind == WEBSCENE_INPUT_POINTER_DOWN
            || input.kind == WEBSCENE_INPUT_KEY_DOWN) {
            last_user_activation = std::chrono::steady_clock::now();
        }
        auto isolate_locker = lock_shared_isolate();
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        // Hit-test the complete host viewport before choosing a browsing
        // context. The most recently loaded iframe does not own all input.
        auto local_context = context.Get(isolate);
        {
            v8::Context::Scope layout_context_scope(local_context);
            activate_css_cascade(local_context);
            ensure_layout();
        }
        const auto keyboard = input.kind == WEBSCENE_INPUT_KEY_DOWN
            || input.kind == WEBSCENE_INPUT_KEY_UP || input.kind == WEBSCENE_INPUT_TEXT;
        auto* initial_hit_target = keyboard ? nullptr : document.hit_test(
            document.body(), static_cast<float>(input.x), static_cast<float>(input.y));
        auto* input_owner = keyboard ? active_element
            : pointer_capture_target != nullptr ? pointer_capture_target : initial_hit_target;
        if (input_owner != nullptr) {
            local_context = context_for_node(*input_owner);
        } else if (keyboard && focused_document_owner_id != 0U) {
            // Child Window.focus() deliberately leaves BODY as the document's
            // activeElement. Route keyboard input by the separately tracked
            // focused browsing context so its Window listeners receive the
            // first key event before any inner control has taken focus.
            const auto focused_context =
                actual_frame_contexts.find(focused_document_owner_id);
            if (focused_context != actual_frame_contexts.end()
                && !focused_context->second.IsEmpty()) {
                local_context = focused_context->second.Get(isolate);
            }
        }
        activate_css_cascade(local_context);
        v8::Context::Scope context_scope(local_context);
        v8::TryCatch try_catch(isolate);
        if (input.kind == WEBSCENE_INPUT_POINTER_DOWN) {
            // Direct manipulation always wins over an in-flight coarse-wheel
            // interpolation.
            cancel_discrete_wheel_scroll();
        }
        const auto report_input_exception = [&] {
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
            report_inspector_exception(try_catch, local_context);
#endif
        };
        // Popup dispatch queries the active V8 realm and can emit input/change
        // events. Enter the input owner's context before asking for the active
        // popup; GetCurrentContext is empty before this scope and dereferencing
        // it crashed the first pointer event in native applications.
        if (!keyboard
            && (input.kind == WEBSCENE_INPUT_POINTER_MOVE
                || input.kind == WEBSCENE_INPUT_POINTER_DOWN
                || input.kind == WEBSCENE_INPUT_POINTER_UP
                || input.kind == WEBSCENE_INPUT_WHEEL)) {
            if (auto handled = dispatch_select_popup_pointer_input(
                    input, initial_hit_target); handled.has_value()) {
                return *handled;
            }
        }
        const auto leave_host = [&]() -> bool {
            // OS/control exits need not have a final mousemove. Clear :hover
            // before dispatch, with no destination element (relatedTarget=null).
            // Capture keeps its boundary until release; a host exit is not up
            // or cancel and must not interrupt a chart/scrollbar drag.
            if (pointer_capture_target != nullptr || scrollbar_drag_target != nullptr) {
                return true;
            }
            update_scrollbar_thumb_interaction(nullptr, nullptr);
            auto* previous = hover_target;
            hover_target = nullptr;
            current_related_target = nullptr;
            current_cursor_kind_value = WEBSCENE_CURSOR_DEFAULT;
            pointer_cursor_update_pending = false;
            has_pointer_position = false;
            current_movement_x = current_movement_y = 0;
            recascade_hover_transition(previous, nullptr, "host-pointer-exit");
            if (previous == nullptr) return true;
            std::vector<dom_node*> exited_nodes;
            for (auto* node = previous; node != nullptr; node = node->parent) {
                exited_nodes.push_back(node);
            }
            const auto dispatch = [&](const char* type, dom_node& node) {
                if (dispatch_input_event_type(input, type, node)) return true;
                last_error = "Native host pointer exit dispatch failed: "
                    + describe_reported_exception(try_catch, local_context);
                report_input_exception();
                return false;
            };
            if (!dispatch("pointerout", *previous)) return false;
            for (auto* node : exited_nodes) {
                if (!dispatch("pointerleave", *node)) return false;
            }
            if (!dispatch("mouseout", *previous)) return false;
            for (auto* node : exited_nodes) {
                if (!dispatch("mouseleave", *node)) return false;
            }
            return true;
        };
        if (input.kind == WEBSCENE_INPUT_POINTER_LEAVE) {
            return leave_host();
        }
        if (input.kind == WEBSCENE_INPUT_KEY_DOWN
            || input.kind == WEBSCENE_INPUT_KEY_UP
            || input.kind == WEBSCENE_INPUT_TEXT) {
            if (!dispatch_keyboard_or_text(input)) {
                last_error = "Native keyboard input dispatch failed: "
                    + describe_reported_exception(try_catch, local_context);
                report_input_exception();
                return false;
            }
            return true;
        }
        const auto viewport = viewport_provider();
        const auto pointer_outside = input.x < 0 || input.y < 0
            || input.x >= viewport.width || input.y >= viewport.height;
        const auto scrollbar_hit_at_pointer = [&]() -> std::optional<scrollbar_thumb_hit> {
            if (pointer_outside
                || (input.kind != WEBSCENE_INPUT_POINTER_MOVE
                    && input.kind != WEBSCENE_INPUT_POINTER_DOWN
                    && input.kind != WEBSCENE_INPUT_POINTER_UP)) return std::nullopt;
            auto* start = initial_hit_target;
            if (start == nullptr) start = &document.body();
            return hit_test_scrollbar_thumb(
                *start, static_cast<float>(input.x), static_cast<float>(input.y));
        };
        auto current_scrollbar_hit = scrollbar_hit_at_pointer();
        if (scrollbar_drag_target == nullptr
            && input.kind == WEBSCENE_INPUT_POINTER_MOVE) {
            update_scrollbar_thumb_interaction(
                current_scrollbar_hit.has_value() ? current_scrollbar_hit->target : nullptr,
                nullptr);
        }
        if (scrollbar_drag_target != nullptr
            && (input.kind == WEBSCENE_INPUT_POINTER_MOVE
                || input.kind == WEBSCENE_INPUT_POINTER_UP)) {
            if (input.kind == WEBSCENE_INPUT_POINTER_MOVE
                && scrollbar_drag_thumb_travel > 0.01F) {
                const auto pointer = scrollbar_drag_vertical ? input.y : input.x;
                const auto next = std::clamp(
                    scrollbar_drag_scroll_origin
                        + static_cast<float>(pointer - scrollbar_drag_pointer_origin)
                            * scrollbar_drag_maximum / scrollbar_drag_thumb_travel,
                    0.0F,
                    scrollbar_drag_maximum);
                auto& current = scrollbar_drag_vertical
                    ? scrollbar_drag_target->scroll_top
                    : scrollbar_drag_target->scroll_left;
                if (std::abs(next - current) > 0.01F) {
                    current = next;
                    document.mark_dirty();
                    if (!dispatch_input_event_type(
                            input, "scroll", *scrollbar_drag_target)) {
                        last_error = "Native scrollbar scroll dispatch failed: "
                            + describe_reported_exception(try_catch, local_context);
                        report_input_exception();
                        return false;
                    }
                }
            }
            if (input.kind == WEBSCENE_INPUT_POINTER_UP) {
                update_scrollbar_thumb_interaction(
                    current_scrollbar_hit.has_value()
                        ? current_scrollbar_hit->target : nullptr,
                    nullptr);
                scrollbar_drag_target = nullptr;
                if (pointer_outside) return leave_host();
            }
            return true;
        }
        if (pointer_outside && pointer_capture_target == nullptr
            && (input.kind == WEBSCENE_INPUT_POINTER_MOVE
                || input.kind == WEBSCENE_INPUT_POINTER_DOWN
                || input.kind == WEBSCENE_INPUT_POINTER_UP)) {
            if (!leave_host()) return false;
            // Hosts capture the mouse for ordinary document-level drags too,
            // even when script does not call setPointerCapture. Keep delivering
            // their move/up stream, without restoring :hover outside the host.
            if (pointer_down_target == nullptr) return true;
        }
        auto* hit_target = initial_hit_target;
        if (hit_target == nullptr) hit_target = &document.body();
        if (pointer_outside) {
            hit_target = pointer_capture_target != nullptr
                ? pointer_capture_target : &active_root();
        }
        if (input.kind == WEBSCENE_INPUT_POINTER_DOWN
            && ((input.flags >> 8U) & 0xffU) <= 1U) {
            if (current_scrollbar_hit.has_value()) {
                update_scrollbar_thumb_interaction(
                    current_scrollbar_hit->target,
                    current_scrollbar_hit->target);
                // Hover declarations can alter the authored minimum extent.
                // Re-read the same retained geometry before beginning drag.
                current_scrollbar_hit = hit_test_scrollbar_thumb(
                    *hit_target, static_cast<float>(input.x), static_cast<float>(input.y));
            }
            if (const auto& hit = current_scrollbar_hit; hit.has_value()) {
                scrollbar_drag_target = hit->target;
                scrollbar_drag_vertical = hit->vertical;
                scrollbar_drag_pointer_origin = hit->pointer_origin;
                scrollbar_drag_scroll_origin = hit->scroll_origin;
                scrollbar_drag_maximum = hit->maximum;
                scrollbar_drag_thumb_travel = hit->thumb_travel;
                return true;
            }
        }
        if (input.kind == WEBSCENE_INPUT_POINTER_UP) {
            update_scrollbar_thumb_interaction(
                current_scrollbar_hit.has_value() ? current_scrollbar_hit->target : nullptr,
                nullptr);
        }
        auto* target = pointer_capture_target != nullptr
            && input.kind != WEBSCENE_INPUT_POINTER_DOWN
            ? pointer_capture_target
            : hit_target;
        current_movement_x = has_pointer_position ? input.x - last_pointer_x : 0;
        current_movement_y = has_pointer_position ? input.y - last_pointer_y : 0;
        if (input.kind != WEBSCENE_INPUT_WHEEL) {
            last_pointer_x = input.x;
            last_pointer_y = input.y;
            has_pointer_position = true;
        }
        if ((input.kind == WEBSCENE_INPUT_POINTER_MOVE
                || input.kind == WEBSCENE_INPUT_POINTER_DOWN)
            && (!pointer_outside || pointer_capture_target != nullptr)
            && hit_target != hover_target) {
            auto* previous_hover_target = hover_target;
            // Browsers update :hover before dispatching the boundary events.
            // Event handlers are therefore allowed to observe the new
            // computed style synchronously from mouseover/pointerover.
            hover_target = hit_target;
            recascade_hover_transition(
                previous_hover_target,
                hover_target,
                "hover-target-change");
            dom_node* common_ancestor = previous_hover_target;
            while (common_ancestor != nullptr) {
                auto* candidate = hit_target;
                while (candidate != nullptr && candidate != common_ancestor) {
                    candidate = candidate->parent;
                }
                if (candidate == common_ancestor) break;
                common_ancestor = common_ancestor->parent;
            }
            if (previous_hover_target != nullptr) {
                current_related_target = hit_target;
                if (!dispatch_input_event_type(input, "pointerout", *previous_hover_target)) {
                    last_error = "Native input transition dispatch failed: "
                        + describe_reported_exception(try_catch, local_context);
                    report_input_exception();
                    return false;
                }
                for (auto* exited = previous_hover_target;
                     exited != nullptr && exited != common_ancestor;
                     exited = exited->parent) {
                    if (!dispatch_input_event_type(input, "pointerleave", *exited)) {
                        last_error = "Native input transition dispatch failed: "
                            + describe_reported_exception(try_catch, local_context);
                        report_input_exception();
                        return false;
                    }
                }
                if (!dispatch_input_event_type(input, "mouseout", *previous_hover_target)) {
                    last_error = "Native input transition dispatch failed: "
                        + describe_reported_exception(try_catch, local_context);
                    report_input_exception();
                    return false;
                }
                for (auto* exited = previous_hover_target;
                     exited != nullptr && exited != common_ancestor;
                     exited = exited->parent) {
                    if (!dispatch_input_event_type(input, "mouseleave", *exited)) {
                        last_error = "Native input transition dispatch failed: "
                            + describe_reported_exception(try_catch, local_context);
                        report_input_exception();
                        return false;
                    }
                }
            }
            current_related_target = previous_hover_target;
            std::vector<dom_node*> entered_nodes;
            for (auto* entered = hit_target;
                 entered != nullptr && entered != common_ancestor;
                 entered = entered->parent) {
                entered_nodes.push_back(entered);
            }
            std::reverse(entered_nodes.begin(), entered_nodes.end());
            if (!dispatch_input_event_type(input, "pointerover", *hit_target)) {
                last_error = "Native input transition dispatch failed: "
                    + describe_reported_exception(try_catch, local_context);
                report_input_exception();
                return false;
            }
            for (auto* entered : entered_nodes) {
                if (!dispatch_input_event_type(input, "pointerenter", *entered)) {
                    last_error = "Native input transition dispatch failed: "
                        + describe_reported_exception(try_catch, local_context);
                    report_input_exception();
                    return false;
                }
            }
            if (!dispatch_input_event_type(input, "mouseover", *hit_target)) {
                last_error = "Native input transition dispatch failed: "
                    + describe_reported_exception(try_catch, local_context);
                report_input_exception();
                return false;
            }
            for (auto* entered : entered_nodes) {
                if (!dispatch_input_event_type(input, "mouseenter", *entered)) {
                    last_error = "Native input transition dispatch failed: "
                        + describe_reported_exception(try_catch, local_context);
                    report_input_exception();
                    return false;
                }
            }
            current_related_target = nullptr;

            // Hover handlers are allowed to replace the element under the
            // pointer (several component libraries do this for icon buttons). The
            // subsequent pointerdown must target the post-hover DOM, not the
            // now-detached node selected before pointerover/mouseenter ran.
            // Keeping the stale target can make a modal disappear through an
            // outside-click handler without invoking its Close/Cancel action,
            // leaving the toolbar opener permanently in its `isOpened` state.
            ensure_layout();
            hit_target = document.hit_test(
                document.body(),
                static_cast<float>(input.x),
                static_cast<float>(input.y));
            if (hit_target == nullptr) hit_target = &active_root();
            if (pointer_capture_target == nullptr
                || input.kind == WEBSCENE_INPUT_POINTER_DOWN) {
                target = hit_target;
            }
            if (hover_target != hit_target) {
                auto* previous_hover_target = hover_target;
                hover_target = hit_target;
                recascade_hover_transition(
                    previous_hover_target,
                    hover_target,
                    "hover-target-replaced");
            }
        }
        const char* types[3]{};
        size_t type_count = 0;
        bool form_control_changed = false;
        dom_node* activated_form_control = nullptr;
        bool activate_label_control_after_click = false;
        checkable_click_activation checkable_activation;
        dom_node* click_target = nullptr;
        dom_node* auxiliary_click_target = nullptr;
        dom_node* context_menu_target = nullptr;
        const auto encoded_button = static_cast<int>((input.flags >> 8U) & 0xffU);
        const auto changed_button = encoded_button == 0 ? 0 : encoded_button - 1;
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (std::getenv("WEBSCENE_PROBE_PROFILE_INPUT") != nullptr
            && (input.kind == WEBSCENE_INPUT_POINTER_MOVE
                || input.kind == WEBSCENE_INPUT_POINTER_DOWN
                || input.kind == WEBSCENE_INPUT_POINTER_UP)) {
            std::cerr << "[Native Engine Probe] input dispatch"
                << ", kind=" << input.kind
                << ", sequence=" << input.sequence
                << ", hit=" << hit_target->id
                << ", target=" << target->id
                << ", capture-target="
                << (pointer_capture_target == nullptr
                    ? 0U : pointer_capture_target->id)
                << ", flags=" << input.flags
                << ", x=" << input.x
                << ", y=" << input.y
                << ", movement-x=" << current_movement_x
                << ", movement-y=" << current_movement_y
                << '\n';
        }
#endif
        switch (input.kind) {
        case WEBSCENE_INPUT_POINTER_MOVE:
            types[type_count++] = "pointermove";
            types[type_count++] = "mousemove";
            break;
        case WEBSCENE_INPUT_POINTER_DOWN:
            {
                auto* previous_active_target = active_pointer_target;
                active_pointer_target = target;
                // Browsers expose :active before pointerdown and mousedown
                // listeners synchronously inspect computed style.
                recascade_active_transition(
                    previous_active_target,
                    active_pointer_target,
                    "active-pointer-down");
            }
            pointer_down_target = target;
            middle_paste_target_id = 0U;
            outbound_drag_threshold_crossed = false;
            pointer_down_x = input.x;
            pointer_down_y = input.y;
            types[type_count++] = "pointerdown";
            types[type_count++] = "mousedown";
            if (changed_button == 2) context_menu_target = target;
            break;
        case WEBSCENE_INPUT_POINTER_UP:
            {
                auto* previous_active_target = active_pointer_target;
                active_pointer_target = nullptr;
                // The physical press has ended before pointerup/mouseup run.
                recascade_active_transition(
                    previous_active_target,
                    nullptr,
                    "active-pointer-up");
            }
            types[type_count++] = "pointerup";
            types[type_count++] = "mouseup";
            if (pointer_down_target != nullptr
                && std::hypot(input.x - pointer_down_x, input.y - pointer_down_y) <= 4.0) {
                // UI Events targets click at the nearest common inclusive
                // ancestor of the press and release targets. Interactive rows
                // reveal/replace icon children on hover, so requiring the same
                // leaf suppressed an otherwise valid click between down/up.
                auto* common_target = pointer_down_target;
                while (common_target != nullptr) {
                    auto* release_ancestor = hit_target;
                    while (release_ancestor != nullptr
                        && release_ancestor != common_target) {
                        release_ancestor = release_ancestor->parent;
                    }
                    if (release_ancestor == common_target) break;
                    common_target = common_target->parent;
                }
                if (common_target == nullptr) common_target = &active_root();
                if (changed_button == 0) {
                    click_target = common_target;
                    activated_form_control = label_control_for_click(*click_target);
                    activate_label_control_after_click = activated_form_control != nullptr
                        && activated_form_control != click_target;
                    if (activated_form_control != nullptr && !activate_label_control_after_click) {
                        // Browser checkable controls change their native checkedness
                        // before click listeners run. Do not invoke the JavaScript
                        // property setter: controlled-input libraries wrap it to
                        // distinguish script writes from a genuine user activation.
                        if (!begin_checkable_click_activation(
                                *activated_form_control,
                                checkable_activation)) {
                            form_control_changed =
                                apply_non_checkable_form_control_click_default(
                                    *activated_form_control);
                        }
                    }
                } else {
                    auxiliary_click_target = common_target;
                }
            }
            break;
        case WEBSCENE_INPUT_WHEEL:
            types[type_count++] = "wheel";
            break;
        default:
            return true;
        }
        bool wheel_default_prevented = false;
        bool pointer_focus_default_prevented = false;
        bool mouse_up_default_prevented = false;
        for (size_t index = 0; index < type_count; ++index) {
            bool event_default_prevented = false;
            const auto observe_default = input.kind == WEBSCENE_INPUT_WHEEL
                || input.kind == WEBSCENE_INPUT_POINTER_DOWN
                || input.kind == WEBSCENE_INPUT_POINTER_UP;
            if (!dispatch_input_event_type(
                    input,
                    types[index],
                    *target,
                    observe_default ? &event_default_prevented : nullptr)) {
                last_error = "Native input dispatch failed: " + describe_reported_exception(try_catch, local_context);
                report_input_exception();
                return false;
            }
            if (input.kind == WEBSCENE_INPUT_WHEEL) {
                wheel_default_prevented = wheel_default_prevented || event_default_prevented;
            } else if (input.kind == WEBSCENE_INPUT_POINTER_DOWN) {
                pointer_focus_default_prevented = pointer_focus_default_prevented
                    || event_default_prevented;
            } else if (input.kind == WEBSCENE_INPUT_POINTER_UP) {
                if (std::string_view{types[index]} == "mouseup") {
                    mouse_up_default_prevented = event_default_prevented;
                }
            }
        }
        if (!maybe_begin_outbound_drag(input)) {
            last_error = "Native outbound drag initiation failed: "
                + describe_reported_exception(try_catch, local_context);
            report_input_exception();
            return false;
        }
        if (input.kind == WEBSCENE_INPUT_POINTER_DOWN
            && changed_button == 0 && !pointer_focus_default_prevented
            && !begin_pointer_text_selection(input, *target)) {
            last_error = "Native selectstart dispatch failed";
            return false;
        }
        if ((input.kind == WEBSCENE_INPUT_POINTER_MOVE
                || input.kind == WEBSCENE_INPUT_POINTER_UP)
            && pointer_text_selection_active) {
            update_pointer_text_selection(input, *hit_target);
        }
        if (input.kind == WEBSCENE_INPUT_WHEEL && !wheel_default_prevented) {
            const auto discrete_wheel =
                (input.flags & WEBSCENE_INPUT_WHEEL_DISCRETE) != 0U;
            const auto platform_smoothed_wheel =
                (input.flags & (WEBSCENE_INPUT_WHEEL_PRECISE
                    | WEBSCENE_INPUT_WHEEL_NATIVE_MOMENTUM)) != 0U;
            const auto animate_wheel = discrete_wheel
                && !platform_smoothed_wheel
                && !css::get_active_accessibility_preferences().reduced_motion;
            if (!animate_wheel) cancel_discrete_wheel_scroll();
            for (auto* scroll_target = target; scroll_target != nullptr;
                scroll_target = scroll_target->parent) {
                // A loaded document exposes HTML beneath the retained native
                // viewport.  Geometry accessors project that HTML root onto
                // the viewport scroll owner, so wheel defaults must skip the
                // exposed wrapper and mutate the same owner as scrollTop.
                if (scroll_target->tag == "html"
                    && scroll_target->parent == &document.body()) {
                    continue;
                }
                const auto maximum_x = std::max(
                    0.0F,
                    scroll_target->scroll_content_width - scroll_target->scroll_viewport_width);
                const auto maximum_y = std::max(
                    0.0F,
                    scroll_target->scroll_content_height - scroll_target->scroll_viewport_height);
                // The retained viewport belongs to the outer document, not
                // whichever iframe context last ran script or loaded. In
                // particular an overflow:hidden embed must not disable page
                // scrolling, including wheel input outside the embed.
                const auto* exposed_root = static_cast<const dom_node*>(
                    context.Get(isolate)->GetAlignedPointerFromEmbedderData(
                        1, v8::kEmbedderDataTypeTagDefault));
                if (exposed_root == nullptr) exposed_root = &document.body();
                const auto root_viewport_scroll_x = scroll_target == &document.body()
                    && (exposed_root->style.overflow_x == overflow_mode::visible
                        || exposed_root->style.scroll_x_enabled);
                const auto root_viewport_scroll_y = scroll_target == &document.body()
                    && (exposed_root->style.overflow_y == overflow_mode::visible
                        || exposed_root->style.scroll_y_enabled);
                const auto [base_x, base_y] = animate_wheel
                    ? discrete_wheel_scroll_base(
                        *scroll_target,
                        static_cast<float>(input.delta_x),
                        static_cast<float>(input.delta_y))
                    : std::pair{
                        scroll_target->scroll_left,
                        scroll_target->scroll_top};
                const auto next_x = (scroll_target->style.scroll_x_enabled
                        || root_viewport_scroll_x)
                    ? std::clamp(
                        base_x + static_cast<float>(input.delta_x),
                        0.0F,
                        maximum_x)
                    : scroll_target->scroll_left;
                const auto next_y = (scroll_target->style.scroll_y_enabled
                        || root_viewport_scroll_y)
                    ? std::clamp(
                        base_y + static_cast<float>(input.delta_y),
                        0.0F,
                        maximum_y)
                    : scroll_target->scroll_top;
                const auto comparison_x = animate_wheel
                    ? base_x : scroll_target->scroll_left;
                const auto comparison_y = animate_wheel
                    ? base_y : scroll_target->scroll_top;
                if (std::abs(next_x - comparison_x) <= 0.01F
                    && std::abs(next_y - comparison_y) <= 0.01F) {
                    continue;
                }
                if (animate_wheel) {
                    begin_discrete_wheel_scroll(
                        *scroll_target,
                        next_x,
                        next_y,
                        local_context,
                        input.sequence);
                    break;
                }
                document.set_scroll_offset(*scroll_target, next_x, next_y);
                if (!dispatch_input_event_type(input, "scroll", *scroll_target)) {
                    last_error = "Native scroll dispatch failed: "
                        + describe_reported_exception(try_catch, local_context);
                    report_input_exception();
                    return false;
                }
                break;
            }
        }
        if (input.kind == WEBSCENE_INPUT_POINTER_DOWN
            && (changed_button == 0 || changed_button == 1)
            && !pointer_focus_default_prevented) {
            auto* focus_target = nearest_pointer_focusable(target);
            focus_visible = false;
            if ((focus_target != nullptr || active_element != nullptr)
                && !set_active_element(focus_target, input)) {
                last_error = "Native focus dispatch failed: "
                    + describe_reported_exception(try_catch, local_context);
                return false;
            }
        }
        if (input.kind == WEBSCENE_INPUT_POINTER_DOWN
            && changed_button == 1 && is_writable_text_control(active_element)) {
            middle_paste_target_id = active_element->id;
        }
        if (context_menu_target != nullptr
            && !dispatch_input_event_type(input, "contextmenu", *context_menu_target)) {
            last_error = "Native contextmenu dispatch failed: "
                + describe_reported_exception(try_catch, local_context);
            return false;
        }
        bool click_default_prevented = false;
        if (click_target != nullptr) {
            if (!dispatch_input_event_type(
                    input,
                    "click",
                    *click_target,
                    &click_default_prevented)) {
                last_error = "Native click dispatch failed: "
                    + describe_reported_exception(try_catch, local_context);
                return false;
            }
            if (checkable_activation.active) {
                if (click_default_prevented) {
                    restore_checkable_click_activation(checkable_activation);
                } else {
                    form_control_changed = checkable_activation.changed;
                }
            }
            if (!click_default_prevented
                && activated_form_control != nullptr
                && !activate_label_control_after_click
                && select_popup_is_candidate(*activated_form_control)) {
                static_cast<void>(open_select_popup(*activated_form_control));
            }
            if (!click_default_prevented && is_submit_button(click_target)) {
                if(auto* form=form_owner(*click_target);form!=nullptr
                    && !dispatch_form_submit(input,*form,click_target)) {
                    last_error="Native form submission dispatch failed";
                    return false;
                }
            }
            if (!click_default_prevented
                && !apply_details_summary_click_default(*click_target, input)) {
                last_error = "Native details summary activation failed";
                return false;
            }
            auto activation_flags = uint32_t{0U};
            const auto new_context_modifier =
                (input.flags & (WEBSCENE_INPUT_POINTER_MODIFIER_CONTROL
                    | WEBSCENE_INPUT_POINTER_MODIFIER_META
                    | WEBSCENE_INPUT_POINTER_MODIFIER_SHIFT)) != 0U;
            if (new_context_modifier) {
                activation_flags |= WEBSCENE_HOST_REQUEST_EXTERNAL_NEW_CONTEXT_V1;
            }
            if ((input.flags & (WEBSCENE_INPUT_POINTER_MODIFIER_CONTROL
                    | WEBSCENE_INPUT_POINTER_MODIFIER_META)) != 0U
                && (input.flags & WEBSCENE_INPUT_POINTER_MODIFIER_SHIFT) == 0U) {
                activation_flags |= WEBSCENE_HOST_REQUEST_EXTERNAL_BACKGROUND_V1;
            }
            if (!click_default_prevented
                && !queue_external_navigation(*click_target, activation_flags)) {
                last_error = "Native external navigation host request could not be queued";
                return false;
            }
        }
        if (auxiliary_click_target != nullptr) {
            bool auxiliary_click_default_prevented = false;
            if (!dispatch_input_event_type(
                    input,
                    "auxclick",
                    *auxiliary_click_target,
                    &auxiliary_click_default_prevented)) {
                last_error = "Native auxiliary click dispatch failed: "
                    + describe_reported_exception(try_catch, local_context);
                return false;
            }
            if (changed_button == 1 && !mouse_up_default_prevented
                && !auxiliary_click_default_prevented) {
                uint32_t activation_flags =
                    WEBSCENE_HOST_REQUEST_EXTERNAL_NEW_CONTEXT_V1;
                if ((input.flags & WEBSCENE_INPUT_POINTER_MODIFIER_SHIFT) == 0U) {
                    activation_flags |=
                        WEBSCENE_HOST_REQUEST_EXTERNAL_BACKGROUND_V1;
                }
                bool navigation_matched = false;
                if (!queue_external_navigation(
                        *auxiliary_click_target,
                        activation_flags,
                        &navigation_matched)) {
                    last_error =
                        "Native auxiliary external navigation host request could not be queued";
                    return false;
                }
                if (!navigation_matched && middle_paste_target_id != 0U) {
                    auto* paste_target = document.find_by_native_id(
                        middle_paste_target_id);
                    if (is_writable_text_control(paste_target)
                        && is_connected(*paste_target)
                        && !dispatch_clipboard_shortcut(
                            *paste_target,
                            "paste",
                            WEBSCENE_HOST_REQUEST_CLIPBOARD_PRIMARY_V1)) {
                        last_error =
                            "Native PRIMARY selection paste could not be queued";
                        return false;
                    }
                }
            }
        }
        if (activate_label_control_after_click && !click_default_prevented) {
            // A label owns the visual hit area for many custom switches;
            // their native input is intentionally zero-width. HTML label
            // activation dispatches a second click at the associated control.
            checkable_click_activation label_checkable_activation;
            if (!begin_checkable_click_activation(
                    *activated_form_control,
                    label_checkable_activation)) {
                form_control_changed = apply_non_checkable_form_control_click_default(
                    *activated_form_control);
            }
            bool control_click_default_prevented = false;
            if (!dispatch_input_event_type(
                    input,
                    "click",
                    *activated_form_control,
                    &control_click_default_prevented)) {
                last_error = "Native label-control activation failed: "
                    + describe_reported_exception(try_catch, local_context);
                return false;
            }
            if (label_checkable_activation.active) {
                if (control_click_default_prevented) {
                    restore_checkable_click_activation(label_checkable_activation);
                    form_control_changed = false;
                } else {
                    form_control_changed = label_checkable_activation.changed;
                }
            }
            if (!control_click_default_prevented
                && activated_form_control != nullptr
                && select_popup_is_candidate(*activated_form_control)) {
                static_cast<void>(open_select_popup(*activated_form_control));
            }
        }
        if (form_control_changed
            && activated_form_control != nullptr
            && is_connected(*activated_form_control)) {
            if (!dispatch_input_event_type(input, "input", *activated_form_control)
                || !dispatch_input_event_type(input, "change", *activated_form_control)) {
                last_error = "Native form-control dispatch failed: "
                    + describe_reported_exception(try_catch, local_context);
                return false;
            }
        }
        if (input.kind == WEBSCENE_INPUT_POINTER_UP) {
            pointer_text_selection_active = false;
            pointer_selection_root_id = 0U;
            pointer_capture_target = nullptr;
            pointer_down_target = nullptr;
            middle_paste_target_id = 0U;
            outbound_drag_threshold_crossed = false;
            if (pointer_outside) return leave_host();
        }
        if (input.kind == WEBSCENE_INPUT_POINTER_MOVE
            || input.kind == WEBSCENE_INPUT_POINTER_DOWN
            || input.kind == WEBSCENE_INPUT_POINTER_UP) {
            if (defer_cursor_update && input.kind == WEBSCENE_INPUT_POINTER_MOVE) {
                pointer_cursor_update_pending = true;
                return true;
            }
            pointer_cursor_update_pending = false;
            ensure_layout();
            auto* cursor_target = document.hit_test(
                document.body(),
                static_cast<float>(input.x),
                static_cast<float>(input.y));
            current_cursor_kind_value = cursor_target == nullptr
                ? WEBSCENE_CURSOR_DEFAULT
                : cursor_kind_for(*cursor_target);

        }
        return true;
    }
