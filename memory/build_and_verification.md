---
title: build_and_verification
type: note
permalink: imguiextension/build-and-verification
tags:
- build
- cmake
- verification
---

# Build and verification

## Build structure

- One CMake tree per configuration: `build/x86/<Debug|Release>`, MSVC x86, C++20, static
  CRT (`/MT`, `/MTd`). `IMGUIEXTENSION_BUILD_TESTS` (default ON) adds the dispatcher test
  to CTest; assertions stay enabled with `/UNDEBUG` in both configurations.
- Source list lives in `cmake/Sources.cmake`: six plugin units, the HLSDK
  `interface.cpp` plumbing and the six Dear ImGui/backend units compiled into the DLL.
- The target is named `IMGUIExtension`, matching the directory, the plugin module name
  and MetaHook's `<Name>.dll` lookup. No `_AVX2` variant is produced.
- Install: `install/x86/<config>/svencoop/metahook/plugins/IMGUIExtension.{dll,pdb}`.
  `build_plugin.ps1` builds and tests both configurations and prints the install command;
  build artifacts stay under `build/`.

## Dependency resolution

| Variable | Fallback order |
| --- | --- |
| `METAHOOK_SOURCE_PATH` | caller → FetchContent `MetaHook` `origin/main` (headers only) |
| `VGUI2EXTENSION_SOURCE_PATH` | caller → sibling `../VGUI2Extension` → pinned FetchContent commit (header only) |
| `IMGUI_SOURCE_PATH` | caller → bundled `thirdparty/imgui` (default; public consumer path) |
| `RAPIDJSON_SOURCE_PATH` | caller → `<MetaHook>/thirdparty/rapidjson` → pinned FetchContent commit `24b5e7a8` (no copy is bundled here) |
| `SDL2_INCLUDE_DIRS` | caller → `<MetaHook>/thirdparty/sdl2-compat/include` (headers only) |
| `FORMAT_VALIDATION_SOURCE_PATH` | caller/env → pinned FetchContent tooling commit |

Nothing is downloaded when the paths are present; the aggregator injects all of them
(`METAHOOK_SOURCE_PATH`, `VGUI2EXTENSION_SOURCE_PATH`, `SDL2_INCLUDE_DIRS`,
`RAPIDJSON_SOURCE_PATH`, `FORMAT_VALIDATION_SOURCE_PATH`).

