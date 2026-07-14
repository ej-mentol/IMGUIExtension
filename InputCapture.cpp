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

// Converts a vgui::KeyCode (the keynum passed to Key_Event) to the corresponding Windows VK code.
// This is a system-level mapping between two key enumeration systems — not a gameplay assumption.
// Covers the full vgui::KeyCode enum from KeyCode.h so any player bind is handled correctly.
static int VKFromVguiKeyCode(int k)
{
	// KEY_0..KEY_9 = 1..10  →  '0'..'9'
	if (k >= 1  && k <= 10) return '0' + (k - 1);
	// KEY_A..KEY_Z = 11..36  →  'A'..'Z'
	if (k >= 11 && k <= 36) return 'A' + (k - 11);
	// KEY_PAD_0..KEY_PAD_9 = 37..46  →  VK_NUMPAD0..VK_NUMPAD9
	if (k >= 37 && k <= 46) return VK_NUMPAD0 + (k - 37);

	switch (k)
	{
	case 47: return VK_DIVIDE;    // KEY_PAD_DIVIDE
	case 48: return VK_MULTIPLY;  // KEY_PAD_MULTIPLY
	case 49: return VK_SUBTRACT;  // KEY_PAD_MINUS
	case 50: return VK_ADD;       // KEY_PAD_PLUS
	case 51: return VK_RETURN;    // KEY_PAD_ENTER  (same VK as main Enter)
	case 52: return VK_DECIMAL;   // KEY_PAD_DECIMAL
	case 53: return VK_OEM_4;     // KEY_LBRACKET
	case 54: return VK_OEM_6;     // KEY_RBRACKET
	case 55: return VK_OEM_1;     // KEY_SEMICOLON
	case 56: return VK_OEM_7;     // KEY_APOSTROPHE
	case 57: return VK_OEM_3;     // KEY_BACKQUOTE
	case 58: return VK_OEM_COMMA; // KEY_COMMA
	case 59: return VK_OEM_PERIOD;// KEY_PERIOD
	case 60: return VK_OEM_2;     // KEY_SLASH
	case 61: return VK_OEM_5;     // KEY_BACKSLASH
	case 62: return VK_OEM_MINUS; // KEY_MINUS
	case 63: return VK_OEM_PLUS;  // KEY_EQUAL
	case 64: return VK_RETURN;    // KEY_ENTER
	case 65: return VK_SPACE;     // KEY_SPACE
	case 66: return VK_BACK;      // KEY_BACKSPACE
	case 67: return VK_TAB;       // KEY_TAB
	case 68: return VK_CAPITAL;   // KEY_CAPSLOCK
	case 69: return VK_NUMLOCK;   // KEY_NUMLOCK
	case 70: return VK_ESCAPE;    // KEY_ESCAPE
	case 71: return VK_SCROLL;    // KEY_SCROLLLOCK
	case 72: return VK_INSERT;    // KEY_INSERT
	case 73: return VK_DELETE;    // KEY_DELETE
	case 74: return VK_HOME;      // KEY_HOME
	case 75: return VK_END;       // KEY_END
	case 76: return VK_PRIOR;     // KEY_PAGEUP
	case 77: return VK_NEXT;      // KEY_PAGEDOWN
	case 78: return VK_PAUSE;     // KEY_BREAK
	case 79: return VK_LSHIFT;    // KEY_LSHIFT
	case 80: return VK_RSHIFT;    // KEY_RSHIFT
	case 81: return VK_LMENU;     // KEY_LALT
	case 82: return VK_RMENU;     // KEY_RALT
	case 83: return VK_LCONTROL;  // KEY_LCONTROL
	case 84: return VK_RCONTROL;  // KEY_RCONTROL
	case 85: return VK_LWIN;      // KEY_LWIN
	case 86: return VK_RWIN;      // KEY_RWIN
	case 87: return VK_APPS;      // KEY_APP
	case 88: return VK_UP;        // KEY_UP
	case 89: return VK_LEFT;      // KEY_LEFT
	case 90: return VK_DOWN;      // KEY_DOWN
	case 91: return VK_RIGHT;     // KEY_RIGHT
	case 92: return VK_F1;        // KEY_F1
	case 93: return VK_F2;        // KEY_F2
	case 94: return VK_F3;        // KEY_F3
	case 95: return VK_F4;        // KEY_F4
	case 96: return VK_F5;        // KEY_F5
	case 97: return VK_F6;        // KEY_F6
	case 98: return VK_F7;        // KEY_F7
	case 99: return VK_F8;        // KEY_F8
	case 100: return VK_F9;       // KEY_F9
	case 101: return VK_F10;      // KEY_F10
	case 102: return VK_F11;      // KEY_F11
	case 103: return VK_F12;      // KEY_F12
	}
	return 0;
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
			// Build snapshot from g_KeyToCommand using keynum→VK→GetAsyncKeyState.
			// This is fully bind-agnostic: we check the actual physical key stored during
			// keydown tracking, not an assumed mapping from command string to key name.
			g_PreCaptureCommands.clear();
			for (const auto& [keynum, cmd] : g_KeyToCommand)
			{
				int vk = VKFromVguiKeyCode(keynum);
				if (vk != 0 && (GetAsyncKeyState(vk) & 0x8000))
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
