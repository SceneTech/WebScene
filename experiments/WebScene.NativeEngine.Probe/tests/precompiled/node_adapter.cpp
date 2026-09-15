// Real-V8 API smoke adapter for hosts without the complete WebScene SDK.
// Node supplies V8 initialization; OpenSSL supplies the SHA-256 implementation.
// Production uses WebScene's own initialization, snapshot and SHA-256 instead.
#include <node.h>
#include <node_buffer.h>
#include <openssl/sha.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "webscene_precompiled_javascript.h"
namespace webscene_native {
namespace {
std::array<uint8_t, 32> sha256(const uint8_t* data, size_t size) {
    std::array<uint8_t, 32> out{}; SHA256(data, size, out.data()); return out;
}
void initialize_v8_process() {}
void configure_startup_snapshot(v8::Isolate::CreateParams&) {}
#include "webscene_precompiled_javascript_support.inc"
}
#include "webscene_precompiled_javascript_api.inc"
}
int node_test_precompile(const char* source, size_t size, const char* name, uint32_t module,
    webscene_precompiled_javascript_sink_v1 sink, void* user, char* error, size_t capacity) {
    return webscene_native::compile_precompiled_javascript_in_isolate(v8::Isolate::GetCurrent(),
        source,size,name,module,sink,user,error,capacity);
}
#define webscene_precompile_javascript_v1 node_test_precompile
#define main webscene_jsc_main
#include "../../tools/precompile_javascript.cpp"
#undef main
#undef webscene_precompile_javascript_v1
namespace {
using namespace v8;
std::string str(Isolate* i, Local<Value> v) { String::Utf8Value s(i,v); return *s ? std::string(*s,s.length()) : ""; }
Local<String> js(Isolate* i, const char* s) { return String::NewFromUtf8(i,s).ToLocalChecked(); }
void run(const FunctionCallbackInfo<Value>& args) {
    auto* isolate = args.GetIsolate();
    auto source = str(isolate,args[0]);
    bool module = args[1]->BooleanValue(isolate);
    auto entry = webscene_native::find_precompiled_javascript(source,module);
    if(!entry) {isolate->ThrowException(Exception::Error(js(isolate,"No registered cache")));return;}
    auto context = Context::New(isolate);
    Context::Scope scope(context);
    auto text=String::NewFromUtf8(isolate,source.data(),NewStringType::kNormal,int(source.size())).ToLocalChecked();
    auto name=js(isolate,"file:///relocated/app.js");
    if(module) {
        Local<Module> compiled;
        if(!webscene_native::consume_precompiled_module(isolate,text,name,*entry,compiled))return;
        auto resolver=[](Local<Context>,Local<String>,Local<FixedArray>,Local<Module>)->MaybeLocal<Module>{return {};};
        if(!compiled->InstantiateModule(context,resolver).FromMaybe(false))return;
        if(compiled->Evaluate(context).IsEmpty())return;
        auto ns=compiled->GetModuleNamespace().As<Object>();
        Local<Value> value;
        if(ns->Get(context,js(isolate,"answer")).ToLocal(&value))args.GetReturnValue().Set(value);
    } else {
        Local<UnboundScript> script;
        if(!webscene_native::consume_precompiled_classic(isolate,text,name,*entry,script))return;
        Local<Value> value;
        if(script->BindToCurrentContext()->Run(context).ToLocal(&value))args.GetReturnValue().Set(value);
    }
}
struct produced {std::vector<uint8_t> bytes; uint32_t tag{}; std::string version,snapshot;};
int receive(const webscene_precompiled_javascript_v1* e,void* p) {
    auto& out=*static_cast<produced*>(p); out.bytes.assign(e->data,e->data+e->data_length);
    out.tag=e->cached_data_version_tag;out.version=e->v8_version;out.snapshot=e->snapshot_fingerprint;return 0;
}
void produce(const FunctionCallbackInfo<Value>& args) {
    auto* i=args.GetIsolate(); auto source=str(i,args[0]); bool module=args[1]->BooleanValue(i);
    produced out;char error[4096]{};
    if(webscene_native::compile_precompiled_javascript_in_isolate(i,source.data(),source.size(),"build://app.js",module,receive,&out,error,sizeof(error))) {
        i->ThrowException(Exception::Error(js(i,error))); return;
    }
    auto c=i->GetCurrentContext(); auto obj=Object::New(i);
    obj->Set(c,js(i,"data"),node::Buffer::Copy(i,reinterpret_cast<char*>(out.bytes.data()),out.bytes.size()).ToLocalChecked()).Check();
    obj->Set(c,js(i,"tag"),Integer::NewFromUnsigned(i,out.tag)).Check();
    obj->Set(c,js(i,"version"),js(i,out.version.c_str())).Check();
    obj->Set(c,js(i,"snapshot"),js(i,out.snapshot.c_str())).Check();
    args.GetReturnValue().Set(obj);
}
void install(const FunctionCallbackInfo<Value>& args) {
    auto* i=args.GetIsolate();auto c=i->GetCurrentContext();auto source=str(i,args[0]);
    auto obj=args[2].As<Object>();auto data=obj->Get(c,js(i,"data")).ToLocalChecked();
    webscene_precompiled_javascript_v1 e{};e.struct_size=sizeof(e);e.version=1;e.is_module=args[1]->BooleanValue(i);
    e.data=reinterpret_cast<uint8_t*>(node::Buffer::Data(data));e.data_length=node::Buffer::Length(data);
    e.cached_data_version_tag=obj->Get(c,js(i,"tag")).ToLocalChecked()->Uint32Value(c).FromJust();
    auto version=str(i,obj->Get(c,js(i,"version")).ToLocalChecked());auto snapshot=str(i,obj->Get(c,js(i,"snapshot")).ToLocalChecked());
    e.v8_version=version.c_str();e.snapshot_fingerprint=snapshot.c_str();
    auto hash=webscene_native::sha256(reinterpret_cast<const uint8_t*>(source.data()),source.size());
    std::copy(hash.begin(),hash.end(),e.source_sha256);
    hash=webscene_native::sha256(e.data,e.data_length);std::copy(hash.begin(),hash.end(),e.payload_sha256);
    if(args.Length()>3&&args[3]->BooleanValue(i))e.payload_sha256[0]^=1;
    args.GetReturnValue().Set(Integer::New(i,webscene_register_precompiled_javascript_v1(&e)));
}
void stats(const FunctionCallbackInfo<Value>& args) {
    auto* i=args.GetIsolate();auto c=i->GetCurrentContext();webscene_precompiled_javascript_stats_v1 s{sizeof(s),1};
    webscene_get_precompiled_javascript_stats_v1(&s);auto obj=Object::New(i);
    obj->Set(c,js(i,"registered"),Number::New(i,double(s.registered_scripts))).Check();
    obj->Set(c,js(i,"hits"),Number::New(i,double(s.cache_hits))).Check();
    obj->Set(c,js(i,"rejections"),Number::New(i,double(s.cache_rejections))).Check();args.GetReturnValue().Set(obj);
}
void cli(const FunctionCallbackInfo<Value>& args) {
    auto* i=args.GetIsolate();auto c=i->GetCurrentContext();auto input=args[0].As<Array>();
    std::vector<std::string> strings{"webscene-jsc"};for(uint32_t k=0;k<input->Length();++k)strings.push_back(str(i,input->Get(c,k).ToLocalChecked()));
    std::vector<char*> ptrs;for(auto& s:strings)ptrs.push_back(s.data());
    args.GetReturnValue().Set(Integer::New(i,webscene_jsc_main(int(ptrs.size()),ptrs.data())));
}
void init(Local<Object> exports) {
    NODE_SET_METHOD(exports,"produce",produce);NODE_SET_METHOD(exports,"install",install);NODE_SET_METHOD(exports,"run",run);NODE_SET_METHOD(exports,"stats",stats);NODE_SET_METHOD(exports,"cli",cli);
}
NODE_MODULE(NODE_GYP_MODULE_NAME,init)
}
