// Steam Frame finger correction - settings app (dashboard overlay).
// v0.4: every finger is corrected while this app runs. Two marks per finger:
// 0 mark (what counts as fully open) and 1 mark (what counts as fully bent).
// Changes apply at once and are saved automatically. Closing the app stops
// correction (the driver restores the original input path).
#ifndef FRAME_UI_ALL_FINGERS_BETA
#error "Only the all-fingers settings app is built from this file"
#endif
#include <openvr.h>
#include "vk-overlay-texture.hpp"
#include "../correction/frame-settings-profile.hpp"
#include "../correction/frame-settings-store.hpp"
#include "../correction/frame-range-settings.hpp"
#include "../correction/frame-beta-status.hpp"
#include "../correction/frame-ui-heartbeat.hpp"
#ifdef FRAME_COMPAT_OBSERVATION_UI
#include "../correction/frame-observation-status.hpp"
#include "../correction/frame-compat-ui-flow.hpp"
#endif
#include <cairo.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string_view>
#include <thread>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <ctime>

namespace {
#ifdef FRAME_FINGER_RELEASE
constexpr const char* overlayKey="frame.finger.correction.release";
#else
constexpr const char* overlayKey="frame.finger.settings.beta";
#endif
volatile std::sig_atomic_t running=1;
void stop(int){running=0;}
constexpr int width=1200,height=950;
bool japanese=true;
std::string tr(std::string_view en,std::string_view ja){return std::string(japanese?ja:en);}
uint64_t steadyMs(){
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
bool writeSmall(const std::string& path,const std::string& content){
    std::string temp=path+".tmp-XXXXXX";
    int fd=mkstemp(temp.data());if(fd<0)return false;
    bool ok=write(fd,content.data(),content.size())==static_cast<ssize_t>(content.size());
    if(close(fd)!=0)ok=false;
    if(ok&&rename(temp.c_str(),path.c_str())==0)return true;
    unlink(temp.c_str());return false;
}
bool writeHeartbeat(const std::string& directory,uint64_t session,uint64_t sequence,uint64_t now,uint64_t revision){
    std::ostringstream body;body<<"FRAME_UI_HEARTBEAT_1 "<<session<<' '<<sequence<<' '<<now<<' '<<revision<<'\n';
    return writeSmall(directory+"/ui-heartbeat.txt",body.str());
}
#ifdef FRAME_COMPAT_OBSERVATION_UI
bool writeObservationRequest(const std::string& directory,const char* file,uint64_t session){
    return writeSmall(directory+"/"+file,"FRAME_OBSERVE_1 "+std::to_string(session)+" "+std::to_string(steadyMs())+"\n");
}
#endif
std::string readSmall(const std::string& path,size_t limit){
    std::ifstream input(path);std::string body(limit+1,'\0');
    input.read(body.data(),static_cast<std::streamsize>(body.size()));
    body.resize(static_cast<size_t>(input.gcount()));
    return body;
}
struct Button {double x,y,w,h;int action;};
struct Slider {double x,y,w,h;unsigned row;};
constexpr double sliderMax=frame_range::uiHighMax,sliderWidth=540;
double sliderValue(const Slider& slider,double x){
    double value=(x-slider.x)/slider.w*sliderMax;
    return frame_range::snap(std::clamp(value,0.0,sliderMax)); // 0.1 steps
}
void text(cairo_t* cr,double x,double y,double size,const std::string& label,double r=.94,double g=.96,double b=1){
    cairo_set_source_rgb(cr,r,g,b);cairo_set_font_size(cr,size);cairo_move_to(cr,x,y);cairo_show_text(cr,label.c_str());
}
std::string fixed2(double value){std::ostringstream out;out<<std::fixed<<std::setprecision(2)<<value;return out.str();}
std::string fixed1(double value){std::ostringstream out;out<<std::fixed<<std::setprecision(1)<<value;return out.str();}
}

int main(int argc,char** argv){
    std::string path,snapshot;bool show=false,adopt=false;double duration=0;
    for(int i=1;i<argc;++i){
        std::string arg=argv[i];
        if(arg=="--start-off")continue; // accepted for older launchers; correction follows the app
        if(arg=="--help"){std::cout<<"frame-finger-settings-beta --profile FILE [--show-dashboard] [--seconds N] [--snapshot FILE] [--adopt-heartbeat]\n";return 0;}
        if(arg=="--show-dashboard"){show=true;continue;}
        // Take over the heartbeat session of a settings app that was just stopped
        // without ending its session (in-place app update, no SteamVR restart).
        if(arg=="--adopt-heartbeat"){adopt=true;continue;}
        if(++i>=argc)return 2;
        if(arg=="--profile")path=argv[i];
        else if(arg=="--snapshot")snapshot=argv[i];
        else if(arg=="--seconds"){try{duration=std::stod(argv[i]);}catch(...){return 2;}if(!std::isfinite(duration)||duration<=0)return 2;}
        else return 2;
    }
    if(path.empty()||!std::filesystem::path(path).is_absolute())return 2;
    const auto directory=std::filesystem::path(path).parent_path().string();

    // Heartbeat: written by a small thread every 100 ms while the UI loop is
    // alive (or still starting). A hung UI stops the heartbeat -> driver restores.
    std::atomic<uint64_t> heartbeatSession{steadyMs()},heartbeatSequence{0},profileRevision{1},uiAlive{0};
    std::atomic<bool> starting{true},heartbeatRun{true};
    if(adopt){
        frame_supervision::UiHeartbeat previous{};
        if(frame_supervision::parseHeartbeat(readSmall(directory+"/ui-heartbeat.txt",160),steadyMs(),previous,1500)){
            heartbeatSession=previous.session;heartbeatSequence=previous.sequence;profileRevision=previous.revision+1; // re-read the migrated profile
            std::cout<<"SettingsUI: adopted heartbeat session "<<previous.session<<'\n'<<std::flush;
        }else std::cout<<"SettingsUI: no fresh heartbeat to adopt; new session\n"<<std::flush;
    }
    std::mutex heartbeatMutex;
    auto beat=[&](){
        std::lock_guard<std::mutex> guard(heartbeatMutex);
        const auto now=steadyMs();
        if(!starting.load()&&now-uiAlive.load()>1000)return; // UI loop stalled
        writeHeartbeat(directory,heartbeatSession.load(),++heartbeatSequence,now,profileRevision.load());
    };
    std::thread heartbeatThread;
    auto startHeartbeat=[&](){
        if(heartbeatThread.joinable())return;
        beat();
        heartbeatThread=std::thread([&](){
            while(heartbeatRun.load()){beat();std::this_thread::sleep_for(std::chrono::milliseconds(100));}
        });
    };
    // Adopting: keep the running session alive from the first moment. Otherwise
    // wait until this instance owns the dashboard overlay, so a second copy that
    // fails to start never disturbs the running session.
    if(adopt)startHeartbeat();
    struct JoinHeartbeat {std::atomic<bool>& run;std::thread& thread;std::mutex& mutex;const std::string& directory;
        ~JoinHeartbeat(){run=false;if(!thread.joinable())return;thread.join();std::lock_guard<std::mutex> guard(mutex);writeHeartbeat(directory,0,0,0,0);}
    } joinHeartbeat{heartbeatRun,heartbeatThread,heartbeatMutex,directory};

    frame_settings::Profile profile;
    if(std::filesystem::exists(path)&&!frame_settings::loadFile(path,profile))std::cerr<<"Invalid saved profile; defaults used\n";
    if(!frame_settings::loadRangeFile(directory+"/range-preferences.txt",profile))std::cerr<<"Invalid range file; 1 marks reset\n";
    frame_range::migrate(profile);
    // What the loaded driver accepts. A v2 status (any age) means a 3.0 driver, so
    // stored values are passed on unchanged from the first write.
    double driverMax=frame_correction::rangeHighMaxLegacy;
    {
        auto body=readSmall(directory+"/range-status.txt",256);std::istringstream in(body);std::string tag;int64_t stamp=-1;
        if(in>>tag>>stamp&&stamp>=0){auto st=frame_beta::parseRangeStatus(body,stamp);if(st.valid)driverMax=st.maxHigh;}
    }
    auto saveLive=[&](){
        auto live=frame_range::forDriver(profile,driverMax);
        bool a=frame_settings::saveFile(directory+"/beta-live-profile.txt",live);
        bool b=frame_settings::saveRangeFile(directory+"/beta-live-range.txt",live);
        return a&&b;
    };
    auto savePreferences=[&](){
        bool a=frame_settings::saveFile(path,profile);
        bool b=frame_settings::saveRangeFile(directory+"/range-preferences.txt",profile);
        return a&&b;
    };
    if(!saveLive())return 2; // preferences are written only when the user changes a mark
    {std::ifstream language(directory+"/language.txt");std::string saved;language>>saved;japanese=saved!="en";}

    vk_overlay::Texture gpu; // destroyed after the overlay (declared before Cleanup)
    vr::EVRInitError init;vr::VR_Init(&init,vr::VRApplication_Overlay);
    for(int attempt=0;adopt&&init!=vr::VRInitError_None&&attempt<10;++attempt){
        std::this_thread::sleep_for(std::chrono::milliseconds(300));vr::VR_Init(&init,vr::VRApplication_Overlay);
    }
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
    // After an in-place update the previous instance may still own the key for
    // a moment, until SteamVR notices it has exited.
    for(int attempt=0;;++attempt){
        auto created=overlay->CreateDashboardOverlay(overlayKey,"Frame Finger Correction",&cleanup.main,&cleanup.iconHandle);
        if(created==vr::VROverlayError_None)break;
        if(!adopt||attempt>=40){std::cerr<<"CreateDashboardOverlay failed: "<<int(created)<<'\n';return 1;}
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    startHeartbeat();
    vr::HmdVector2_t scale{{float(width),float(height)}};
    if(overlay->SetOverlayWidthInMeters(cleanup.main,1.4f)!=0||overlay->SetOverlayInputMethod(cleanup.main,vr::VROverlayInputMethod_Mouse)!=0||overlay->SetOverlayMouseScale(cleanup.main,&scale)!=0)return 1;
    cleanup.surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,width,height);cleanup.cr=cairo_create(cleanup.surface);
    if(cairo_surface_status(cleanup.surface)!=CAIRO_STATUS_SUCCESS||cairo_status(cleanup.cr)!=CAIRO_STATUS_SUCCESS)return 1;
    cairo_select_font_face(cleanup.cr,"Noto Sans CJK JP",CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_NORMAL);
    const bool useGpu=std::getenv("FRAME_FINGER_RAW_OVERLAY")==nullptr&&gpu.init(width,height);
    std::cout<<"SettingsUI: overlay upload="<<(useGpu?"vulkan texture":"raw ("+gpu.error()+")")<<'\n'<<std::flush;
    std::vector<uint8_t> rgba;

    bool autoLaunch=vr::VRApplications()->GetApplicationAutoLaunch(overlayKey);
    std::string message,startupIssue;
    frame_beta::UiStatus betaStatus;
#ifdef FRAME_COMPAT_OBSERVATION_UI
    std::string compatVerdict,compatReason;
    frame_compat_ui::Flow compatFlow;
#endif
    int64_t polled=-1;bool rangeDriver=false;
    std::vector<Button> buttons;std::vector<Slider> sliders;
    int dragRow=-1;bool dragHigh=false;Slider dragSlider{};
    bool liveDirty=false,prefsDirty=false,dirty=true,iconReady=false;
    auto lastLive=std::chrono::steady_clock::now(),lastPrefs=lastLive,lastRender=lastLive-std::chrono::seconds(1);
    std::signal(SIGINT,stop);std::signal(SIGTERM,stop);
    const auto start=std::chrono::steady_clock::now();
    // A changed 0 mark bumps the revision (the driver re-reads the profile);
    // the 1 mark lives in the range file and is re-read every tick.
    auto changed=[&](bool low){if(low)++profileRevision;liveDirty=prefsDirty=dirty=true;};
    auto apply=[&](unsigned row,bool high,double value){
        auto& finger=profile.fingers[row];const double low0=finger.threshold,high0=finger.high;
        if(high&&value>=finger.high&&finger.high>=frame_range::uiHighMax)return; // saved >2: "1+" never lowers it
        if(high)frame_range::setHighUi(finger,value,frame_range::uiHighMax);else frame_range::setLowUi(finger,value);
        if(finger.threshold!=low0||finger.high!=high0)changed(finger.threshold!=low0);
    };
    const char* fingerEn[]{"Index","Middle","Ring","Pinky"};
    const char* fingerJa[]{"人差し指","中指","薬指","小指"};
    uiAlive=steadyMs();starting=false;

    while(running&&(duration==0||std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<duration)){
        uiAlive=steadyMs();
        const auto wall=time(nullptr);
        if(wall!=polled){
            polled=wall;
            std::string issue=readSmall(directory+"/startup-issue.txt",64);
            issue=issue.substr(0,issue.find('\n'));
            if(issue!="RESTART_REQUIRED"&&issue!="START_FAILED"&&issue!="UPDATE_REQUIRED")issue.clear();
            if(issue!=startupIssue){startupIssue=issue;dirty=true;}
            auto range=frame_beta::parseRangeStatus(readSmall(directory+"/range-status.txt",256),wall);
            // Only an explicit status changes the limit; a missing/stale one keeps it,
            // so stored values are never rewritten just because a status is late.
            if(range.valid!=rangeDriver){rangeDriver=range.valid;dirty=true;}
            if(range.valid&&range.maxHigh!=driverMax){driverMax=range.maxHigh;liveDirty=dirty=true;}
            auto latest=frame_beta::parseUiStatus(readSmall(directory+"/beta-status.txt",512),wall);
            if(latest.valid!=betaStatus.valid||latest.on!=betaStatus.on){betaStatus=latest;dirty=true;}
#ifdef FRAME_COMPAT_OBSERVATION_UI
            auto diagnostic=frame_observation::parseDiagnosticStatus(readSmall(directory+"/compat-status.txt",160),wall);
            std::string reason=diagnostic.verdict=="rejected"?diagnostic.reason:"";
            if(diagnostic.verdict!=compatVerdict||reason!=compatReason){compatVerdict=diagnostic.verdict;compatReason=reason;dirty=true;}
            const auto command=compatFlow.next(compatVerdict,true);
            if(command!=frame_compat_ui::Command::None){
                const char* file=command==frame_compat_ui::Command::Observe?"observe-request.txt":"observe-activate.txt";
                if(!saveLive()||!writeObservationRequest(directory,file,heartbeatSession.load())){
                    compatFlow.failed();message=tr("Could not start correction (file write failed).","補正を開始できませんでした（ファイル書き込み失敗）");
                }
                dirty=true;
            }
#endif
        }
        vr::VREvent_t event{};
        while(overlay->PollNextOverlayEvent(cleanup.main,&event,sizeof(event))){
            if(event.eventType==vr::VREvent_Quit){running=0;continue;}
            if(event.eventType==vr::VREvent_FocusLeave||event.eventType==vr::VREvent_DashboardDeactivated){dragRow=-1;continue;}
            const bool mouse=event.eventType==vr::VREvent_MouseButtonDown||event.eventType==vr::VREvent_MouseMove||event.eventType==vr::VREvent_MouseButtonUp;
            if(!mouse)continue;
            const double sx=event.data.mouse.x,sy=height-event.data.mouse.y;
            const bool left=event.data.mouse.button==vr::VRMouseButton_Left;
            if(event.eventType==vr::VREvent_MouseButtonDown&&left&&dragRow<0){
                for(const auto& slider:sliders)if(sx>=slider.x-20&&sx<slider.x+slider.w+20&&sy>=slider.y&&sy<slider.y+slider.h){
                    const auto& finger=profile.fingers[slider.row];double value=sliderValue(slider,sx);
                    dragRow=static_cast<int>(slider.row);dragSlider=slider;
                    dragHigh=std::abs(value-finger.high)<std::abs(value-finger.threshold)||value>finger.high;
                    apply(slider.row,dragHigh,value);break;
                }
                continue;
            }
            if(dragRow>=0){
                if(event.eventType==vr::VREvent_MouseMove||(event.eventType==vr::VREvent_MouseButtonUp&&left))
                    apply(static_cast<unsigned>(dragRow),dragHigh,sliderValue(dragSlider,sx));
                if(event.eventType==vr::VREvent_MouseButtonUp&&left)dragRow=-1;
                continue;
            }
            if(event.eventType!=vr::VREvent_MouseButtonUp||!left)continue;
            for(const auto& button:buttons)if(sx>=button.x&&sx<button.x+button.w&&sy>=button.y&&sy<button.y+button.h){
                const int action=button.action;
                if(action==102){running=0;break;}
                if(action==106){
                    if(writeSmall(directory+"/language.txt",japanese?"en":"ja"))japanese=!japanese;
                    message.clear();dirty=true;break;
                }
                if(action==105){
                    auto apps=vr::VRApplications();bool desired=!apps->GetApplicationAutoLaunch(overlayKey);
                    auto result=apps->SetApplicationAutoLaunch(overlayKey,desired);
                    autoLaunch=apps->GetApplicationAutoLaunch(overlayKey);
                    message=result==vr::VRApplicationError_None&&autoLaunch==desired?"":
                        tr("Auto-start setting failed. App registration is required.","自動起動を設定できませんでした。SteamVRへの登録を確認してください。");
                    dirty=true;break;
                }
                if(action>=300&&action<332){
                    const unsigned row=static_cast<unsigned>(action-300)/4,kind=static_cast<unsigned>(action-300)%4;
                    const auto& finger=profile.fingers[row];
                    if(kind<2)apply(row,false,frame_range::stepFrom(finger.threshold,kind?1:-1));
                    else apply(row,true,frame_range::stepFrom(finger.high,kind==3?1:-1));
                    break;
                }
            }
        }
        const auto now=std::chrono::steady_clock::now();
        if(liveDirty&&now-lastLive>=std::chrono::milliseconds(100)){
            if(!saveLive())message=tr("Could not apply the change.","変更を反映できませんでした。");
            liveDirty=false;lastLive=now;
        }
        if(prefsDirty&&dragRow<0&&now-lastPrefs>=std::chrono::milliseconds(500)){
            if(!savePreferences())message=tr("Could not save settings.","設定を保存できませんでした。");
            prefsDirty=false;lastPrefs=now;
        }
        // Redraw only on change, at most ~30 times per second.
        if(dirty&&now-lastRender>=std::chrono::milliseconds(33)){
            lastRender=now;
            auto cr=cleanup.cr;cairo_set_source_rgb(cr,.045,.065,.09);cairo_paint(cr);buttons.clear();sliders.clear();
            text(cr,25,48,30,tr("Steam Frame finger correction","Steam Frame 指トラ補正"));
            text(cr,25,84,18,tr("Drag the marks. 0 mark = fully open, 1 mark = fully bent. Lower the 1 mark if a finger does not bend enough.",
                                "印をドラッグ：0の印＝伸ばした状態、1の印＝曲げきった状態。曲がりきらない指は1の印を下げる"),.75,.80,.88);
            auto button=[&](double x,double y,double w,double h,int action,const std::string& label,double size=18){
                buttons.push_back({x,y,w,h,action});
                cairo_set_source_rgb(cr,.11,.22,.30);cairo_rectangle(cr,x,y,w,h);cairo_fill(cr);
                cairo_text_extents_t ext;cairo_set_font_size(cr,size);cairo_text_extents(cr,label.c_str(),&ext);
                text(cr,x+(w-ext.x_advance)/2,y+h/2+size*.36,size,label);
            };
            for(unsigned side=0;side<2;++side){
                const double x=25+side*590;
                text(cr,x,130,24,side?tr("Right hand","右手"):tr("Left hand","左手"));
                for(unsigned f=0;f<4;++f){
                    const unsigned row=side*4+f;const auto& entry=profile.fingers[row];const double y=145+f*150;
                    const bool limited=entry.high>driverMax+1e-9;
                    text(cr,x,y+26,21,japanese?fingerJa[f]:fingerEn[f]);
                    text(cr,x+110,y+26,18,tr("0 mark ","0の印 ")+fixed1(entry.threshold)+"   "+tr("1 mark ","1の印 ")+fixed1(entry.high)+
                         (limited?tr(" (now max ",  "（今は")+fixed2(driverMax)+tr(")","まで）"):""),.85,.88,.95);
                    const int base=300+static_cast<int>(row)*4;
                    button(x+    0,y+38,60,40,base+0,"0 −");button(x+ 66,y+38,60,40,base+1,"0 +");
                    button(x+150,y+38,60,40,base+2,"1 −");button(x+216,y+38,60,40,base+3,"1 +");
                    const double lx=x+10,ly=y+118,lw=sliderWidth;
                    sliders.push_back({lx,y+88,lw,56,row});
                    cairo_set_line_width(cr,4);
                    cairo_set_source_rgb(cr,.30,.34,.40);cairo_move_to(cr,lx,ly);cairo_line_to(cr,lx+lw,ly);cairo_stroke(cr);
                    cairo_set_source_rgb(cr,.75,.80,.88);cairo_move_to(cr,lx,ly);cairo_line_to(cr,lx+lw/sliderMax,ly);cairo_stroke(cr);
                    cairo_set_line_width(cr,2);
                    // Ticks every 0.1; longer at 0.5, labelled at whole numbers and halves.
                    for(int tick=0;tick<=int(std::lround(sliderMax*10));++tick){
                        const double tx=lx+lw*tick/(sliderMax*10);const bool whole=tick%10==0,half=tick%5==0;
                        const double h=whole?9:half?7:4;
                        cairo_set_line_width(cr,whole?2:1);cairo_set_source_rgb(cr,.75,.80,.88);
                        cairo_move_to(cr,tx,ly-h);cairo_line_to(cr,tx,ly+h);cairo_stroke(cr);
                        if(whole)text(cr,tx-4,ly+25,13,std::to_string(tick/10),.6,.65,.72);
                        else if(half)text(cr,tx-9,ly+25,11,fixed1(tick/10.0),.5,.55,.62);
                    }
                    cairo_set_line_width(cr,2);
                    const double a=lx+lw*entry.threshold/sliderMax,b=lx+lw*std::min(entry.high,sliderMax)/sliderMax; // >2 saved: drawn at the end, value kept
                    cairo_set_source_rgba(cr,.25,.65,1,.35);cairo_rectangle(cr,a,ly-5,b-a,10);cairo_fill(cr);
                    auto mark=[&](double mx,double r,double g,double bl,const char* label){
                        cairo_set_source_rgb(cr,r,g,bl);cairo_move_to(cr,mx,ly-4);cairo_line_to(cr,mx-10,ly-22);cairo_line_to(cr,mx+10,ly-22);cairo_close_path(cr);cairo_fill(cr);
                        cairo_rectangle(cr,mx-2,ly-6,4,12);cairo_fill(cr);
                        text(cr,mx-4,ly-26,13,label);
                    };
                    mark(a,.35,.85,.45,"0");mark(b,limited?.6:1,limited?.5:.62,limited?.4:.25,"1");
                }
            }
            // Status
            std::string status;double sr=.94,sg=.96,sb=1;
            if(false){}
#ifdef FRAME_COMPAT_OBSERVATION_UI
            else if(compatVerdict=="active"){status=tr("Correction: ON","補正中");sr=.45;sg=.9;sb=.55;}
            else if(compatVerdict=="waiting")status=tr("Waiting for the controllers. Correction resumes by itself when they are back","コントローラーを待っています。つながると自動で補正を再開します");
            else if(compatVerdict=="observing"||compatVerdict=="verified"||compatVerdict=="structurally_compatible")
                status=tr("Checking the input path (not correcting yet)","入力経路を確認中（まだ補正していません）");
            else if(compatVerdict=="stopped")status=tr("Correction paused; resuming automatically","補正を一時停止中。自動で再開します");
            else if(compatVerdict=="rejected"||compatVerdict=="restore_failed"){
                status=tr("Correction stopped. Restart SteamVR to use it again","補正は停止しました。もう一度使うにはSteamVRの再起動が必要です");sr=1;sg=.7;sb=.4;
                if(!compatReason.empty())status+=" ("+compatReason+")";
            }
#endif
            else if(startupIssue=="RESTART_REQUIRED")status=tr("Restart Frame to load the installed driver","ドライバーを読み込むにはFrameを再起動してください");
            else if(startupIssue=="UPDATE_REQUIRED")status=tr("SteamVR update detected: correction unavailable","未対応のSteamVR更新を検出：補正できません");
            else if(startupIssue=="START_FAILED")status=tr("Correction unavailable: see startup log","補正できません：起動ログを確認してください");
            else if(!betaStatus.valid)status=tr("Driver not connected","ドライバー未接続");
            else if(betaStatus.on){status=tr("Correction: ON","補正中");sr=.45;sg=.9;sb=.55;}
            else status=tr("Correction: starting","補正を準備中");
            text(cr,25,775,22,status,sr,sg,sb);
            if(driverMax<frame_range::uiHighMax)
                text(cr,25,805,16,tr("This driver accepts 1 marks up to ","今のドライバーでは1の印は")+fixed2(driverMax)+
                     tr(".","まで有効"),.75,.80,.88);
            button(25,825,380,52,105,autoLaunch?tr("SteamVR auto-start: ON","SteamVR 自動起動：オン"):tr("SteamVR auto-start: OFF","SteamVR 自動起動：オフ"));
            button(425,825,360,52,106,"言語 / Language：日本語 / English");
            button(805,825,370,52,102,tr("Close (stops correction)","閉じる（補正も止まります）"));
            text(cr,25,912,16,message.empty()?tr("Changes apply and are saved automatically.","変更はすぐに反映され、自動で保存されます。"):message,.75,.80,.88);
            cairo_surface_flush(cleanup.surface);
            const auto source=cairo_image_surface_get_data(cleanup.surface);const int stride=cairo_image_surface_get_stride(cleanup.surface);
            vr::EVROverlayError result;
            auto toRgba=[&](){
                rgba.resize(size_t(width)*height*4);
                for(int y=0;y<height;++y)for(int x=0;x<width;++x){auto a=source+y*stride+x*4;auto b=rgba.data()+(y*width+x)*4;b[0]=a[2];b[1]=a[1];b[2]=a[0];b[3]=a[3];}
            };
            if(useGpu)result=gpu.upload(overlay,cleanup.main,source,stride);
            else{toRgba();result=overlay->SetOverlayRaw(cleanup.main,rgba.data(),width,height,4);}
            if(!iconReady){if(useGpu)toRgba();iconReady=overlay->SetOverlayRaw(cleanup.iconHandle,rgba.data(),width,height,4)==vr::VROverlayError_None;}
            if(result!=vr::VROverlayError_None){std::cerr<<"Overlay upload failed: "<<int(result)<<'\n';return 1;}
            dirty=false;
        }
        if(show){overlay->ShowDashboard(overlayKey);show=false;}
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    if(prefsDirty)savePreferences();
    if(!snapshot.empty()&&cairo_surface_write_to_png(cleanup.surface,snapshot.c_str())!=CAIRO_STATUS_SUCCESS)return 1;
}
