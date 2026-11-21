#include <openvr.h>
#include "../correction/frame-settings-profile.hpp"
#include "../correction/frame-settings-store.hpp"
#include "../correction/vr-trial-status.hpp"
#ifdef FRAME_UI_ALL_FINGERS_BETA
#include "../correction/frame-beta-status.hpp"
#ifdef FRAME_COMPAT_OBSERVATION_UI
#include "../correction/frame-observation-status.hpp"
#include "../correction/frame-compat-ui-flow.hpp"
#endif
#endif
#include <cairo.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
#include <string_view>
#include <unistd.h>
#include <fcntl.h>
#include <ctime>

namespace {
#if defined(FRAME_UI_SUPERVISED_TRIAL) || defined(FRAME_UI_ALL_FINGERS_BETA)
#ifdef FRAME_UI_ALL_FINGERS_BETA
#ifdef FRAME_FINGER_RELEASE
constexpr const char* overlayKey="frame.finger.correction.release";
#else
constexpr const char* overlayKey="frame.finger.settings.beta";
#endif
#else
constexpr const char* overlayKey="frame.finger.settings.supervised";
#endif
constexpr const char* intentName="supervised-intent.txt";
constexpr const char* statusName="supervised-status.txt";
constexpr const char* brokerName="supervised-broker.txt";
constexpr const char* requestTag="FRAME_VR_SUPERVISED_1";
constexpr const char* trialDescription="LEFT PINKY manual, maximum 20s. Other fingers / learning: pending.";
constexpr const char* trialButton="START LEFT PINKY - UP TO 20s";
constexpr const char* startMessage="LEFT PINKY only, maximum 20s; heartbeat and PC OFF required.";
#else
constexpr const char* overlayKey="frame.finger.settings.preview";
constexpr const char* intentName="trial-intent.txt";
constexpr const char* statusName="trial-status.txt";
constexpr const char* brokerName="trial-broker.txt";
constexpr const char* requestTag="FRAME_VR_TRIAL_1";
constexpr const char* trialDescription="Live verification: LEFT PINKY manual (5s trial). Other fingers / learning: pending.";
constexpr const char* trialButton="TEST LEFT PINKY - 5 SECONDS";
constexpr const char* startMessage="LEFT PINKY only, maximum 5s. PC OFF confirmation required.";
#endif
volatile std::sig_atomic_t running=1;
void stop(int){running=0;}
constexpr int width=1200,height=1050;
bool japanese=false;
std::string localized(std::string_view label){
    if(!japanese)return std::string(label);
    static constexpr std::pair<std::string_view,std::string_view> labels[]{
        {"STEAM FRAME - FINGER SETTINGS","Steam Frame 指トラ補正"},
        {"ALL 8 FINGERS / SOFTWARE DEADZONE / No firmware or calibration changes","左右8指のデッドゾーン補正"},
        {"LEFT","左手"},
        {"RIGHT","右手"},
        {"INDEX","人差し指"},
        {"MIDDLE","中指"},
        {"RING","薬指"},
        {"PINKY","小指"},
        {"SELECTED","対象"},
        {"SELECT","対象外"},
        {"DYNAMIC","学習モード"},
        {"MANUAL","手動"},
        {"LEARN","学習"},
        {"LEARNED","学習済み"},
        {"READY","準備完了"},
        {"WAITING","待機中"},
        {"DYNAMIC: extend the selected finger, click LEARN, hold 1.5s. No automatic learning.","学習する指を伸ばし、学習ボタンを押して1.5秒間その姿勢を保ってください。"},
        {"CORRECTION: ON (click to turn OFF)","補正設定：オン"},
        {"CORRECTION: OFF (click to turn ON)","補正設定：オフ"},
        {"SAVE SETTINGS","設定を保存"},
        {"CLOSE","閉じる"},
        {"STEAMVR AUTO-START: ON","SteamVR 自動起動：オン"},
        {"STEAMVR AUTO-START: OFF","SteamVR 自動起動：オフ"},
        {"Restart Frame to load the installed driver","ドライバーを読み込むにはFrameを再起動してください"},
        {"SteamVR update detected: correction unavailable","未対応のSteamVR更新を検出：補正停止中"},
        {"Correction unavailable: see startup log","補正できません：起動ログを確認してください"},
        {"Native status: DISCONNECTED","補正状態：未接続"},
        {"Native status: ON","補正状態：オン"},
        {"Native status: OFF","補正状態：オフ"},
        {"Settings are applied and saved immediately. Closing this app stops correction.","変更はすぐに保存されます。アプリを閉じると補正は止まります。"},
        {"Changes apply immediately. The previous ON/OFF setting is restored at startup.","起動時には前回の補正設定を引き継ぎます。"},
        {"SteamVR auto-start preference saved.","自動起動設定を保存しました。"},
        {"Auto-start setting failed. App registration is required.","自動起動を設定できませんでした。SteamVRへの登録を確認してください。"},
        {"Turn correction ON and select this finger in DYNAMIC mode first.","補正をオンにし、対象の指を学習モードにしてください。"},
        {"Learning 1.5s: hold this finger EXTENDED. Then learning freezes.","学習中：指を伸ばしたまま1.5秒間保ってください。"},
        {"Learning request failed.","学習を開始できませんでした。"},
        {"Settings saved. Trial state is shown separately.","設定を保存しました。"},
        {"Save failed; previous profile retained.","保存に失敗しました。以前の設定を保持します。"},
        {"Applied and saved. Extend a finger before clicking LEARN.","変更を反映して保存しました。"},
        {"Apply/save failed; check application status.","反映または保存に失敗しました。"},
        {"Language preference saved.","表示言語を保存しました。"},
        {"Could not save language preference.","表示言語を保存できませんでした。"},
        {"Language: 日本語 / English","言語：日本語 / English"},
        {"Deadzone: ","デッドゾーン："},
        {"ALL 8 FINGERS / DEADZONE + RANGE (mark 0 / mark 1) / No firmware changes","左右8指：デッドゾーンと範囲補正（0の印・1の印）"},
        {"mark 0: ","0の印："},
        {" (learned)","（学習値）"},
        {"mark 1: ","1の印："},
        {"Drag the marks on each line: left = mark 0, right = mark 1 (lower mark 1 = curls more easily).","線の上の印をドラッグ：左が0の印、右が1の印（1の印を下げるほど曲がりやすい）"},
        {"Range (mark 1) active in driver","範囲補正（1の印）：ドライバーで有効"},
        {"Mark 1 needs the updated driver: restart SteamVR to load it","1の印は更新版ドライバーの読み込み（SteamVR再起動）後に効きます"},
    };
    for(const auto& [en,ja]:labels)if(label==en)return std::string(ja);
    return std::string(label);
}
bool saveLanguage(const std::string& path,bool ja){
    std::string temp=path+".tmp-XXXXXX";
    int fd=mkstemp(temp.data());if(fd<0)return false;
    bool ok=write(fd,ja?"ja":"en",2)==2;
    if(close(fd)!=0)ok=false;
    if(ok&&rename(temp.c_str(),path.c_str())==0)return true;
    unlink(temp.c_str());return false;
}
struct Button {double x,y,w,h;int action;std::string label;};
void text(cairo_t* cr,double x,double y,double size,const std::string& label){
    cairo_set_source_rgb(cr,.94,.96,1);cairo_set_font_size(cr,size);cairo_move_to(cr,x,y);cairo_show_text(cr,localized(label).c_str());
}
std::string requestTrial(const std::string& directory,bool start,double threshold){
    auto ticks=std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream nonce;nonce<<std::hex<<std::setw(16)<<std::setfill('0')<<static_cast<uint64_t>(ticks);
    std::string pattern=directory+"/trial-intent.tmp-XXXXXX";
    std::vector<char> name(pattern.begin(),pattern.end());name.push_back(0);
    int fd=mkstemp(name.data());if(fd<0)return {};
    std::ostringstream body;body<<requestTag<<' '<<time(nullptr)<<' '<<nonce.str()<<' '<<(start?"START":"STOP")<<' '<<std::setprecision(17)<<threshold<<'\n';
    auto content=body.str();
    bool ok=write(fd,content.data(),content.size())==static_cast<ssize_t>(content.size());
    if(ok&&fsync(fd)!=0)ok=false;
    if(close(fd)!=0)ok=false;
    if(ok&&rename(name.data(),(directory+"/"+intentName).c_str())!=0)ok=false;
    if(!ok){unlink(name.data());return {};}
    return nonce.str();
}
#ifdef FRAME_UI_ALL_FINGERS_BETA
std::string requestBetaLearn(const std::string& directory,unsigned row){
    std::ostringstream nonce;nonce<<std::hex<<std::setw(16)<<std::setfill('0')<<static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    std::ostringstream body;body<<"FRAME_BETA_LEARN_1 "<<time(nullptr)<<' '<<nonce.str()<<' '<<row<<'\n';
    auto content=body.str(),pattern=directory+"/beta-learn.tmp-XXXXXX";std::vector<char> tmp(pattern.begin(),pattern.end());tmp.push_back(0);
    int fd=mkstemp(tmp.data());if(fd<0)return {};
    bool ok=write(fd,content.data(),content.size())==static_cast<ssize_t>(content.size());if(close(fd)!=0)ok=false;
    if(ok&&rename(tmp.data(),(directory+"/beta-learn.txt").c_str())==0)return nonce.str();
    unlink(tmp.data());return {};
}
#endif
#ifdef FRAME_COMPAT_OBSERVATION_UI
bool writeObservationRequest(const std::string& directory,const char* file,uint64_t session){
    auto pattern=directory+"/observe.tmp-XXXXXX";
    int fd=mkstemp(pattern.data());if(fd<0)return false;
    const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    auto body="FRAME_OBSERVE_1 "+std::to_string(session)+" "+std::to_string(now)+"\n";
    bool ok=write(fd,body.data(),body.size())==static_cast<ssize_t>(body.size());
    if(close(fd)!=0)ok=false;
    if(ok&&rename(pattern.c_str(),(directory+"/"+file).c_str())==0)return true;
    unlink(pattern.c_str());return false;
}
#endif
bool writeHeartbeat(const std::string& directory,uint64_t session,uint64_t sequence,uint64_t now,uint64_t revision){
    std::ostringstream body;body<<"FRAME_UI_HEARTBEAT_1 "<<session<<' '<<sequence<<' '<<now<<' '<<revision<<'\n';
    auto content=body.str();std::string pattern=directory+"/ui-heartbeat.tmp-XXXXXX";
    std::vector<char> name(pattern.begin(),pattern.end());name.push_back(0);
    int fd=mkstemp(name.data());if(fd<0)return false;
    bool ok=write(fd,content.data(),content.size())==static_cast<ssize_t>(content.size());
    if(close(fd)!=0)ok=false;
    if(ok&&rename(name.data(),(directory+"/ui-heartbeat.txt").c_str())!=0)ok=false;
    if(!ok)unlink(name.data());
    return ok;
}
// Mark 0 is the existing deadzone (profile v1, still readable by older
// releases); mark 1 lives in a separate range file read by newer drivers.
bool saveLive(const std::string& directory,const frame_settings::Profile& profile){
    bool live=frame_settings::saveFile(directory+"/beta-live-profile.txt",profile);
    bool range=frame_settings::saveRangeFile(directory+"/beta-live-range.txt",profile);
    return live&&range;
}
bool savePreferences(const std::string& path,const std::string& directory,const frame_settings::Profile& profile){
    bool saved=frame_settings::saveFile(path,profile);
    bool range=frame_settings::saveRangeFile(directory+"/range-preferences.txt",profile);
    return saved&&range;
}
// Slider: one line per finger, 0.0 .. 2.0. The 0..1 part is drawn brighter.
constexpr double sliderMax=2.0,sliderWidth=420;
struct Slider {double x,y,w,h;unsigned row;};
double sliderValue(const Slider& slider,double x){
    double value=(x-slider.x)/slider.w*sliderMax;
    return std::round(std::clamp(value,0.0,sliderMax)*100)/100;
}
// Mark limits: mark 0 in 0..0.9 (as before), mark 1 in 0.1..2.0, gap >= 0.05.
void setLow(frame_settings::Finger& finger,double value){finger.threshold=std::clamp(value,0.0,std::min(.9,finger.high-.05));}
// low is the effective 0 mark (learned value for a learned DYNAMIC finger).
void setHigh(frame_settings::Finger& finger,double value,double low){
    finger.high=std::clamp(value,std::max(frame_correction::rangeHighMin,std::min(low,.95)+frame_correction::rangeMinGap),frame_correction::rangeHighMax);
}
}
int main(int argc,char** argv){
    bool startOff=false;
    std::string path,snapshot;bool show=false;double duration=0;
    for(int i=1;i<argc;++i){
        std::string arg=argv[i];
        if(arg=="--start-off"){startOff=true;continue;}
        if(arg=="--help"){std::cout<<"frame-finger-settings-overlay --profile FILE [--show-dashboard] [--seconds N] [--snapshot FILE]\nSettings UI. A trial requires a matching native candidate and explicit START; no firmware commands.\n";return 0;}
        if(arg=="--show-dashboard"){show=true;continue;}
        if(++i>=argc)return 2;
        if(arg=="--profile")path=argv[i];
        else if(arg=="--snapshot")snapshot=argv[i];
        else if(arg=="--seconds"){try{duration=std::stod(argv[i]);}catch(...){return 2;}if(!std::isfinite(duration)||duration<=0)return 2;}
        else return 2;
    }
    if(path.empty()||!std::filesystem::path(path).is_absolute())return 2;
    auto directory=std::filesystem::path(path).parent_path().string();
    frame_settings::Profile profile;
    if(std::filesystem::exists(path)){
        if(!frame_settings::loadFile(path,profile)){std::cerr<<"Invalid profile; unchanged\n";return 2;}
        std::cout<<"SettingsUI: saved profile loaded; live correction OFF\n"<<std::flush;
    }
    if(!frame_settings::loadRangeFile(directory+"/range-preferences.txt",profile))
        std::cerr<<"Invalid range file; mark 1 reset to 1.0\n";
    for(auto& finger:profile.fingers)setLow(finger,finger.threshold);
#ifdef FRAME_UI_ALL_FINGERS_BETA
    if(startOff)profile.requested=false;
    if(!saveLive(directory,profile))return 2;
    frame_beta::UiStatus betaStatus;
    int64_t learnPoll=-1;
    std::string pendingLearnId;
    std::chrono::steady_clock::time_point learnDeadline{};
#endif
    vr::EVRInitError init;vr::VR_Init(&init,vr::VRApplication_Overlay);
    if(init!=vr::VRInitError_None){std::cerr<<vr::VR_GetVRInitErrorAsEnglishDescription(init)<<'\n';return 1;}
    struct Cleanup {
        vr::IVROverlay* overlay=nullptr;vr::VROverlayHandle_t main=0,iconHandle=0;
        cairo_surface_t* surface=nullptr;cairo_t* cr=nullptr;
        ~Cleanup(){
            if(overlay){if(main){overlay->ClearOverlayTexture(main);overlay->DestroyOverlay(main);}if(iconHandle){overlay->ClearOverlayTexture(iconHandle);overlay->DestroyOverlay(iconHandle);}}
            if(cr)cairo_destroy(cr);
            if(surface)cairo_surface_destroy(surface);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            vr::VR_Shutdown();
        }
    } cleanup;
    cleanup.overlay=vr::VROverlay();auto overlay=cleanup.overlay;if(!overlay)return 1;
    if(overlay->CreateDashboardOverlay(overlayKey,"Frame Finger Correction",&cleanup.main,&cleanup.iconHandle)!=0)return 1;
    vr::HmdVector2_t scale{{float(width),float(height)}};
    if(overlay->SetOverlayWidthInMeters(cleanup.main,1.4f)!=0||overlay->SetOverlayInputMethod(cleanup.main,vr::VROverlayInputMethod_Mouse)!=0||overlay->SetOverlayMouseScale(cleanup.main,&scale)!=0)return 1;
    cleanup.surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,width,height);cleanup.cr=cairo_create(cleanup.surface);
    if(cairo_surface_status(cleanup.surface)!=CAIRO_STATUS_SUCCESS||cairo_status(cleanup.cr)!=CAIRO_STATUS_SUCCESS)return 1;
