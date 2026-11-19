#include "overlay/controller_navigation.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include "imgui.h"
struct ScrollResult { float before, after; int presses; };
static ScrollResult scroll_menu(float raw, bool corrected) {
    auto* context = ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;io.DisplaySize = {800,600};io.DeltaTime = 1.0f/60;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad | ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->AddFontDefault();
    ScrollResult result{};
    for (int f = 0; f < 185; ++f) {
        const float value = f >= 5 ? raw : 0;
        const auto nav = corrected ? overlay::navigation_input(value, true, 0.15f)
                                   : overlay::NavigationInput{value > 0.5f, value};
        io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown,nav.down,nav.value);
        ImGui::NewFrame();
        ImGui::SetNextWindowSize({400,250});ImGui::Begin("Settings");
        if (f == 0) ImGui::SetWindowFocus();
        if (f < 4) ImGui::SetScrollY(200);
        if (f == 4) result.before = ImGui::GetScrollY();
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadLStickDown,true)) ++result.presses;
        for (int i = 0; i < 100; ++i) ImGui::Text("Setting %d",i);
        result.after = ImGui::GetScrollY();
        ImGui::End();ImGui::Render();
    }
    ImGui::DestroyContext(context);return result;
}
int main() {
    using overlay::navigation_input;
    // Measured idle Series X values, even with the gameplay dead zone set to zero.
    for (float drift : {0.0f, 1269/32768.0f, 606/32768.0f, 926/32768.0f, 0.15f}) {
        auto n = navigation_input(drift, true, 0);
        assert(!n.down && n.value == 0); // no analog scrolling and no navigation/repeat event
    }
    assert(navigation_input(0.2f, true, 0.25f).value == 0); // honor larger user dead zones
    auto small = navigation_input(0.3f, true, 0.15f);
    assert(!small.down && small.value > 0); // deliberate gentle scrolling
    assert(!navigation_input(0.5f, true, 0.15f).down);
    assert(navigation_input(0.51f, true, 0.15f).down); // original selection/repeat threshold
    float previous = 0;
    for (int i = 16; i <= 100; ++i) {
        auto n = navigation_input(i/100.0f, true, 0.15f);
        assert(n.value > previous && n.value <= 1);previous = n.value;
    }
    assert(std::abs(navigation_input(1, true, 0.15f).value - 1) < 1e-6f);
    assert(navigation_input(1, false, 0.9f).down && navigation_input(1, false, 0.9f).value == 1);
    assert(navigation_input(0, false, 0.15f).value == 0);
    assert(navigation_input(std::numeric_limits<float>::quiet_NaN(), true, 0.15f).value == 0);
    // Exercise real ImGui: the old path scrolls with Down=false from the measured idle left Y.
    const auto old = scroll_menu(1646/32768.0f,false);
    const auto fixed = scroll_menu(1646/32768.0f,true);
    const auto deliberate = scroll_menu(0.7f,true);
    printf("ImGui: idle scroll %.0f -> %.0f old; %.0f -> %.0f corrected; deliberate repeat %d presses\n",
           old.before,old.after,fixed.before,fixed.after,deliberate.presses);
    assert(old.after > old.before && old.presses == 0);
    assert(fixed.after == fixed.before && fixed.presses == 0);
    assert(deliberate.after > deliberate.before && deliberate.presses > 2);
    puts("controller_navigation_test: idle drift, dead zones, deliberate scrolling and repeat threshold passed");
}
