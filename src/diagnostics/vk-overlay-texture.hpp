#pragma once
// Uploads a CPU-drawn BGRA image (cairo ARGB32) to a persistent Vulkan image and
// hands it to SteamVR with SetOverlayTexture. SetOverlayRaw re-sends the whole
// image through IPC on every call and makes the dashboard panel blink; a GPU
// texture swap does not. Two images alternate so the compositor never reads an
// image while it is being written. Any failure -> ready()==false, caller falls
// back to SetOverlayRaw.
#include <vulkan/vulkan.h>
#include <openvr.h>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>
namespace vk_overlay {
inline std::vector<std::string> splitSpaces(const std::string& text){
    std::vector<std::string> out;std::istringstream input(text);std::string item;
    while(input>>item)out.push_back(item);
    return out;
}
class Texture {
    VkInstance instance_=VK_NULL_HANDLE;VkPhysicalDevice physical_=VK_NULL_HANDLE;VkDevice device_=VK_NULL_HANDLE;
    VkQueue queue_=VK_NULL_HANDLE;uint32_t family_=0;
    VkImage images_[2]{};VkDeviceMemory imageMemory_[2]{};bool initialized_[2]{};
    VkBuffer staging_=VK_NULL_HANDLE;VkDeviceMemory stagingMemory_=VK_NULL_HANDLE;void* mapped_=nullptr;
    VkCommandPool pool_=VK_NULL_HANDLE;VkCommandBuffer command_=VK_NULL_HANDLE;VkFence fence_=VK_NULL_HANDLE;
    uint32_t width_=0,height_=0;int next_=0;bool ready_=false;std::string error_;
    bool fail(const char* what){error_=what;destroy();return false;}
    int memoryType(uint32_t bits,VkMemoryPropertyFlags flags){
        VkPhysicalDeviceMemoryProperties props;vkGetPhysicalDeviceMemoryProperties(physical_,&props);
        for(uint32_t i=0;i<props.memoryTypeCount;++i)if((bits&(1u<<i))&&(props.memoryTypes[i].propertyFlags&flags)==flags)return int(i);
        return -1;
    }
public:
    ~Texture(){destroy();}
    bool ready()const{return ready_;}
    const std::string& error()const{return error_;}
    bool init(uint32_t width,uint32_t height){
        width_=width;height_=height;
        auto compositor=vr::VRCompositor();auto system=vr::VRSystem();
        if(!compositor||!system)return fail("no compositor/system interface");
        std::vector<std::string> instanceExt;
        {uint32_t n=compositor->GetVulkanInstanceExtensionsRequired(nullptr,0);
         if(n){std::string buf(n,'\0');compositor->GetVulkanInstanceExtensionsRequired(buf.data(),n);instanceExt=splitSpaces(buf.c_str());}}
        std::vector<const char*> ie;for(auto& e:instanceExt)ie.push_back(e.c_str());
        VkApplicationInfo app{};app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;app.pApplicationName="frame-finger-settings";app.apiVersion=VK_API_VERSION_1_0;
        VkInstanceCreateInfo ici{};ici.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;ici.pApplicationInfo=&app;
        ici.enabledExtensionCount=uint32_t(ie.size());ici.ppEnabledExtensionNames=ie.data();
        if(vkCreateInstance(&ici,nullptr,&instance_)!=VK_SUCCESS)return fail("vkCreateInstance");
        uint64_t hmdDevice=0;system->GetOutputDevice(&hmdDevice,vr::TextureType_Vulkan,instance_);
        physical_=reinterpret_cast<VkPhysicalDevice>(hmdDevice);
        if(!physical_){
            uint32_t n=0;vkEnumeratePhysicalDevices(instance_,&n,nullptr);if(!n)return fail("no physical device");
            std::vector<VkPhysicalDevice> all(n);vkEnumeratePhysicalDevices(instance_,&n,all.data());physical_=all[0];
        }
        {uint32_t n=0;vkGetPhysicalDeviceQueueFamilyProperties(physical_,&n,nullptr);
         std::vector<VkQueueFamilyProperties> families(n);vkGetPhysicalDeviceQueueFamilyProperties(physical_,&n,families.data());
         bool found=false;for(uint32_t i=0;i<n;++i)if(families[i].queueFlags&VK_QUEUE_GRAPHICS_BIT){family_=i;found=true;break;}
         if(!found)return fail("no graphics queue");}
        std::vector<std::string> deviceExt;
        {uint32_t n=compositor->GetVulkanDeviceExtensionsRequired(physical_,nullptr,0);
         if(n){std::string buf(n,'\0');compositor->GetVulkanDeviceExtensionsRequired(physical_,buf.data(),n);deviceExt=splitSpaces(buf.c_str());}}
        std::vector<const char*> de;for(auto& e:deviceExt)de.push_back(e.c_str());
        float priority=1;VkDeviceQueueCreateInfo qci{};qci.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex=family_;qci.queueCount=1;qci.pQueuePriorities=&priority;
        VkDeviceCreateInfo dci{};dci.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;dci.queueCreateInfoCount=1;dci.pQueueCreateInfos=&qci;
        dci.enabledExtensionCount=uint32_t(de.size());dci.ppEnabledExtensionNames=de.data();
        if(vkCreateDevice(physical_,&dci,nullptr,&device_)!=VK_SUCCESS)return fail("vkCreateDevice");
        vkGetDeviceQueue(device_,family_,0,&queue_);
        for(auto i=0;i<2;++i){
            VkImageCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;info.imageType=VK_IMAGE_TYPE_2D;info.format=VK_FORMAT_B8G8R8A8_UNORM;
            info.extent={width,height,1};info.mipLevels=1;info.arrayLayers=1;info.samples=VK_SAMPLE_COUNT_1_BIT;info.tiling=VK_IMAGE_TILING_OPTIMAL;
            info.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
            info.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
            if(vkCreateImage(device_,&info,nullptr,&images_[i])!=VK_SUCCESS)return fail("vkCreateImage");
            VkMemoryRequirements req;vkGetImageMemoryRequirements(device_,images_[i],&req);
            int type=memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);if(type<0)type=memoryType(req.memoryTypeBits,0);
            VkMemoryAllocateInfo alloc{};alloc.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;alloc.allocationSize=req.size;alloc.memoryTypeIndex=uint32_t(type);
            if(type<0||vkAllocateMemory(device_,&alloc,nullptr,&imageMemory_[i])!=VK_SUCCESS||vkBindImageMemory(device_,images_[i],imageMemory_[i],0)!=VK_SUCCESS)
                return fail("image memory");
        }
        VkBufferCreateInfo bci{};bci.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;bci.size=VkDeviceSize(width)*height*4;bci.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        if(vkCreateBuffer(device_,&bci,nullptr,&staging_)!=VK_SUCCESS)return fail("vkCreateBuffer");
        {VkMemoryRequirements req;vkGetBufferMemoryRequirements(device_,staging_,&req);
         int type=memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
         VkMemoryAllocateInfo alloc{};alloc.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;alloc.allocationSize=req.size;alloc.memoryTypeIndex=uint32_t(type);
         if(type<0||vkAllocateMemory(device_,&alloc,nullptr,&stagingMemory_)!=VK_SUCCESS||vkBindBufferMemory(device_,staging_,stagingMemory_,0)!=VK_SUCCESS||
            vkMapMemory(device_,stagingMemory_,0,bci.size,0,&mapped_)!=VK_SUCCESS)return fail("staging memory");}
        VkCommandPoolCreateInfo pci{};pci.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;pci.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;pci.queueFamilyIndex=family_;
        if(vkCreateCommandPool(device_,&pci,nullptr,&pool_)!=VK_SUCCESS)return fail("vkCreateCommandPool");
        VkCommandBufferAllocateInfo cai{};cai.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;cai.commandPool=pool_;cai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;cai.commandBufferCount=1;
        if(vkAllocateCommandBuffers(device_,&cai,&command_)!=VK_SUCCESS)return fail("vkAllocateCommandBuffers");
        VkFenceCreateInfo fci{};fci.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if(vkCreateFence(device_,&fci,nullptr,&fence_)!=VK_SUCCESS)return fail("vkCreateFence");
        ready_=true;return true;
    }
    // bgra: width*height*4 bytes, row stride in bytes.
    vr::EVROverlayError upload(vr::IVROverlay* overlay,vr::VROverlayHandle_t handle,const unsigned char* bgra,int stride){
        if(!ready_)return vr::VROverlayError_RequestFailed;
        auto* dst=static_cast<unsigned char*>(mapped_);
        for(uint32_t y=0;y<height_;++y)std::memcpy(dst+size_t(y)*width_*4,bgra+size_t(y)*size_t(stride),size_t(width_)*4);
        const int i=next_;next_^=1;
        vkResetCommandBuffer(command_,0);
        VkCommandBufferBeginInfo begin{};begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if(vkBeginCommandBuffer(command_,&begin)!=VK_SUCCESS)return vr::VROverlayError_RequestFailed;
        VkImageMemoryBarrier barrier{};barrier.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.image=images_[i];
        barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        barrier.oldLayout=initialized_[i]?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask=0;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(command_,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={width_,height_,1};
        vkCmdCopyBufferToImage(command_,staging_,images_[i],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; // layout OpenVR expects
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command_,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        if(vkEndCommandBuffer(command_)!=VK_SUCCESS)return vr::VROverlayError_RequestFailed;
        VkSubmitInfo submit{};submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&command_;
        vkResetFences(device_,1,&fence_);
        if(vkQueueSubmit(queue_,1,&submit,fence_)!=VK_SUCCESS||vkWaitForFences(device_,1,&fence_,VK_TRUE,1000000000ull)!=VK_SUCCESS)
            return vr::VROverlayError_RequestFailed;
        initialized_[i]=true;
        vr::VRVulkanTextureData_t data{};
        data.m_nImage=reinterpret_cast<uint64_t>(images_[i]);data.m_pDevice=device_;data.m_pPhysicalDevice=physical_;data.m_pInstance=instance_;
        data.m_pQueue=queue_;data.m_nQueueFamilyIndex=family_;data.m_nWidth=width_;data.m_nHeight=height_;
        data.m_nFormat=VK_FORMAT_B8G8R8A8_UNORM;data.m_nSampleCount=1;
        vr::Texture_t texture{&data,vr::TextureType_Vulkan,vr::ColorSpace_Auto};
        return overlay->SetOverlayTexture(handle,&texture);
    }
    void destroy(){
        ready_=false;
        if(device_){
            vkDeviceWaitIdle(device_);
            if(fence_)vkDestroyFence(device_,fence_,nullptr);
            if(pool_)vkDestroyCommandPool(device_,pool_,nullptr);
            if(staging_)vkDestroyBuffer(device_,staging_,nullptr);
            if(stagingMemory_)vkFreeMemory(device_,stagingMemory_,nullptr);
            for(int i=0;i<2;++i){if(images_[i])vkDestroyImage(device_,images_[i],nullptr);if(imageMemory_[i])vkFreeMemory(device_,imageMemory_[i],nullptr);images_[i]=VK_NULL_HANDLE;imageMemory_[i]=VK_NULL_HANDLE;}
            vkDestroyDevice(device_,nullptr);
        }
        if(instance_)vkDestroyInstance(instance_,nullptr);
        instance_=VK_NULL_HANDLE;device_=VK_NULL_HANDLE;fence_=VK_NULL_HANDLE;pool_=VK_NULL_HANDLE;staging_=VK_NULL_HANDLE;stagingMemory_=VK_NULL_HANDLE;mapped_=nullptr;command_=VK_NULL_HANDLE;
    }
};
}
