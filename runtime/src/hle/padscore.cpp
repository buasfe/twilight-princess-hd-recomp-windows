// padscore: Wii Remote / Pro Controller (WPAD, KPAD). Only a Pro Controller on channel 0 exists,
// and only while the keyboard/host controllers are set to act as one (Input menu). Struct layouts
// and constants follow Cemu's padscore.
#include "../crashrec.h"
#include "../runtime.h"
#include "../input.h"
#include "../motion/motion.h"
#include "../rumble.h"
#include "padscore.h"

namespace {
constexpr int32_t kWpadErrNone = 0, kWpadErrNoController = -1;
constexpr int32_t kKpadErrNone = 0, kKpadErrNoController = -2;
constexpr uint8_t kDevURCC = 31, kDevNone = 253, kFormatURCC = 22;

bool connected(uint32_t chan) { return chan == 0 && input::pro_controller(); }

// VPAD button bits (input::PadState) -> Pro Controller (URCC) bits
uint32_t pro_buttons(uint32_t v) {
    static const uint32_t map[][2] = {
        {input::kUp, 0x1},     {input::kLeft, 0x2},    {input::kZR, 0x4},     {input::kX, 0x8},
        {input::kA, 0x10},     {input::kY, 0x20},      {input::kB, 0x40},     {input::kZL, 0x80},
        {input::kR, 0x200},    {input::kPlus, 0x400},  {input::kHome, 0x800}, {input::kMinus, 0x1000},
        {input::kL, 0x2000},   {input::kDown, 0x4000}, {input::kRight, 0x8000},
        {input::kStickR, 0x10000}, {input::kStickL, 0x20000},
    };
    uint32_t out = 0;
    for (auto& m : map)
        if (v & m[0]) out |= m[1];
    return out;
}
}  // namespace

HLE(padscore, KPADInit) {}
HLE(padscore, KPADInitEx) {}
HLE(padscore, KPADShutdown) {}
HLE(padscore, KPADEnableDPD) {}
HLE(padscore, KPADDisableDPD) {}
HLE(padscore, KPADEnableMpls) {}
HLE(padscore, KPADDisableMpls) {}
namespace {
uint32_t g_connect_cb[4] = {};
bool g_connect_pending = false, g_connected = false, g_connect_busy = false;
uint32_t g_last_hold = 0;
}

namespace padscore_hle {
void service_connect(Cpu* c) {
    if (g_connect_busy) return;  // callbacks may read the pads themselves
    const bool on = connected(0);
    if (on != g_connected) {
        g_connected = on;
        g_connect_pending = true;
        g_last_hold = 0;
    }
    if (!g_connect_pending) return;
    g_connect_pending = false;
    if (const uint32_t cb = g_connect_cb[0]) {
        g_connect_busy = true;
        LOG("[pad] KPAD connect callback %08X: channel 0 %s", cb, on ? "connected" : "disconnected");
        // guest_call preserves the stack/LR but clobbers volatile argument registers.
        // An asynchronous connection event must preserve the interrupted HLE caller.
        const Cpu saved = *c;
        guest_call(c, cb, {0u, (uint32_t)(on ? kWpadErrNone : kWpadErrNoController)});
        const uint32_t core = c->core;
        void* thread = c->thread;
        *c = saved;
        c->core = core;
        c->thread = thread;
        g_connect_busy = false;
    }
}
}

HLE(padscore, KPADSetConnectCallback) {
    const uint32_t chan = arg(c, 0), cb = arg(c, 1);
    uint32_t previous = 0;
    if (chan < 4) {
        previous = g_connect_cb[chan];
        g_connect_cb[chan] = cb;
        if (chan == 0 && cb && connected(chan)) g_connect_pending = true;
    }
    ret(c, previous);
}
HLE(padscore, KPADGetMplsWorkSize) { ret(c, 0x5FE0); }
HLE(padscore, KPADSetMplsWorkarea) {}
HLE(padscore, WPADEnableURCC) {}
HLE(padscore, WPADEnableWiiRemote) {}
HLE(padscore, WPADDisconnect) {}
// The Pro Controller motor: cmd 0 stops it, 1 runs it until the game stops it. The Pro
// Controller is only connected while the keyboard/controllers act as one (see connected).
HLE(padscore, WPADControlMotor) {
    uint32_t chan = arg(c, 0), cmd = arg(c, 1);
    if (connected(chan)) {
        TRACE("[pad] WPADControlMotor(%u, %u) -> %s", chan, cmd, cmd ? "rumble" : "stop");
        rumble::pro_motor(chan, cmd != 0);
    }
    ret(c, (uint32_t)kWpadErrNone);
}
HLE(padscore, WPADGetBatteryLevel) { ret(c, 4); }  // full
HLE(padscore, WPADCanSendStreamData) { ret(c, 0); }
HLE(padscore, WPADSendStreamData) { ret(c, (uint32_t)kWpadErrNoController); }
HLE(padscore, WPADControlSpeaker) { ret(c, (uint32_t)kWpadErrNoController); }

HLE(padscore, WPADProbe) {
    // (chan, uint32* type) -> WPAD error
    uint32_t chan = arg(c, 0), type = arg(c, 1);
    bool on = connected(chan);
    TRACE("[pad] WPADProbe(%u) -> %s", chan, on ? "pro" : "none");
    if (type) st32(type, on ? kDevURCC : kDevNone);
    ret(c, (uint32_t)(on ? kWpadErrNone : kWpadErrNoController));
}

HLE(padscore, KPADReadEx) {
    // (chan, KPADStatus* buf, count, int32* err) -> samples written
    uint32_t chan = arg(c, 0), st = arg(c, 1), count = arg(c, 2), err = arg(c, 3);
    padscore_hle::service_connect(c);
    TRACE("[pad] KPADReadEx(%u, %08X, %u) connected=%d", chan, st, count, connected(chan));
    if (!connected(chan) || !st || (int32_t)count <= 0) {
        if (err) st32(err, (uint32_t)(connected(chan) ? kKpadErrNone : kKpadErrNoController));
        ret(c, 0);
        return;
    }
    input::PadState p = crashrec::read(1);  // crash recovery records/replays it
    motion::right_stick(p.rx, p.ry);  // the Pro Controller's stick decides gyro use too (motion.h)
    uint32_t hold = pro_buttons(p.buttons);
    memset(mem::ptr(st), 0, 0xF0);
    st8(st + 0x5C, kDevURCC);     // devType
    st8(st + 0x5D, 0);            // wpadErr
    st8(st + 0x5F, kFormatURCC);  // data_format
    st32(st + 0x60, hold);                // ex_status.uc.hold
    st32(st + 0x64, hold & ~g_last_hold); // trig
    st32(st + 0x68, g_last_hold & ~hold); // release
    g_last_hold = hold;
    stf32(st + 0x6C, p.lx); stf32(st + 0x70, p.ly);  // lstick
    stf32(st + 0x74, p.rx); stf32(st + 0x78, p.ry);  // rstick
    st32(st + 0x7C, 1);                   // charge
    st32(st + 0x80, 1);                   // cable
    if (err) st32(err, kKpadErrNone);
    ret(c, 1);
}
