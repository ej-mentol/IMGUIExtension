#pragma once

#include <vector>
#include <algorithm>

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>

#include "IImGuiExtension.h"
#include "plugins.h"

inline const char* SafeGetModuleName(IImGuiExtensionCallbacks* cb)
{
	__try
	{
		return cb ? cb->GetModuleName() : "<null>";
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return "<crashed in GetModuleName>";
	}
}

// Single-threaded, re-entrancy-tolerant callback registry.
//
// Contract:
//  - Everything runs on the engine's main thread inside GoldSrc hooks.
//  - Register/Unregister may be called from inside a dispatch (OnImGuiFrame,
//    WantsInputCapture, Shutdown, ...). Structural changes are applied through
//    index-based iteration plus a deferred-addition queue.
//  - Unregister() invokes Shutdown() synchronously, before returning, because the
//    caller may destroy itself immediately afterwards.
class CImGuiDispatcher
{
public:
	void Initialize()
	{
		m_bInitialized = true;
		++m_IterationDepth;
		for (size_t i = 0; i < m_Callbacks.size(); ++i)
		{
			if (m_Callbacks[i]) m_Callbacks[i]->Initialize();
		}
		--m_IterationDepth;
		ProcessPending();
	}

	void Start(cl_enginefunc_t* engineFuncs, int interfaceVersion)
	{
		m_bStarted = true;
		m_pEngineFuncs = engineFuncs;
		m_iInterfaceVersion = interfaceVersion;
		++m_IterationDepth;
		for (size_t i = 0; i < m_Callbacks.size(); ++i)
		{
			if (m_Callbacks[i]) m_Callbacks[i]->Start(engineFuncs, interfaceVersion);
		}
		--m_IterationDepth;
		ProcessPending();
	}

	void Shutdown()
	{
		// Detach the list first: a consumer that calls Unregister() from inside its
		// own Shutdown() would otherwise recurse into this loop and destroy the
		// same object twice.
		std::vector<IImGuiExtensionCallbacks*> callbacks = std::move(m_Callbacks);
		m_Callbacks.clear();
		m_PendingAdditions.clear();
		m_bInitialized = false;
		m_bStarted = false;
		m_pEngineFuncs = nullptr;
		m_iInterfaceVersion = 0;

		++m_IterationDepth;
		for (size_t i = 0; i < callbacks.size(); ++i)
		{
			if (callbacks[i]) callbacks[i]->Shutdown();
		}
		--m_IterationDepth;

		// Callbacks registered during shutdown have no lifetime left to serve.
		m_PendingAdditions.clear();
	}

	void Register(IImGuiExtensionCallbacks* cb)
	{
		if (!cb) return;

		if (m_IterationDepth > 0)
		{
			m_PendingAdditions.push_back(cb);
			return;
		}

		auto it = std::find(m_Callbacks.begin(), m_Callbacks.end(), cb);
		if (it == m_Callbacks.end())
		{
			// Add to the vector BEFORE calling to prevent re-entrancy issues
			m_Callbacks.push_back(cb);

			if (m_bInitialized) cb->Initialize();
			if (m_bStarted) cb->Start(m_pEngineFuncs, m_iInterfaceVersion);

			Sort();
		}
	}

	void Unregister(IImGuiExtensionCallbacks* cb)
	{
		if (!cb) return;

		if (m_IterationDepth > 0)
		{
			// Reentrant path (called from within WantsInputCapture, AllowKeyPassthrough, etc.)
			// Shutdown() runs immediately: the caller may destroy itself right after
			// Unregister() returns, so deferring it would be a use-after-free.
			// The slot is cleared first so a re-entrant query inside Shutdown() cannot
			// reach the object being torn down. Index-based iteration makes the
			// structural change safe.
			for (size_t i = 0; i < m_Callbacks.size(); ++i)
			{
				if (m_Callbacks[i] == cb)
				{
					m_Callbacks[i] = nullptr;
					cb->Shutdown();
				}
			}

			// Remove from pending additions if it never made it into the main list
			auto itAdd = std::find(m_PendingAdditions.begin(), m_PendingAdditions.end(), cb);
			if (itAdd != m_PendingAdditions.end())
				m_PendingAdditions.erase(itAdd);

			return;
		}

		auto it = std::find(m_Callbacks.begin(), m_Callbacks.end(), cb);
		if (it != m_Callbacks.end())
		{
			// Erase the slot first, then call Shutdown.
			m_Callbacks.erase(it);
			cb->Shutdown();
		}
	}

