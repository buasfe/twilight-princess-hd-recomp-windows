// Runtime-only composition: the font is read from the user's game, never distributed.
#pragma once
#include "button_prompts.h"
#include <span>
#include <vector>
#include <array>

namespace prompt_art {
struct Mask {
    int width=0,height=0,advance=0;
    std::vector<uint8_t> pixels;
    float sample(float x,float y) const;
};
class Font {
public:
    bool load(std::span<const uint8_t> bfn);
    bool ready() const {return !letters_['A'].pixels.empty();}
    Mask caption(std::string_view text) const;
private:
    std::array<Mask,128> letters_;
};
// Geometry from ZeldaWWHDRecomp (MPL-2.0); captions use TP's own font at run time.
std::vector<uint8_t> render(const button_prompts::Prompt& prompt,const Font& font,
                           int width,int height,bool framed);
}
