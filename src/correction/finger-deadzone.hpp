#pragma once
#include <algorithm>
#include <cmath>

namespace frame_correction {
// Two-mark range remap. `low` is the "new 0" (the deadzone), `high` is the
// "new 1". Inputs between the marks are stretched to 0..1, so high<1 makes a
// weak bend read as fully closed (e.g. high=0.4: a 40% bend is a full curl).
// high may be set up to 2.0; then even a full bend stays below 1. The raw
// math can exceed 1, but the written value is always clamped to 0..1 because
// SteamVR finger scalars are normalized one-sided values and a value above 1
// is not verified to be accepted by SteamVR or applications.
inline constexpr double rangeMinGap=0.05,rangeHighMin=0.1,rangeHighMax=2.0;
inline bool validHigh(double high){return std::isfinite(high)&&high>=rangeHighMin&&high<=rangeHighMax;}
// Unclamped two-mark math (can exceed 1 above high). Requires high>low.
inline double remapRangeRaw(double input,double low,double high){return (input-low)/(high-low);}
// Value that may be written to SteamVR. The low mark is pulled below high so
// the denominator is never smaller than rangeMinGap.
// The low mark actually used for a given high (shared by the UI display).
inline double effectiveLow(double low,double high){return std::clamp(low,0.0,high-rangeMinGap);}
inline double remapRange(double input,double low,double high){
    if(!std::isfinite(input)||!std::isfinite(low)||!validHigh(high))return input;
    low=effectiveLow(low,high);
    return std::clamp(remapRangeRaw(input,low,high),0.0,1.0);
}
enum class DeadzoneMode { Manual, Dynamic };
struct DeadzoneSettings {
    bool enabled=false;
    DeadzoneMode mode=DeadzoneMode::Manual;
    double manual=0.2;
    double margin=0.02;
    double maximum=0.95;
    double updatePerSecond=0.02;
    double high=1.0; // "new 1" mark; 1.0 keeps the original deadzone behavior
};
// One instance per side and finger. State is RAM-only and never writes input.
class FingerDeadzone {
    double baseline=0;
    bool acquired=false;
public:
    void reset(){baseline=0;acquired=false;}
    bool ready()const{return acquired;}
    double threshold(const DeadzoneSettings& s)const{
        return s.mode==DeadzoneMode::Manual?s.manual:std::min(s.maximum,baseline+s.margin);
    }
    static bool valid(const DeadzoneSettings& s){
        return std::isfinite(s.manual)&&std::isfinite(s.margin)&&std::isfinite(s.maximum)&&
            std::isfinite(s.updatePerSecond)&&s.manual>=0&&s.manual<=s.maximum&&
            s.margin>=0&&s.margin<=0.2&&s.maximum>=0&&s.maximum<=0.95&&
            s.updatePerSecond>0&&s.updatePerSecond<=0.2&&validHigh(s.high);
    }
    // extendedConfirmed must come from deliberate user input, never inferred curl.
    // frozen prevents updates. Reconnection/serial change must call reset().
    bool observe(double input,double dt,bool extendedConfirmed,bool frozen,const DeadzoneSettings& s){
        if(!s.enabled||s.mode!=DeadzoneMode::Dynamic||!valid(s)||!extendedConfirmed||frozen||
           !std::isfinite(input)||input<0||input>s.maximum||
           !std::isfinite(dt)||dt<=0||dt>0.25)return false;
        if(!acquired){baseline=input;acquired=true;return true;}
        baseline+=std::clamp(input-baseline,-s.updatePerSecond*dt,s.updatePerSecond*dt);
        return true;
    }
    bool setLearnedBaseline(double value,const DeadzoneSettings& s){
        if(!s.enabled||s.mode!=DeadzoneMode::Dynamic||!valid(s)||
           !std::isfinite(value)||value<0||value>s.maximum)return false;
        baseline=value;acquired=true;return true;
    }
    double map(double input,const DeadzoneSettings& s)const{
        if(!s.enabled||!valid(s)||!std::isfinite(input)||input<0||input>1||
           (s.mode==DeadzoneMode::Dynamic&&!acquired))return input;
        return remapRange(input,threshold(s),s.high);
    }
};
}
