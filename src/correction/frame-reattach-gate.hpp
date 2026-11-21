#pragma once
// Re-attach after the driver restored the original slot (app closed, or a
// controller slept/reconnected: identity_changed). The pre-attach checks run
// again on every attempt. Controllers that are missing or still settling
// (census_missing / census_unstable / finger_missing) are a temporary state,
// exactly like at SteamVR startup: wait and re-check about once a second,
// then offer attaching again ("structurally_compatible"), which the settings
// app answers with a fresh observe request. Every other reason stays final.
#include "runtime-compat-check.hpp"
#include "frame-observation.hpp"
#include <cstdint>
#include <string>
namespace frame_reattach {
inline bool transient(frame_compat_check::Reason reason){
    using frame_compat_check::Reason;
    return reason==Reason::CensusMissing||reason==Reason::CensusUnstable||reason==Reason::FingerMissing;
}
// After a confirmed restore: may a later session attach again? Yes when the
// session ended (app closed), timed out, or a controller changed identity
// (sleep/reconnect). Value/receiver/clock anomalies need a SteamVR restart.
inline bool mayReattach(frame_observation::Reason reason){
    using frame_observation::Reason;
    return reason==Reason::None||reason==Reason::SessionLost||reason==Reason::Timeout||reason==Reason::IdentityChanged;
}
class Gate {
    bool waiting_=false;uint64_t nextRetry_=0;
public:
    static constexpr uint64_t retryMs=1000;
    bool waiting()const{return waiting_;}
    bool retryDue(uint64_t now)const{return waiting_&&now>=nextRetry_;}
    // Result of the pre-attach checks; returns the new driver status line.
    std::string checked(frame_compat_check::Reason reason,uint64_t now){
        if(reason==frame_compat_check::Reason::None){waiting_=false;return "structurally_compatible none";}
        if(transient(reason)){waiting_=true;nextRetry_=now+retryMs;return "waiting none";}
        waiting_=false;return std::string("rejected ")+frame_compat_check::reasonName(reason);
    }
};
}
