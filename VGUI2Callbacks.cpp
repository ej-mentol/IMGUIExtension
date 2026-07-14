#include <metahook.h>
#include <chrono>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "thirdparty/imgui/imgui.h"
#include "thirdparty/imgui/imgui_internal.h"
#include "thirdparty/imgui/backends/imgui_impl_win32.h"
#include "thirdparty/imgui/backends/imgui_impl_opengl2.h"

#include <Interface/IVGUI2Extension.h>

#include "plugins.h"
#include "exportfuncs.h"
#include "InputCapture.h"
#include "VGUI2Callbacks.h"

// Callback classes bridged from VGUI2Extension
class CImGuiGameConsoleCallbacks : public IVGUI2Extension_GameConsoleCallbacks
{
public:
	int GetAltitude() const override { return 1000; }

	void Activate(VGUI2Extension_CallbackContext* CallbackContext) override { m_bConsoleVisible = true; }
	void Initialize(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void Hide(VGUI2Extension_CallbackContext* CallbackContext) override { m_bConsoleVisible = false; }
	void Clear(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void IsConsoleVisible(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void Printf(IVGUI2Extension_String* str, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void DPrintf(IVGUI2Extension_String* str, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void SetParent(vgui::VPANEL parent, VGUI2Extension_CallbackContext* CallbackContext) override {}

	bool m_bConsoleVisible = false;
};

static CImGuiGameConsoleCallbacks s_ConsoleCallbacks;

class CImGuiBaseUICallbacks : public IVGUI2Extension_BaseUICallbacks
{
public:
	int GetAltitude() const override { return 1000; } // Render on top of everything

	void Initialize(CreateInterfaceFn* factories, int count) override {}
	void Start(struct cl_enginefuncs_s* engineFuncs, int interfaceVersion) override {}
	void Shutdown() override {}

	void Key_Event(int& down, int& keynum, const char*& pszCurrentBinding, VGUI2Extension_CallbackContext* CallbackContext) override
	{
		if (down == 0)
		{
			// On keyup, GoldSrc may not pass pszCurrentBinding — use keynum→command map to find what to release.
			auto it = g_KeyToCommand.find(keynum);
			if (it != g_KeyToCommand.end())
			{
				g_HeldCommands.erase(it->second);
				g_KeyToCommand.erase(it);
			}
		}
		else
		{
			// On keydown, record the physical key→command mapping unconditionally.
			// The physical state of the key must be tracked regardless of whether ImGui has capture,
			// so that restoration on capture OFF is accurate even if the key was pressed during menu open.
			if (pszCurrentBinding)
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
		}

		if (g_Dispatcher.AnyWantsInputCapture())
		{
			// Allow keyup events to pass through so the engine can process key releases
			if (down == 0)
			{
				return;
			}

			// Allow console toggle to pass through (general engine control)
			if (pszCurrentBinding)
			{
				if (strstr(pszCurrentBinding, "toggleconsole") != nullptr)
				{
					return;
				}
			}

			// Check if any callback specifically allows this key to pass through
			if (g_Dispatcher.AnyAllowsKeyPassthrough(keynum, pszCurrentBinding))
			{
				return;
			}

			// Block key event in the engine if ImGui wants capture
			CallbackContext->Result = VGUI2Extension_Result::SUPERCEDE;
		}
	}

	void CallEngineSurfaceAppProc(void*& pevent, void*& userData, VGUI2Extension_CallbackContext* CallbackContext) override {}

	void CallEngineSurfaceWndProc(void*& hwnd, unsigned int& msg, unsigned int& wparam, long& lparam, VGUI2Extension_CallbackContext* CallbackContext) override {}

	void Paint(int& x, int& y, int& right, int& bottom, VGUI2Extension_CallbackContext* CallbackContext) override
	{
		static std::chrono::steady_clock::time_point s_LastFrameTime = std::chrono::steady_clock::now();

		ImGui_InitOnce();
		if (!g_bImGuiInitialized) return;

		ImGui::SetCurrentContext(g_pImGuiContext);

		if (!CallbackContext->IsPost)
		{
			auto now = std::chrono::steady_clock::now();
			auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - s_LastFrameTime).count();

			if (elapsed > 500)
			{
				s_LastFrameTime = now;

				HWND hWnd = GetGameHWND();
				RECT rc = { 0 };
				if (hWnd && GetClientRect(hWnd, &rc))
				{
					glwidth = rc.right - rc.left;
					glheight = rc.bottom - rc.top;
				}
				else
				{
					glwidth = right - x;
					glheight = bottom - y;
				}

				if (glwidth > 0 && glheight > 0)
				{
					auto& io = ImGui::GetIO();
					io.DisplaySize = ImVec2((float)glwidth, (float)glheight);

					static int s_lastLogWidth = 0;
					static int s_lastLogHeight = 0;
					if (glwidth != s_lastLogWidth || glheight != s_lastLogHeight)
					{
						s_lastLogWidth = glwidth;
						s_lastLogHeight = glheight;
						gEngfuncs.Con_DPrintf("[IMGUIExtension] Paint Resolution: %dx%d (VGUI: x=%d y=%d r=%d b=%d, hWnd=%p)\n",
							glwidth, glheight, x, y, right, bottom, hWnd);
					}

					UpdateInputCaptureState();

					ImGui_ImplWin32_NewFrame();
					ImGui_ImplOpenGL2_NewFrame();
					ImGui::NewFrame();

					io.MouseDrawCursor = false; // Always use hardware OS cursor

					g_Dispatcher.Dispatch([&](IImGuiExtensionCallbacks* cb) {
						ImGui::SetCurrentContext(g_pImGuiContext);
						cb->OnImGuiFrame();
					});

					g_bImGuiFrameReady = true;
				}
			}
		}
		else
		{
			if (g_bImGuiFrameReady)
			{
				ImGui::Render();
				ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
				g_bImGuiFrameReady = false;
			}
		}
	}

	void HideGameUI(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void ActivateGameUI(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void HideConsole(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void ShowConsole(VGUI2Extension_CallbackContext* CallbackContext) override {}
};

static CImGuiBaseUICallbacks s_BaseUICallbacks;

class CImGuiGameUICallbacks : public IVGUI2Extension_GameUICallbacks
{
public:
	int GetAltitude() const override { return 1000; }

	void Initialize(CreateInterfaceFn* factories, int count) override {}
	void PreStart(struct cl_enginefuncs_s* engineFuncs, int interfaceVersion, void* system) override {}
	void Start(struct cl_enginefuncs_s* engineFuncs, int interfaceVersion, void* system) override {}
	void Shutdown() override {}
	void PostShutdown() override {}

	void DisconnectFromServer(VGUI2Extension_CallbackContext* CallbackContext) override
	{
		s_bGameUIActive = true;
		g_Dispatcher.ForceReleaseInput();
		UpdateInputCaptureState();
	}
	void LoadingStarted(const char*& resourceType, const char*& resourceName, VGUI2Extension_CallbackContext* CallbackContext) override
	{
		s_bGameUIActive = true;
		g_Dispatcher.ForceReleaseInput();
		UpdateInputCaptureState();
	}
	void HideGameUI(VGUI2Extension_CallbackContext* CallbackContext) override
	{
		s_bGameUIActive = false;
	}
	void ActivateGameUI(VGUI2Extension_CallbackContext* CallbackContext) override
	{
		s_bGameUIActive = true;
		g_Dispatcher.ForceReleaseInput();
		UpdateInputCaptureState();
	}
	void ActivateDemoUI(VGUI2Extension_CallbackContext* CallbackContext) override {}

	void HasExclusiveInput(VGUI2Extension_CallbackContext* CallbackContext) override
	{
		if (AnyWantsInputCapture())
		{
			static int state = 1;
			CallbackContext->Result = VGUI2Extension_Result::SUPERCEDE;
			CallbackContext->pPluginReturnValue = &state;
		}
	}

	void IsGameUIActive(VGUI2Extension_CallbackContext* CallbackContext) override {}

	void RunFrame(VGUI2Extension_CallbackContext* CallbackContext) override {}
	void ConnectToServer(const char*& game, int& IP, int& port, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void LoadingFinished(const char*& resourceType, const char*& resourceName, VGUI2Extension_CallbackContext* CallbackContext) override
	{
		s_bGameUIActive = false;
	}
	void StartProgressBar(const char*& progressType, int& progressSteps, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void ContinueProgressBar(int& progressPoint, float& progressFraction, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void StopProgressBar(bool& bError, const char*& failureReason, const char*& extendedReason, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void SetProgressBarStatusText(const char*& statusText, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void SetSecondaryProgressBar(float& progress, VGUI2Extension_CallbackContext* CallbackContext) override {}
	void SetSecondaryProgressBarText(const char*& statusText, VGUI2Extension_CallbackContext* CallbackContext) override {}

	const char* GetControlModuleName() const override { return "ImGuiExtension"; }
};

static CImGuiGameUICallbacks s_GameUICallbacks;

void IMGUI_VGUI2Extension_Init(void)
{
	auto hVGUI2Extension = g_pMetaHookAPI->GetPluginModuleHandleByBaseFileName("VGUI2Extension");
	if (hVGUI2Extension)
	{
		auto factory = Sys_GetFactory(hVGUI2Extension);
		if (factory)
		{
			g_pVGUI2Extension = (IVGUI2Extension*)factory(VGUI2_EXTENSION_INTERFACE_VERSION, NULL);
		}
	}

	if (g_pVGUI2Extension)
	{
		g_pVGUI2Extension->RegisterBaseUICallbacks(&s_BaseUICallbacks);
		g_pVGUI2Extension->RegisterGameUICallbacks(&s_GameUICallbacks);
		g_pVGUI2Extension->RegisterGameConsoleCallbacks(&s_ConsoleCallbacks);
	}
	else
	{
		gEngfuncs.Con_Printf("[IMGUIExtension] Failed to get VGUI2Extension interface!\n");
	}
}

void IMGUI_VGUI2Extension_Shutdown(void)
{
	if (g_pVGUI2Extension)
	{
		g_pVGUI2Extension->UnregisterBaseUICallbacks(&s_BaseUICallbacks);
		g_pVGUI2Extension->UnregisterGameUICallbacks(&s_GameUICallbacks);
		g_pVGUI2Extension->UnregisterGameConsoleCallbacks(&s_ConsoleCallbacks);
	}
}
