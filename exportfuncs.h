#pragma once

#include "Dispatcher.h"

extern CImGuiDispatcher g_Dispatcher;

void IMGUI_VGUI2Extension_Init(void);
void IMGUI_VGUI2Extension_Shutdown(void);
void ImGui_Shutdown(void);
void ImGui_VidReinit(void);
