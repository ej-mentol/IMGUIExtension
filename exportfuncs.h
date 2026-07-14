#pragma once

#include "Dispatcher.h"

class IVGUI2Extension;

extern CImGuiDispatcher g_Dispatcher;
extern IVGUI2Extension* g_pVGUI2Extension;

void IMGUI_VGUI2Extension_Init(void);
void IMGUI_VGUI2Extension_Shutdown(void);
void ImGui_Shutdown(void);
void ImGui_VidReinit(void);
