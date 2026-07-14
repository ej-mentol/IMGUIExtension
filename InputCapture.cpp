#include <metahook.h>
#include <chrono>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "thirdparty/imgui/imgui.h"
#include "thirdparty/imgui/imgui_internal.h"
#include "thirdparty/imgui/backends/imgui_impl_win32.h"
#include "thirdparty/imgui/backends/imgui_impl_opengl2.h"

#include <cvardef.h>
#include "plugins.h"
#include "privatefuncs.h"
#include "exportfuncs.h"
#include "InputCapture.h"
#include "FontAtlas.h"

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

ImGuiContext* g_pImGuiContext = nullptr;
bool g_bImGuiInitialized = false;
bool g_bImGuiFrameReady = false;
// Set to true because when the DLL loads, GoldSrc/Sven is starting up and the Main Menu (GameUI) is active.
// Also acts as a safety default during hot-reloads (vid_restart) to prevent capturing the mouse 
// before callbacks (ActivateGameUI/LoadingStarted) sync the state.
bool s_bGameUIActive = true;

static bool s_bLastWantsCapture = false;
static std::chrono::steady_clock::time_point s_LastCaptureTransition = std::chrono::steady_clock::now();
static int s_iCaptureTransitionCount = 0;
static WNDPROC g_pfnOldWndProc = nullptr;
static char s_szIniPath[MAX_PATH] = {};

int GetDeveloperLevel()
{
	if (gEngfuncs.pfnGetCvarFloat)
	{
		return (int)gEngfuncs.pfnGetCvarFloat("developer");
	}
	return 0;
}

struct FindWindowData {
	DWORD processId;
	HWND hWnd;
};

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
	FindWindowData* data = (FindWindowData*)lParam;
	DWORD processId = 0;
	GetWindowThreadProcessId(hwnd, &processId);
	if (processId == data->processId)
	{
		char className[256];
		if (GetClassNameA(hwnd, className, sizeof(className)))
		{
			if (strcmp(className, "SDL_app") == 0 || strcmp(className, "ValveHalfLifeWindow") == 0)
			{
				data->hWnd = hwnd;
				return FALSE; // Stop enumerating
			}
		}
	}
	return TRUE;
}

HWND GetGameHWND()
{
	static HWND s_hGameWnd = NULL;
	if (s_hGameWnd)
		return s_hGameWnd;

	if (g_pfnSys_GetMainWindow && g_pfnSDL_GetWindowWMInfo)
	{
		SDL_Window* pSdlWindow = (SDL_Window*)g_pfnSys_GetMainWindow();
		if (pSdlWindow)
		{
			SDL_SysWMinfo info = {};
			SDL_VERSION(&info.version);
			if (g_pfnSDL_GetWindowWMInfo(pSdlWindow, &info))
			{
				s_hGameWnd = info.info.win.window;
				return s_hGameWnd;
			}
		}
	}

	FindWindowData data = { GetCurrentProcessId(), NULL };
	EnumWindows(EnumWindowsProc, (LPARAM)&data);
	if (data.hWnd)
	{
		s_hGameWnd = data.hWnd;
		return s_hGameWnd;
	}

	HWND h = FindWindowA("SDL_app", NULL);
	if (!h) h = FindWindowA("ValveHalfLifeWindow", NULL);
	if (h)
	{
		s_hGameWnd = h;
		return s_hGameWnd;
	}

	return GetForegroundWindow();
}

LRESULT CALLBACK ImGui_WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	if (g_bImGuiInitialized)
	{
		ImGui::SetCurrentContext(g_pImGuiContext);

		// Pass to ImGui Win32 handler
		if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
		{
			if (g_Dispatcher.AnyWantsInputCapture())
			{
				return 1; // Supercede
			}
		}

		// Also block game inputs when ImGui wants capture
		if (g_Dispatcher.AnyWantsInputCapture())
		{
			switch (msg)
			{
			case WM_MOUSEMOVE:
			case WM_LBUTTONDOWN: case WM_LBUTTONUP:
			case WM_RBUTTONDOWN: case WM_RBUTTONUP:
			case WM_MBUTTONDOWN: case WM_MBUTTONUP:
			case WM_XBUTTONDOWN: case WM_XBUTTONUP:
			case WM_MOUSEWHEEL:
			case WM_CHAR:
				return 0; // Block
			}
		}
	}

	if (g_pfnOldWndProc)
	{
		return CallWindowProc(g_pfnOldWndProc, hwnd, msg, wp, lp);
	}
	return DefWindowProc(hwnd, msg, wp, lp);
}



bool AnyWantsInputCapture()
{
	return g_Dispatcher.AnyWantsInputCapture();
}

