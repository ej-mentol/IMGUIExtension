#include <metahook.h>
#include <Interface/IVGUI2Extension.h>
#include "plugins.h"
#include "privatefuncs.h"
#include "exportfuncs.h"
#include "IImGuiExtension.h"
#include "FontAtlas.h"
#include "InputCapture.h"
#include "VGUI2Callbacks.h"

CImGuiDispatcher g_Dispatcher;
IVGUI2Extension* g_pVGUI2Extension = nullptr;

// Hooked Client Export Functions
int HUD_Redraw(float time, int intermission)
{
	return g_pfnHUD_Redraw ? g_pfnHUD_Redraw(time, intermission) : 0;
}

void CL_CreateMove(float frametime, struct usercmd_s* cmd, int active)
{
	// Record the current input buttons every frame.
	// This is the authoritative source: the engine has already applied all bindings to cmd->buttons.
	// We read it at capture ON to build the restore snapshot without any key-name assumptions.
	if (cmd)
		g_LastButtons = cmd->buttons;

	if (g_pfnCL_CreateMove)
		g_pfnCL_CreateMove(frametime, cmd, active);
}

void IN_MouseEvent(int mstate)
{
	if (g_Dispatcher.AnyWantsInputCapture())
	{
		// Block mouse clicks from triggering engine/weapon events
		return;
	}
	if (g_pfnIN_MouseEvent)
		g_pfnIN_MouseEvent(mstate);
}

void IN_Accumulate(void)
{
	if (g_Dispatcher.AnyWantsInputCapture())
	{
		if (g_pfnSDL_GetRelativeMouseState)
		{
			int rx = 0, ry = 0;
			g_pfnSDL_GetRelativeMouseState(&rx, &ry);
			g_vRawDeltaX = (float)rx;
			g_vRawDeltaY = (float)ry;
		}

		// While any consumer wants input capture, the engine's own IN_Accumulate is
		// skipped so its mouse re-centering cannot fight ImGui's cursor. The exact
		// engine-side interaction is not fully isolated; see README "Known limitations".
	}
	else
	{
		g_vRawDeltaX = 0.0f;
		g_vRawDeltaY = 0.0f;
		if (g_pfnIN_Accumulate)
			g_pfnIN_Accumulate();
	}
}