#ifdef FRAME_UI_ALL_FINGERS_BETA
    const auto languagePath=directory+"/language.txt";
    {std::ifstream language(languagePath);std::string saved;language>>saved;japanese=saved!="en";}
#endif
    cairo_select_font_face(cleanup.cr,"Noto Sans CJK JP",CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_NORMAL);
    std::vector<uint8_t> rgba(width*height*4);std::vector<Button> buttons;
    std::vector<Slider> sliders;int dragRow=-1;bool dragHigh=false,rangeDriver=false,liveDirty=false;int64_t rangePolled=-1;
    Slider dragSlider{};std::array<double,8> effectiveLows{-1,-1,-1,-1,-1,-1,-1,-1};
    auto lastLive=std::chrono::steady_clock::now();
    bool dirty=true,iconReady=false;std::string message="Preferences do not start correction. Trial requires the guarded broker.";
#ifdef FRAME_UI_ALL_FINGERS_BETA
    message="Changes apply immediately. The previous ON/OFF setting is restored at startup.";
#endif
#ifdef FRAME_UI_ALL_FINGERS_BETA
    bool autoLaunch=vr::VRApplications()->GetApplicationAutoLaunch(overlayKey);
#endif
    std::string startupIssue;
#ifdef FRAME_COMPAT_OBSERVATION_UI
    std::string compatLine, compatVerdict;
    unsigned compatWitnessed=0;
    frame_compat_ui::Flow compatFlow;
    int64_t compatPolled=-1;
