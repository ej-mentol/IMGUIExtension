# AGENTS.md

This file provides guidance and important rules working with code in this repository.

## When coding / building plan

- Use a progressive disclosure approach for agent coding in this repository: start from high-level information in the Basic Memory knowledge base first, and only locate/read specific files or symbols when necessary, instead of expanding a large amount of context at once.

### Basic Memory knowledge base (project-scoped, `memory/`)

- Notes live in `memory/` (markdown with YAML frontmatter: `title`/`type`/`permalink`), tracked in git.
- Notes use the `imguiextension/` permalink prefix to distinguish them from the source repository.

### High-level information in this repository (read corresponding notes first)

- Purpose, layout, lifecycle and consumer contract: `project_overview`
- Build commands, dependency resolution and verification status: `build_and_verification`
- Dispatcher/registration semantics and input capture state machine: `callback_contract`
- Font discovery, config validation, merge ordering and glyph ranges: `font_pipeline`
- Formatting toolchain, commands and check scope: `CppFormatting`

### When notes are insufficient: source entry points (query and read on demand)

- Build: `CMakeLists.txt`, `cmake/Sources.cmake` (explicit compile list), `cmake/Dependencies.cmake`, `build_plugin.ps1`
- Lifecycle entry: `src/plugins.cpp`; callback registry `src/Dispatcher.h`; frame/backend ownership `src/InputCapture.cpp`; VGUI2 bridge `src/VGUI2Callbacks.cpp`; fonts `src/FontAtlas.cpp`
- Public API: `IImGuiExtension.h` at the repository root. Sibling plugins include it and `thirdparty/imgui` by relative path, so both locations are a stable external contract.
- Tests: `tests/dispatcher_tests.cpp` and `tests/font_glyph_ranges_tests.cpp` via CTest; assertions stay enabled in Release (`/UNDEBUG`)
- MetaHook SDK is consumed, not built: `METAHOOK_SOURCE_PATH`, otherwise the latest `main` via FetchContent. `VGUI2EXTENSION_SOURCE_PATH` is needed only for `Interface/IVGUI2Extension.h`. SDL2 headers come from `SDL2_INCLUDE_DIRS` or MetaHook's `thirdparty/sdl2-compat/include`; SDL is never built or linked.
- Build output: `build/x86/<configuration>/`; install output: `install/x86/<configuration>/`. Neither is tracked, and nothing is deployed to the game automatically.

## Repository rules

- Preserve the plugin exports, interface version, callback ABI and runtime layout. Consumers depend on `IImGuiExtension.h`'s path, `IMGUI_EXTENSION_INTERFACE_VERSION` and the documented `SetCurrentContext` pattern.
- Keep `cmake/Sources.cmake` as the explicit compile list and keep the target named `IMGUIExtension`.
- Never subclass a window that does not belong to this process; the HWND arbitration is intentionally strict.
- Keep the ImGui context stable across video mode changes and backend re-initialization. Destroying and recreating the context would invalidate pointers consumers may hold.
- Unregistration must remain synchronous (`Shutdown()` before `UnregisterCallbacks()` returns); callbacks may free themselves immediately afterwards.
- Match the naming, indentation and comment style of the original files in `src/`. Do not reformat unrelated code.
- Font entries are validated before ImGui sees them (regular file, >100 bytes) and loaded with `ImFontFlags_NoLoadError`; merge sources must attach to the primary (first usable non-merge) font, not to whichever base was added last. Never eager-build the atlas: ImGui 1.92 bakes lazily and the backend owns the texture.
- Glyph-range pointers must outlive the font; combined ranges go through `src/FontGlyphRanges.h` (process-lifetime storage), never through a stack buffer.
- Do not add third-party submodules to this repository. Dear ImGui is bundled only because it is a public consumer path; RapidJSON comes from MetaHook's shared tree (`RAPIDJSON_SOURCE_PATH`), with a pinned FetchContent commit as the standalone fallback.
- Verification distinguishes compile, simulated dispatcher tests and an actual game run. Claims about rendering, input capture or per-engine compatibility require an in-game check.