// Maps a game command string to the set of Windows VK codes that could physically trigger it,
// covering default WASD, arrow keys, and numpad layouts.
// Used ONLY to filter g_HeldCommands at capture ON — not as a primary restore mechanism.
// If a command has no known VK mapping, it is excluded from the snapshot (safe default).
static std::vector<int> GetVKsForCommand(const char* cmd)
{
	if (strcmp(cmd, "+forward")   == 0) return { 'W', VK_UP,    VK_NUMPAD8 };
	if (strcmp(cmd, "+back")      == 0) return { 'S', VK_DOWN,  VK_NUMPAD2 };
	if (strcmp(cmd, "+moveleft")  == 0) return { 'A', VK_LEFT,  VK_NUMPAD4 };
	if (strcmp(cmd, "+moveright") == 0) return { 'D', VK_RIGHT, VK_NUMPAD6 };
	if (strcmp(cmd, "+jump")      == 0) return { VK_SPACE };
	if (strcmp(cmd, "+duck")      == 0) return { VK_LCONTROL, VK_RCONTROL };
	if (strcmp(cmd, "+use")       == 0) return { 'E', 'F' };
	if (strcmp(cmd, "+attack")    == 0) return { VK_LBUTTON };
	if (strcmp(cmd, "+attack2")   == 0) return { VK_RBUTTON };
	return {};
}

static bool IsCommandPhysicallyHeld(const std::string& cmd)
{
	for (int vk : GetVKsForCommand(cmd.c_str()))
	{
		if (GetAsyncKeyState(vk) & 0x8000)
			return true;
	}
	return false;
}

void UpdateInputCaptureState()
{
	bool anyWantsCapture = g_Dispatcher.AnyWantsInputCapture();
	if (anyWantsCapture != s_bLastWantsCapture)
	{
		auto now = std::chrono::steady_clock::now();
		auto sinceLastMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_LastCaptureTransition).count();
		s_LastCaptureTransition = now;
		s_iCaptureTransitionCount++;

		POINT pt = {};
		GetCursorPos(&pt);

		s_bLastWantsCapture = anyWantsCapture;
		if (anyWantsCapture)
		{
			// Build snapshot: only include commands that are physically held RIGHT NOW.
			// Key_Event is not called on keyup by GoldSrc, so g_HeldCommands accumulates stale
			// entries over time. GetAsyncKeyState at this exact moment is the only reliable filter.
			g_PreCaptureCommands.clear();
			for (const auto& cmd : g_HeldCommands)
			{
				if (IsCommandPhysicallyHeld(cmd))
					g_PreCaptureCommands.insert(cmd);
			}
			g_HeldCommands.clear();
			g_KeyToCommand.clear();

			if (g_pfnIN_DeactivateMouse)
				g_pfnIN_DeactivateMouse();

			if (gExportfuncs.IN_ClearStates)
				gExportfuncs.IN_ClearStates();

			if (gEngfuncs.pfnClientCmd)
			{
				gEngfuncs.pfnClientCmd((char*)"-attack");
				gEngfuncs.pfnClientCmd((char*)"-attack2");
				gEngfuncs.pfnClientCmd((char*)"-forward");
				gEngfuncs.pfnClientCmd((char*)"-back");
				gEngfuncs.pfnClientCmd((char*)"-moveleft");
				gEngfuncs.pfnClientCmd((char*)"-moveright");
				gEngfuncs.pfnClientCmd((char*)"-jump");
				gEngfuncs.pfnClientCmd((char*)"-duck");
				gEngfuncs.pfnClientCmd((char*)"-use");
			}

			if (g_pfnSDL_SetRelativeMouseMode)
				g_pfnSDL_SetRelativeMouseMode(SDL_FALSE);

			ClipCursor(nullptr);
			int guard = 0;
			while (ShowCursor(TRUE) < 0 && guard++ < 16) {}

			gEngfuncs.Con_DPrintf("[IMGUIExtension] capture ON  (+%lldms since last), cursorPos=(%ld,%ld), snapshot=%zu\n",
				(long long)sinceLastMs, pt.x, pt.y, g_PreCaptureCommands.size());
		}
		else
		{
			bool bShouldRestoreMouse = true;
			if (s_bGameUIActive)
				bShouldRestoreMouse = false;
			if (gEngfuncs.pfnGetLevelName)
			{
				const char* level = gEngfuncs.pfnGetLevelName();
				if (!level || !level[0])
					bShouldRestoreMouse = false;
			}

			if (bShouldRestoreMouse)
			{
				if (g_pfnIN_ActivateMouse)
					g_pfnIN_ActivateMouse();

				if (g_pfnSDL_SetRelativeMouseMode)
					g_pfnSDL_SetRelativeMouseMode(SDL_TRUE);

				if (gEngfuncs.pfnClientCmd)
				{
					// Restore only from the snapshot taken at capture ON, not the live set.
					// The live set may contain keys pressed during menu navigation which are
					// already in the correct state once the menu closes.
					for (const auto& cmd : g_PreCaptureCommands)
					{
						gEngfuncs.Con_DPrintf("[IMGUIExtension] Restoring command: %s\n", cmd.c_str());
						gEngfuncs.pfnClientCmd((char*)cmd.c_str());
					}
				}
			}

			g_PreCaptureCommands.clear();
			// Discard any commands accumulated while the menu was open —
			// those keypresses were blocked by SUPERCEDE and the engine never saw them,
			// so they must not pollute the next capture ON snapshot.
			g_HeldCommands.clear();
			g_KeyToCommand.clear();

			gEngfuncs.Con_DPrintf("[IMGUIExtension] capture OFF (+%lldms since last), cursorPos=(%ld,%ld), restored=%d\n",
				(long long)sinceLastMs, pt.x, pt.y, bShouldRestoreMouse);
		}
	}
}

