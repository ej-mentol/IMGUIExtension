# IMGUIExtension

`IMGUIExtension` is a MetaHook plugin that hosts Dear ImGui for Sven Co-op. It manages the shared ImGui context, Win32 window message interception, and OpenGL rendering so consumer plugins (such as custom menus or HUD overlays) do not need to bundle individual rendering backends.

---

## Technical Overview

Architecture details for GoldSrc/Sven Co-op rendering and VGUI2 integration:

### Architecture

1. **Context & Font Atlas**
   The global `ImGuiContext` is created lazily on the first rendered frame. Fonts are loaded and merged dynamically based on the local configuration file `imguiextension/imguiextension.json` before backend compilation. By default, the extension:
   * **Configures Local Fonts**: Lists and configures TrueType font files placed in the `imguiextension/fonts/` directory.
   * **Supports Merged Glyphs**: Merges multiple icon or symbol fonts (such as Font Awesome or Segoe UI Symbol) using ImGui's `MergeMode = true`, covering standard Latin, Cyrillic, and Private Use Area (PUA) symbol charts `[0xF000, 0xFFFF]`.
   * **Validates before loading**: Every configured entry is checked (regular file, >100 bytes) and loaded with `ImFontFlags_NoLoadError`; missing or invalid entries are logged and skipped instead of asserting.
   * **Keeps the context stable**: Video mode changes rebuild only the Win32/OpenGL2 backends. The context pointer returned by `GetImGuiContext()` remains valid across the entire engine session.

2. **Cached HWND Arbitrator**
   Sven Co-op's VGUI2 engine creates native Win32 child text boxes (e.g. when chat input gains focus), which collapses standard active window metrics. `IMGUIExtension` uses an `EnumWindows` callback matching the process ID against class names `"SDL_app"` or `"ValveHalfLifeWindow"`, caching the top-level game window. There is deliberately no `GetForegroundWindow()` fallback to prevent attaching to unrelated processes.

3. **SDL2 Relative Mouse Mode Sync**
   Deactivates SDL2 relative mouse mode when consumer UIs request input capture, and restores it when capture is released to prevent camera angle snaps. SDL2 functions are resolved dynamically from `SDL2.dll`.

4. **Resolution Switch Handling**
   GoldSrc re-creates the OpenGL device context on video mode or resolution changes. `IMGUIExtension` detects this during `BaseUI::Paint` and `HUD_VidInit`, unhooks the subclassed `WNDPROC`, discards unrendered frames, and re-initializes backend states. If re-initialization fails, it rolls back cleanly and retries on the next frame.

5. **Reentrant Dispatcher (`CImGuiDispatcher`)**
   Manages consumer plugin callbacks:
   * **Index-based iteration**: Loops use indices and recursion depth tracking to prevent iterator invalidation during nested calls.
   * **Deferred registration**: Registrations during active dispatch loops are queued and applied once execution unwinds.
   * **Synchronous unregistration**: `Unregister()` executes `Shutdown()` synchronously before releasing the callback, clearing the slot first to prevent re-entrant queries.
   * **Structured Exception Handling (SEH)**: Callback invocations (`Initialize`, `Start`, `OnImGuiFrame`, `WantsInputCapture`, `AllowKeyPassthrough`, `Shutdown`) are wrapped in `__try` / `__except` blocks. Crashed consumers are marked as faulted, logged once to the console, and excluded from subsequent dispatches and capture queries. The ImGui state is restored using `ErrorRecoveryTryToRecoverState`.

---

## Repository layout

| Path | Content |
| --- | --- |
| `CMakeLists.txt`, `cmake/` | CMake build, dependency bootstrap, source list, formatting and debug-launch modules |
| `src/` | Plugin implementation: lifecycle, dispatcher, input capture, fonts, VGUI2 bridge, private symbols |
| `IImGuiExtension.h` | **Public consumer API.** Kept at the repository root because sibling plugins include it as `IMGUIExtension/IImGuiExtension.h` |
| `thirdparty/imgui` | Dear ImGui (pinned submodule). Also a public path: consumers include `IMGUIExtension/thirdparty/imgui/imgui.h` and compile their own copy |
| `tests/` | Dispatcher regression tests, run by CTest with assertions enabled in both configurations |
| `memory/` | Project notes (overview, build/verification, callback contract, formatting) |
| `build/`, `install/` | Generated. Ignored; a standalone build never writes outside this directory |

