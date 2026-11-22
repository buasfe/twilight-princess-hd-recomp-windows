#pragma once
#include "input_map.h"
#include <string_view>
namespace button_prompts {
enum class HudBadge { None, Face, Bumper, Trigger };
struct ResourceRule {int action=-1;bool framed=false;HudBadge badge=HudBadge::None;bool dimmed=false;};
// TP HD USA Rev 2 names verified from LayoutRevo and its COutFont table. In particular
// GC-era names survive: font_02 is X, font_03 Y, font_06/37 ZL, font_59 ZR.
inline ResourceRule resource_rule(std::string_view name){
    const auto slash=name.find_last_of("/\\");if(slash!=name.npos)name.remove_prefix(slash+1);
    using namespace input_map;
    // The HD HUD and contextual A prompt use complete button pictures, separate
    // from the legacy letter panes and the inline COutFont icons below.
    struct BadgePair {std::string_view normal,dimmed;int action;HudBadge shape;};
    static constexpr BadgePair badges[] = {
        {"tr_a_btn_00.bti",  "tr_a_btn_01.bti",  kA,  HudBadge::Face},
        {"tr_b_btn_00.bti",  "tr_b_btn_03.bti",  kB,  HudBadge::Face},
        {"tr_x_btn_00.bti",  "tr_x_btn_01.bti",  kX,  HudBadge::Face},
        {"tr_y_btn_00.bti",  "tr_y_btn_01.bti",  kY,  HudBadge::Face},
        {"tr_l_btn_00.bti",  "tr_l_btn_01.bti",  kL,  HudBadge::Bumper},
        {"tr_r_btn_00.bti",  "tr_r_btn_01.bti",  kR,  HudBadge::Bumper},
        {"tr_zl_btn_00.bti", "tr_zl_btn_01.bti", kZL, HudBadge::Trigger},
        {"tr_zr_btn_00.bti", "tr_zr_btn_01.bti", kZR, HudBadge::Trigger},
    };
    for(const auto& pair:badges)
        if(name==pair.normal||name==pair.dimmed)
            return {pair.action,true,pair.shape,name==pair.dimmed};
    if(name=="tt_zelda_button_l_text.bti")return {kL,false};
    if(name=="tt_zelda_button_a_text.bti"||name=="tt_zelda_button_a_8ia.bti")return {kA,false};
    if(name=="tt_zelda_button_b_text.bti")return {kB,false};
    if(name=="tt_zelda_button_x_text.bti")return {kX,false};
    if(name=="tt_zelda_button_y_text.bti")return {kY,false};
    if(name=="tt_zelda_button_r_text.bti"||name=="im__zelda_button_r_text.bti")return {kR,false};
    if(name=="im_zelda_+button_text_00.bti")return {kPlus,false};
    if(name=="im_zelda_-button_text_00.bti")return {kMinus,false};
    if(name=="font_00.bti")return {kA,true};
    if(name=="font_01.bti"||name=="font_40.bti")return {kB,true};
    if(name=="font_02.bti"||name=="font_41.bti")return {kX,true};
    if(name=="font_03.bti"||name=="font_42.bti")return {kY,true};
    if(name=="font_04.bti"||name=="font_24.bti")return {kL,true};
    if(name=="font_05.bti")return {kR,true};
    if(name=="font_06.bti"||name=="font_37.bti")return {kZL,true};
    if(name=="font_59.bti")return {kZR,true};
    if(name=="font_38.bti")return {kMinus,true};
    if(name=="font_39.bti")return {kPlus,true};
    if(name=="font_57.bti")return {kStickRClick,true};
    if(name=="font_58.bti")return {kStickLClick,true};
    return {};
}
}
