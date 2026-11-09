---
title: callback_contract
type: note
permalink: imguiextension/callback-contract
tags:
- dispatcher
- api
- input
---

# Dispatcher and input capture contract

## Registration

- `IImGuiExtension::RegisterCallbacks` / `UnregisterCallbacks` are main-thread only.
- Ordering is descending `GetAltitude()`; equal altitudes keep their relative order.
- Registering after `Dispatcher.Initialize()`/`Start()` immediately delivers the lifecycle
  callbacks the new consumer missed. Registering *during* a dispatch is queued and applied
  after the outermost loop unwinds, so it never runs inside the loop that added it.
- `UnregisterCallbacks` invokes `Shutdown()` synchronously before returning and clears the
  slot first. A consumer may therefore free itself immediately after unregistering.
  `Dispatcher::Shutdown()` detaches its list before invoking consumers, so a `Shutdown()`
  that unregisters other callbacks cannot recurse or double-destroy.
- Lifecycle: `Initialize()` once, `Start(engineFuncs, interfaceVersion)` with
  `METAHOOK_API_VERSION` from `HUD_Init`, `Shutdown()` once (on unregister or dispatcher
  shutdown). Late registrations receive the callbacks already reached.

## Frame callbacks

- `OnImGuiFrame()` runs between `NewFrame()` and `Render()` in the VGUI2 paint pre-pass.
  Consumers must call `ImGui::SetCurrentContext(GetImGuiContext())` themselves first;
  every DLL carries its own `GImGui`.
- A callback that throws a structured exception is reported and skipped. Its ImGui window
  stack is unwound back to the pre-callback depth so later callbacks and the host do not
  inherit a mismatched `Begin()`/`End()` pair.
- Long-running work in the callback stretches the frame; rendering runs in the paint
  post-pass only when a frame was actually built.

## Input capture

- `WantsInputCapture()` is an OR over all callbacks. While true:
  - the WndProc subclass passes messages to the ImGui Win32 handler and then swallows
    mouse messages/`WM_CHAR`;
  - `IN_MouseEvent` is not forwarded, so engine weapon/UI clicks do not fire;
  - `IN_Accumulate` is not forwarded; raw deltas are polled through
    `SDL_GetRelativeMouseState` and exposed by `GetRawMouseDelta`.
- Transition to capture (in the paint pass): snapshot the action bits from the last
  `CL_CreateMove`, deactivate engine mouse, clear imput states, release every `+`action,
  disable SDL2 relative mouse mode, release the cursor clip and show the cursor.
- Transition back: restore only the snapshot commands (not the live key set), re-activate
  the engine mouse and SDL2 relative mode — except while GameUI is active or no level is
  loaded, where the cursor must stay free.
- `GameUI` callbacks (`ActivateGameUI`, `DisconnectFromServer`, `LoadingStarted`) set the
  GameUI-active flag, call `OnForceRelease()` on every consumer and resynchronize capture;
  `HideGameUI`/`LoadingFinished` clear it.
- `AllowKeyPassthrough(keynum, pszCurrentBinding)` lets a consumer release individual keys
  while capturing. Unbound keys and every key release are forwarded before this callback runs,
  and bindings containing `toggleconsole` always pass, so the built-in default returns false.

## Logging

A normal session is silent. Capture transitions, display-size changes and per-font load lines
print only at `developer > 1` (`LogVerbose` in `InputCapture.cpp`); normal-level output is
reserved for failures and warnings (missing font, unparsable config, missing VGUI2Extension).

## Keyboard capture path (and the historical crash)

`BaseUI::Key_Event` (VGUI2Extension) forwards engine key events with `pszCurrentBinding`
pointing at the engine's binding entry.

Crash history (two distinct sources from the same era):

1. **Unbound keys and key combinations** (`8b8fdd7`). A key without a binding passes
   `pszCurrentBinding == nullptr`, and `strstr(nullptr, ...)` crashed. Combinations hit this
   because modifiers, and often the second key, are unbound — a single bound key on its own
   worked, which is why the symptom looked inconsistent. Fixed by the null guard.
2. **Keyup** (`8d6351a`). GoldSrc does not guarantee a valid binding pointer on release; it
   can be dangling, so a null check alone is not enough. Fixed by returning before touching the
   binding when `down == 0`.

The first two early returns of `CImGuiBaseUICallbacks::Key_Event`, in this order, are
load-bearing and must stay:

1. `down == 0` → return **before touching the binding**; the release passes to the engine so
   held commands are cleared.
2. `!pszCurrentBinding` → return; an unbound key (or a modifier of a combination) has no
   command to suppress.

The keydown path then copies the binding into a `std::string` for the duration of the dispatch
(a consumer may rebind, invalidating the engine string), passes through anything containing
`toggleconsole`, asks `AllowKeyPassthrough(keynum, binding)`, and only then marks the event
`SUPERCEDE`.

`keynum` is the engine key number forwarded verbatim (HLSDK `keydefs.h`: printable keys are
lowercased ASCII, special keys start at 0x80). It is **not** a Windows `VK_*` code; the old
`VK_OEM_3` default never matched and was removed in favour of the key-agnostic
`toggleconsole` check. Consumers' `pszCurrentBinding &&` guards around their own bind names are
redundant but harmless.

Keep in mind:

- Unbound keys and every keyup always reach the engine while capturing.
- `AllowKeyPassthrough` is never called for keyup or for null bindings, and the binding
  pointer is only valid during the call.
- `6db0d50` questioned whether this path runs during normal gameplay (vs. only with the VGUI
  overlay active) and moved key tracking to `HUD_Key_Event`; that experiment was reverted in
  `3a35f66`. Do not change the hook point without an in-game test.