void ImGui_InitOnce()
{
	if (g_bImGuiInitialized) return;

	HWND hWnd = GetGameHWND();
	if (!hWnd) return;

	g_pImGuiContext = ImGui::CreateContext();
	ImGui::SetCurrentContext(g_pImGuiContext);

	FILESYSTEM_ANY_CREATEDIR("imguiextension", "GAME");
	FILESYSTEM_ANY_CREATEDIR("imguiextension/fonts", "GAME");
	if (!FILESYSTEM_ANY_GETLOCALPATH("imguiextension/imgui.ini", s_szIniPath, sizeof(s_szIniPath)))
	{
		std::string gameDir = g_pMetaHookAPI->GetGameDirectory();
		sprintf(s_szIniPath, "%s/imguiextension/imgui.ini", gameDir.c_str());
	}
	for (char* p = s_szIniPath; *p; ++p)
		if (*p == '\\') *p = '/';
	ImGui::GetIO().IniFilename = s_szIniPath;

	LoadFontsFromConfig();

	if (ImGui_ImplWin32_Init(hWnd))
	{
		if (ImGui_ImplOpenGL2_Init())
		{
			g_pfnOldWndProc = (WNDPROC)SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)ImGui_WndProc);
			g_bImGuiInitialized = true;
		}
		else
		{
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext(g_pImGuiContext);
			g_pImGuiContext = nullptr;
		}
	}
	else
	{
		ImGui::DestroyContext(g_pImGuiContext);
		g_pImGuiContext = nullptr;
	}
}

void ImGui_Shutdown()
{
	if (!g_bImGuiInitialized) return;

	HWND hWnd = GetGameHWND();
	if (hWnd && g_pfnOldWndProc)
	{
		WNDPROC current = (WNDPROC)GetWindowLongPtr(hWnd, GWLP_WNDPROC);
		if (current == ImGui_WndProc)
		{
			SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)g_pfnOldWndProc);
		}
		g_pfnOldWndProc = nullptr;
	}

	ImGui_ImplOpenGL2_Shutdown();
	ImGui_ImplWin32_Shutdown();

	if (g_pImGuiContext)
	{
		ImGui::DestroyContext(g_pImGuiContext);
		g_pImGuiContext = nullptr;
	}

	g_bImGuiInitialized = false;
	g_bImGuiFrameReady = false;
}

void ImGui_VidReinit()
{
	if (!g_bImGuiInitialized) return;

	HWND hWnd = GetGameHWND();
	if (hWnd && g_pfnOldWndProc)
	{
		WNDPROC current = (WNDPROC)GetWindowLongPtr(hWnd, GWLP_WNDPROC);
		if (current == ImGui_WndProc)
		{
			SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)g_pfnOldWndProc);
		}
		g_pfnOldWndProc = nullptr;
	}

	ImGui_ImplOpenGL2_Shutdown();
	ImGui_ImplWin32_Shutdown();

	if (ImGui_ImplWin32_Init(hWnd))
	{
		if (ImGui_ImplOpenGL2_Init())
		{
			g_pfnOldWndProc = (WNDPROC)SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)ImGui_WndProc);
		}
		else
		{
			ImGui_ImplWin32_Shutdown();
			g_bImGuiInitialized = false;
			g_bImGuiFrameReady = false;
		}
	}
	else
	{
		g_bImGuiInitialized = false;
		g_bImGuiFrameReady = false;
	}
}
