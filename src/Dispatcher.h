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

// Restores every Dear ImGui stack (windows, IDs, styles, colors, fonts, tables, ...)
// to the depth captured before a consumer callback ran.
inline void RecoverImGuiState(ImGuiContext* ctx, const ImGuiErrorRecoveryState& state)
{
	if (!ctx) return;
	ImGuiIO& io = ctx->IO;
	const bool savedAssert = io.ConfigErrorRecoveryEnableAssert;
	const bool savedTooltip = io.ConfigErrorRecoveryEnableTooltip;

	// The recovery is intentional here; do not turn it into a Debug assert or display error tooltip.
	io.ConfigErrorRecoveryEnableAssert = false;
	io.ConfigErrorRecoveryEnableTooltip = false;
	__try
	{
		ImGui::ErrorRecoveryTryToRecoverState(&state);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		// Even if ImGui internals were corrupted by the consumer, prevent secondary crash
	}
	io.ConfigErrorRecoveryEnableAssert = savedAssert;
	io.ConfigErrorRecoveryEnableTooltip = savedTooltip;
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
			if (m_Callbacks[i]) SafeInitialize(m_Callbacks[i]);
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
			if (m_Callbacks[i]) SafeStart(m_Callbacks[i], engineFuncs, interfaceVersion);
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
		m_Faulted.clear();
		m_bInitialized = false;
		m_bStarted = false;
		m_pEngineFuncs = nullptr;
		m_iInterfaceVersion = 0;

		++m_IterationDepth;
		for (size_t i = 0; i < callbacks.size(); ++i)
		{
			if (callbacks[i]) SafeShutdown(callbacks[i]);
		}
		--m_IterationDepth;

		// Callbacks registered during shutdown have no lifetime left to serve.
		m_PendingAdditions.clear();
	}

	void Register(IImGuiExtensionCallbacks* cb)
	{
		if (!cb) return;
		ClearFaulted(cb);

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

			if (m_bInitialized) SafeInitialize(cb);
			if (m_bStarted) SafeStart(cb, m_pEngineFuncs, m_iInterfaceVersion);

			Sort();
		}
	}

	void Unregister(IImGuiExtensionCallbacks* cb)
	{
		if (!cb) return;
		ClearFaulted(cb);

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
					SafeShutdown(cb);
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
			SafeShutdown(cb);
		}
	}

	bool AnyWantsInputCapture()
	{
		++m_IterationDepth;
		bool result = false;
		for (size_t i = 0; i < m_Callbacks.size(); ++i)
		{
			auto cb = m_Callbacks[i];
			if (!cb || IsFaulted(cb))
				continue;

			bool wants = false;
			if (!SafeQuery(cb, [](IImGuiExtensionCallbacks* c) { return c->WantsInputCapture(); }, wants))
				continue; // crashed: reported and disabled, must not hold input hostage

			if (wants) { result = true; break; }
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
			if (!cb || IsFaulted(cb))
				continue;

			bool allows = false;
			if (!SafeQuery(cb, [&](IImGuiExtensionCallbacks* c) { return c->AllowKeyPassthrough(keynum, pszCurrentBinding); }, allows))
				continue;

			if (allows) { result = true; break; }
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
	void Dispatch(Func&& func)
	{
		++m_IterationDepth;
		for (size_t i = 0; i < m_Callbacks.size(); ++i)
		{
			auto cb = m_Callbacks[i];
			if (!cb || IsFaulted(cb))
				continue;

			SafeInvoke(cb, func); // a crash marks the consumer as faulted
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
	// Calls Shutdown() with crash protection. The consumer is going away,
	// so no fault flag is kept for it afterwards.
	void SafeShutdown(IImGuiExtensionCallbacks* cb)
	{
		if (!cb) return;

		DWORD code = 0;
		__try
		{
			cb->Shutdown();
		}
		__except (code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
		{
			const char* pszModuleName = SafeGetModuleName(cb);
			if (gEngfuncs.Con_Printf)
			{
				gEngfuncs.Con_Printf("[IMGUIExtension] Crash caught in consumer Shutdown(): %s (code 0x%08X).\n",
					pszModuleName ? pszModuleName : "<unknown>", code);
			}
		}
		ClearFaulted(cb);
	}

	void SafeInitialize(IImGuiExtensionCallbacks* cb)
	{
		if (!cb || IsFaulted(cb)) return;

		DWORD code = 0;
		__try
		{
			cb->Initialize();
			return;
		}
		__except (code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
		{
		}
		ReportFault(cb, code);
	}

	void SafeStart(IImGuiExtensionCallbacks* cb, cl_enginefunc_t* engineFuncs, int interfaceVersion)
	{
		if (!cb || IsFaulted(cb)) return;

		DWORD code = 0;
		__try
		{
			cb->Start(engineFuncs, interfaceVersion);
			return;
		}
		__except (code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
		{
		}
		ReportFault(cb, code);
	}

	// Runs a void callback. On a crash: reports once, disables the consumer and
	// rolls the ImGui state back to what it was before the call.
	template<typename Func>
	bool SafeInvoke(IImGuiExtensionCallbacks* cb, Func&& func)
	{
		ImGuiContext* ctx = ImGui::GetCurrentContext();
		// Never touch the recovery API unless a window is current within a frame scope.
		const bool recoverable = ctx && ctx->WithinFrameScope && ctx->CurrentWindow;

		ImGuiErrorRecoveryState recoveryState;
		if (recoverable)
			ImGui::ErrorRecoveryStoreState(&recoveryState);

		DWORD code = 0;
		__try
		{
			func(cb);
			return true;
		}
		__except (code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
		{
		}

		ReportFault(cb, code);
		if (recoverable)
			RecoverImGuiState(ctx, recoveryState);
		return false;
	}

	// Runs a bool query. Returns false if the callback crashed (outValue is then unusable).
	template<typename Func>
	bool SafeQuery(IImGuiExtensionCallbacks* cb, Func&& query, bool& outValue)
	{
		DWORD code = 0;
		__try
		{
			outValue = query(cb);
			return true;
		}
		__except (code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
		{
		}

		ReportFault(cb, code);
		return false;
	}

	void ReportFault(IImGuiExtensionCallbacks* cb, DWORD code)
	{
		MarkFaulted(cb);
		const char* pszModuleName = SafeGetModuleName(cb);
		if (gEngfuncs.Con_Printf)
		{
			gEngfuncs.Con_Printf("[IMGUIExtension] Crash caught in consumer callback: %s (code 0x%08X). "
				"The consumer is disabled until it registers again.\n",
				pszModuleName ? pszModuleName : "<unknown>", code);
		}
	}

	bool IsFaulted(const IImGuiExtensionCallbacks* cb) const
	{
		return std::find(m_Faulted.begin(), m_Faulted.end(), cb) != m_Faulted.end();
	}

	void MarkFaulted(IImGuiExtensionCallbacks* cb)
	{
		if (!IsFaulted(cb))
			m_Faulted.push_back(cb);
	}

	void ClearFaulted(IImGuiExtensionCallbacks* cb)
	{
		m_Faulted.erase(std::remove(m_Faulted.begin(), m_Faulted.end(), cb), m_Faulted.end());
	}

	// Consumers that crashed once. Skipped everywhere except Shutdown(), so they
	// still get their cleanup call.
	std::vector<IImGuiExtensionCallbacks*> m_Faulted;
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

					if (m_bInitialized) SafeInitialize(cb);
					if (m_bStarted) SafeStart(cb, m_pEngineFuncs, m_iInterfaceVersion);
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
