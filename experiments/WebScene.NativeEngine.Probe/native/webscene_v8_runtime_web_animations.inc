    static bool web_animation_property(v8::Local<v8::Context> context,
        v8::Local<v8::Object> object,const char* name,v8::Local<v8::Value>& value)
    { return object->Get(context,js_string(v8::Isolate::GetCurrent(),name)).ToLocal(&value); }

    static void create_web_animation(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        auto* self=current(info.GetIsolate());
        auto context=info.GetIsolate()->GetCurrentContext();
        if(info.Length()!=8||!info[0]->IsObject()||!info[1]->IsArray())return;
        auto target=info[0].As<v8::Object>();
        if(target->InternalFieldCount()<1)return;
        auto* node=static_cast<dom_node*>(target->GetAlignedPointerFromInternalField(0,v8::kEmbedderDataTypeTagDefault));
        if(node==nullptr||node->kind!=dom_node_kind::element||!self->is_connected(*node))return;
        auto frames=info[1].As<v8::Array>();
        if(frames->Length()<2U||frames->Length()>16U)return;
        node_style::animation_data::keyframe_animation effect;
        effect.duration_ms=static_cast<float>(info[2]->NumberValue(context).FromMaybe(-1));
        effect.delay_ms=static_cast<float>(info[3]->NumberValue(context).FromMaybe(0));
        effect.iterations=static_cast<float>(info[4]->NumberValue(context).FromMaybe(-1));
        const auto direction=info[5]->Uint32Value(context).FromMaybe(4U);
        const auto fill=info[6]->Uint32Value(context).FromMaybe(4U);
        if(!std::isfinite(effect.duration_ms)||effect.duration_ms<0||effect.duration_ms>3'600'000.0F
            ||!std::isfinite(effect.delay_ms)||std::abs(effect.delay_ms)>3'600'000.0F
            ||effect.iterations<0||(std::isfinite(effect.iterations)&&effect.iterations>1'000'000.0F)
            ||direction>3U||fill>3U)return;
        effect.direction=static_cast<node_style::animation_data::direction_kind>(direction);
        effect.fill_mode=static_cast<node_style::animation_data::fill_kind>(fill);
        node_style::transition_timing timing;
        css::parse_transition_timing(to_utf8(info.GetIsolate(),info[7]),timing);
        effect.x1=timing.x1;effect.y1=timing.y1;effect.x2=timing.x2;effect.y2=timing.y2;
        effect.step_count=timing.step_count;effect.timing_kind=timing.kind;effect.step_position=timing.steps_position;
        bool opacity=false,transform=false;
        for(uint32_t index=0;index<frames->Length();++index){
            v8::Local<v8::Value> raw;
            if(!frames->Get(context,index).ToLocal(&raw)||!raw->IsObject())return;
            auto frame=raw.As<v8::Object>();
            v8::Local<v8::Value> offset_value,opacity_value,transform_value;
            if(!web_animation_property(context,frame,"offset",offset_value)||!offset_value->IsNumber()
                ||!web_animation_property(context,frame,"opacity",opacity_value)
                ||!web_animation_property(context,frame,"transform",transform_value))return;
            const auto offset=static_cast<float>(offset_value->NumberValue(context).FromMaybe(-1));
            if(!std::isfinite(offset)||offset<0||offset>1)return;
            if(!opacity_value->IsNullOrUndefined()){
                const auto value=static_cast<float>(opacity_value->NumberValue(context).FromMaybe(
                    std::numeric_limits<double>::quiet_NaN()));
                if(!std::isfinite(value))return;
                effect.opacity_keyframes.push_back({offset,std::clamp(value,0.0F,1.0F)});opacity=true;
            }
            if(!transform_value->IsNullOrUndefined()){
                const auto parsed=css::parse_keyframe_transform(to_utf8(info.GetIsolate(),transform_value));
                if(!parsed)return;
                effect.translation_keyframes.push_back({offset,parsed->translate_x,parsed->translate_y});
                effect.scale_keyframes.push_back({offset,parsed->scale_x,parsed->scale_y});
                effect.rotation_keyframes.push_back({offset,parsed->rotate_degrees});transform=true;
            }
        }
        if((opacity&&effect.opacity_keyframes.size()!=frames->Length())
            ||(transform&&effect.translation_keyframes.size()!=frames->Length())||(!opacity&&!transform))return;
        auto id=self->next_web_animation_id++;
        if(id==0U){id=self->next_web_animation_id++;}
        if(self->document.create_web_animation(*node,id,std::move(effect)))
            info.GetReturnValue().Set(v8::Integer::NewFromUnsigned(info.GetIsolate(),id));
    }

    static void control_web_animation(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if(info.Length()<2||!info[0]->IsUint32()||!info[1]->IsUint32())return;
        auto context=info.GetIsolate()->GetCurrentContext();
        const auto command=info[1]->Uint32Value(context).FromMaybe(5U);
        if(command>4U)return;
        const auto seek=info.Length()>2?info[2]->NumberValue(context).FromMaybe(0):0;
        info.GetReturnValue().Set(v8::Boolean::New(info.GetIsolate(),current(info.GetIsolate())->document.control_web_animation(
            info[0].As<v8::Uint32>()->Value(),static_cast<native_document::web_animation_command>(command),seek)));
    }

    static void web_animation_status(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if(info.Length()!=1||!info[0]->IsUint32())return;
        const auto state=current(info.GetIsolate())->document.web_animation_status(info[0].As<v8::Uint32>()->Value());
        if(!state)return;
        auto context=info.GetIsolate()->GetCurrentContext();auto result=v8::Object::New(info.GetIsolate());
        result->Set(context,js_string(info.GetIsolate(),"currentTime"),v8::Number::New(info.GetIsolate(),state->current_time_ms)).Check();
        result->Set(context,js_string(info.GetIsolate(),"paused"),v8::Boolean::New(info.GetIsolate(),state->paused)).Check();
        result->Set(context,js_string(info.GetIsolate(),"finished"),v8::Boolean::New(info.GetIsolate(),state->finished)).Check();
        info.GetReturnValue().Set(result);
    }

    static void element_web_animation_ids(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        if(info.Length()!=1||!info[0]->IsObject())return;
        auto target=info[0].As<v8::Object>();if(target->InternalFieldCount()<1)return;
        auto* node=static_cast<dom_node*>(target->GetAlignedPointerFromInternalField(0,v8::kEmbedderDataTypeTagDefault));
        if(node==nullptr)return;
        const auto ids=current(info.GetIsolate())->document.web_animation_ids(*node);
        auto result=v8::Array::New(info.GetIsolate(),static_cast<int>(ids.size()));
        auto context=info.GetIsolate()->GetCurrentContext();
        for(uint32_t index=0;index<ids.size();++index)result->Set(context,index,v8::Integer::NewFromUnsigned(info.GetIsolate(),ids[index])).Check();
        info.GetReturnValue().Set(result);
    }

    void install_web_animations_api(v8::Local<v8::Context> local_context)
    {
        auto global=local_context->Global();
        const auto install=[&](const char* name,v8::FunctionCallback callback){
            global->Set(local_context,js_string(isolate,name),v8::Function::New(local_context,callback).ToLocalChecked()).Check();};
        install("__webSceneCreateWebAnimation",create_web_animation);
        install("__webSceneControlWebAnimation",control_web_animation);
        install("__webSceneWebAnimationStatus",web_animation_status);
        install("__webSceneElementWebAnimationIds",element_web_animation_ids);
        auto script=v8::Script::Compile(local_context,js_string(isolate,webAnimationsCompatibilityScript.data())).ToLocalChecked();
        script->Run(local_context).ToLocalChecked();
    }
