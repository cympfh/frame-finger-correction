#pragma once
// Simplified settings model (v0.4): every finger is always corrected while the
// settings app runs, with two marks per finger:
//   0 mark (low)  0.0 .. 0.9, default 0
//   1 mark (high) 0.1 .. 2.0, default 1   (at least 0.05 above the 0 mark)
// The driver accepts 1 marks up to 3.0; the app's slider offers 0..2 only.
// A saved value above 2 is kept untouched until the user moves that mark.
// Defaults 0 / 1 are an exact passthrough. The marks are written in the v1
// profile + range files that every range-capable driver already reads.
#include "finger-deadzone.hpp"
#include "frame-settings-profile.hpp"
#include <algorithm>
#include <cmath>
namespace frame_range {
inline constexpr double lowMax=.9,uiHighMax=2.0;
inline double clampLow(double low,double high){
    if(!std::isfinite(low))low=0;
    return std::clamp(low,0.0,std::max(0.0,std::min(lowMax,high-frame_correction::rangeMinGap)));
}
inline double clampHigh(double high,double low,double maxHigh){
    if(!std::isfinite(high))high=1;
    maxHigh=std::clamp(maxHigh,frame_correction::rangeHighMin,frame_correction::rangeHighMax);
    return std::clamp(high,std::max(frame_correction::rangeHighMin,low+frame_correction::rangeMinGap),maxHigh);
}
// Saved preferences from v0.3 (selection / learning) become plain marks:
// only a selected MANUAL finger keeps its deadzone as the 0 mark.
inline void migrate(frame_settings::Profile& profile){
    profile.requested=true;
    for(auto& finger:profile.fingers){
        double low=finger.selected&&!finger.dynamic?finger.threshold:0.0;
        double high=frame_correction::validHigh(finger.high)?finger.high:1.0;
        finger.selected=true;finger.dynamic=false;
        finger.threshold=clampLow(low,high);
        finger.high=clampHigh(high,finger.threshold,frame_correction::rangeHighMax); // saved values are kept (up to 3.0)
    }
}
// What the running driver gets. Values above what it accepts are clamped so a
// legacy (2.0) driver never sees a range file it would reject.
inline frame_settings::Profile forDriver(const frame_settings::Profile& stored,double driverMaxHigh){
    frame_settings::Profile out=stored;out.requested=true;
    for(auto& finger:out.fingers){
        finger.selected=true;finger.dynamic=false;
        finger.high=clampHigh(finger.high,0,driverMaxHigh);
        finger.threshold=clampLow(finger.threshold,finger.high);
    }
    return out;
}
inline void setLow(frame_settings::Finger& finger,double value){finger.threshold=clampLow(value,finger.high);}
inline void setHigh(frame_settings::Finger& finger,double value,double maxHigh){finger.high=clampHigh(value,finger.threshold,maxHigh);}
// Settings app UI: marks move on a 0.1 grid. Only a mark the user moves is
// changed; stored off-grid values (e.g. 0.38) stay as they are until then.
inline constexpr double uiStep=.1;
inline double snap(double v){return std::round(v*10)/10;}
inline double snapDown(double v){return std::floor(v*10+1e-6)/10;}
inline double snapUp(double v){return std::ceil(v*10-1e-6)/10;}
inline bool onGrid(double v){return std::abs(v*10-std::round(v*10))<1e-6;}
// One button step: to the next grid value in that direction (0.38 -> 0.4 / 0.3).
inline double stepFrom(double v,int direction){return direction>0?snapDown(v)+uiStep:snapUp(v)-uiStep;}
inline void setLowUi(frame_settings::Finger& finger,double value){
    setLow(finger,snap(value));
    if(!onGrid(finger.threshold))finger.threshold=snapDown(finger.threshold);
}
inline void setHighUi(frame_settings::Finger& finger,double value,double maxHigh){
    setHigh(finger,snap(value),maxHigh);
    if(!onGrid(finger.high))finger.high=std::min(snapUp(finger.high),snapDown(maxHigh));
}
}