#endif
    std::string trialId,trialStatus="No trial requested";
    bool brokerReady=false,trialPending=false;
    std::signal(SIGINT,stop);std::signal(SIGTERM,stop);auto start=std::chrono::steady_clock::now();
    uint64_t heartbeatSession=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(start.time_since_epoch()).count());
    uint64_t heartbeatSequence=0,profileRevision=1;auto nextHeartbeat=start;
    auto learnedLow=[&](unsigned row){return profile.fingers[row].dynamic&&effectiveLows[row]>=0;};
    auto shownLow=[&](unsigned row){return learnedLow(row)?effectiveLows[row]:profile.fingers[row].threshold;};
    // Only a changed 0 mark or selection bumps the revision; the driver resets
    // learned DYNAMIC values on a revision change, so moving the 1 mark must not.
    auto applyDrag=[&](double value){
        auto row=static_cast<unsigned>(dragRow);auto& finger=profile.fingers[row];
        const bool wasSelected=finger.selected;const double low=finger.threshold,high=finger.high;
        if(dragHigh)setHigh(finger,value,shownLow(row));else setLow(finger,value);
        if(finger.threshold==low&&finger.high==high)return;
        finger.selected=true;
        if(!wasSelected||finger.threshold!=low)++profileRevision;
        liveDirty=true;dirty=true;
    };
    auto finishDrag=[&](){
        bool applied=true;
        if(liveDirty){applied=saveLive(directory,profile);liveDirty=false;lastLive=std::chrono::steady_clock::now();}
        bool saved=savePreferences(path,directory,profile);
        message=applied&&saved?"Applied and saved. Extend a finger before clicking LEARN.":"Apply/save failed; check application status.";
        dragRow=-1;dirty=true;
    };
    const char* fingers[]{"INDEX","MIDDLE","RING","PINKY"};
    while(running&&(duration==0||std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<duration)){
        auto heartbeatNow=std::chrono::steady_clock::now();
        if(heartbeatNow>=nextHeartbeat){
            nextHeartbeat=heartbeatNow+std::chrono::milliseconds(100);
            writeHeartbeat(directory,heartbeatSession,++heartbeatSequence,
                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(heartbeatNow.time_since_epoch()).count()),profileRevision);
        }
