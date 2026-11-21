// Simulates the driver's re-attach control flow together with the settings
// app's request flow: controller sleeps -> identity_changed -> restore ->
// finger_missing while it is away -> waiting -> back -> attach -> active.
#include "frame-reattach-gate.hpp"
#include "frame-compat-ui-flow.hpp"
#include <cassert>
#include <iostream>
using frame_compat_check::Reason;
using frame_compat_ui::Command;
namespace {
struct Driver {
    frame_observation::Trial trial;frame_reattach::Gate gate;
    std::string status="structurally_compatible none";
    frame_observation::Handles handles{1,2,3,4,5,6,7,8};
    bool controllersPresent=true,attached=false;unsigned budget=100;uint64_t session=7; // attached = hook installed (offProbe.active())
    Reason census()const{return controllersPresent?Reason::None:Reason::FingerMissing;}
    std::string verdict()const{return status.substr(0,status.find(' '));}
    // One driver tick (mirrors experimentalTick). request: fresh observe/activate from the app.
    void tick(uint64_t now,bool observe,bool activate){
        if(!attached){
            if(gate.waiting()){if(gate.retryDue(now))status=gate.checked(census(),now);return;}
            if(status!="structurally_compatible none"||!observe)return;
            if(auto r=census();r!=Reason::None){status=gate.checked(r,now);return;}
            trial=frame_observation::Trial{};
            assert(trial.start(true,true,session,handles,now));
            attached=true;status="observing 0";return;
        }
        auto live=controllersPresent?handles:frame_observation::Handles{};
        trial.check(session,live,now);
        if(controllersPresent&&trial.state()==frame_observation::State::Observing)
            for(auto h:handles)trial.sample(h,10,.5f,0,now);
        if(activate&&trial.state()==frame_observation::State::Verified)trial.activate(true,session,live,now);
        if(trial.state()==frame_observation::State::RestorePending){
            auto reason=trial.reason();attached=false;
            trial.restored(true,false);
            if(trial.state()!=frame_observation::State::RestartRequired&&budget>0&&frame_reattach::mayReattach(reason)){
                --budget;trial=frame_observation::Trial{};status="structurally_compatible none";
            }else status=std::string("rejected ")+frame_observation::reasonCode(reason);
            return;
        }
        status=trial.state()==frame_observation::State::Active?"active 255":trial.state()==frame_observation::State::Verified?"verified 255":"observing 0";
    }
};
// Runs app+driver for n ticks of 500 ms; returns the last driver verdict.
std::string run(Driver& d,frame_compat_ui::Flow& app,uint64_t& now,int n){
    for(int i=0;i<n;++i){
        now+=500;auto cmd=app.next(d.verdict(),true);
        d.tick(now,cmd==Command::Observe,cmd==Command::Activate);
    }
    return d.verdict();
}
}
int main(){
    // Gate unit behaviour.
    frame_reattach::Gate g;
    assert(g.checked(Reason::FingerMissing,1000)=="waiting none"&&g.waiting());
    assert(!g.retryDue(1500)&&g.retryDue(2000));
    assert(g.checked(Reason::CensusUnstable,2000)=="waiting none");
    assert(g.checked(Reason::None,3000)=="structurally_compatible none"&&!g.waiting());
    assert(g.checked(Reason::StubNotBranch,4000)=="rejected stub_not_branch"&&!g.waiting());
    assert(frame_reattach::transient(Reason::CensusMissing)&&!frame_reattach::transient(Reason::FingerDuplicate));
    assert(frame_reattach::mayReattach(frame_observation::Reason::IdentityChanged));
    assert(!frame_reattach::mayReattach(frame_observation::Reason::BadValue));
    assert(!frame_reattach::mayReattach(frame_observation::Reason::ReceiverChanged));

    // End to end: start, sleep, wake (twice), all without a SteamVR restart.
    Driver d;frame_compat_ui::Flow app;uint64_t now=10000;
    assert(run(d,app,now,6)=="active");
    for(int cycle=0;cycle<2;++cycle){
        d.controllersPresent=false;                      // controller enters standby
        assert(run(d,app,now,10)=="waiting");           // identity_changed -> restore -> finger_missing
        assert(!d.trial.allows(1,10,.5f));              // nothing corrected while away
        d.controllersPresent=true;                       // controller reconnects
        assert(run(d,app,now,10)=="active");            // re-checked, re-attached, active again
    }
    // A real anomaly still ends in "rejected" (needs a SteamVR restart).
    d.trial.sample(1,99,.5f,0,now+500);                 // receiver changed
    d.tick(now+500,false,false);
    assert(d.verdict()=="rejected");
    std::cout<<"PASS: controller sleep -> waiting -> reconnect -> active (no restart); anomalies still final\n";
}
