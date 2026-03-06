# Q3VIBE Engine

Q3VIBE Engine is an ultra-developed Quake 3 engine fork built on top of Quake3e. The repository now uses a Meson/Ninja build as the active build system, with the imported upstream documentation and legacy build entry points preserved under `archive/`.

## Project Features So Far

- Modern build tools
- SDL3

## Project Goals

- Use Quake3e as a practical upstream base for ongoing engine work.
- Expand the engine with more ambitious rendering, platform, tooling, and runtime improvements.
- Treat full Windows, Linux, and macOS support as a core engineering requirement, not a later portability pass.
- Keep Meson/Ninja, dependency wrappers, CI, and editor tooling aligned so cross-platform support stays continuously buildable and testable.
- Preserve useful Quake 3 compatibility where it does not block forward development.
- Keep upstream imports understandable by separating archived upstream assets from active Q3VIBE-facing project files.

## Build

Verified locally today:

- Windows x86_64 with `clang-cl`, `lld-link`, Meson, and Ninja.
- Linux x86_64 with GCC, Meson, and Ninja through the SDL3 platform layer.

Supported build contract:

- Windows x86_64 uses the native Win32 platform layer.
- Linux x86_64 uses the SDL platform layer.
- macOS x86_64 and aarch64 are first-class Meson targets through the SDL platform layer and are expected to stay green in CI.

### Windows x86_64

Debug:

```powershell
meson setup build\windows-clangcl-debug --native-file toolchains\windows-clangcl.ini --buildtype debug
meson compile -C build\windows-clangcl-debug
```

Release:

```powershell
meson setup build\windows-clangcl-release --native-file toolchains\windows-clangcl.ini --buildtype release
meson compile -C build\windows-clangcl-release
```

Outputs:

- `build/windows-clangcl-debug/quake3e.x64.exe`
- `build/windows-clangcl-debug/quake3e.ded.x64.exe`
- `build/windows-clangcl-debug/quake3e_opengl_x86_64.dll`
- `build/windows-clangcl-debug/quake3e_vulkan_x86_64.dll`

### Linux x86_64

Prerequisites: `libsdl3-dev` or the vendored SDL3 wrapper toolchain dependencies, `libcurl4-openssl-dev`, OpenGL/Vulkan runtime support, Meson, Ninja, and Python 3.

Debug:

```bash
meson setup build/linux-debug --buildtype debug
meson compile -C build/linux-debug
```

Release:

```bash
meson setup build/linux-release --buildtype release
meson compile -C build/linux-release
```

Outputs:

- `build/linux-debug/quake3e.x64`
- `build/linux-debug/quake3e.ded.x64`
- `build/linux-debug/quake3e_opengl_x86_64.so`
- `build/linux-debug/quake3e_vulkan_x86_64.so`

### macOS

Prerequisites: Xcode Command Line Tools, Homebrew `sdl3`, Homebrew `curl`, Meson, Ninja, and Python 3.

Debug:

```bash
meson setup build/macos-debug --buildtype debug
meson compile -C build/macos-debug
```

Release:

```bash
meson setup build/macos-release --buildtype release
meson compile -C build/macos-release
```

Expected outputs:

- Intel macOS: `quake3e.x86_64`, `quake3e.ded.x86_64`, `quake3e_opengl_x86_64.dylib`, `quake3e_vulkan_x86_64.dylib`
- Apple Silicon macOS: `quake3e.aarch64`, `quake3e.ded.aarch64`, `quake3e_opengl_aarch64.dylib`, `quake3e_vulkan_aarch64.dylib`

## Repository Layout

- `code/`: engine, renderer, platform, and game-facing source imported from Quake3e.
- `meson.build`, `meson.options`: active top-level build definition.
- `subprojects/`: Meson dependency wrappers and vendored external dependency trees for `libjpeg`, `libogg`, `libvorbis`, `libcurl`, and SDL3, following the same fallback-driven layout used in `../WORR/`.
- `subprojects/sdl3/`: the active SDL3 Meson wrapper.
- `subprojects/sdl3-upstream/`: the vendored SDL 3.4.2 source tree consumed by the SDL3 wrapper.
- `toolchains/`: checked-in Meson native files.
- `build-support/meson/sources/meson.build`: generated source manifest used by Meson for engine sources only.
- `scripts/`: Meson maintenance helpers, including source-list and GLSL string generation.
- `.github/workflows/build.yml`: cross-platform Meson CI for Windows, Linux, and macOS runners.
- `archive/documents/`: archived upstream documentation.
- `archive/buildsystems/upstream/`: archived upstream CMake, Make, and Visual Studio build files.

## Licensing

Q3VIBE is distributed under GPLv3. The imported Quake III / Quake3e source headers remain `GPL-2.0-or-later`, which is compatible with GPLv3 distribution. The full GPLv3 text is in `LICENSE`.

Third-party bundled components keep their own notices under the imported source tree and archived upstream documents.

## Upstream Credits

Q3VIBE Engine is based on [Quake3e](https://github.com/ec-/Quake3e), imported from upstream commit `46add7d` from `upstream/main` on March 6, 2026.

Quake3e is itself derived from the Quake III Arena source release by id Software. The original upstream README, build notes, and license text are preserved in `archive/documents/`.
