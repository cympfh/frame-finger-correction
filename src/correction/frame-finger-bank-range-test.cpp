#include "frame-finger-bank.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace frame_beta;using namespace frame_scalar;
static float received=0;
static int32_t original(void*,uint64_t,float value,double){received=value;return 0;}
int main(){
 // Left pinky (row 3) with new 0 = 0.1 and new 1 = 0.4, through the real forward path.
 std::array<std::optional<Binding>,8> input;
 for(unsigned row=0;row<8;++row){unsigned device=row<4?1:2;auto hand=row<4?Hand::Left:Hand::Right;std::string serial=row<4?"left":"right";
  input[row]=Binding{ExistingScalar{(uint64_t(device)<<32)|(row%4+10),0x100000000ull|device,fingerPaths[row%4],serial,hand,true,true,0,false},DeviceSnapshot{device,serial,"frame_controller",hand,true,0},0x4000+row*0x100};}
 FingerBank bank;bank.configure(input,1000);
 frame_settings::Profile profile;profile.requested=true;
 profile.fingers[3]={true,false,.1};profile.fingers[3].high=.4;
 profile.fingers[2]={true,false,.0};profile.fingers[2].high=2.;
 auto receiver=reinterpret_cast<void*>(uintptr_t(0x1234));
 assert(bank.renew(profile,1,true,1000,1000,1000));
 auto pinky=input[3]->scalar.component,ring=input[2]->scalar.component,index=input[0]->scalar.component;
 bank.forward(original,receiver,pinky,.25f,0,1001);assert(received==.25f); // First real call verifies the route, unchanged.
 bank.forward(original,receiver,pinky,.25f,0,1002);assert(std::abs(received-.5f)<1e-6);
 bank.forward(original,receiver,pinky,.4f,0,1003);assert(received==1.f);
 bank.forward(original,receiver,pinky,.9f,0,1004);assert(received==1.f); // Never above 1.
 bank.forward(original,receiver,pinky,.05f,0,1005);assert(received==0.f);
 bank.forward(original,receiver,ring,1.f,0,1006);bank.forward(original,receiver,ring,1.f,0,1007);assert(std::abs(received-.5f)<1e-6);
 bank.forward(original,receiver,index,.3f,0,1008);bank.forward(original,receiver,index,.3f,0,1009);assert(received==.3f); // Not selected.
 profile.requested=false;assert(bank.renew(profile,2,true,1010,1010,1010));
 bank.forward(original,receiver,pinky,.25f,0,1011);assert(received==.25f); // OFF forwards raw.
 std::cout<<"PASS: per-finger new 0 / new 1 through FingerBank::forward, clamp, unselected and OFF bypass\n";
}
