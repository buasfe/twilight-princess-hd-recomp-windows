// TP HD USA Rev 2 J2D glyph resources. Cached GX2 display lists retain image addresses;
// resolve named glyph images at sampling time, keeping the native descriptors and pane geometry.
#include "button_glyphs.h"
#include "button_prompts.h"
#include "prompt_art.h"
#include "prompt_resources.h"
#include "runtime.h"
#include "cemu_shim.h"
#include "gx2_texture_regs.h"
#include <array>
#include <map>
#include <mutex>
#include <cstring>
#include <atomic>
#include <set>

extern "C" {
void f_02B8BAF8_orig(Cpu*);void f_02B8BB88_orig(Cpu*);void f_02BA6218_orig(Cpu*);
void f_02BDC0B0_orig(Cpu*);void f_02BE6F60_orig(Cpu*);void f_02BE78C4_orig(Cpu*);
}
namespace {
struct Resource {std::string name;std::array<uint8_t,32> header;};
struct Sheet {
    uint32_t image=0,size=0,owner=0;std::array<uint8_t,0x9C> descriptor{};
    std::string applied;
};
std::mutex mutex;
std::map<uint32_t,Resource> resources;
std::map<uint32_t,Sheet> sheets;
prompt_art::Font font;
std::map<uint32_t,uint32_t> overrides;
std::map<uint32_t,std::string> override_labels;
std::map<std::string,uint32_t> variants;
std::atomic<bool> has_overrides{false};
bool tracing(){static const bool on=getenv("TPHD_GLYPH_TRACE")!=nullptr;return on;}
uint32_t image_base(const GX2Surface& surface) {
    const uint32_t image=surface.imagePtr;
    return Latte::TM_IsMacroTiled(surface.tileMode)?image&~0x700u:image;
}
std::vector<uint8_t> render_art(const button_prompts::Prompt& prompt,
                                const button_prompts::ResourceRule& rule,int w,int h) {
    using button_prompts::HudBadge;
    if(rule.badge==HudBadge::None)return prompt_art::render(prompt,font,w,h,rule.framed);
    // Match the transparent margins of the native HD button pictures, which
    // are square even for the long, low L/R buttons.
    const int inner_w=std::max(1,w*(rule.badge==HudBadge::Face?81:99)/100);
    const int inner_h=std::max(1,h*(rule.badge==HudBadge::Bumper?49:83)/100);
    const auto badge=prompt_art::render(prompt,font,inner_w,inner_h,true);
    if(badge.empty())return {};
    std::vector<uint8_t> rgba(w*h*4);
    for(int y=0;y<inner_h;++y)for(int x=0;x<inner_w;++x){
        const auto* src=badge.data()+(y*inner_w+x)*4;
        auto* dst=rgba.data()+((y+(h-inner_h)/2)*w+x+(w-inner_w)/2)*4;
        const uint8_t shade=rule.dimmed?uint8_t(src[0]*3/4):src[0];
        dst[0]=dst[1]=dst[2]=shade;dst[3]=src[3];
    }
    return rgba;
}
std::string name_at(uint32_t p){std::string s;if(p)for(int n=0;n<255;++n){char c=ld8(p+n);if(!c)break;s+=c;}return s;}
uint32_t native_texture(uint32_t image){
    for(uint32_t slot=(image>>5)&4095,n=0;n<4096;++n,slot=(slot+1)&4095){
        const auto key=ld32(0x1028595C+slot*4);if(!key)break;
        if(key==(image&~31u)){const auto h=ld32(0x1028995C+slot*4),pool=ld32(0x1012CABC);return h<4095&&pool?pool+h*168:0;}
    }
    return 0;
}
void loaded(uint32_t p,const std::string& name){
    if(!p)return;std::lock_guard lock(mutex);
    if(auto old=resources.find(p);old!=resources.end()&&(old->second.name!=name||memcmp(old->second.header.data(),mem::ptr(p),32))){
        for(auto it=sheets.begin();it!=sheets.end();)if(it->second.owner==p){overrides.erase(it->second.image);it=sheets.erase(it);}else ++it;
        has_overrides.store(!overrides.empty(),std::memory_order_release);
    }
    if(name=="rodan_b_24_22.bfn"&&!font.ready()&&ld32(p)==0x464F4E54){
        const auto size=ld32(p+8);if(size>=32&&size<=8*1024*1024&&uint64_t(p)+size<=mem::kMem2End)font.load({mem::ptr(p),size});
    }
    if(button_prompts::resource_rule(name).action<0){resources.erase(p);return;}
    Resource r{name,{}};memcpy(r.header.data(),mem::ptr(p),32);resources.insert_or_assign(p,std::move(r));
}
bool encode(const GX2::GX2Texture& t,const std::vector<uint8_t>& rgba,std::vector<uint8_t>& out){
    const auto& s=t.surface;const uint32_t w=s.width,h=s.height,fmt=uint32_t(s.format.value())&63;
    const uint32_t bytes=fmt==1||fmt==2?1:fmt==7?2:fmt==26?4:0;if(!bytes||s.depth!=1||rgba.size()!=w*h*4)return false;
    LatteAddrLib::AddrSurfaceInfo_OUT g{};
    LatteAddrLib::GX2CalculateSurfaceInfo(s.format,w,h,s.depth,s.dim,s.tileMode,s.aa,0,&g);
    const auto tile=g.hwTileMode;const uint32_t pitch=s.pitch;
    const uint32_t sw=uint32_t(s.imagePtr)^((uint32_t(s.swizzle)>>16)>0?uint32_t(s.swizzle)&0xffffu:0);
    LatteAddrLib::CachedSurfaceAddrInfo cached{};
    if(Latte::TM_IsMacroTiled(tile))LatteAddrLib::SetupCachedSurfaceAddrInfo(&cached,0,0,bytes*8,pitch,g.height,g.depth,1,tile,false,(sw>>8)&1,(sw>>9)&3);
    for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){
        uint32_t offset;
        if(tile==Latte::E_HWTILEMODE::TM_LINEAR_GENERAL||tile==Latte::E_HWTILEMODE::TM_LINEAR_ALIGNED)offset=LatteAddrLib::ComputeSurfaceAddrFromCoordLinear(x,y,0,0,bytes*8,pitch,g.height,g.depth);
        else if(!Latte::TM_IsMacroTiled(tile))offset=LatteAddrLib::ComputeSurfaceAddrFromCoordMicroTiled(x,y,0,bytes*8,pitch,g.height,tile,false);
        else offset=LatteAddrLib::ComputeSurfaceAddrFromCoordMacroTiledCached(x,y,&cached);
        if(uint64_t(offset)+bytes>out.size())return false;
        const auto* p=rgba.data()+(y*w+x)*4;auto* dst=out.data()+offset;
        if(fmt==1)dst[0]=p[3]; // native I8 letter: intensity is also its alpha
        else if(fmt==2)dst[0]=(p[0]&0xF0)|(p[3]>>4);
        else if(fmt==7){dst[0]=p[0];dst[1]=p[3];}
        else memcpy(dst,p,4);
    }
    return true;
}
}
namespace button_glyphs {
uint32_t texture_image(uint32_t original){
    if(!has_overrides.load(std::memory_order_acquire))return original;
    std::lock_guard lock(mutex);
    const auto it=overrides.find(original);if(it==overrides.end())return original;
    if(tracing()){
        static std::set<std::pair<uint32_t,uint32_t>> logged;
        if(logged.size()<256&&logged.emplace(original,it->second).second)
            LOG("[glyph] Vulkan sampled %s %08X -> %08X",override_labels.at(original).c_str(),original,it->second);
    }
    return it->second;
}
void refresh(Cpu* c){
    (void)c;
    std::lock_guard lock(mutex);const auto m=input_map::current();const auto family=button_prompts::active_device().family;
    if(button_prompts::style(m,family)==button_prompts::Style::Original){
        if(tracing()&&!overrides.empty())LOG("[glyph] restored %zu original sampled textures",overrides.size());
        overrides.clear();override_labels.clear();sheets.clear();
        has_overrides.store(false,std::memory_order_release);
        return;
    }
    for(const auto& [p,r]:resources){
        if(memcmp(mem::ptr(p),r.header.data(),32))continue;
        const auto rule=button_prompts::resource_rule(r.name);const auto prompt=button_prompts::resolve(m,family,rule.action);
        const uint32_t offset=ld32(p+28),texture=native_texture(p+(offset?offset:32));if(!texture)continue;
        auto it=sheets.find(texture);
        auto t=*reinterpret_cast<const GX2::GX2Texture*>(mem::ptr(texture));
        const auto& s=t.surface;const uint32_t image=image_base(s),size=s.imageSize,w=s.width,h=s.height;
        if(!image||!size||size>1024*1024||w<1||h<1||w>512||h>512||s.numLevels!=1||uint64_t(image)+size>mem::kMem2End)continue;
        if(it!=sheets.end()&&(it->second.image!=image||it->second.size!=size||memcmp(it->second.descriptor.data(),&t,sizeof(t)))){overrides.erase(it->second.image);sheets.erase(it);it=sheets.end();}
        if(it==sheets.end()){
            Sheet sheet;sheet.image=image;sheet.size=size;sheet.owner=p;memcpy(sheet.descriptor.data(),&t,sizeof(t));
            it=sheets.emplace(texture,std::move(sheet)).first;
        }
        auto& sheet=it->second;
        const std::string key=std::to_string(int(prompt.style))+":"+std::to_string(prompt.input)+":"+prompt.caption;
        if(sheet.applied==key)continue;
        {
            auto layout=s;layout.imagePtr=0;layout.mipPtr=0;
            std::string variant_key=key+(rule.framed?":icon:":":letter:")+std::to_string(int(rule.badge))+(rule.dimmed?":dim:":":normal:");
            variant_key.append(reinterpret_cast<const char*>(&layout),sizeof layout);
            const auto pipe_bank_bits=uint32_t(s.imagePtr)&0x700u;
            variant_key.append(reinterpret_cast<const char*>(&pipe_bank_bits),sizeof pipe_bank_bits);
            auto variant=variants.find(variant_key);
            if(variant==variants.end()){
                if(variants.size()>=512)continue;
                std::vector<uint8_t> pixels(size);
                const auto rgba=render_art(prompt,rule,w,h);
                if(rgba.empty()||!encode(t,rgba,pixels))continue;
                // Macro-tiled image bases must not overlap the 0x700 bank-swizzle bits.
                const auto generated=mem::host_alloc(size,2048);memcpy(mem::ptr(generated),pixels.data(),size);
                variant=variants.emplace(std::move(variant_key),generated).first;
            }
            overrides[image]=variant->second;
            if(tracing()){
                override_labels[image]=r.name+" "+prompt.caption;
                LOG("[glyph] prepared %s base %08X %ux%u format %X -> %08X",
                    override_labels[image].c_str(),image,w,h,uint32_t(s.format.value()),variant->second);
            }
        }
        sheet.applied=key;
    }
    has_overrides.store(!overrides.empty(),std::memory_order_release);
}
}
extern "C" void hook_02B8BAF8(Cpu* c){const auto n=name_at(c->r[4]);f_02B8BAF8_orig(c);loaded(c->r[3],n);}
extern "C" void hook_02B8BB88(Cpu* c){const auto n=name_at(c->r[5]);f_02B8BB88_orig(c);loaded(c->r[3],n);}
extern "C" void hook_02BA6218(Cpu* c){const auto type=ld8(c->r[3]);const auto n=type>=2&&type<=4?name_at(c->r[3]+2):std::string{};f_02BA6218_orig(c);if(!n.empty())loaded(c->r[3],n);}
// Legacy hook entries retained for generator compatibility; HD binding uses its native table.
extern "C" void hook_02BDC0B0(Cpu* c){f_02BDC0B0_orig(c);}
extern "C" void hook_02BE6F60(Cpu* c){f_02BE6F60_orig(c);}
extern "C" void hook_02BE78C4(Cpu* c){f_02BE78C4_orig(c);}
