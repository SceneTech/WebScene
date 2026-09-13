#include <webscene/compiled_document.hpp>
#include <graphics/native_webgpu_offscreen_surface.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
import runtime.headless.ui;

using namespace std::chrono_literals;
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::string evaluate(webscene_engine* engine, const std::string& expression) {
    const auto source = "JSON.stringify(" + expression + ")";
    webscene_interop_evaluate_request_v3 request{};
    request.struct_size=sizeof(request); request.version=3;
    request.source=source.data(); request.source_length=source.size();
    request.document_name="runtime-headless-test"; request.document_name_length=21;
    const auto operation=webscene_engine_begin_evaluate_v3(engine,&request,[](void*,uint64_t){},nullptr);
    require(operation!=0,"Runtime evaluation rejected");
    const auto deadline=std::chrono::steady_clock::now()+20s;
    const webscene_interop_result_view_v3* result=nullptr;
    while (!(result=webscene_engine_take_invoke_result_v3(engine,operation))) {
        if (std::chrono::steady_clock::now()>=deadline) {
            webscene_engine_cancel_invoke_v3(engine,operation);
            throw std::runtime_error("Runtime evaluation timed out");
        }
        std::this_thread::sleep_for(1ms);
    }
    const auto status=result->status;
    std::string text;
    if(status!=WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3)
        text.assign(result->error_bytes,result->error_byte_count);
    else {
        const auto& value=result->values[result->root_value_index];
        if(value.kind==WEBSCENE_INTEROP_VALUE_STRING_V3)
            text.assign(result->utf8_bytes+value.offset,value.length);
    }
    webscene_interop_result_release_v3(result,result->lease_id);
    if(status!=WEBSCENE_INTEROP_RESULT_SUCCEEDED_V3)
        throw std::runtime_error("Runtime expression failed: "+text);
    return text;
}
void eventually(webscene_engine* engine,const std::string& expression,const std::string& expected) {
    const auto deadline=std::chrono::steady_clock::now()+20s;
    std::string last;
    do {
        last=evaluate(engine,expression);
        if(last==expected)return;
        std::this_thread::sleep_for(5ms);
    } while(std::chrono::steady_clock::now()<deadline);
    throw std::runtime_error("Expected "+expression+" == "+expected+", got "+last+"; failure="+evaluate(engine,"window.failure"));
}
using image_ptr=std::unique_ptr<webscene_gpu_image_lease_v3,decltype(&webscene_gpu_image_release_v3)>;
image_ptr image(webscene_engine* engine,uint32_t width,uint32_t height,uint64_t after=0) {
    const auto deadline=std::chrono::steady_clock::now()+20s;
    webscene_scene_acquire_options_v3 options{};
    options.struct_size=sizeof(options);options.scene_version=3;
    // The offscreen compositor consumes only completed images, not external GPU waits.
    options.consumer_capabilities=~uint64_t(WEBSCENE_SCENE_CAPABILITY_PRODUCER_GPU_WAITS);
    do {
        const webscene_scene_view_v3* view=nullptr;
        const auto status=webscene_engine_acquire_next_scene_v3(engine,&options,&view);
        if(status==WEBSCENE_SCENE_ACQUIRE_SUCCESS && view) {
            webscene_gpu_image_lease_v3* found=nullptr;
            for(uint32_t i=0;i<webscene_scene_gpu_image_count_v3(view);++i) {
                webscene_gpu_image_lease_v3* candidate=nullptr;
                if(webscene_scene_retain_gpu_image_v3(view,i,&candidate)!=WEBSCENE_SCENE_ACQUIRE_SUCCESS)continue;
                const auto m=candidate->value.describe();
                if(m.width==width && m.height==height && m.content_serial>after) {found=candidate;break;}
                webscene_gpu_image_release_v3(candidate);
            }
            webscene_scene_acknowledge_v3(view);
            webscene_scene_release_v3(view);
            if(found)return image_ptr(found,webscene_gpu_image_release_v3);
        } else if(status!=WEBSCENE_SCENE_ACQUIRE_EMPTY) {
            throw std::runtime_error("Scene acquisition failed: "+std::to_string(status));
        }
        std::this_thread::sleep_for(2ms);
    } while(std::chrono::steady_clock::now()<deadline);
    throw std::runtime_error("Completed Runtime GPU image not published; failure="+evaluate(engine,"window.failure"));
}
void color(const webscene_gpu_image_lease_v3& frame,uint8_t r,uint8_t g,uint8_t b) {
    const auto capture=webscene::graphics::capture_offscreen_image(frame);
    require(capture.pixels.size()==size_t(capture.metadata.width)*capture.metadata.height*4,"Incorrect capture extent");
    for(size_t i=0;i<capture.pixels.size();i+=4)
        require(capture.pixels[i]==r && capture.pixels[i+1]==g && capture.pixels[i+2]==b && capture.pixels[i+3]==255,
                "Runtime GPU frame has incorrect pixels");
}
}
int main(int argc,char** argv) {
    try {
        const std::string mode=argc>1?argv[1]:"";
        if(mode=="--reject-software")unsetenv("WEBSCENE_HEADLESS_FORCE_SOFTWARE_ADAPTER");
        webscene_engine_options options{};options.struct_size=sizeof(options);
        if(mode!="--policy-disabled")options.webgpu_policy_callback=[](void*,const char* url,size_t size)->uint32_t {
            return std::string_view(url,size).starts_with("file://")?WEBSCENE_WEBGPU_OFFSCREEN:WEBSCENE_WEBGPU_DISABLED;
        };
        std::unique_ptr<webscene_engine,decltype(&webscene_engine_destroy)> engine(
            webscene_engine_create_with_options(&options),webscene_engine_destroy);
        require(bool(engine),"Engine creation failed");
        require(webscene::register_compiled_document(engine.get(),"runtime-test",compiled_engine::build("")),"Compiled package registration failed");
        const std::string url="file:///runtime-headless/Main.html";
        require(webscene_engine_load_compiled_document_v1(engine.get(),"runtime-test",12,url.data(),url.size(),nullptr),"Compiled Runtime load rejected");
        if(mode=="--policy-disabled") {
            eventually(engine.get(),"typeof navigator.gpu","\"undefined\"");
            std::cout<<"Disabled WebGPU policy remained disabled\n";return 0;
        }
        if(mode=="--reject-software") {
            eventually(engine.get(),"typeof window.failure === 'string' && window.failure.length > 0","true");
            require(evaluate(engine.get(),"window.runtimeReady")=="false","Software adapter was accepted without opt-in");
            std::cout<<"Software adapter requires explicit host opt-in\n";return 0;
        }
        eventually(engine.get(),"window.runtimeReady","true");
        eventually(engine.get(),"window.workerValue","42");
        require(evaluate(engine.get(),"window.rafCount")=="0","RAF advanced without host frame input");
        auto red=image(engine.get(),64,64);color(*red,255,0,0);
        uint64_t sequence=0;
        auto input=[&](uint32_t kind,uint32_t flags,double x,double y=0,double dx=0,double dy=0) {
            webscene_input_event event{kind,flags,++sequence,x,y,dx,dy};
            require(webscene_engine_enqueue(engine.get(),&event),"Input admission failed");
        };
        input(WEBSCENE_INPUT_POINTER_MOVE,0,40,30);
        input(WEBSCENE_INPUT_POINTER_DOWN,1,40,30);
        input(WEBSCENE_INPUT_POINTER_UP,0,40,30);
        eventually(engine.get(),"document.getElementById('count').textContent","\"1\"");
        auto green=image(engine.get(),64,64,red->value.describe().content_serial);
        color(*green,0,255,0);color(*red,255,0,0);
        input(WEBSCENE_INPUT_FRAME,0,100);
        eventually(engine.get(),"window.rafCount","1");
        input(WEBSCENE_INPUT_RESIZE,0,128,96,1);
        eventually(engine.get(),"document.getElementById('scene').width","96");
        auto resized=image(engine.get(),96,48);color(*resized,0,255,0);
        color(*red,255,0,0);color(*green,0,255,0);
        red.reset();green.reset();resized.reset();
        engine.reset();
        std::cout<<"Runtime: compiled HTML, JS input, Worker, manual RAF, Vulkan pixels, immutable leases, resize and shutdown passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