Optional aggregator registration (one registry line in the workspace root
`cmake/Components.cmake`; not part of this repository's history):

```
"Plugins/IMGUIExtension|plugins|METAHOOKSV_BUILD_PLUGIN_IMGUIEXTENSION|METAHOOK_SOURCE_PATH,VGUI2EXTENSION_SOURCE_PATH,SDL2_INCLUDE_DIRS,RAPIDJSON_SOURCE_PATH,FORMAT_VALIDATION_SOURCE_PATH"
```

## Verification record (2026-11-09)

- Configure: VS 18 2026 generator, `-A Win32`, Release and Debug trees; format-only
  (`FORMAT_VALIDATION_ONLY=ON`) tree configured too.
- Build: Release and Debug, 0 errors. Only the expected `D9025` (CMake's `/DNDEBUG`
  overridden by the test target's `/UNDEBUG`).
- Tests: `ctest -C Release` and `-C Debug`, 2/2 passed
  (`IMGUIExtensionDispatcherTests`: sorting, capture query, safe/self unregistration during
  iteration, shutdown reentrancy, deferred registration, lifecycle and late registration;
  `IMGUIExtensionFontTests`: range combination/coalescing/sorting and pointer stability, plus
  a missing-font load that must return null instead of asserting — meaningful because tests
  build with `/UNDEBUG`).
- Install: staged `svencoop/metahook/plugins/IMGUIExtension.dll` + `.pdb`.
- Dependency probe: rebuilt against MetaHook's `thirdparty/rapidjson` after the bundled
  submodule was removed (2/2 tests), and separately compiled cleanly against ImGui
  `b62bfd6b0` ("1.92.9 WIP") before returning the submodule to `v1.92.8`.
- DLL exports `CreateInterface` (ordinal 1), as expected for a MetaHook plugin.
- Not verified: the pinned `format-check` target (clang-format 23.1.3 is not installed in
  this environment) and any in-game run. Do not claim rendering/input behavior was
  validated in-game without repeating it against a real client.

## Font system hardening (2026-11-09)

Reviewed against the bundled Dear ImGui 1.92.8 sources; the plugin was using the pre-1.92
font model in three places.

- `GetTexDataAsAlpha8()` (developer > 1 diagnostic) forced `ImFontAtlasBuildMain()` at load
  time, set `TexDesiredFormat` to Alpha8 while `imgui_impl_opengl2.cpp` asserts
  `ImTextureFormat_RGBA32`, and on the legacy path preloaded every glyph of every range
  (large CJK ranges included). Removed; the atlas is now built lazily by the backend.
- `AddFontFromFileTTF` asserts through `IM_ASSERT_USER_ERROR` for unreadable data and
  `AddFontFromMemoryTTF` asserts `font_data_size > 100`. Entries are now pre-validated
  (resolve, regular file, >100 bytes) and loaded with `ImFontFlags_NoLoadError`.
- Merge sources were attached to the *last* added base font because base entries were loaded
  before merge entries. The load order is now primary base → merges → extra bases, with the
  first usable base promoted when the declared primary is unusable.
- A failed re-parse of a regenerated config left `fontsArrPtr` pointing into the overwritten
  document; it now falls back to the built-in font.
- Multiple `ranges` names used to be last-wins; they are combined in
  `src/FontGlyphRanges.h` with process-lifetime storage (ImGui only keeps the pointer).
- Sizes are clamped to `6..200` and the default-font fallback is skipped when a fallback base
  already exists.

## Migration notes

- The old tree was a standalone vcxproj wired to the historical solution layout
  (`$(SolutionDir)include`, `tools\global*.props`, per-project `Release*` output). It was
  replaced by this CMake project; the `.vcxproj*` files and the auto-version pre-build
  event are gone.
- `version.h` is now generated into `<build>/generated/` from `cmake/Version.h.in`; the
  tracked copy was removed so builds do not dirty the repository. `-DIMGUIEXTENSION_VERSION`
  overrides the stamped `1.0.0-dev.<yymmdd.HHmm>` value.
- Sibling consumers resolve `IMGUIExtension/IImGuiExtension.h` and
  `IMGUIExtension/thirdparty/imgui` by path, so those two locations were deliberately kept
  outside `src/`.

## Visual Studio 18 C++ modules pipeline (2026-11-09)

- Symptom: a Debug build failed with `MSB4018` in `GetLibraryModuleSources` and
  `SetModuleDependencies`, inner `System.OutOfMemoryException` while parsing a library
  manifest, for both the plugin and the test project.
- Cause: Visual Studio 18 runs the C++20 modules pipeline for every C++20 project
  (`ComputeStdModulesCompileInputs` parses `$(MSVC_STL_MODULES_JSON)` and injects `std.ixx`),
  although this project uses no modules. `ScanSourceForModuleDependencies=false` alone does
  not stop those targets.
- Fix: `cmake/SDK.cmake` now sets `VS_GLOBAL_EnableCppModules=false` on every target and, with
  CMake >= 3.28, `CXX_SCAN_FOR_MODULES OFF`. Generated projects contain
  `<EnableCppModules>false</EnableCppModules>` and no module tasks run.
- Verification: clean Debug and Release builds, 2/2 tests. `--parallel` still triggers the
  separate MSBuild 18 spurious failure ("Errors: 0"), so `build_plugin.ps1` stays sequential.
- Other components that hit the same MSB4018 need the same two properties; this repository
  only fixes its own targets.

## Logging and command-list cleanup (2026-11-09)

- Normal-level console output is now limited to failures/warnings. Capture transitions,
  display-size changes, per-font loads and per-command restores are gone or behind
  `developer > 1` (`LogVerbose`). A session with working fonts and VGUI2Extension is silent.
- The duplicated hard-coded `-attack/-forward/...` release list was replaced by a loop over the
  existing button table, so a new action cannot be added to one list and forgotten in the other.
- The console binding name is a named constant (`kConsoleCommand`) instead of an inline literal.
- The cleanup exposed a missing `#include <string>` in `InputCapture.cpp` (previously supplied
  transitively by `<chrono>`); it is now explicit, along with `<vector>`.
- Verification: Debug/Release build and 2/2 tests. No in-game run.

## Input/keyboard contract (2026-11-09)

- Re-read the two historical crash fixes (`8d6351a`, `8b8fdd7`). Both early returns in
  `CImGuiBaseUICallbacks::Key_Event` are load-bearing and were kept unchanged, now with comments
  that state why (engine may hand a null/dangling binding on keyup; the crash reproduced by
  releasing a key while moving with a capture UI open).
- Fixed a contract bug in the public API: `AllowKeyPassthrough`'s default compared `keynum` with
  `VK_OEM_3`, while VGUI2Extension forwards the engine key number verbatim (HLSDK `keydefs.h`:
  lowercased ASCII / 0x80+), so the check could never match. The default now returns false and
  the header documents the real domain; console access remains guaranteed by the key-agnostic
  `toggleconsole` binding check in the host.
- No consumer compares `keynum` itself (`RadialMenu` and `EntityFlow` forward it to the default),
  so the change is behavior-neutral for existing plugins. Their `pszCurrentBinding &&` guards
  are redundant but harmless.
- The binding string is copied into a `std::string` for the dispatch so a consumer that rebinds
  during the callbacks cannot invalidate the pointer for later consumers.
- Verification: Debug/Release build, 2/2 tests. In-game keyboard behavior (menu while moving,
  key release, console) was not re-verified here.

## Dependency cleanup (2026-11-09)

- `thirdparty/rapidjson` was removed from `.gitmodules`, from the index and from disk.
  RapidJSON belongs to MetaHook's shared dependency set, which the aggregator already injects
  as `RAPIDJSON_SOURCE_PATH`. `cmake/Dependencies.cmake` now resolves caller → sibling
  `<MetaHook>/thirdparty/rapidjson` → pinned upstream `24b5e7a8`.
- Version note: the workspace copy under `MetaHook/thirdparty/rapidjson` is a few commits
  older than the removed pin (it lacks the `kParseValidateEncodingFlag` bounds fix in
  `document.h`). The parser usage here (`Document::Parse`, member/array queries, default
  flags) is unaffected; bumping MetaHook's rapidjson submodule to `24b5e7a8` is recommended
  so every consumer shares the fixed version.
- `CommandBuilder/CommandBuilderTypes.cpp` hard-included the removed folder; it now prefers
  `../../MetaHook/thirdparty/rapidjson` with `__has_include` and keeps the old path and
  `<rapidjson/...>` as fallbacks. That plugin has no CMake project in this workspace, so the
  edit is compile-unverified.
- Dear ImGui stays pinned at release tag `v1.92.8` (`8936b58fe`). The local clone also holds
  upstream `master` (`b62bfd6b0`, "1.92.9 WIP", 116 commits ahead); the plugin builds cleanly
  against it and both test suites pass, but it is a moving development branch with breaking
  changes and consumer plugins were not rebuilt against it, so it was not pinned. Refresh
  with `git -C thirdparty/imgui fetch --tags` and bump to the next release tag instead.