#ifdef FRAME_UI_ALL_FINGERS_BETA
        {
            std::ifstream issueFile(directory+"/startup-issue.txt");std::string issue;
            std::getline(issueFile,issue);
            if(issue!="RESTART_REQUIRED"&&issue!="START_FAILED"&&issue!="UPDATE_REQUIRED")issue.clear();
            if(issue!=startupIssue){startupIssue=issue;dirty=true;}
#ifdef FRAME_COMPAT_OBSERVATION_UI
            const auto wall=time(nullptr);
            if(wall!=compatPolled){
                compatPolled=wall;
                std::ifstream status(directory+"/compat-status.txt");
                char body[161]{};
                status.read(body,sizeof(body));
                auto diagnostic=frame_observation::parseDiagnosticStatus(std::string(body,static_cast<size_t>(status.gcount())),wall);
                std::string line;
                if(diagnostic.verdict=="structurally_compatible")
                    line="Structural candidate; correction unavailable";
                else if(diagnostic.verdict=="restore_failed")line="restart_required";
                else if(diagnostic.verdict=="rejected")
                    line=diagnostic.reason;
                if(line!=compatLine||diagnostic.verdict!=compatVerdict||diagnostic.witnessed!=compatWitnessed){
                    compatLine=std::move(line);compatVerdict=diagnostic.verdict;
                    compatWitnessed=diagnostic.witnessed;dirty=true;
                }
                const auto command=compatFlow.next(compatVerdict,profile.requested);
                if(command!=frame_compat_ui::Command::None){
                    const char* file=command==frame_compat_ui::Command::Observe?"observe-request.txt":"observe-activate.txt";
                    if(!saveLive(directory,profile)||
                       !writeObservationRequest(directory,file,heartbeatSession)){
                        compatFlow.failed();profile.requested=false;++profileRevision;
                        saveLive(directory,profile);
                        savePreferences(path,directory,profile);
                        message=japanese?"\u88dc\u6b63\u8981\u6c42\u3092\u4fdd\u5b58\u3067\u304d\u307e\u305b\u3093\u3067\u3057\u305f\u3002":"Could not save correction request.";
                    }
                    dirty=true;
                }
            }
#endif
            if(time(nullptr)!=rangePolled){
                rangePolled=time(nullptr);
                std::ifstream rangeFile(directory+"/range-status.txt");char body[257]{};rangeFile.read(body,sizeof(body));
                auto range=frame_beta::parseRangeStatus(std::string(body,static_cast<size_t>(rangeFile.gcount())),rangePolled);
                if(range.valid!=rangeDriver||range.low!=effectiveLows){rangeDriver=range.valid;effectiveLows=range.low;dirty=true;}
            }
            std::ifstream input(directory+"/beta-status.txt");std::string content((std::istreambuf_iterator<char>(input)),{});
            auto latest=frame_beta::parseUiStatus(content,time(nullptr));
            const auto learnWall=time(nullptr);
            if(!pendingLearnId.empty()&&learnPoll!=learnWall){
                learnPoll=learnWall;
                std::ifstream result(directory+"/learn-result.txt");
                char buffer[129]{};result.read(buffer,sizeof(buffer));
                std::string body(buffer,static_cast<size_t>(result.gcount()));
                auto outcome=frame_beta::parseLearnResult(body,pendingLearnId,learnWall);
                if(outcome!=frame_beta::LearnOutcome::Invalid){
                    pendingLearnId.clear();
                    message=outcome==frame_beta::LearnOutcome::Applied?(japanese?"学習結果を反映しました。":"Learning applied."):
                        (japanese?"入力サンプル不足：前の補正値を維持しました。":"Not enough input samples; previous baseline kept.");
                    dirty=true;
                }else if(std::chrono::steady_clock::now()>=learnDeadline){
                    pendingLearnId.clear();
                    message=japanese?"学習結果を確認できませんでした。":"Learning result could not be confirmed.";
                    dirty=true;
                }
            }
            if(latest.valid!=betaStatus.valid||latest.on!=betaStatus.on||latest.present!=betaStatus.present||latest.witnessed!=betaStatus.witnessed||latest.learned!=betaStatus.learned){betaStatus=latest;dirty=true;}
        }
