# Windows x86-64 development build

Use Windows 11, an x86-64 LLVM-MinGW UCRT toolchain, CMake, Ninja and Python 3. Put
`clang`, `clang++`, `windres`, `llvm-dlltool`, `cmake`, `ctest`, `ninja` and `python`
on PowerShell's `PATH`. The recompiled guest needs Clang's `musttail` support.
The GPU needs a Vulkan 1.3 driver or Vulkan 1.1/1.2 with `VK_KHR_dynamic_rendering`.

The smoke build used LLVM-MinGW UCRT 20261020 (Clang 23.1.4), CMake
4.3.1-msvc1, Ninja 1.13.2 and Python 3.13.5.

Run these checks from the repository root.

```powershell
$repo = (Get-Location).Path
git rev-parse HEAD
clang --version       # target should be x86_64-w64-windows-gnu
cmake --version
ninja --version
python --version
vulkaninfo --summary  # if installed
```

## Build and test the runtime

These commands download pinned build inputs and write under `build/`. They use
Khronos headers and create a MinGW import archive to satisfy CMake's Vulkan
discovery, as in the [Wind Waker HD Windows build](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/blob/main/.github/workflows/release.yml).
SDL loads the driver-supplied `vulkan-1.dll` at runtime.

```powershell
New-Item -ItemType Directory -Force build/deps | Out-Null
$vkTag = 'vulkan-sdk-1.4.350.1'
$archive = 'build/deps/vulkan-headers.tar.gz'
curl.exe -fLsS "https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/$vkTag.tar.gz" -o $archive
if ($LASTEXITCODE -or (Get-FileHash $archive).Hash -ne '6D1BB65E49520344CC0A48AF3DC02E993781EFFF14C7EBDCB8AE9FA23DDF7E83') {
    throw 'Vulkan-Headers download or checksum failed'
}
tar -xf $archive -C build/deps
if ($LASTEXITCODE) { throw 'Vulkan-Headers extraction failed' }
$vk = (Resolve-Path "build/deps/Vulkan-Headers-$vkTag").Path
$def = Join-Path $vk 'vulkan-1.def'
curl.exe -fLsS "https://raw.githubusercontent.com/KhronosGroup/Vulkan-Loader/$vkTag/loader/vulkan-1.def" -o $def
if ($LASTEXITCODE -or (Get-FileHash $def).Hash -ne '9BA339B7F5EE2DF28487698A6840ECF095FAC58B415C2C89AD9F161E9D316378') {
    throw 'Vulkan loader definition download or checksum failed'
}
llvm-dlltool -m i386:x86-64 -d $def -l "$vk/libvulkan-1.a"
if ($LASTEXITCODE) { throw 'Vulkan import archive build failed' }

$options = @(
    '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release'
    '-DCMAKE_C_COMPILER=clang', '-DCMAKE_CXX_COMPILER=clang++'
    '-DWWHD_RENDERER=VULKAN', '-DWWHD_BUNDLED_DEPS=ON'
    "-DVulkan_INCLUDE_DIR=$vk/include", "-DVulkan_LIBRARY=$vk/libvulkan-1.a"
)
python tools/recomp/stubgen.py build/gen-stub
if ($LASTEXITCODE) { throw 'Stub generation failed' }
cmake -S . -B build/windows-stub @options "-DGEN_DIR=$repo/build/gen-stub"
if ($LASTEXITCODE) { throw 'Configuration failed' }
cmake --build build/windows-stub --parallel 2
if ($LASTEXITCODE) { throw 'Build failed' }
ctest --test-dir build/windows-stub --output-on-failure
if ($LASTEXITCODE) { throw 'CTest failed' }
python tools/installer/test_setup.py
if ($LASTEXITCODE) { throw 'Installer tests failed' }
```

CMake fetches pinned SDL3, glslang, zlib, LZ4 and zstd sources on the first
configure and copies `SDL3.dll` next to `wwhd.exe`. Keep the LLVM-MinGW `bin`
directory on `PATH` for its runtime DLLs. The commands use two build jobs.
Adjust the job count to suit your available memory and workload.

Run the stub smoke test in a local Windows desktop session. The Vulkan log
identifies the selected GPU. A successful run exits with code 0 and reports
the results of upload, readback, shader, depth/stencil and swapchain checks.
Game code generation and game boot are tested in the next section.

```powershell
& ./build/windows-stub/wwhd.exe --renderer-smoke 2>&1 | Tee-Object build/windows-smoke.log
$LASTEXITCODE
```

For a validation run, make `VK_LAYER_KHRONOS_validation` available through the
Vulkan SDK or a local Khronos layer directory. Set `VK_LAYER_PATH` to the
directory containing its JSON manifest if the loader does not already discover
it. Close graphics overlays such as RivaTuner Statistics Server for this run.
Its global hook can intercept Vulkan calls even with its implicit layer disabled.