	bool AnyWantsInputCapture()
	{
		++m_IterationDepth;
		bool result = false;
		for (size_t i = 0; i < m_Callbacks.size(); ++i)
		{
			auto cb = m_Callbacks[i];
			if (cb)
			{
				__try {
					if (cb->WantsInputCapture()) { result = true; break; }
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					// Ignore crashes inside the consumer plugin
				}
			}
		}
		--m_IterationDepth;
		// Important: Do not call ProcessPending() here to avoid breaking structures during interrupts
		return result;
	}

	bool AnyAllowsKeyPassthrough(int keynum, const char* pszCurrentBinding)
	{
		++m_IterationDepth;
		bool result = false;
		for (size_t i = 0; i < m_Callbacks.size(); ++i)
		{
			auto cb = m_Callbacks[i];
			if (cb)
			{
				__try {
					if (cb->AllowKeyPassthrough(keynum, pszCurrentBinding)) { result = true; break; }
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					// Ignore crashes inside the consumer plugin
				}
			}
		}
		--m_IterationDepth;
		return result;
	}

	void ForceReleaseInput()
	{
		Dispatch([](IImGuiExtensionCallbacks* cb) {
			if (cb) cb->OnForceRelease();
		});
	}

	template<typename Func>
	static void SafeInvoke(IImGuiExtensionCallbacks* cb, Func&& func)
	{
		ImGuiContext* ctx = ImGui::GetCurrentContext();
		const int savedWindowStackSize = ctx ? ctx->CurrentWindowStack.Size : 0;

		__try
		{
			func(cb);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			const char* pszModuleName = SafeGetModuleName(cb);
			gEngfuncs.Con_Printf("[IMGUIExtension] Crash caught in consumer callback: %s (code 0x%08X)\n",
				pszModuleName ? pszModuleName : "<unknown>", GetExceptionCode());

			// Restore ImGui window stack — if the callback crashed inside Begin() without
			// a matching End(), CurrentWindowStack is imbalanced. The next callback would
			// inherit corrupted state: IM_ASSERT in Debug, silent memory corruption in Release.
			if (ctx)
			{
				while (ctx->CurrentWindowStack.Size > savedWindowStackSize)
				{
					ImGuiWindow* window = ctx->CurrentWindow;
					if (window && (window->Flags & ImGuiWindowFlags_ChildWindow))
						ImGui::EndChild();
					else
						ImGui::End();
				}
			}
		}
	}

	template<typename Func>
	void Dispatch(Func&& func)
	{
		++m_IterationDepth;
		for (size_t i = 0; i < m_Callbacks.size(); ++i)
		{
			auto cb = m_Callbacks[i];
			if (cb)
			{
				SafeInvoke(cb, func);
			}
		}
		--m_IterationDepth;

		ProcessPending();
	}

	std::vector<IImGuiExtensionCallbacks*> GetCallbacks() const
	{
		std::vector<IImGuiExtensionCallbacks*> result;
		result.reserve(m_Callbacks.size());
		for (size_t i = 0; i < m_Callbacks.size(); ++i)
		{
			if (m_Callbacks[i]) result.push_back(m_Callbacks[i]);
		}
		return result;
	}

private:
	void ProcessPending()
	{
		if (m_IterationDepth > 0) return;

		// 1. Clear null pointers
		auto newEnd = std::remove(m_Callbacks.begin(), m_Callbacks.end(), nullptr);
		if (newEnd != m_Callbacks.end())
		{
			m_Callbacks.erase(newEnd, m_Callbacks.end());
		}

		// 2. Process deferred additions
		if (!m_PendingAdditions.empty())
		{
			auto pending = m_PendingAdditions;
			m_PendingAdditions.clear();

			bool added = false;
			for (size_t i = 0; i < pending.size(); ++i)
			{
				auto cb = pending[i];
				if (!cb) continue;

				auto it = std::find(m_Callbacks.begin(), m_Callbacks.end(), cb);
				if (it == m_Callbacks.end())
				{
					m_Callbacks.push_back(cb);
					added = true;

					if (m_bInitialized) cb->Initialize();
					if (m_bStarted) cb->Start(m_pEngineFuncs, m_iInterfaceVersion);
				}
			}

			if (added) Sort();
		}
	}

	void Sort()
	{
		std::stable_sort(m_Callbacks.begin(), m_Callbacks.end(), [](IImGuiExtensionCallbacks* a, IImGuiExtensionCallbacks* b) {
			if (!a) return false;
			if (!b) return true;
			return a->GetAltitude() > b->GetAltitude();
		});
	}

	std::vector<IImGuiExtensionCallbacks*> m_Callbacks;
	std::vector<IImGuiExtensionCallbacks*> m_PendingAdditions;
	int m_IterationDepth = 0;

	// State tracking
	bool m_bInitialized = false;
	bool m_bStarted = false;
	cl_enginefunc_t* m_pEngineFuncs = nullptr;
	int m_iInterfaceVersion = 0;
};