#endif
        vr::VREvent_t event{};
        {
            std::ifstream broker(directory+"/"+brokerName);
            std::string content((std::istreambuf_iterator<char>(broker)),{});
            bool ready=content.size()<=128&&frame_settings::brokerReady(content,time(nullptr));
            if(ready!=brokerReady){brokerReady=ready;dirty=true;}
        }
        if(!trialId.empty()){
            std::ifstream status(directory+"/"+statusName);
            std::string content((std::istreambuf_iterator<char>(status)),{});
            std::string latest=content.size()<=256?frame_settings::trialStatus(content,trialId,time(nullptr)):"Status unconfirmed";
            if(latest=="STOPPED"||latest=="ERROR")trialPending=false;
            if(latest!=trialStatus){trialStatus=latest;dirty=true;}
        }
        while(overlay->PollNextOverlayEvent(cleanup.main,&event,sizeof(event))){
            if(event.eventType==vr::VREvent_Quit){running=0;continue;}
            {
                // Slider drag: press near a line grabs the nearest mark, move drags it,
                // left release (or focus loss) applies and saves. While dragging the
                // live file is written at most every 100 ms.
                if(event.eventType==vr::VREvent_FocusLeave||event.eventType==vr::VREvent_DashboardDeactivated){
                    if(dragRow>=0)finishDrag();
                    continue;
                }
                const bool mouse=event.eventType==vr::VREvent_MouseButtonDown||event.eventType==vr::VREvent_MouseMove||event.eventType==vr::VREvent_MouseButtonUp;
                const double sx=mouse?event.data.mouse.x:0,sy=mouse?height-event.data.mouse.y:0;
                const bool leftDown=event.eventType==vr::VREvent_MouseButtonDown&&event.data.mouse.button==vr::VRMouseButton_Left;
                const bool leftUp=event.eventType==vr::VREvent_MouseButtonUp&&event.data.mouse.button==vr::VRMouseButton_Left;
                if(leftDown&&dragRow<0){
                    for(const auto& slider:sliders)if(sx>=slider.x-15&&sx<slider.x+slider.w+15&&sy>=slider.y&&sy<slider.y+slider.h){
                        const auto& finger=profile.fingers[slider.row];double value=sliderValue(slider,sx);
                        dragRow=static_cast<int>(slider.row);dragSlider=slider;
                        // A learned DYNAMIC finger has no manual 0 mark to drag.
                        dragHigh=learnedLow(slider.row)||std::abs(value-finger.high)<std::abs(value-finger.threshold)||value>finger.high;
                        applyDrag(value);break;
                    }
                    if(dragRow>=0)continue;
                }
                if(dragRow>=0&&(event.eventType==vr::VREvent_MouseMove||leftUp)){
                    applyDrag(sliderValue(dragSlider,sx));
                    if(leftUp)finishDrag();
                    continue;
                }
                if(dragRow>=0&&event.eventType==vr::VREvent_MouseButtonUp)continue; // other buttons while dragging
            }
            if(event.eventType!=vr::VREvent_MouseButtonUp||event.data.mouse.button!=vr::VRMouseButton_Left)continue;
            double x=event.data.mouse.x,y=height-event.data.mouse.y;
            for(const auto& button:buttons)if(x>=button.x&&x<button.x+button.w&&y>=button.y&&y<button.y+button.h){
                int action=button.action;
                std::cout<<"SettingsUI: click action="<<action<<'\n'<<std::flush;
#ifdef FRAME_UI_ALL_FINGERS_BETA
#ifdef FRAME_COMPAT_OBSERVATION_UI

#endif
                if(action==106){
                    if(saveLanguage(languagePath,!japanese)){japanese=!japanese;message="Language preference saved.";}
                    else message="Could not save language preference.";
                    dirty=true;break;
                }
                if(action==105){
                    auto apps=vr::VRApplications();bool desired=!apps->GetApplicationAutoLaunch(overlayKey);
                    auto result=apps->SetApplicationAutoLaunch(overlayKey,desired);
                    autoLaunch=apps->GetApplicationAutoLaunch(overlayKey);
                    message=result==vr::VRApplicationError_None&&autoLaunch==desired?"SteamVR auto-start preference saved.":"Auto-start setting failed. App registration is required.";
                    dirty=true;break;
                }
                if(action>=300&&action<316){
                    const auto row=static_cast<unsigned>(action-300)/2;auto& finger=profile.fingers[row];
                    if(!finger.selected){finger.selected=true;++profileRevision;} // 1 mark alone keeps learned values
                    setHigh(finger,finger.high+((action-300)%2?.05:-.05),shownLow(row));
                    bool applied=saveLive(directory,profile),saved=savePreferences(path,directory,profile);
                    message=applied&&saved?"Applied and saved. Extend a finger before clicking LEARN.":"Apply/save failed; check application status.";
                    dirty=true;break;
                }
                if(action>=200&&action<208){
                    const auto row=static_cast<unsigned>(action-200);const auto& finger=profile.fingers[row];
                    if(!profile.requested||!finger.selected||!finger.dynamic)message="Turn correction ON and select this finger in DYNAMIC mode first.";
                    else{
                        pendingLearnId=requestBetaLearn(directory,row);
                        learnDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(6);
                        message=pendingLearnId.empty()?"Learning request failed.":"Learning 1.5s: hold this finger EXTENDED. Then learning freezes.";
                    }
                    dirty=true;break;
                }
#endif
                if(action==103||action==104){
                    if(action==103&&(!brokerReady||trialPending)){
                        message=trialPending?"Trial pending. Use STOP; do not retry.":"Trial broker is offline. START is disabled.";dirty=true;break;
                    }
                    auto& entry=profile.fingers[3];
                    if(action==103&&(!entry.selected||entry.dynamic)){message="Select LEFT PINKY in MANUAL mode first.";dirty=true;break;}
                    auto id=requestTrial(directory,action==103,entry.threshold);
                    if(action==103&&!id.empty()){trialId=id;trialStatus="Requested; waiting for guarded broker";trialPending=true;}
                    message=id.empty()?"Request failed.":action==103?startMessage:"Stop requested; awaiting restoration acknowledgement.";
                    dirty=true;break;
                }
                if(action==100)profile.requested=!profile.requested;
                else if(action==101){bool saved=savePreferences(path,directory,profile);message=saved?"Settings saved. Trial state is shown separately.":"Save failed; previous profile retained.";std::cout<<"SettingsUI: save="<<saved<<'\n'<<std::flush;dirty=true;break;}
                else if(action==102){running=0;break;}
                else {auto& entry=profile.fingers[action/4];switch(action%4){case 0:entry.selected=!entry.selected;break;case 1:entry.dynamic=!entry.dynamic;break;case 2:setLow(entry,entry.threshold-.05);break;case 3:setLow(entry,entry.threshold+.05);break;}}
                ++profileRevision;
#ifdef FRAME_UI_ALL_FINGERS_BETA
                bool applied=saveLive(directory,profile);
                bool saved=savePreferences(path,directory,profile);
                message=applied&&saved?"Applied and saved. Extend a finger before clicking LEARN.":"Apply/save failed; check application status.";dirty=true;break;
#endif
                message="Unsaved settings. Trial state is shown separately.";dirty=true;break;
            }
        }
        if(liveDirty&&std::chrono::steady_clock::now()-lastLive>=std::chrono::milliseconds(100)){
            if(!saveLive(directory,profile))message="Apply/save failed; check application status.";
            liveDirty=false;lastLive=std::chrono::steady_clock::now();dirty=true;
        }
        if(dirty){
            auto cr=cleanup.cr;cairo_set_source_rgb(cr,.045,.065,.09);cairo_paint(cr);buttons.clear();sliders.clear();
            text(cr,25,45,30,"STEAM FRAME - FINGER SETTINGS");
            #ifdef FRAME_UI_ALL_FINGERS_BETA
            text(cr,25,80,20,"ALL 8 FINGERS / DEADZONE + RANGE (mark 0 / mark 1) / No firmware changes");
#else
            text(cr,25,80,20,"Bounded trial only / Other fingers: preferences only / No firmware changes");
#endif
            auto button=[&](double x,double y,double w,int action,const std::string& label){
                buttons.push_back({x,y,w,48,action,label});
                bool disabled=action==103&&(!brokerReady||trialPending);
                cairo_set_source_rgb(cr,disabled?.12:.11,disabled?.13:.22,disabled?.14:.30);cairo_rectangle(cr,x,y,w,48);cairo_fill(cr);text(cr,x+10,y+31,18,label);
            };
#ifdef FRAME_UI_ALL_FINGERS_BETA
            text(cr,25,103,15,"Drag the marks on each line: left = mark 0, right = mark 1 (lower mark 1 = curls more easily).");
            text(cr,700,45,15,rangeDriver?"Range (mark 1) active in driver":"Mark 1 needs the updated driver: restart SteamVR to load it");
#endif
            for(unsigned side=0;side<2;++side){
                double x=25+side*590;text(cr,x,130,25,side?"RIGHT":"LEFT");
                for(unsigned finger=0;finger<4;++finger){
                    unsigned row=side*4+finger;auto& entry=profile.fingers[row];double y=150+finger*155;
                    std::ostringstream value,high;value<<std::fixed<<std::setprecision(2)<<shownLow(row)<<(learnedLow(row)?localized(" (learned)"):"");high<<std::fixed<<std::setprecision(2)<<entry.high;
                    text(cr,x,y+20,21,localized(fingers[finger])+"  "+localized("mark 0: ")+value.str()+"  "+localized("mark 1: ")+high.str());
                    {
                        // Line 0..2 with the 0..1 part highlighted, ticks at 0/1/2, two marks.
                        const double lx=x+10,ly=y+112,lw=sliderWidth;
                        sliders.push_back({lx,y+88,lw,48,row});
                        cairo_set_line_width(cr,4);cairo_set_source_rgb(cr,.30,.34,.40);
                        cairo_move_to(cr,lx+lw/2,ly);cairo_line_to(cr,lx+lw,ly);cairo_stroke(cr);
                        cairo_set_source_rgb(cr,.75,.80,.88);cairo_move_to(cr,lx,ly);cairo_line_to(cr,lx+lw/2,ly);cairo_stroke(cr);
                        cairo_set_line_width(cr,2);
                        for(int tick=0;tick<=2;++tick){double tx=lx+lw*tick/2;cairo_move_to(cr,tx,ly-9);cairo_line_to(cr,tx,ly+9);cairo_stroke(cr);
                            text(cr,tx-5,ly+26,13,std::to_string(tick));}
                        const double a=lx+lw*frame_correction::effectiveLow(shownLow(row),entry.high)/sliderMax,b=lx+lw*entry.high/sliderMax;
                        cairo_set_source_rgba(cr,.25,.65,1,.35);cairo_rectangle(cr,a,ly-5,b-a,10);cairo_fill(cr);
                        auto mark=[&](double mx,double r,double g,double bl,const char* label){
                            cairo_set_source_rgb(cr,r,g,bl);cairo_move_to(cr,mx,ly-4);cairo_line_to(cr,mx-9,ly-20);cairo_line_to(cr,mx+9,ly-20);cairo_close_path(cr);cairo_fill(cr);
                            cairo_rectangle(cr,mx-2,ly-6,4,12);cairo_fill(cr);
                            cairo_set_source_rgb(cr,.94,.96,1);cairo_set_font_size(cr,12);cairo_move_to(cr,mx-4,ly-23);cairo_show_text(cr,label);
                        };
                        mark(a,.35,.85,.45,"0");mark(b,1,.62,.25,"1");
                        button(x+450,y+88,56,300+static_cast<int>(row)*2,"1-");button(x+514,y+88,56,301+static_cast<int>(row)*2,"1+");
                    }
                    button(x,y+30,130,row*4,entry.selected?"SELECTED":"SELECT");
                    button(x+140,y+30,170,row*4+1,entry.dynamic?"DYNAMIC":"MANUAL");
                    button(x+320,y+30,70,row*4+2,"0-");button(x+400,y+30,70,row*4+3,"0+");
#ifdef FRAME_UI_ALL_FINGERS_BETA
                    button(x+480,y+30,90,200+static_cast<int>(row),"LEARN");
#ifdef FRAME_COMPAT_OBSERVATION_UI
                    if(compatVerdict=="observing")text(cr,x+480,y+20,14,
                        (compatWitnessed&(1u<<row))?(japanese?"確認済み":"Verified"):(japanese?"未確認":"Pending"));
                    else
#endif
                    text(cr,x+480,y+20,14,(betaStatus.learned&(1u<<row))?"LEARNED":(betaStatus.witnessed&(1u<<row))?"READY":"WAITING");
#endif
                }
            }
#ifdef FRAME_UI_ALL_FINGERS_BETA
#ifdef FRAME_COMPAT_OBSERVATION_UI
            if(compatVerdict=="observing")
                text(cr,25,800,18,japanese?"\u5165\u529b\u7d4c\u8def\u3092\u81ea\u52d5\u78ba\u8a8d\u4e2d (\u88dc\u6b63OFF)":"Checking input route automatically (correction OFF)");
            else
#endif
            text(cr,25,800,18,"DYNAMIC: extend the selected finger, click LEARN, hold 1.5s. No automatic learning.");
            button(25,825,540,100,profile.requested?"CORRECTION: ON (click to turn OFF)":"CORRECTION: OFF (click to turn ON)");
#else
            text(cr,25,800,18,trialDescription);
            button(25,825,540,100,profile.requested?"STARTUP PREFERENCE: ON (not active)":"STARTUP PREFERENCE: OFF");
#endif
            button(600,825,255,101,"SAVE SETTINGS");button(875,825,290,102,"CLOSE");
#ifdef FRAME_UI_ALL_FINGERS_BETA
            button(600,890,565,106,"Language: 日本語 / English");
            button(25,890,540,105,autoLaunch?"STEAMVR AUTO-START: ON":"STEAMVR AUTO-START: OFF");
#ifdef FRAME_COMPAT_OBSERVATION_UI
            if(compatVerdict=="waiting")text(cr,25,965,20,japanese?"Controller\u306e\u63a5\u7d9a\u30fb\u5165\u529b\u6e96\u5099\u3092\u5f85\u3063\u3066\u3044\u307e\u3059 (\u88dc\u6b63OFF)":"Waiting for controller inputs (correction OFF)");
            else if(compatVerdict=="observing")text(cr,25,965,20,japanese?"通常入力を待っています。準備動作は不要です（補正OFF）":"Waiting for normal input; no preparation needed (correction OFF)");
            else if(compatVerdict=="verified")text(cr,25,965,20,japanese?"入力経路を確認：補正設定に従って動作します":"Input route verified: following correction setting");
            else if(compatVerdict=="active")text(cr,25,965,20,profile.requested?(japanese?"構造検証済み：補正オン":"Structurally verified: correction ON"):(japanese?"構造検証済み：補正オフ":"Structurally verified: correction OFF"));
            else if(compatVerdict=="stopped")text(cr,25,965,20,japanese?"停止・復元済み":"Stopped: slot restored");
            else if(compatLine=="restart_required")text(cr,25,965,20,japanese?"\u5fa9\u5143\u672a\u78ba\u8a8d\uff1aSteamVR\u3092\u518d\u8d77\u52d5\u3057\u3066\u304f\u3060\u3055\u3044":"Restore unconfirmed: restart SteamVR");
            else if(compatLine=="Structural candidate; correction unavailable")text(cr,25,965,20,japanese?"構造検証に合格：補正ONで自動検証します":"Structure passed: turn correction ON to verify");
            else if(!compatLine.empty())text(cr,25,965,20,(japanese?"未対応：":"Unsupported: ")+compatLine);
            else
#endif
            text(cr,25,965,23,startupIssue=="RESTART_REQUIRED"?"Restart Frame to load the installed driver":startupIssue=="UPDATE_REQUIRED"?"SteamVR update detected: correction unavailable":startupIssue=="START_FAILED"?"Correction unavailable: see startup log":!betaStatus.valid?"Native status: DISCONNECTED":betaStatus.on?"Native status: ON":"Native status: OFF");
            text(cr,25,990,18,"Settings are applied and saved immediately. Closing this app stops correction.");text(cr,25,1035,17,message);
#else
            button(25,900,540,103,trialButton);button(600,900,565,104,"STOP TRIAL");
            text(cr,25,990,20,"Trial: "+trialStatus+" / Broker: "+(brokerReady?"READY":"OFFLINE/BUSY"));text(cr,25,1035,17,message);
#endif
            cairo_surface_flush(cleanup.surface);auto source=cairo_image_surface_get_data(cleanup.surface);int stride=cairo_image_surface_get_stride(cleanup.surface);
            for(int y=0;y<height;++y)for(int x=0;x<width;++x){auto a=source+y*stride+x*4;auto b=rgba.data()+(y*width+x)*4;b[0]=a[2];b[1]=a[1];b[2]=a[0];b[3]=a[3];}
            auto result=overlay->SetOverlayRaw(cleanup.main,rgba.data(),width,height,4);
            if(result!=vr::VROverlayError_None){std::cerr<<"SetOverlayRaw failed: "<<int(result)<<'\n';return 1;}
            if(!iconReady)iconReady=overlay->SetOverlayRaw(cleanup.iconHandle,rgba.data(),width,height,4)==vr::VROverlayError_None;
            dirty=false;
        }
        if(show){overlay->ShowDashboard(overlayKey);show=false;}
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if(!trialId.empty())requestTrial(directory,false,profile.fingers[3].threshold);
    writeHeartbeat(directory,0,0,0,0);
    if(!snapshot.empty()&&cairo_surface_write_to_png(cleanup.surface,snapshot.c_str())!=CAIRO_STATUS_SUCCESS)return 1;
}

