#pragma once
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
namespace frame_settings {
// threshold is the "new 0" mark (deadzone). high is the "new 1" mark; it is
// stored separately (FRAME_FINGER_RANGE_1) so the v1 profile stays readable by
// older releases.
struct Finger {bool selected=false,dynamic=false;double threshold=.2,high=1.0;};
inline bool validRangeHigh(double high){return std::isfinite(high)&&high>=.1&&high<=2.0;}
struct Profile {bool requested=false;std::array<Finger,8> fingers{};};
inline bool parse(const std::string& text,Profile& result){
    if(text.size()>4096)return false;
    std::istringstream input(text);std::string tag,extra;int requested=-1;
    Profile next;
    if(!(input>>tag>>requested)||tag!="FRAME_FINGER_PROFILE_1"||(requested!=0&&requested!=1))return false;
    next.requested=requested==1;
    for(unsigned row=0;row<8;++row){
        int side=-1,finger=-1,selected=-1;std::string mode;double threshold=0;
        if(!(input>>side>>finger>>selected>>mode>>threshold)||side!=int(row/4)||finger!=int(row%4)||
           (selected!=0&&selected!=1)||(mode!="manual"&&mode!="dynamic")||
           !std::isfinite(threshold)||threshold<0||threshold>.9)return false;
        next.fingers[row]={selected==1,mode=="dynamic",threshold};
    }
    if(input>>extra)return false;
    result=next;return true;
}
inline std::string serialize(const Profile& profile){
    std::ostringstream output;output<<"FRAME_FINGER_PROFILE_1 "<<profile.requested<<'\n'<<std::setprecision(17);
    for(unsigned row=0;row<8;++row){
        const auto& entry=profile.fingers[row];
        if(!std::isfinite(entry.threshold)||entry.threshold<0||entry.threshold>.9||!validRangeHigh(entry.high))return {};
        output<<row/4<<' '<<row%4<<' '<<entry.selected<<' '<<(entry.dynamic?"dynamic":"manual")<<' '<<entry.threshold<<'\n';
    }
    return output.str();
}
// Range file: only the "new 1" marks. Applying it never changes other fields.
inline bool parseRange(const std::string& text,Profile& result){
    if(text.size()>4096)return false;
    std::istringstream input(text);std::string tag,extra;std::array<double,8> highs{};
    if(!(input>>tag)||tag!="FRAME_FINGER_RANGE_1")return false;
    for(unsigned row=0;row<8;++row){
        int side=-1,finger=-1;double high=0;
        if(!(input>>side>>finger>>high)||side!=int(row/4)||finger!=int(row%4)||!validRangeHigh(high))return false;
        highs[row]=high;
    }
    if(input>>extra)return false;
    for(unsigned row=0;row<8;++row)result.fingers[row].high=highs[row];
    return true;
}
inline std::string serializeRange(const Profile& profile){
    std::ostringstream output;output<<"FRAME_FINGER_RANGE_1\n"<<std::setprecision(17);
    for(unsigned row=0;row<8;++row){
        if(!validRangeHigh(profile.fingers[row].high))return {};
        output<<row/4<<' '<<row%4<<' '<<profile.fingers[row].high<<'\n';
    }
    return output.str();
}
}
