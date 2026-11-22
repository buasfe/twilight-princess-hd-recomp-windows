#include "button_prompts.h"
#include <algorithm>
#include <mutex>

namespace button_prompts {
const Device* Tracker::active() const {
    const auto it=devices_.find(active_);return it==devices_.end()?nullptr:&it->second;
}
void Tracker::fallback() {
    uint32_t next=0;
    for(const auto& [id,d]:devices_) if(!d.virtual_device){next=id;break;}
    if(!next&&!devices_.empty())next=devices_.begin()->first;
    if(next!=active_){active_=next;++generation_;}
}
void Tracker::add(Device d) {
    if(d.id==active_)++generation_;
    const auto old=active();const bool choose=!old||(old->virtual_device&&!d.virtual_device);
    const auto id=d.id;devices_.insert_or_assign(id,std::move(d));
    if(choose&&active_!=id){active_=id;++generation_;}
}
void Tracker::remove(uint32_t id) {devices_.erase(id);if(active_==id)fallback();}
void Tracker::activity(uint32_t id) {
    const auto it=devices_.find(id);if(it==devices_.end()||active_==id)return;
    // SDL virtual pads cannot reveal the physical controller behind Steam Input/DS4Windows.
    // A physical device with a reliable identity wins over its virtual duplicate; F1 can override.
    if(it->second.virtual_device&&std::any_of(devices_.begin(),devices_.end(),[](const auto& p){return !p.second.virtual_device;}))return;
    active_=id;++generation_;
}
Style style(const input_map::Mapping& m, Family f) {
    using P=input_map::Mapping::Prompts;
    switch(m.prompts){
    case P::kOriginal:return Style::Original;
    case P::kXbox:return Style::Xbox;
    case P::kPlayStation:return Style::PlayStation;
    case P::kKeyboard:return Style::Keyboard;
    default:break;
    }
    switch(f){case Family::Xbox:return Style::Xbox;case Family::PlayStation:return Style::PlayStation;
    case Family::Nintendo:return Style::Nintendo;default:return Style::Original;}
}
const char* style_name(Style s) {
    switch(s){case Style::Xbox:return "Xbox";case Style::PlayStation:return "PlayStation";
    case Style::Nintendo:return "Nintendo";case Style::Keyboard:return "Keyboard / Mouse";default:return "Original Wii U";}
}
Prompt resolve(const input_map::Mapping& m, Family f, int a) {
    Prompt out;out.style=style(m,f);
    if(a<0||a>=input_map::kActionCount){out.style=Style::Original;return out;}
    if(out.original())return out;
    if(out.style==Style::Keyboard){
        int key=input_map::kNoKey;for(int k:m.keys[a])if(k!=input_map::kNoKey){key=k;break;}
        out.caption=key==input_map::kNoKey?"None":input_map::key_short_label(key);
        // Captions use the same physical key identities as Controls; plain text fits keycaps.
        if(out.caption=="\xE2\x86\x91")out.caption="Up";
        if(out.caption=="\xE2\x86\x93")out.caption="Down";
        if(out.caption=="\xE2\x86\x90")out.caption="Left";
        if(out.caption=="\xE2\x86\x92")out.caption="Right";
        if(std::any_of(out.caption.begin(),out.caption.end(),[](unsigned char c){return c>=128;}))out.caption=input_map::key_id(key);
        if(out.caption=="Return")out.caption="Enter";
        if(out.caption=="LeftShift")out.caption="LShift";
        if(out.caption=="RightShift")out.caption="RShift";
        if(out.caption=="LeftControl")out.caption="LCtrl";
        if(out.caption=="RightControl")out.caption="RCtrl";
        if(out.caption=="LeftOption")out.caption="LAlt";
        if(out.caption=="RightOption")out.caption="RAlt";
        return out;
    }
    using namespace input_map;
    out.input=m.pad[a];
    if(out.input>=kPadA&&out.input<=kPadY){
        static const char* xbox[]={"A","B","X","Y"};
        static const char* ps[]={"Cross","Circle","Square","Triangle"};
        static const char* nin[]={"B","A","Y","X"};
        out.caption=(out.style==Style::PlayStation?ps:out.style==Style::Nintendo?nin:xbox)[out.input-kPadA];
    }else if(out.input>=kPadLB&&out.input<=kPadRT){
        static const char* xbox[]={"LB","RB","LT","RT"};
        static const char* ps[]={"L1","R1","L2","R2"};
        static const char* nin[]={"L","R","ZL","ZR"};
        out.caption=(out.style==Style::PlayStation?ps:out.style==Style::Nintendo?nin:xbox)[out.input-kPadLB];
    }else{
        switch(out.input){
        case kPadMenu:out.caption=out.style==Style::PlayStation?"Options":"Menu";break;
        case kPadOptions:out.caption=out.style==Style::PlayStation?"Create":"View";break;
        case kPadHome:out.caption="Home";break;
        case kPadL3:out.caption=out.style==Style::Xbox?"LS":"L3";break;
        case kPadR3:out.caption=out.style==Style::Xbox?"RS":"R3";break;
        case kPadDUp:case kPadLSUp:case kPadRSUp:out.caption="Up";break;
        case kPadDDown:case kPadLSDown:case kPadRSDown:out.caption="Down";break;
        case kPadDLeft:case kPadLSLeft:case kPadRSLeft:out.caption="Left";break;
        case kPadDRight:case kPadLSRight:case kPadRSRight:out.caption="Right";break;
        default:out.caption="None";break;
        }
    }
    return out;
}
namespace {std::mutex mutex;Tracker tracker;}
void connected(Device d){std::lock_guard lock(mutex);tracker.add(std::move(d));}
void disconnected(uint32_t id){std::lock_guard lock(mutex);tracker.remove(id);}
void activity(uint32_t id){std::lock_guard lock(mutex);tracker.activity(id);}
Device active_device(){std::lock_guard lock(mutex);const auto* d=tracker.active();return d?*d:Device{0,Family::Unknown,false,"No controller"};}
uint32_t device_generation(){std::lock_guard lock(mutex);return tracker.generation();}
}
