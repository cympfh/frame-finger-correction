#include "frame-range-settings.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
static bool near(double a,double b){return std::abs(a-b)<1e-9;}
int main(){
 using frame_settings::Profile;
 // Migration from v0.3 preferences: selection/learning are gone, all fingers on.
 Profile old;old.requested=false;
 old.fingers[0]={false,false,.3};          // not selected -> 0 mark 0
 old.fingers[1]={true,true,.25};           // learning -> 0 mark 0
 old.fingers[2]={true,false,.15};          // manual -> kept
 old.fingers[3]={true,false,.2};old.fingers[3].high=.5;
 frame_range::migrate(old);
 assert(old.requested);
 for(auto& f:old.fingers)assert(f.selected&&!f.dynamic);
 assert(old.fingers[0].threshold==0&&old.fingers[1].threshold==0&&near(old.fingers[2].threshold,.15));
 assert(near(old.fingers[3].threshold,.2)&&near(old.fingers[3].high,.5));
 // Slider offers up to 2.0, but a saved 3.0 is never rewritten by loading.
 Profile big;big.fingers[7]={true,false,0};big.fingers[7].high=3.0;
 frame_range::migrate(big);assert(near(big.fingers[7].high,3.0));
 assert(near(frame_range::forDriver(big,3.0).fingers[7].high,3.0));
 assert(frame_range::uiHighMax==2.0);
 assert(old.fingers[4].threshold==0); // default (unselected) finger
 // Defaults: a fresh profile becomes 0 / 1 for every finger (exact passthrough).
 Profile fresh;frame_range::migrate(fresh);
 for(auto& f:fresh.fingers)assert(f.threshold==0&&f.high==1);
 frame_correction::FingerDeadzone zone;
 frame_correction::DeadzoneSettings s;s.enabled=true;s.manual=fresh.fingers[0].threshold;s.high=fresh.fingers[0].high;
 for(double v:{0.,.0001,.37,.5,.9999,1.})assert(zone.map(v,s)==v);
 // Marks: limits and gap.
 frame_settings::Finger f;f.threshold=0;f.high=1;
 frame_range::setHigh(f,2.7,3.0);assert(near(f.high,2.7));
 frame_range::setHigh(f,9,3.0);assert(near(f.high,3.0));
 frame_range::setLow(f,.95);assert(near(f.threshold,.9));
 frame_range::setHigh(f,.5,3.0);assert(near(f.high,.95)); // stays 0.05 above the 0 mark
 frame_range::setLow(f,-1);assert(f.threshold==0);
 frame_range::setHigh(f,0,3.0);assert(near(f.high,.1));
 frame_range::setLow(f,.5);assert(near(f.threshold,.05));
 // What a legacy (2.0) driver gets: clamped, always valid for the v1 files.
 Profile p;frame_range::migrate(p);p.fingers[3].high=2.6;p.fingers[7].threshold=.9;p.fingers[7].high=.95;
 auto legacy=frame_range::forDriver(p,2.0);
 assert(near(legacy.fingers[3].high,2.0)&&legacy.requested);
 assert(!frame_settings::serializeRange(legacy).empty()&&!frame_settings::serialize(legacy).empty());
 auto full=frame_range::forDriver(p,3.0);assert(near(full.fingers[3].high,2.6));
 assert(near(full.fingers[7].threshold,.9)&&near(full.fingers[7].high,.95));
 // The stored preference keeps 2.6 even when the driver only takes 2.0.
 assert(near(p.fingers[3].high,2.6));
 // 0.1 grid in the app.
 assert(near(frame_range::stepFrom(.38,1),.4)&&near(frame_range::stepFrom(.38,-1),.3));
 assert(near(frame_range::stepFrom(.4,1),.5)&&near(frame_range::stepFrom(.4,-1),.3));
 assert(near(frame_range::stepFrom(1.0,1),1.1)&&near(frame_range::stepFrom(.3,-1),.2));
 frame_settings::Finger g;g.threshold=.38;g.high=.62;           // stored off-grid values
 frame_settings::Finger untouched=g;
 frame_range::setHighUi(g,.47,2.0);assert(near(g.high,.5)&&near(g.threshold,.38)); // only the moved mark changes
 frame_range::setHighUi(g,.1,2.0);assert(near(g.high,.5));      // >= 0 mark + gap, rounded up to grid
 frame_range::setLowUi(g,.6);assert(near(g.threshold,.4)); // below 1 mark - gap, rounded down
 frame_range::setHighUi(g,2.3,2.0);assert(near(g.high,2.0));
 frame_range::setLowUi(g,-.2);assert(g.threshold==0);
 assert(near(untouched.threshold,.38)&&near(untouched.high,.62));
 Profile stored;stored.fingers[3].selected=true;stored.fingers[3].threshold=.38;stored.fingers[3].high=.62;
 frame_range::migrate(stored);assert(near(stored.fingers[3].threshold,.38)&&near(stored.fingers[3].high,.62)); // load keeps them
 std::cout<<"PASS: v0.4 settings model: migration, 0/1 passthrough, mark limits, legacy-driver clamp, 0.1 UI grid\n";
}
