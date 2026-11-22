#include "prompt_art.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace prompt_art {
namespace {
uint16_t u16(std::span<const uint8_t> b,size_t p){return uint16_t(b[p])<<8|b[p+1];}
uint32_t u32(std::span<const uint8_t> b,size_t p){return uint32_t(u16(b,p))<<16|u16(b,p+2);}
struct Block {size_t p,n;};
float segment(float x,float y,float ax,float ay,float bx,float by){
    const float dx=bx-ax,dy=by-ay;
    const float t=std::clamp(((x-ax)*dx+(y-ay)*dy)/(dx*dx+dy*dy),0.f,1.f);
    return std::hypot(x-ax-t*dx,y-ay-t*dy);
}
// Adapted from ZeldaWWHDRecomp's button_glyph_art.h, Mozilla Public License 2.0.
float shape_distance(int face,float x,float y,float s){
    switch(face){
    case 0:return std::min(segment(x,y,-s,-s,s,s),segment(x,y,-s,s,s,-s));
    case 1:return std::fabs(std::hypot(x,y)-s);
    case 2:return std::fabs(std::max(std::fabs(x),std::fabs(y))-s*.9f);
    default:return std::min({segment(x,y,0,-s,-s*1.02f,s*.68f),segment(x,y,-s*1.02f,s*.68f,s*1.02f,s*.68f),segment(x,y,s*1.02f,s*.68f,0,-s)});
    }
}
}
float Mask::sample(float x,float y) const {
    const int ix=int(std::floor(x)),iy=int(std::floor(y));const float fx=x-ix,fy=y-iy;
    auto at=[&](int a,int b){return a<0||b<0||a>=width||b>=height?0.f:float(pixels[b*width+a]);};
    return (at(ix,iy)*(1-fx)+at(ix+1,iy)*fx)*(1-fy)+(at(ix,iy+1)*(1-fx)+at(ix+1,iy+1)*fx)*fy;
}
bool Font::load(std::span<const uint8_t> b){
    if(b.size()<32||memcmp(b.data(),"FONTbfn1",8)||u32(b,8)!=b.size()||b.size()>8*1024*1024)return false;
    std::vector<Block> maps,glyphs,widths;
    for(size_t p=32;p+8<=b.size();){
        const size_t n=u32(b,p+4);if(n<8||n>b.size()-p)return false;
        if(!memcmp(b.data()+p,"MAP1",4))maps.push_back({p,n});
        if(!memcmp(b.data()+p,"GLY1",4))glyphs.push_back({p,n});
        if(!memcmp(b.data()+p,"WID1",4))widths.push_back({p,n});
        p+=n;
    }
    std::array<Mask,128> next;
    for(int ch=32;ch<127;++ch){
        int code=-1;
        for(auto [p,n]:maps){
            if(n<16)return false;
            const int method=u16(b,p+8),first=u16(b,p+10),last=u16(b,p+12),count=u16(b,p+14);
            if(ch<first||ch>last)continue;
            if(method==0)code=ch-first;
            else if(method==2){size_t o=16+2*(ch-first);if(o+2>n)return false;code=u16(b,p+o);}
            else if(method==3){if(16+size_t(count)*4>n)return false;for(int i=0;i<count;++i)if(u16(b,p+16+4*i)==ch){code=u16(b,p+18+4*i);break;}}
            if(code>=0)break;
        }
        if(code<0)continue;
        for(auto [p,n]:glyphs){
            if(n<32)return false;
            if(code<u16(b,p+8)||code>u16(b,p+10))continue;
            const int cw=u16(b,p+12),hh=u16(b,p+14),fmt=u16(b,p+20),cols=u16(b,p+22),rows=u16(b,p+24),tw=u16(b,p+26),th=u16(b,p+28);
            const uint32_t page_size=u32(b,p+16);
            // TP HD's legacy BFN uses I8 8x4 tiles. Reject unsupported formats rather than
            // guessing at pixels or introducing a dependency on copyrighted font assets.
            if(fmt!=1||cw<1||hh<1||tw<1||th<1||tw>4096||th>4096||!cols||!rows)return false;
            const int index=code-u16(b,p+8),page=index/(cols*rows),cell=index%(cols*rows),ox=(cell%cols)*cw,oy=(cell/cols)*hh;
            const size_t data=32+size_t(page)*page_size;
            if(ox+cw>tw||oy+hh>th||data+page_size>n)return false;
            auto& g=next[ch];g.width=cw;g.height=hh;g.advance=cw;g.pixels.resize(cw*hh);
            for(auto [wp,wn]:widths)if(wn>=12&&code>=u16(b,wp+8)&&code<=u16(b,wp+10)){
                size_t off=12+2*(code-u16(b,wp+8));if(off+2>wn)return false;g.advance=b[wp+off+1];break;
            }
            for(int y=0;y<hh;++y)for(int x=0;x<cw;++x){
                const int sx=x+ox,sy=y+oy;
                const size_t off=(sy/4)*((tw+7)/8)*32+(sx/8)*32+(sy%4)*8+sx%8;
                if(off>=page_size)return false;g.pixels[y*cw+x]=b[p+data+off];
            }
            break;
        }
    }
    if(next['A'].pixels.empty()||next['B'].pixels.empty())return false;
    letters_=std::move(next);return true;
}
Mask Font::caption(std::string_view text) const {
    Mask out;if(!ready()||text.empty())return out;
    int width=0,height=0;
    for(unsigned char c:text){if(c>=128||letters_[c].pixels.empty())return {};width+=letters_[c].advance;height=std::max(height,letters_[c].height);}
    if(width<=0||width>2048)return {};
    // Keep native bearings/spacing, then crop the composition to its actual ink.
    Mask full{width+128,height,0,std::vector<uint8_t>((width+128)*height)};
    int origin=0,left=full.width,top=height,right=0,bottom=0;
    for(unsigned char c:text){const auto& g=letters_[c];for(int y=0;y<g.height;++y)for(int x=0;x<g.width;++x){
        const auto a=g.pixels[y*g.width+x];const int xx=x+origin;
        if(a){full.pixels[y*full.width+xx]=std::max(full.pixels[y*full.width+xx],a);left=std::min(left,xx);right=std::max(right,xx+1);top=std::min(top,y);bottom=std::max(bottom,y+1);}
    }
        origin+=g.advance;
    }
    if(right<=left||bottom<=top)return {};
    out.width=right-left;out.height=bottom-top;out.advance=width;out.pixels.resize(out.width*out.height);
    for(int y=0;y<out.height;++y)std::copy_n(full.pixels.begin()+(y+top)*full.width+left,out.width,out.pixels.begin()+y*out.width);
    return out;
}
std::vector<uint8_t> render(const button_prompts::Prompt& p,const Font& font,int w,int h,bool framed){
    if(p.original()||w<1||h<1||w>512||h>512)return {};
    const bool shape=p.style==button_prompts::Style::PlayStation&&p.input>=input_map::kPadA&&p.input<=input_map::kPadY;
    const auto caption=shape?Mask{}:font.caption(p.caption);
    if(!shape&&caption.pixels.empty())return {};
    std::vector<uint8_t> out(w*h*4);
    // Native letter panes are deliberately narrow. Fit two-letter shoulders to their pane
    // without moving, resizing or recolouring the game's surrounding button plates.
    const float sx=float(w)*(framed?.60f:.92f)/(caption.width+2),sy=float(h)*(framed?.52f:.86f)/(caption.height+2);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        float coverage=0;
        if(shape){
            const float s=std::min(w,h)*(framed?.22f:.34f),stroke=std::max(1.f,std::min(w,h)*.075f);
            for(int yy=0;yy<4;++yy)for(int xx=0;xx<4;++xx)
                coverage+=shape_distance(p.input-input_map::kPadA,x+(xx+.5f)/4-w*.5f,y+(yy+.5f)/4-h*.5f,s)<stroke*.5f?255.f/16:0.f;
        }else coverage=caption.sample((x+.5f-w*.5f)/sx+caption.width*.5f-.5f,(y+.5f-h*.5f)/sy+caption.height*.5f-.5f);
        float alpha=coverage,ink=255;
        if(framed){
            const float px=(x+.5f-w*.5f)/(w*.46f),py=(y+.5f-h*.5f)/(h*.46f);
            const bool face=p.style!=button_prompts::Style::Keyboard&&p.input>=input_map::kPadA&&p.input<=input_map::kPadY;
            const float d=face?std::hypot(px,py):std::hypot(std::max(std::fabs(px)-.78f,0.f),std::max(std::fabs(py)-.65f,0.f))/.22f;
            const float a=std::clamp((1.f-d)*std::min(w,h)*.46f+.5f,0.f,1.f)*255;
            const float bg=d>.9f?130.f:232.f;
            alpha=std::max(a,coverage);ink=alpha?(bg*a*(1-coverage/255)+40*coverage)/alpha:255;
        }
        const int off=(y*w+x)*4;out[off]=out[off+1]=out[off+2]=uint8_t(std::clamp(ink,0.f,255.f));out[off+3]=uint8_t(std::clamp(alpha,0.f,255.f));
    }
    return out;
}
}
