#pragma once
#include <string>
#include <string_view>
namespace frame_compat_ui {
enum class Command { None, Observe, Activate };
// UI intent only: the native controller independently revalidates everything.
// A saved ON preference is the same intent as clicking ON. OFF never attaches.
// If the driver restores the slot during a session and offers attaching again
// (structurally_compatible after observing/verified/active/stopped/waiting),
// the flow re-arms, at most maxRearms times per app session. "waiting" covers
// controllers that slept or disconnected and came back.
class Flow {
    bool observationSent_=false, activationSent_=false, failed_=false;
    std::string last_;unsigned rearms_=0;
public:
    static constexpr unsigned maxRearms=100;
    Command next(std::string_view verdict, bool requested) {
        const std::string previous=last_;last_=std::string(verdict);
        if(failed_||!requested)return Command::None;
        if(verdict=="structurally_compatible"&&observationSent_&&rearms_<maxRearms&&
           (previous=="observing"||previous=="verified"||previous=="active"||previous=="stopped"||previous=="waiting")){
            ++rearms_;observationSent_=activationSent_=false;
        }
        if(verdict=="structurally_compatible"&&!observationSent_){
            observationSent_=true;return Command::Observe;
        }
        if(verdict=="verified"&&observationSent_&&!activationSent_){
            activationSent_=true;return Command::Activate;
        }
        return Command::None;
    }
    void failed(){failed_=true;}
};
}