```powershell
$env:WWHD_VK_VALIDATION = '1'
$env:VK_LAYER_VALIDATE_SYNC = '1'
& ./build/windows-stub/wwhd.exe --renderer-smoke 2>&1 | Tee-Object build/windows-smoke-validation.log
$LASTEXITCODE
Select-String -Path build/windows-smoke-validation.log -Pattern 'validation error|VUID-|SYNC-HAZARD'
Remove-Item Env:WWHD_VK_VALIDATION, Env:VK_LAYER_VALIDATE_SYNC
```

## Build from your extracted disc

Use your own extracted USA Wii U disc (title ID `00050000-1019E500`, Rev 2,
version 81). The folder must contain `code/Zelda.rpx`, `content/` and `meta/`.
Configure the game in a separate build directory in the same PowerShell session,
using the `$options` above. The build commands check the RPX hash locally.

For a `.wud` or `.wux` dump, the stub build includes an extractor. Supply the
paths to your own key files and a new output folder. Use that output folder as
`$game` in the next block.

```powershell
$image = 'D:\path\to\your-disc.wux'
$discKey = 'D:\path\to\your-disc.key'
$commonKey = 'D:\path\to\your-common.key'
& ./build/windows-stub/wwhd-extract.exe --disc-key $discKey --common-key $commonKey extract $image build/extracted
if ($LASTEXITCODE) { throw 'Disc extraction failed' }
```

A Wii U archive of your disc (`.wua`) can be extracted by title ID. Cemu 2.6
can create this archive from its Title Manager using **Convert to compressed
Wii U archive (.wua)** on the base game. Close Cemu after exporting.

```powershell
$archive = 'D:\path\to\your-disc.wua'
& ./build/windows-stub/wwhd-extract.exe --title 000500001019e500 --progress extract $archive build/extracted
if ($LASTEXITCODE) { throw 'Archive extraction failed' }
```

```powershell
$game = 'D:\path\to\extracted-twilight-princess-hd'
$rpx = Join-Path $game 'code/Zelda.rpx'
if ((Get-FileHash -LiteralPath $rpx -Algorithm SHA256).Hash -ne '28922198517578C9261AC0B0B04A586252033D3BBCF7980B79E37638775B12A9') {
    throw 'Expected USA disc Rev 2 Zelda.rpx'
}
python tools/recomp/recomp.py $rpx build/gen
if ($LASTEXITCODE) { throw 'Recompilation failed' }
cmake -S . -B build/windows-game @options "-DGEN_DIR=$repo/build/gen"
if ($LASTEXITCODE) { throw 'Configuration failed' }
cmake --build build/windows-game --target wwhd --parallel 2
if ($LASTEXITCODE) { throw 'Build failed' }

$env:WWHD_INTERP = '0'
$env:WWHD_ASPECT = '16:9'
$env:WWHD_RES_SCALE = '1'
& ./build/windows-game/wwhd.exe --game $game --save "$repo/build/windows-test-save" 2>&1 |
    Tee-Object build/windows-game.log
Remove-Item Env:WWHD_INTERP, Env:WWHD_ASPECT, Env:WWHD_RES_SCALE
```

This launch uses a separate test save directory. A source build normally stores
settings and caches under `%APPDATA%\TPHD`. To isolate those too, create an empty
`portable.txt` next to the game executable before the first run. It uses
`build/user/` with the paths above.

Record whether the SDL windows open, the RTX 3070 is selected, the logos and title
screen appear, and a new Quest Log reaches the opening area. At that point, check
that Link moves, audio plays, the GamePad view works, and resizing and fullscreen
(`F11` or `Alt+Enter`) work. Check Xbox or DualSense input and reconnect if you have
one. Use 16:9 at 1x first. Check ultrawide and optional interpolation after the
baseline works. For a failure, keep the launch log, any `captures/crash-*.log`
from the working directory, the GPU and driver line, and a screenshot of the
last visible frame if applicable. Keep the RPX, assets, generated code, saves and
caches local.

For a repeatable benchmark, save in **Collection > Save Game** and make a
separate save state with **Shift+F1** after positioning Link and the camera.
Standard Quest Logs and the Windows LZ4 save state both reloaded in fresh
processes in the Ordon test. To load state slot 1 automatically, set
`WWHD_STATE_DIR` to its folder and `WWHD_STATE_LOAD_AT=60:1` before launch.
Check the log for `Loaded slot 1` before measuring, and use the same executable
for comparisons.

## Source release scope

The Windows preview provides source archives. The placeholder build checks the toolchain and
renderer. The playable executable links code generated from your local input. Keep that
executable, the generated files and all game data private. [Windows preview release notes](windows-preview-release.md)
