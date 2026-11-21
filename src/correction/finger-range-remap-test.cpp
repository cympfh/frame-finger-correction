#include "finger-deadzone.hpp"
#include "frame-settings-profile.hpp"
#include "frame-settings-store.hpp"
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <unistd.h>
using namespace frame_correction;
static bool near(double a,double b){return std::abs(a-b)<1e-9;}
int main(){
 // Identity marks (0,1) leave every value unchanged.
 for(double v:{0.,.1,.5,.99,1.})assert(near(remapRange(v,0,1),v));
 // Weak pinky: new 1 = 0.4 -> a 20% bend reads as 50%, 40% or more as fully closed.
 assert(near(remapRange(.2,0,.4),.5));assert(near(remapRange(.4,0,.4),1));assert(near(remapRange(.8,0,.4),1));
 // Both marks: (in-new0)/(new1-new0), clamped at 0 below new 0.
 assert(near(remapRange(.25,.1,.4),.5));assert(remapRange(.05,.1,.4)==0);assert(remapRange(0,.1,.4)==0);
 // Marks beyond 1: a full bend stays below 1 (curls less).
 assert(near(remapRange(1,.1,2.),.9/1.9));assert(near(remapRange(1,0,2.),.5));
 // The raw math exceeds 1 above new 1, but the written value is clamped to 1.
 assert(remapRangeRaw(.8,0,.4)>1.99);assert(remapRange(.8,0,.4)==1);
 // new 0 too close to new 1 is pulled down to keep a 0.05 gap (no division blow-up).
 assert(near(remapRange(.3,.39,.4),(.3-.35)/.05<0?0:(.3-.35)/.05));assert(near(remapRange(.375,.39,.4),.5));
 // Invalid new 1 or non-finite input: passthrough, never NaN from the mark math.
 auto nan=std::numeric_limits<double>::quiet_NaN();
 assert(near(remapRange(.3,0,.05),.3));assert(near(remapRange(.3,0,3.5),.3));
 assert(near(remapRange(1,0,3.),1./3));assert(near(remapRange(.75,0,2.5),.3));
 // Default marks are an exact passthrough.
 for(double v:{0.,.123456789,.5,.999,1.})assert(remapRange(v,0,1)==v);
 assert(near(remapRange(.3,0,nan),.3));
 assert(std::isnan(remapRange(nan,0,.4)));

 // FingerDeadzone: default high=1 keeps the original deadzone formula.
 FingerDeadzone zone;DeadzoneSettings s;s.enabled=true;s.manual=.2;
 assert(near(zone.map(.6,s),(.6-.2)/(1-.2)));
 s.high=.5;assert(near(zone.map(.35,s),.5));assert(zone.map(.6,s)==1);assert(zone.map(.1,s)==0);
 s.high=.05;assert(!FingerDeadzone::valid(s));assert(near(zone.map(.6,s),.6));
 // Dynamic: learned baseline+margin is new 0, new 1 still applies.
 s.high=.5;s.mode=DeadzoneMode::Dynamic;assert(zone.setLearnedBaseline(.1,s));
 assert(near(zone.threshold(s),.12));assert(near(zone.map(.31,s),(.31-.12)/(.5-.12)));
 // Learned baseline above new 1 cannot invert the mapping.
 assert(zone.setLearnedBaseline(.6,s));assert(near(zone.map(.45,s),0));assert(zone.map(.5,s)==1);

 // Range file round trip; applying it changes only "new 1".
 frame_settings::Profile profile;profile.requested=true;profile.fingers[3]={true,false,.1};
 profile.fingers[3].high=.4;profile.fingers[7].high=2.;
 auto text=frame_settings::serializeRange(profile);assert(!text.empty());
 frame_settings::Profile restored;restored.fingers[3].threshold=.33;
 assert(frame_settings::parseRange(text,restored));
 assert(near(restored.fingers[3].high,.4)&&near(restored.fingers[7].high,2.)&&near(restored.fingers[0].high,1.));
 assert(near(restored.fingers[3].threshold,.33)&&!restored.requested);
 // v1 profile text is unchanged by "new 1" (older releases still read it).
 frame_settings::Profile plain=profile;plain.fingers[3].high=1;plain.fingers[7].high=1;
 assert(frame_settings::serialize(profile)==frame_settings::serialize(plain));
 // Invalid range text never replaces state.
 for(std::string bad:{std::string("FRAME_FINGER_RANGE_1\n0 0 1\n"),"FRAME_FINGER_RANGE_2\n"+text.substr(21),
     text+"extra\n",std::string("FRAME_FINGER_RANGE_1\n")+"0 0 3.5\n0 1 1\n0 2 1\n0 3 1\n1 0 1\n1 1 1\n1 2 1\n1 3 1\n",
     std::string("FRAME_FINGER_RANGE_1\n")+"0 0 nan\n0 1 1\n0 2 1\n0 3 1\n1 0 1\n1 1 1\n1 2 1\n1 3 1\n"}){
  frame_settings::Profile keep;keep.fingers[0].high=.7;
  assert(!frame_settings::parseRange(bad,keep));assert(near(keep.fingers[0].high,.7));
 }
 profile.fingers[2].high=.01;assert(frame_settings::serializeRange(profile).empty());assert(frame_settings::serialize(profile).empty());
 profile.fingers[2].high=1;

 // Store: missing range file is fine (marks stay 1.0); round trip through disk.
 char dir[]="/tmp/ffc-range-XXXXXX";assert(mkdtemp(dir));
 std::string path=std::string(dir)+"/range-preferences.txt";
 frame_settings::Profile loaded;assert(frame_settings::loadRangeFile(path,loaded)&&near(loaded.fingers[3].high,1));
 assert(frame_settings::saveRangeFile(path,profile));
 assert(frame_settings::loadRangeFile(path,loaded)&&near(loaded.fingers[3].high,.4));
 unlink(path.c_str());rmdir(dir);
 std::cout<<"PASS: two-mark remap math, clamp to 0..1, marks beyond 1, gap guard, deadzone compatibility, dynamic, range file parse/serialize/store\n";
}
