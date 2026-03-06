# Q3VIBE Engine Agent Notes

## Build Baseline

- The active build system is Meson/Ninja.
- Cross-platform support is a core project goal. Do not introduce Windows-only assumptions into build logic, dependency wiring, renderer loading, or tooling without an explicit decision.
- Verified local targets today:
  - Windows x86_64 using `clang-cl` and `lld-link`
  - Linux x86_64 using GCC through the SDL platform layer
- Windows debug build:
  - `meson setup build\windows-clangcl-debug --native-file toolchains\windows-clangcl.ini --buildtype debug`
  - `meson compile -C build\windows-clangcl-debug`
- Windows release build:
  - `meson setup build\windows-clangcl-release --native-file toolchains\windows-clangcl.ini --buildtype release`
  - `meson compile -C build\windows-clangcl-release`
- Linux debug build:
  - `meson setup build/linux-debug --buildtype debug`
  - `meson compile -C build/linux-debug`
- Linux release build:
  - `meson setup build/linux-release --buildtype release`
  - `meson compile -C build/linux-release`
- macOS is part of the supported Meson contract through the SDL path:
  - `meson setup build/macos-debug --buildtype debug`
  - `meson compile -C build/macos-debug`
  - `meson setup build/macos-release --buildtype release`
  - `meson compile -C build/macos-release`
- When changing Meson files, platform code, or dependency wrappers, preserve Windows, Linux, and macOS support together. Update CI/editor tooling in the same change when needed.

## Source List Maintenance

- Meson source manifests are generated, not hand-maintained.
- The generator is `scripts/generate_meson_source_lists.py`.
- Regenerate after adding, removing, or renaming C or GLSL files:
  - `python scripts/generate_meson_source_lists.py`
- The generated output is `build-support/meson/sources/meson.build`.
- Vendored dependency source lists live in `subprojects/`, not in the generated engine manifest.

## Renderer Layout

- The client executable and renderer modules are built side by side in the Meson build directory.
- Verified Windows outputs:
  - `quake3e.x64.exe`
  - `quake3e.ded.x64.exe`
  - `quake3e_opengl_x86_64.dll`
  - `quake3e_vulkan_x86_64.dll`
- Verified Linux outputs:
  - `quake3e.x64`
  - `quake3e.ded.x64`
  - `quake3e_opengl_x86_64.so`
  - `quake3e_vulkan_x86_64.so`
- Expected macOS outputs:
  - `quake3e.x86_64` or `quake3e.aarch64`
  - `quake3e.ded.x86_64` or `quake3e.ded.aarch64`
  - `quake3e_opengl_x86_64.dylib` or `quake3e_opengl_aarch64.dylib`
  - `quake3e_vulkan_x86_64.dylib` or `quake3e_vulkan_aarch64.dylib`
- The OpenGL2 renderer is wired in Meson but disabled by default.

## Archived Upstream Build Files

- Upstream CMake, Make, and Visual Studio project files are archived under `archive/buildsystems/upstream/`.
- Do not add new root build entry points outside the Meson path unless explicitly requested.

## Dependency Wrappers

- External dependencies are resolved from the top-level build through `dependency(..., fallback: ...)`.
- Vendored wrappers live under `subprojects/` and mirror the WORR-style dependency contract instead of hard-coding `code/lib*` paths in the root `meson.build`.
- On Windows, `libjpeg`, `libogg`, `libvorbis`, and `libcurl` use vendored Meson wrappers.
- On Linux and macOS, SDL3 and libcurl resolve from the host environment first when available, with vendored wrappers remaining available where configured.
- The active SDL wrapper is `subprojects/sdl3/`, and its vendored upstream tree lives in `subprojects/sdl3-upstream/`.

## Licensing

- Top-level distribution is GPLv3 via `LICENSE`.
- Imported engine source headers remain `GPL-2.0-or-later`; keep that distinction intact when editing notices.

## Tooling

- VS Code tasks and debug launchers live in `.vscode/`.
- VS Code tasks should stay usable on Windows, Linux, and macOS.
- GitHub Actions should use the Meson toolchain, not the archived upstream build files, and should keep Windows, Linux, and macOS coverage in place.
