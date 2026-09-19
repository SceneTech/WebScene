#include "graphics/linux_external_image.h"
#include <cerrno>
#include <stdexcept>
#include <system_error>
#include <vector>

using namespace webscene::graphics;
namespace {
void require(bool value){if(!value)throw std::runtime_error("Linux external-image contract failed");}
struct fake_fd_ops {
    static inline int duplicate_calls=0;
    static inline int fail_at=-1;
    static inline std::vector<int> closed;
    static int empty()noexcept{return -1;}
    static bool valid(int value)noexcept{return value>=0;}
    static void close(int value)noexcept{closed.push_back(value);}
    static int duplicate(int borrowed){
        const auto call=duplicate_calls++;
        if(call==fail_at)throw std::system_error(EMFILE,std::generic_category(),"fake duplicate");
        return borrowed+1000;
    }
    static void reset(){duplicate_calls=0;fail_at=-1;closed.clear();}
};
struct provider final : linux_external_image_provider {
    linux_external_image_snapshot descriptor;
    bool fail{};
    bool export_image(const image_metadata& expected,linux_external_image_snapshot& result)const override{
        if(fail)return false;
        result=descriptor;
        return same_image_metadata(expected,result.metadata);
    }
};
image_metadata metadata(){return {1,2,3,4,5,6,640,480,image_format::bgra8_unorm,
    image_alpha::premultiplied,image_color_space::srgb,image_orientation::top_left};}
linux_external_image_snapshot descriptor(){
    linux_external_image_snapshot value;
    value.metadata=metadata();value.memory_handle=linux_memory_handle::opaque_fd;
    value.queue_sharing=linux_queue_sharing::exclusive;value.allocation_size=640ULL*480*4;
    value.memory_type_index=2;value.vk_format=44;value.vk_image_type=1;value.vk_tiling=0;
    value.vk_usage=0x14;value.sample_count=1;value.mip_level_count=1;value.array_layer_count=1;
    value.producer_layout=7;value.consumer_layout=5;value.producer_queue_family=2;
    value.consumer_queue_family=3;value.device_uuid[0]=1;value.driver_uuid[0]=2;
    value.dedicated_allocation=true;value.planes.push_back({10,0,0});
    value.waits.push_back({linux_sync_handle::vk_semaphore_opaque_fd,11,9,17,true});
    return value;
}
struct fixture {
    std::shared_ptr<provider> source=std::make_shared<provider>();
    owned_image_pool pool{source};
    owned_image_pool::producer producer;
    owned_image_pool::retained retained;
    owned_image_pool::consumer consumer;
    fixture():producer(std::move(*pool.acquire())),retained(make_retained()),consumer(std::move(*retained.begin_consumer())){}
    owned_image_pool::retained make_retained(){
        source->descriptor=descriptor();producer.set_metadata(source->descriptor.metadata);
        producer.begin();producer.complete();return std::move(*producer.publish());
    }
    ~fixture(){consumer.complete();}
};
void test_duplicate_and_close(){
    fake_fd_ops::reset();
    fixture value;
    {
        auto owner=basic_linux_shared_image_owner<fake_fd_ops>::create(value.consumer);
        require(owner&&owner->planes().size()==1&&owner->waits().size()==1);
        require(owner->planes()[0].fd.get()==1010&&owner->waits()[0].fd.get()==1011);
        require(owner->snapshot().planes.empty()&&owner->snapshot().waits.empty());
    }
    require(fake_fd_ops::closed.size()==2);
    require((fake_fd_ops::closed[0]==1010||fake_fd_ops::closed[1]==1010)
        &&(fake_fd_ops::closed[0]==1011||fake_fd_ops::closed[1]==1011));
}
void test_atomic_failure_rollback(){
    fake_fd_ops::reset();fake_fd_ops::fail_at=1;
    fixture value;
    bool failed=false;
    try{(void)basic_linux_shared_image_owner<fake_fd_ops>::create(value.consumer);}
    catch(const std::system_error&){failed=true;}
    require(failed&&fake_fd_ops::closed==std::vector<int>({1010}));
}
void test_descriptor_rejection(){
    auto value=descriptor();
    require(valid_linux_external_snapshot(value,value.metadata));
    value.producer_layout=0;require(!valid_linux_external_snapshot(value,value.metadata));
    value=descriptor();value.device_uuid={};require(!valid_linux_external_snapshot(value,value.metadata));
    value=descriptor();value.waits.clear();require(!valid_linux_external_snapshot(value,value.metadata));
    value.producer_complete=true;require(valid_linux_external_snapshot(value,value.metadata));
    value=descriptor();value.waits[0].signaled_value=0;
    require(!valid_linux_external_snapshot(value,value.metadata));
    value=descriptor();value.waits[0]={linux_sync_handle::sync_fd,11,9,1,false};
    require(valid_linux_external_snapshot(value,value.metadata));
    value.waits[0].signaled_value=0;require(!valid_linux_external_snapshot(value,value.metadata));
    value=descriptor();value.queue_sharing=linux_queue_sharing::concurrent;
    require(!valid_linux_external_snapshot(value,value.metadata));
    value.producer_queue_family=value.consumer_queue_family=UINT32_MAX;
    value.vk_sharing_mode=1;value.vk_queue_family_index_count=2;
    value.vk_queue_family_indices[0]=2;value.vk_queue_family_indices[1]=3;
    require(valid_linux_external_snapshot(value,value.metadata));
    value.vk_queue_family_indices[1]=2;
    require(!valid_linux_external_snapshot(value,value.metadata));
}
}
int main(){test_duplicate_and_close();test_atomic_failure_rollback();test_descriptor_rejection();}
