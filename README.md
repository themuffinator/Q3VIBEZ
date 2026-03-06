# Q3VIBE Engine

Q3VIBE Engine is an ultra-developed Quake 3 engine fork built on top of Quake3e. This repository starts from an imported Quake3e snapshot and reserves the top-level documentation for Q3VIBE-specific direction, while the original upstream docs live under `archive/documents/`.

## Project Goals

- Use Quake3e as a practical upstream base for ongoing engine work.
- Expand the engine with more ambitious rendering, platform, tooling, and runtime improvements.
- Preserve useful Quake 3 compatibility where it does not block forward development.
- Keep upstream imports and future merge work understandable by separating archived upstream docs from active project docs.

## Repository Layout

- `code/`: engine, renderer, and game-side source imported from Quake3e.
- `cmake_modules/`, `CMakeLists.txt`, `Makefile`: build system entry points.
- `archive/documents/`: archived documentation imported from the upstream Quake3e tree.

## Upstream Credits

Q3VIBE Engine is based on [Quake3e](https://github.com/ec-/Quake3e), imported from upstream commit `46add7d` from `upstream/main` on March 6, 2026.

Quake3e is itself derived from the Quake III Arena source release by id Software. The archived upstream README, build notes, and license text are preserved in `archive/documents/`.
