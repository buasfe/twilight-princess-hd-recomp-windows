# Twilight Princess HD: Recompiled — Windows Edition

**Windows Preview — 20th Anniversary**

An experimental Windows x86-64 and Vulkan source port maintained by [buasfe](https://github.com/buasfe). It builds on [AsadSi’s Twilight Princess HD recompilation](https://github.com/AsadSi/twilight-princess-hd-recomp), which targeted Apple Silicon and Metal, and the [ZeldaWWHDRecomp](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp) recompiler, Cafe OS runtime and graphics infrastructure. The recompiler translates your own Wii U executable locally and links it to the native runtime.

## What has been tested

- Native Windows 11 gameplay with Vulkan on an NVIDIA GeForce RTX 3070, including 21:9 ultrawide and 2× internal resolution.
- Xbox Series controller input in both Wii U GamePad and Pro Controller modes. F1 **Controls** manages controller mode, bindings and stick dead zones. The Windows preview includes fixes for Pro mode reconnect handling, gameplay input after closing F1 and idle scrolling in the settings overlay.
- Standard save and reload, audio, HUD, area transitions and extended play in Ordon. The original 30 Hz gameplay clock is available. The 60, 120 and 240 FPS options interpolate presentation from that clock when display and system performance permit.
- Windows x86-64 is the tested platform for this preview. The source retains the upstream macOS backend.

This preview has limited hardware and progression coverage. Intermittent UI or text animation issues have been reported with high-FPS presentation, along with occasional white flicker. Use the original 30 Hz presentation as a comparison when reporting either issue.

## Requirements

- Windows 11 x86-64 and a Vulkan 1.3 driver, or Vulkan 1.1/1.2 with `VK_KHR_dynamic_rendering`.
- An x86-64 LLVM-MinGW UCRT toolchain, CMake 3.20 or later, Ninja, Python 3 and Git. The build uses Clang's `musttail` support. CMake can build pinned SDL3, glslang, zlib, LZ4 and zstd dependencies.
- Your own compatible Wii U game dump, extracted locally with `code/Zelda.rpx`, `content/` and `meta/`. The supported input is the **USA disc, Rev 2, version 81** (title ID `00050000-1019E500`). Support for European discs, other US revisions, updates and DLC has not been validated.

## Build from source

1. Follow the [Windows build guide](docs/windows.md) to prepare Vulkan headers and build the placeholder target. This can check the toolchain without game data.
2. Point the guide at your locally extracted game folder. The instructions verify the input revision by SHA-256, generate C locally and build the native executable.
3. Launch with `--game` pointing at the extracted folder. Set `--save` to a separate directory for preview sessions.

The release contains source only. Keep the extracted files, keys, generated C, linked game executable, shader caches and saves on your machine.

## Development branches

| Branch | Scope |
|---|---|
| `main` | Windows preview and verified controller fixes |
| `codex/controller-glyphs-source` | Unfinished mapping-aware Xbox and PlayStation prompts. Selected Quest Log and HUD contexts have been exercised. Broader dialogue and menu coverage and physical DualSense Auto detection remain open. |
| `codex/true-60-source` | Limited Ordon timing experiment with independent grounded movement and collision, horizontal camera updates and extra skeletal pose samples. The rest of the game continues on its legacy clock. |

## Report a bug

Open an issue in this repository with the source revision, Windows version, GPU and driver, controller mode, resolution and aspect ratio, and the steps to reproduce. For timing or graphics issues, include the presentation mode and whether the same issue occurs at 30 Hz. Check any log for private paths or tokens before posting it. A short redacted excerpt is usually enough.

## Credits and licence

The project source is licensed under the [Mozilla Public License 2.0](LICENSE). Vendored dependencies retain their own licence notices. The project is unofficial and has no affiliation with or endorsement from Nintendo. Nintendo’s game and artwork are outside the scope of this licence. [Full contributor acknowledgements](CREDITS.md)