RapidJSON is **not** bundled here: it belongs to MetaHook's shared dependency set and comes
from `<MetaHook>/thirdparty/rapidjson` (or an explicit `RAPIDJSON_SOURCE_PATH`); a standalone
build without a MetaHook tree falls back to a pinned upstream commit.

## Building

Requirements: Windows, MSVC (x86/Win32), CMake 3.21+, and a MetaHook source tree
(`METAHOOK_SOURCE_PATH`). `VGUI2EXTENSION_SOURCE_PATH`, `RAPIDJSON_SOURCE_PATH`,
`SDL2_INCLUDE_DIRS` and `FORMAT_VALIDATION_SOURCE_PATH` are optional overrides; the
bootstrap otherwise uses a sibling checkout, MetaHook's RapidJSON and SDL2 header trees, or
a pinned fetch.

```powershell
# One script for Debug + Release: configure, build, test, then drop the DLL next to the sources
.\build_plugin.ps1

# Equivalent manual steps, one build tree per configuration
cmake -S . -B build/x86/Release -G "Visual Studio 18 2026" -A Win32 `
      -DMETAHOOK_SOURCE_PATH=C:/path/to/MetaHook
cmake --build build/x86/Release --config Release
ctest --test-dir build/x86/Release -C Release --output-on-failure
cmake --install build/x86/Release --config Release
```

* Build output: `build/x86/<configuration>/`.
* Install output: `install/x86/<configuration>/svencoop/metahook/plugins/`.
* The plugin version is stamped at configure time and generated into the build tree
  (`build/.../generated/version.h`), so building never dirties tracked sources. Override it
  with `-DIMGUIEXTENSION_VERSION=...`.
* There is no `Release_AVX2` variant any more. MetaHook only treats `_AVX2`/`_SSE*` names
  as a legacy fallback when the baseline DLL is absent, and a stale variant shadowing a
  fresh baseline is a documented hazard.
* Building inside the aggregator: add one registry entry to `cmake/Components.cmake` and
  the injected paths follow the shared conventions (see
  `memory/build_and_verification.md`).

## For consumer plugins

* Include `<IMGUIExtension/IImGuiExtension.h>` (path relative to the plugin directory) and
  compile your own Dear ImGui from `IMGUIExtension/thirdparty/imgui`.
* Resolve `IMGUI_EXTENSION_INTERFACE_VERSION` (`ImGuiExtension001`) from the
  `IMGUIExtension` module in `LoadClient` and register callbacks there; unregister in
  `Shutdown`/`ExitGame` before freeing anything the callbacks reference.
* Every DLL has its own `GImGui`; call `ImGui::SetCurrentContext(g_pImGuiExtension->GetImGuiContext())`
  at the start of `OnImGuiFrame`.
* Report input capture through `WantsInputCapture()` and let `AllowKeyPassthrough()` release
  keys the engine must keep. `keynum` is the engine key number forwarded verbatim (not a
  Windows VK code), and the host already passes through any binding containing
  `toggleconsole`; unbound keys and key releases always reach the engine.
* Callback order is by descending `GetAltitude()`; `WantsInputCapture()` is an OR over all
  callbacks, so a single requesting consumer suppresses game mouse input for everybody.
* Plugin load order does not matter: registration before the dispatcher is initialized is
  queued and initialized later in the same client session.

## Runtime files

The plugin works in `<game>/<mod>/` (`GAME` search path) and prefers an addon override when
fonts are present there:

| Path | Purpose |
| --- | --- |
| `imguiextension/imguiextension.json` | Font list (`file`, `size`, `merge`, `ranges`). Generated from the fonts directory when missing or empty |
| `imguiextension/fonts/` | `.ttf`/`.otf` fonts; `*_addon/imguiextension/fonts` wins when it contains fonts |
| `imguiextension/imgui.ini` | Window layout persistence |

## Fonts

`imguiextension.json` lists font sources in load order:

```json
{
  "fonts": [
    { "file": "Inter-Regular.ttf", "size": 18.0, "ranges": ["latin", "cyrillic"] },
    { "file": "fa-solid-900.ttf",  "size": 18.0, "merge": true, "ranges": ["symbols"] },
    { "file": "Inter-Bold.ttf",    "size": 18.0 }
  ]
}
```

* The **first usable non-merge** entry is the primary font (`Fonts[0]`, the default font of
  every consumer). Merge entries are attached to it, then any further base fonts are added
  as separately selectable fonts. If the declared primary is missing or broken, the first
  usable base takes its place.
* `ranges` accepts `latin`, `cyrillic`, `symbols`, `default`, `korean`, `japanese`,
  `chinese`; several names are combined into one glyph-range set. Omitting `ranges` uses
  ImGui's default ranges.
* Entries are validated before they reach ImGui: unresolvable names, directories and files
  of 100 bytes or less are logged and skipped. `size` outside `6..200` becomes `18.0`.
* The config is generated from the fonts directory only when it is missing or lists no
  fonts; an existing file is never rewritten. Delete it to rescan after adding fonts.
* The atlas is built lazily by ImGui 1.92 and owned by the OpenGL2 backend; the plugin never
  calls the obsolete eager `GetTexDataAsAlpha8()` path.

## Formatting

The component uses the workspace-wide FormatValidation configuration (clang-format
23.1.3, DiligentCore style, four-space indentation). Targets are explicit and not part of
a normal build:

```powershell
python -m pip install clang-format==23.1.3
cmake -S . -B build/format -G "Visual Studio 18 2026" -DFORMAT_VALIDATION_ONLY=ON
cmake --build build/format --config Release --target format-check
cmake --build build/format --config Release --target format
```

## Concurrency & Safety

* **C++ Standard**: Compiled under C++20 (`stdcpp20`).
* **Thread Model**: Single-threaded by design. All dispatcher state (`CImGuiDispatcher`), registration, and callback iteration run exclusively on the engine's main thread inside GoldSrc's hook points (`HUD_Init`, `BaseUI::Paint`, `IN_*`). There is no internal locking - consumers must not call `RegisterCallbacks`/`UnregisterCallbacks` or any `IImGuiExtension` method from a worker thread.
* **Reentrancy & Mutation Safety**: The dispatcher explicitly supports *reentrant* calls and dynamic structural modifications. Registration during active iteration at any depth is deferred and applied once the outermost execution loop unwinds; unregistration during iteration shuts the consumer down immediately and clears its slot first.
* Consumers must avoid long-running or blocking code in `OnImGuiFrame` callbacks to maintain consistent rendering frame times.

## Known limitations

* Rendering is driven by `VGUI2Extension`'s `BaseUI::Paint` bridge. Without that plugin the
  context may be created on demand but no frame is presented; a warning is printed once.
* The `Paint` bridge throttles frame construction to ~500 µs. Menu animation is therefore
  tied to the VGUI2 paint rate, not to a separate timer.
* Input capture is a global OR: while any consumer captures, the engine's `IN_MouseEvent`
  and `IN_Accumulate` are suppressed for every consumer and for the game.
* Keyboard passthrough is decided from the engine binding string. Keys without a binding
  (modifiers and combinations in particular) and every key release are passed straight through;
  dereferencing the binding pointer there is unsafe (null or stale), so those early returns are
  load-bearing, not leftovers.
* While any consumer captures, the engine's `IN_Accumulate` is not called; its mouse
  re-centering interaction with ImGui has not been fully isolated on the engine side.
* Whether `BaseUI::Key_Event` fires during normal gameplay (as opposed to only while the VGUI
  overlay is active) has not been re-verified in this repository; the `HUD_Key_Event`
  experiment that assumed it did not was reverted. Do not change the hook point without an
  in-game test.
* `Sys_GetMainWindow` is resolved from the engine module by name; engines that do not
  export it fall back to the window-class scan.
* Font file names travel through the engine's narrow ANSI filesystem API and ImGui's
  `fopen`, so font names outside the system ANSI code page cannot be loaded. The generated
  JSON also writes those bytes verbatim and then fails UTF-8 parsing.
* A non-empty config is not regenerated automatically; a config whose entries all became
  unusable falls back to the built-in font and only logs the individual failures.
* ~~Consumer callback crash leaves ImGui window and internal stacks unbalanced~~ (resolved: state recovery via `ErrorRecoveryTryToRecoverState` and faulty callback isolation).
* ~~Crashing consumer during shutdown or unregister causes host process termination~~ (resolved: `SafeShutdown` isolates SEH exceptions during cleanup).
* ~~Sequential `pfnClientCmd` invocations concatenate in GoldSrc `Cbuf_AddText`~~ (resolved: commands explicitly terminated with `\n`).
* ~~Resolution switch causing recursive `WNDPROC` subclassing~~ (resolved: existing `WNDPROC` restored prior to resetting window handle and guarded against self-subclassing).

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
