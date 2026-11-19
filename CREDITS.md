# Credits

- **[AsadSi and the Twilight Princess HD Recompiled contributors](https://github.com/AsadSi/twilight-princess-hd-recomp)** adapted the recompiler, runtime and rendering to the Wii U HD game and built the original Apple Silicon port.
- **[buasfe](https://github.com/buasfe)** maintains the independent Windows Edition and tested Windows gameplay and controllers.
- **[ZeldaWWHDRecomp](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp)**, the Wind Waker HD port this Twilight Princess HD port is forked from: the recompiler, Cafe OS layer, renderers, installer and almost everything else. Lukas S (lsch) and contributors: rhemfur (Rhener Furtado), Sean13128, Eduardo Serralvo Gil, Manoah B., depende3000, fda, arcadematicas, mhbxyz. MPL-2.0.
- **[Cemu](https://github.com/cemu-project/Cemu)** supplied Wii U GPU documentation and the MPL-2.0 Latte address library and shader decompiler in `runtime/third_party/cemu/`.
- **[zeldaret/tp](https://github.com/zeldaret/tp)**, the Twilight Princess decompilation: names and structures used throughout.

## How this was made

Asad's additions were developed with AI assistance from Claude. The Windows Edition used Codex assistance for development and testing.

## Source provenance

The Windows preview source is based on AsadSi's upstream history at `1f208ec` and the Windows controller baseline `723a832`. The original development history lives in the upstream repository and the maintainer's local Git branches. The preview starts with an audited source snapshot. [Upstream Twilight Princess HD history](https://github.com/AsadSi/twilight-princess-hd-recomp/commits/main/)

The `runtime/third_party/` folders retain the notices for Cemu, Dear ImGui, fmt, metal-cpp and xxHash. CMake fetches SDL3, glslang, zlib, LZ4 and zstd from their pinned upstream releases. Each dependency retains its own licence.
