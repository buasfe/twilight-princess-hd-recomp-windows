#pragma once
struct Cpu;
namespace padscore_hle {
// Deliver connection changes from a guest pad read, never from the UI thread.
void service_connect(Cpu* c);
}
