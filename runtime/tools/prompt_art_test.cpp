#include "prompt_art.h"
#include "prompt_font_fixture.h"
#include <cassert>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <cstring>
using namespace prompt_art;
using namespace prompt_test;
int main(int argc,char** argv){
    auto b=fixture();Font f;assert(f.load(b)&&f.ready());
    assert(f.caption("AB").width==4&&f.caption("AB").height==4);
    for(int n=0;n<32;++n)assert(!Font{}.load(std::span(b.data(),n)));
    auto invalid=b;be32(invalid,36,0x7fffffff);assert(!Font{}.load(invalid));
    invalid=b;be16(invalid,48+22,0);assert(!Font{}.load(invalid));
    invalid=b;be16(invalid,48+20,6);assert(!Font{}.load(invalid));
    if(argc>1){std::ifstream file(argv[1],std::ios::binary);std::vector<uint8_t> real((std::istreambuf_iterator<char>(file)),{});assert(f.load(real));}
    const char* labels[]={"A","B","X","Y","LB","RB","LT","RT","L1","R1","L2","R2","Space","None","Options","Create","Up","Left"};
    for(const char* s:labels)assert(!f.caption(s).pixels.empty());
    for(auto style:{button_prompts::Style::Xbox,button_prompts::Style::PlayStation,button_prompts::Style::Keyboard})for(int i=0;i<4;++i)for(bool framed:{true,false}){
        button_prompts::Prompt p{style,input_map::kPadA+i,labels[i]};
        const auto pixels=render(p,f,128,128,framed);assert(pixels.size()==65536);
        int covered=0;for(int n=3;n<int(pixels.size());n+=4)covered+=pixels[n]>0;assert(covered>200&&covered<16000);
        if(argc>2){const auto name=std::string(argv[2])+std::to_string(int(style))+"-"+std::to_string(i)+(framed?"-icon":"-letter")+".rgba";std::ofstream out(name,std::ios::binary);out.write((const char*)pixels.data(),pixels.size());}
    }
    assert(render({},f,64,64,true).empty());assert(render({button_prompts::Style::Xbox,1,"A"},f,0,64,true).empty());
    puts("prompt_art_test: bounded BFN parser, captions, generated shapes, frames and original fallback passed");
}
