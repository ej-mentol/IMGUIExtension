# IMGUIExtension

`IMGUIExtension` is a core arbitrator/host plugin designed to integrate Dear ImGui into Sven Co-op using the MetaHook framework. By managing the global ImGui context, Win32 window message interception, and OpenGL rendering state, it provides a stable environment for lightweight consumer plugins (such as custom menus or HUD overlays) without requiring them to bundle individual rendering backends.

---

## Technical Overview

The plugin solves several architecture-specific hurdles inherent to GoldSrc/Sven Co-op's rendering pipeline and VGUI2 focus management.

### Key Architectural Features

1. **Unified Context & Font Atlas**
   The global `ImGuiContext` is created lazily on the first rendered frame. Fonts are loaded and merged dynamically based on the local configuration file `imguiextension/imguiextension.json` before backend compilation. By default, the extension:
   * **Configures Local Fonts**: Automatically lists and configures TrueType font files placed in the `imguiextension/fonts/` directory.
   * **Supports Merged Glyphs**: Allows merging multiple icon or symbol fonts (such as Font Awesome or Segoe UI Symbol) using ImGui's `MergeMode = true`, covering standard Latin, Cyrillic, and Private Use Area (PUA) symbol charts `[0xF000, 0xFFFF]`.
   * **Validates before loading**: every configured entry is resolved and checked (regular file, >100 bytes) and loaded with `ImFontFlags_NoLoadError`, so a stale or truncated config entry is logged and skipped instead of asserting inside ImGui's loader.
   * **Keeps the context stable**: a video mode change rebuilds only the Win32/OpenGL2 backends. The context pointer returned by `GetImGuiContext()` stays valid for the whole engine session, so consumers may follow the documented "set current context each frame" pattern without re-resolving anything.

2. **Cached HWND Arbitrator**
   Sven Co-op's VGUI2 engine creates native Win32 child text boxes (e.g., when the chat box gains focus), which collapses the standard active window metrics. `IMGUIExtension` uses an `EnumWindows` callback to match the current process ID against class names `"SDL_app"` or `"ValveHalfLifeWindow"`, caching the true top-level game window. There is deliberately no `GetForegroundWindow()` fallback: subclassing an unrelated application's window must not happen, so a missing window simply retries on the next frame.

3. **SDL2 Relative Mouse Mode Sync**
   Properly intercepts and deactivates SDL2 relative mouse mode when consumer UIs request input capture. When capture is released, it restores the relative state to prevent the in-game camera from snapping. SDL2 is resolved from the already loaded `SDL2.dll`; only its headers are a build dependency.

4. **Resolution Switch Safeguards**
   GoldSrc destroys and re-creates the OpenGL device context upon video mode changes or resolution switches. `IMGUIExtension` catches this during `BaseUI::Paint` and `HUD_VidInit`, restores the WndProc subclass, discards a frame that was begun but never rendered, and re-initializes the GL2/Win32 backend states. A failed (re-)initialization is rolled back and retried on a later frame instead of leaving a half-installed subclass.

5. **Robust Reentrant Dispatcher**
   To safely host multiple independent consumer plugins, the core utilizes a highly resilient dispatch system (`CImGuiDispatcher`). It prevents common modding crashes through:
   * **Zero-Iterator-Invalidation**: All callback iterations use safe index-based loops and a recursion depth counter instead of standard iterators.
   * **Deferred Mutation Queues**: If a consumer plugin registers itself (or another component) in the middle of a dispatch loop or an input event, the addition is queued and applied afterwards.
   * **Synchronous unregistration**: `UnregisterCallbacks()` runs the consumer's `Shutdown()` before returning, so a plugin may free itself immediately; a `Shutdown()` that unregisters other callbacks cannot recurse.
   * **SEH Protection**: Consumer callback invocations and input checks are wrapped in Structured Exception Handling (`__try` / `__except`) blocks, ensuring a crash or null-pointer dereference in a client plugin does not crash the host extension or the game process. The ImGui window stack is unwound after a caught exception so later callbacks start clean.

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

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
