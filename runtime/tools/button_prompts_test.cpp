#include "button_prompts.h"
#include "prompt_resources.h"
#include <cassert>
#include <cstdio>
using namespace button_prompts;
using namespace input_map;
int main(){
    auto m=Mapping::defaults();const auto bindings=m.pad;
    assert(resolve(m,Family::Xbox,kA).caption=="B"); // Nintendo A is east on the default layout
    assert(resolve(m,Family::Xbox,kB).caption=="A");
    assert(resolve(m,Family::PlayStation,kA).caption=="Circle");
    assert(resolve(m,Family::PlayStation,kB).caption=="Cross");
    assert(resolve(m,Family::Nintendo,kA).caption=="A");
    for(int a=kL;a<=kZR;++a)assert(!resolve(m,Family::Xbox,a).caption.empty());
    m.pad[kA]=kPadLT;m.pad[kB]=kPadX;
    assert(resolve(m,Family::Xbox,kA).caption=="LT");
    assert(resolve(m,Family::PlayStation,kA).caption=="L2");
    assert(resolve(m,Family::PlayStation,kB).caption=="Square");
    m.prompts=Mapping::Prompts::kOriginal;
    for(int a=0;a<kActionCount;++a)assert(resolve(m,Family::PlayStation,a).original());
    m.prompts=Mapping::Prompts::kKeyboard;m.keys[kA]={kNoKey,key_from_id("Space")};
    assert(resolve(m,Family::Xbox,kA).caption=="Space");
    m.keys[kA]={kNoKey,kNoKey};assert(resolve(m,Family::Xbox,kA).caption=="None");
    for(int p=0;p<5;++p){m.prompts=Mapping::Prompts(p);Mapping loaded;assert(from_json(to_json(m),loaded));assert(loaded==m);}
    Mapping old;assert(from_json("{\"options\":{\"stick_deadzone\":0.15}}",old));
    assert(old.prompts==Mapping::Prompts::kAuto&&old.pad==bindings&&old.deadzone==0.15f);
    Tracker t;t.add({9,Family::Xbox,true,"Virtual Xbox"});assert(t.active()->id==9);
    t.add({2,Family::PlayStation,false,"DualSense"});assert(t.active()->id==2);
    t.activity(9);assert(t.active()->id==2); // virtual duplicate cannot hide reliable physical identity
    t.add({3,Family::Xbox,false,"Series X"});t.activity(3);assert(t.active()->family==Family::Xbox);
    const auto gen=t.generation();t.activity(3);assert(t.generation()==gen);
    t.activity(2);assert(t.active()->family==Family::PlayStation);
    t.remove(2);assert(t.active()->id==3);t.remove(3);assert(t.active()->id==9);
    t.remove(9);assert(!t.active());t.add({4,Family::Nintendo,false,"Switch Pro"});assert(t.active()->family==Family::Nintendo);
    assert(style(Mapping::defaults(),Family::Unknown)==Style::Original);
    assert(resource_rule("font_02.bti").action==kX&&resource_rule("font_03.bti").action==kY);
    assert(resource_rule("font_06.bti").action==kZL&&resource_rule("font_59.bti").action==kZR);
    assert(resource_rule("tr_a_btn_00.bti").badge==HudBadge::Face);
    assert(resource_rule("tr_b_btn_03.bti").action==kB&&resource_rule("tr_b_btn_03.bti").dimmed);
    assert(resource_rule("tr_l_btn_01.bti").badge==HudBadge::Bumper);
    assert(resource_rule("tr_zr_btn_01.bti").action==kZR&&resource_rule("tr_zr_btn_01.bti").badge==HudBadge::Trigger);
    assert(resource_rule("tr_cross_btn.bti").action<0);
    assert(resource_rule("some/icon/font_40.bti").action==kB);
    for(const char* unrelated:{"font_13.bti","font_54.bti","font_55.bti","tt_zelda_button_ab_maru.bti","mybutton_a_text.bti"})assert(resource_rule(unrelated).action<0);
    puts("button_prompts_test: families, hotplug, virtual duplicates, actual bindings, overrides and persistence passed");
}
