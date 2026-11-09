---
title: project_overview
type: note
permalink: imguiextension/project-overview
tags:
- architecture
- plugin
- imgui
---

# IMGUIExtension project overview

## Purpose and boundary

IMGUIExtension is a MetaHook V4 plugin that hosts a single Dear ImGui context for the
process, renders it inside the VGUI2 paint pass (through VGUI2Extension), and arbitrates
keyboard/mouse capture between consumer plugins and the engine. It owns all ImGui/backend
state; consumers own only their callbacks and their own copy of the ImGui library.

It is not a UI framework and it does not draw anything by itself: without a registered
consumer the frame is empty, and without VGUI2Extension no frame is presented at all.

## Layout

- `src/plugins.cpp`: `IPluginsV4` lifecycle, client export hooks (`HUD_Init`,
  `HUD_VidInit`, `HUD_Redraw`, `IN_MouseEvent`, `IN_Accumulate`, `CL_CreateMove`), the
  `IImGuiExtension` implementation and the `IMGUI_EXTENSION_INTERFACE_VERSION` export.
- `src/Dispatcher.h`: `CImGuiDispatcher`, the reentrancy-tolerant callback registry
  (see [[imguiextension/callback-contract]]).
- `src/InputCapture.cpp`: context creation, Win32/OpenGL2 backend ownership, WndProc
  subclass, HWND arbitration, capture state machine and mouse restore.
- `src/VGUI2Callbacks.cpp`: VGUI2Extension bridge (`BaseUI::Paint` frame build/render,
  `GameUI` capture release, console callbacks) and selection of the GameUI-active state.
- `src/FontAtlas.cpp`: `imguiextension/imguiextension.json` parsing (RapidJSON), entry
  validation, primary/merge ordering and the font atlas, including automatic config
  generation from the fonts directory. Details: [[imguiextension/font-pipeline]].
- `src/FontGlyphRanges.h`: combines several `ranges` names into one ImWchar list kept for
  the process lifetime (ImGui only stores the pointer).
- `src/privatefuncs.cpp`: runtime resolution of `SDL2.dll` exports and
  `Sys_GetMainWindow`.
- `IImGuiExtension.h` (repository root): the public consumer API and interface version.
- `thirdparty/imgui`: pinned Dear ImGui submodule. The ImGui path is part of the public
  contract because consumers compile their own copy.
- RapidJSON is not bundled: the font parser consumes MetaHook's `thirdparty/rapidjson`
  (`RAPIDJSON_SOURCE_PATH`), or the pinned upstream commit for standalone builds.

## Lifecycle

1. `Init` stores the host API/interface pointers and the filesystem pointers. The HL25
   filesystem is used only when the legacy pointer is absent (`FILESYSTEM_ANY_*`).
2. `LoadEngine` copies `cl_enginefunc_t`; hooks are installed in `LoadClient`, followed by
   `PrivateFuncs_Init`, VGUI2Extension resolution and `Dispatcher.Initialize()`.
3. Consumers may register before that point: registration is stored and initialized when
   the dispatcher initializes.
4. `HUD_Init` calls `Dispatcher.Start`. Frames are built in the VGUI2 `Paint` pre-call and
   rendered in the post-call; the ImGui context is created on first use.
5. `ExitGame`/`Shutdown` unregister VGUI2 callbacks and destroy the context and backends.
   Both entry points are idempotent because a DLL unload need not be preceded by exit.

## Context and backend ownership

The context is created once per engine session and kept across video mode changes;
`ImGui_VidReinit` only tears down and rebuilds the Win32/OpenGL2 backends and the WndProc
subclass. This is a contract: consumers call `GetImGuiContext()` repeatedly and expect the
pointer to remain valid. A frame that was begun but never rendered is discarded with
`EndFrame()` before the next `NewFrame()` to keep ImGui's frame assertions satisfied.

Window resolution order: cached HWND (validated with `IsWindow`), `Sys_GetMainWindow`
plus `SDL_GetWindowWMInfo`, an `EnumWindows` scan for this process, then a class-name
lookup. There is no foreground-window fallback.

## Runtime files

`<gamedir>/imguiextension/imguiextension.json`, `.../fonts/`, `.../imgui.ini`;
`<gamedir>_addon/imguiextension/fonts` takes precedence when it contains fonts.

## Known limits

- Input capture is a global OR: whichever consumer requests capture suppresses engine
  mouse look for everyone.
- Frame construction is throttled to ~500 µs in the paint pass.
- `Sys_GetMainWindow` is optional; engines without that export use the window scan.
