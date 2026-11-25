# Windows setup release checklist

Build the setup ZIP from placeholder guest sources. Use a fresh build directory and stage the ZIP
outside the working game installation.

## Build the game-free ZIP

Use the Vulkan headers and import archive preparation from the [Windows development guide](windows.md#build-and-test-the-runtime).
Use the LLVM-MinGW version pinned in [toolchains.json](../tools/installer/toolchains.json).
Download the official archive, compare its SHA-256 against that file and put the verified toolchain's
bin directory first on PATH. Use the same toolchain for the SDK and the installer smoke build.
Download and verify the official Windows embeddable Python ZIP listed in the same file.

Use the upstream [MinGW-w64 runtime notice](https://github.com/mingw-w64/mingw-w64/blob/57b595039040eaa15bece85b7cc71d952281b269/COPYING.MinGW-w64-runtime/COPYING.MinGW-w64-runtime.txt) for the pinned toolchain and the [OpenSSL 3.5.9 licence](https://github.com/openssl/openssl/blob/openssl-3.5.9/LICENSE.txt) for the embedded Python's SSL libraries.

The following PowerShell example assumes the prepared Vulkan SDK paths are under $vk, the verified
LLVM-MinGW is unpacked to $llvm and the pinned embeddable Python ZIP is at $pythonZip.
Set $mingwLicense and $opensslLicense to the downloaded notice files.
Set `$version` to the source tag that will accompany the installer. Use forward slashes for paths
passed in CMake cache arguments.

~~~powershell
$repo = (Get-Location).Path
$version = 'v0.1.0-windows-setup.1'
$llvm = $llvm.Replace([char]92, '/')
$vk = $vk.Replace([char]92, '/')
$build = Join-Path $repo 'build/setup-release'
$gen = Join-Path $repo 'build/setup-placeholder'
$dist = Join-Path $repo 'build/setup-dist'
if ((Test-Path $build) -or (Test-Path $gen) -or (Test-Path $dist)) {
    throw 'Use fresh release build and output directories.'
}

python -B tools/recomp/stubgen.py $gen
if ($LASTEXITCODE) { throw 'Stub generation failed' }
cmake -S . -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DGEN_DIR=$gen" `
    "-DWWHD_VERSION=$version" -DWWHD_RENDERER=VULKAN -DWWHD_BUNDLED_DEPS=ON -DWWHD_SETUP_GUI=ON `
    "-DCMAKE_C_COMPILER=$llvm/bin/x86_64-w64-mingw32-clang.exe" `
    "-DCMAKE_CXX_COMPILER=$llvm/bin/x86_64-w64-mingw32-clang++.exe" `
    "-DCMAKE_RC_COMPILER=$llvm/bin/x86_64-w64-mingw32-windres.exe" `
    "-DCMAKE_EXE_LINKER_FLAGS=-static" "-DVulkan_INCLUDE_DIR=$vk/include" "-DVulkan_LIBRARY=$vk/libvulkan-1.a"
if ($LASTEXITCODE) { throw 'Configuration failed' }
ninja -C $build -d keeprsp -j 2 wwhd wwhd-setup wwhd-extract
if ($LASTEXITCODE) { throw 'Build failed' }
python -B tools/installer/test_setup.py
if ($LASTEXITCODE) { throw 'Installer tests failed' }

$deps = Join-Path $build '_deps'
python -B tools/release/package.py --build $build --platform windows-x86_64 --version $version `
    --toolchain llvm-mingw-20260922 --out $dist --setup-gui "$build/wwhd-setup.exe" `
    --windows-python $pythonZip --runtime-file "$build/SDL3.dll" `
    --license "SDL3 (zlib)=$deps/sdl3-src/LICENSE.txt" `
    --license "Vulkan-Headers (Apache-2.0)=$vk/LICENSE.md" `
    --license "glslang (BSD-3-Clause and others)=$deps/glslang-src/LICENSE.txt" `
    --license "zlib (zlib)=$deps/zlib-src/LICENSE" `
    --license "LZ4 (BSD-2-Clause)=$deps/lz4-src/lib/LICENSE" `
    --license "HIDAPI (BSD-3-Clause)=$deps/sdl3-src/src/hidapi/LICENSE-bsd.txt" `
    --license "SDL yuv2rgb (BSD-3-Clause)=$deps/sdl3-src/src/video/yuv2rgb/LICENSE" `
    --license "LLVM libc++ (Apache-2.0 WITH LLVM-exception)=$llvm/LICENSE.TXT" `
    --license "MinGW-w64 runtime=$mingwLicense" `
    --license "OpenSSL (Apache-2.0)=$opensslLicense"
if ($LASTEXITCODE) { throw 'Packaging failed' }

$zip = Join-Path $dist "TwilightPrincessHD-$version-windows-x86_64.zip"
python -B tools/release/guard.py --windows-setup $zip
if ($LASTEXITCODE) { throw 'Package audit failed' }
$asset = Join-Path $dist 'TwilightPrincessHD-Windows-Setup-x64.zip'
if (Test-Path $asset) { throw 'An asset already exists in this output directory.' }
Move-Item -LiteralPath $zip -Destination $asset
Get-FileHash -Algorithm SHA256 $asset
~~~

Keep the build inputs and log with the audit. The packager checks the Ninja guest target against
fresh stub output, leaves the guest archive out of the SDK and validates the final ZIP layout and
bundled Python hashes. Review the provenance of the allowed executable and library files.
Publish the exact audited ZIP and its SHA-256 with the corresponding MPL-2.0 source tag.

## Validate before publishing

- Unzip the audited ZIP into an empty folder whose path contains spaces. Run the packaged launcher
  with `SDL_VIDEO_DRIVER=offscreen` and `--self-test`. The launcher should exit successfully
  after receiving the bundled Python's hello. Set a separate data folder for tests with `--data-dir`.
- In that clean copy, run `tools/Setup in a console window.bat` with `--yes --gen-dir` pointing at
  the generated placeholders and `--no-launch`. Verify the built executable exists and `install.json`
  says `placeholder_code` is true. This checks the pinned compiler and real SDK recipe without any game.
- Check unsupported inputs. Then use a separate extracted ZIP and destination for one complete
  installation from a verified local USA Rev 2 copy. Confirm direct launch, Shift reopening, repair
  and save preservation. Check optional GameCube ISO selection and idle playback, then clear the
  selection and confirm the normal Wii U loop. Record the environments and controller configurations
  actually tested.
- Keep this as a separate release and leave the 20th Anniversary source release available.
  Keep the locally generated executable, extracted files, keys and validation logs off the release.
