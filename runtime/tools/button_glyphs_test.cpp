// Cached HD glyph sheets and Original restoration; only synthetic guest data.
#include "runtime.h"
#include "button_glyphs.h"
#include "button_prompts.h"
#include "cemu_shim.h"
#include "gx2_texture_regs.h"
#include <cassert>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
extern "C" void hook_02B8BAF8(Cpu*);
constexpr uint32_t resource=0x11000000,name=resource+0x1000,pool=resource+0x10000,image=resource+0x20000;
void log_msg(const char*,...){}
namespace mem {uint32_t host_alloc(uint32_t n,uint32_t a){static uint32_t p=kHostStart;const uint32_t q=(p+a-1)&~(a-1);p=q+n;return q;}}
extern "C" void f_02B8BAF8_orig(Cpu* c){c->r[3]=resource;}
extern "C" void f_02B8BB88_orig(Cpu*){}
extern "C" void f_02BA6218_orig(Cpu*){}
extern "C" void f_02BDC0B0_orig(Cpu*){}
extern "C" void f_02BE6F60_orig(Cpu*){}
extern "C" void f_02BE78C4_orig(Cpu*){}
void map(uint32_t start,uint32_t size){
#ifdef _WIN32
    assert(VirtualAlloc(mem::ptr(start),size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==mem::ptr(start));
#else
    assert(mmap(mem::ptr(start),size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0)==mem::ptr(start));
#endif
}
int main(){
    map(0x10120000,65536);map(0x10280000,65536);map(resource,0x40000);map(mem::kHostStart,65536);
    st32(0x1012CABC,pool);st32(0x1028595C+8,0xC0000040);st32(0x1028995C+8,7);
    auto* t=reinterpret_cast<GX2::GX2Texture*>(mem::ptr(pool+7*168));
    t->surface.dim=Latte::E_DIM::DIM_2D;t->surface.width=16;t->surface.height=16;t->surface.depth=1;t->surface.numLevels=1;
    t->surface.format=Latte::E_GX2SURFFMT::R8_G8_B8_A8_UNORM;t->surface.tileMode=Latte::E_GX2TILEMODE::TM_LINEAR_SPECIAL;
    t->surface.imagePtr=image;t->surface.imageSize=1024;t->surface.pitch=16;t->compSel=0x00010203;
    memset(mem::ptr(image),0x77,1024);const auto original=std::vector<uint8_t>(mem::ptr(image),mem::ptr(image)+1024);
    st16(resource+2,88);st16(resource+4,88);st32(resource+28,0xC0000040-resource);strcpy((char*)mem::ptr(name),"tr_a_btn_00.bti");
    Cpu c{};c.r[4]=name;hook_02B8BAF8(&c);const Cpu saved=c;
    auto m=input_map::Mapping::defaults();m.prompts=input_map::Mapping::Prompts::kOriginal;input_map::set_current(m,false);
    button_glyphs::refresh(&c);assert(button_glyphs::texture_image(image)==image&&!memcmp(mem::ptr(image),original.data(),1024));
    m.prompts=input_map::Mapping::Prompts::kPlayStation;input_map::set_current(m,false);button_glyphs::refresh(&c);
    const uint32_t generated=button_glyphs::texture_image(image);
    assert(generated>=mem::kHostStart&&(generated&2047)==0&&!memcmp(mem::ptr(image),original.data(),1024)&&!memcmp(&saved,&c,sizeof c));
    const auto circle=std::vector<uint8_t>(mem::ptr(generated),mem::ptr(generated)+1024);button_glyphs::refresh(&c);assert(button_glyphs::texture_image(image)==generated);
    m.pad[input_map::kA]=input_map::kPadX;input_map::set_current(m,false);button_glyphs::refresh(&c);const auto square=button_glyphs::texture_image(image);assert(square!=generated&&memcmp(circle.data(),mem::ptr(square),1024));
    m.prompts=input_map::Mapping::Prompts::kOriginal;input_map::set_current(m,false);button_glyphs::refresh(&c);assert(button_glyphs::texture_image(image)==image&&!memcmp(original.data(),mem::ptr(image),1024));
    assert(button_glyphs::texture_image(image+0x100000)==image+0x100000);
    m.prompts=input_map::Mapping::Prompts::kPlayStation;m.pad[input_map::kA]=input_map::kPadB;
    input_map::set_current(m,false);button_glyphs::refresh(&c);
    assert(button_glyphs::texture_image(image)==generated); // cached variant can be selected again
    assert(t->surface.width==16&&t->surface.height==16&&t->surface.imagePtr==image); // cached material descriptors and layout stay native
    puts("button_glyphs_test: cached HD sheets, live mapping changes, exact Original restoration, CPU preservation and no repeated work passed");
}
