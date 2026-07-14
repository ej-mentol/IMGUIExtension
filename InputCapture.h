#pragma once
#include <windows.h>
#include "thirdparty/imgui/imgui.h"

extern ImGuiContext* g_pImGuiContext;
extern bool g_bImGuiInitialized;
extern bool g_bImGuiFrameReady;
extern bool s_bGameUIActive;

void ImGui_InitOnce();
void ImGui_Shutdown();
void ImGui_VidReinit();
bool AnyWantsInputCapture();
void UpdateInputCaptureState();
HWND GetGameHWND();
LRESULT CALLBACK ImGui_WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
int GetDeveloperLevel();
