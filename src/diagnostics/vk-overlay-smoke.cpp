// Manual check of the Vulkan overlay upload on a device: creates a separate,
// hidden overlay, uploads a few frames, reports the result. No settings files.
#include "vk-overlay-texture.hpp"
#include <iostream>
#include <vector>
int main(){
    vr::EVRInitError init;vr::VR_Init(&init,vr::VRApplication_Background);
    if(init!=vr::VRInitError_None){std::cerr<<"VR_Init: "<<vr::VR_GetVRInitErrorAsEnglishDescription(init)<<'\n';return 1;}
    int code=0;
    vk_overlay::Texture gpu; // must outlive VR_Shutdown: vrclient keeps objects on this device
    {
        auto overlay=vr::VROverlay();vr::VROverlayHandle_t handle=0;
        if(!overlay||overlay->CreateOverlay("frame.finger.vk.smoke","vk smoke",&handle)!=vr::VROverlayError_None){std::cerr<<"CreateOverlay failed\n";code=1;}
        else if(!gpu.init(1200,950)){std::cerr<<"vulkan init failed: "<<gpu.error()<<'\n';code=2;}
        else{
            std::vector<unsigned char> image(1200*950*4);
            for(int frame=0;frame<5&&!code;++frame){
                for(size_t i=0;i<image.size();i+=4){image[i]=uint8_t(frame*40);image[i+1]=80;image[i+2]=160;image[i+3]=255;}
                auto result=gpu.upload(overlay,handle,image.data(),1200*4);
                std::cout<<"frame "<<frame<<" SetOverlayTexture="<<int(result)<<std::endl;
                if(result!=vr::VROverlayError_None)code=3;
            }
        }
        if(handle){overlay->ClearOverlayTexture(handle);overlay->DestroyOverlay(handle);}
    }
    vr::VR_Shutdown();
    std::cout<<(code?"FAIL":"OK")<<'\n';
    return code;
}
