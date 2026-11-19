// Actual KPAD HLE boundary: connection events, argument preservation, and Pro samples.
#include "runtime.h"
#include "input.h"
#include "hle/padscore.h"
#include <cassert>
#include <map>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
namespace {
auto& functions(){static std::map<std::string,PpcFunc> f;return f;}
bool pro=false;
input::PadState pad;
int events=0;uint32_t eventStatus=0;
}
HleReg::HleReg(const char* lib,const char* name,PpcFunc fn){functions()[std::string(lib)+":"+name]=fn;}
bool g_trace_hle=false;
void log_msg(const char*,...){}
namespace input {bool pro_controller(){return pro;}}
namespace crashrec {input::PadState read(int){return pad;}}
namespace motion {void right_stick(float,float){}}
namespace rumble {void pro_motor(uint32_t,bool){}}
uint32_t guest_call(Cpu* c,uint32_t fn,std::initializer_list<uint32_t> args){
    assert(fn==0x020086FC||fn==0x020086F8);
    assert(args.size()==2&&*args.begin()==0);eventStatus=*(args.begin()+1);++events;
    padscore_hle::service_connect(c); // re-entry cannot notify recursively
    c->r[3]=0xBAD;c->r[4]=0xBAD;c->f[0].ps0=123;c->cr[0]=1;
    return 0;
}
uint32_t call(const char* name,std::initializer_list<uint32_t> args){
    Cpu c{};int i=3;for(auto a:args)c.r[i++]=a;functions().at(std::string("padscore:")+name)(&c);return c.r[3];
}
int main(){
    constexpr uint32_t st=0x10000000,err=st+0x100,type=st+0x104;
#ifdef _WIN32
    auto memory=VirtualAlloc(mem::ptr(st),65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
#else
    auto memory=mmap(mem::ptr(st),65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
#endif
    assert(memory==mem::ptr(st));
    assert(call("KPADSetConnectCallback",{4,0x020086FC})==0);
    assert(call("KPADSetConnectCallback",{0,0x020086FC})==0);
    Cpu c{};c.r[3]=17;c.r[4]=18;
    padscore_hle::service_connect(&c);assert(events==0);
    pro=true;padscore_hle::service_connect(&c);
    assert(events==1&&eventStatus==0&&c.r[3]==17&&c.r[4]==18&&c.f[0].ps0==0&&c.cr[0]==0);
    padscore_hle::service_connect(&c);assert(events==1);
    assert(call("WPADProbe",{0,type})==0&&ld32(type)==31);
    assert(call("KPADSetConnectCallback",{0,0x020086F8})==0x020086FC);
    pad.buttons=input::kA|input::kPlus;pad.lx=0.75f;pad.ly=-0.5f;pad.rx=-0.25f;pad.ry=1;
    memset(mem::ptr(st),0xCD,0xF0);
    assert(call("KPADReadEx",{0,st,1,err})==1&&ld32(err)==0);
    assert(events==2&&ld8(st+0x5C)==31&&ld8(st+0x5F)==22);
    assert(ld32(st+0x60)==0x410&&ld32(st+0x64)==0x410&&ld32(st+0x68)==0);
    assert(ldf32(st+0x6C)==0.75f&&ldf32(st+0x70)==-0.5f&&ldf32(st+0x74)==-0.25f&&ldf32(st+0x78)==1);
    assert(call("KPADReadEx",{0,st,1,err})==1&&ld32(st+0x64)==0);
    pad.buttons=0;assert(call("KPADReadEx",{0,st,1,err})==1&&ld32(st+0x68)==0x410);
    assert(call("KPADReadEx",{1,st,1,err})==0&&int32_t(ld32(err))==-2);
    pro=false;padscore_hle::service_connect(&c);assert(events==3&&int32_t(eventStatus)==-1);
    assert(call("KPADReadEx",{0,st,1,err})==0&&int32_t(ld32(err))==-2);
    pro=true;pad.buttons=input::kA;assert(call("KPADReadEx",{0,st,1,err})==1);
    assert(events==4&&ld32(st+0x64)==0x10); // fresh edge after reconnect
    assert(call("KPADReadEx",{0,st,0,err})==0);
    puts("padscore_test: connection/reconnect, callback registers, KPAD fields and input edges passed");
}
