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
	return g_pfnHUD_Redraw(time, intermission);
}

int HUD_Key_Event(int down, int keynum, const char* pszCurrentBinding)
{
	// Track keydown events for the input capture snapshot.
	// GoldSrc only calls this on keydown (down == 1) during normal gameplay.
	// Blocking is handled by VGUI2Callbacks Key_Event — this hook is tracking only.
	if (down == 1 && pszCurrentBinding)
	{
		std::string binding = pszCurrentBinding;
		if (binding == "+attack" || binding == "+attack2" ||
			binding == "+forward" || binding == "+back" ||
			binding == "+moveleft" || binding == "+moveright" ||
			binding == "+jump" || binding == "+duck" ||
			binding == "+use")
		{
			g_KeyToCommand[keynum] = binding;
			g_HeldCommands.insert(binding);
		}
	}

	return g_pfnHUD_Key_Event ? g_pfnHUD_Key_Event(down, keynum, pszCurrentBinding) : 1;
}

void IN_MouseEvent(int mstate)
{
	if (g_Dispatcher.AnyWantsInputCapture())
	{
		// Block mouse clicks from triggering engine/weapon events
		return;
	}
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

		// While any consumer wants input capture, we intentionally skip the engine's own
		// IN_Accumulate to avoid its internal mouse re-centering behavior fighting with
		// ImGui. See README § Known Issues — root cause on the engine side not fully isolated.
	}
	else
	{
		g_vRawDeltaX = 0.0f;
		g_vRawDeltaY = 0.0f;
		g_pfnIN_Accumulate();
	}
}
