#pragma once

#include <vector>
#include <algorithm>
#include "IImGuiExtension.h"
#include "plugins.h"

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
		++m_IterationDepth;
		for (size_t i = 0; i < m_Callbacks.size(); ++i)
		{
			if (m_Callbacks[i]) m_Callbacks[i]->Shutdown();
		}
		--m_IterationDepth;

		m_Callbacks.clear();
		m_PendingAdditions.clear();
		m_PendingShutdowns.clear();
		m_bInitialized = false;
		m_bStarted = false;
		m_pEngineFuncs = nullptr;
		m_iInterfaceVersion = 0;
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
			bool wasActive = false;
			for (size_t i = 0; i < m_Callbacks.size(); ++i)
			{
				if (m_Callbacks[i] == cb)
				{
					m_Callbacks[i] = nullptr;
					wasActive = true;
				}
			}
			
			// Remove from pending additions if it got stuck there
			auto itAdd = std::find(m_PendingAdditions.begin(), m_PendingAdditions.end(), cb);
			if (itAdd != m_PendingAdditions.end())
				m_PendingAdditions.erase(itAdd);

			if (wasActive)
			{
				if (std::find(m_PendingShutdowns.begin(), m_PendingShutdowns.end(), cb) == m_PendingShutdowns.end())
				{
					m_PendingShutdowns.push_back(cb);
				}
			}
			return;
		}

		auto it = std::find(m_Callbacks.begin(), m_Callbacks.end(), cb);
		if (it != m_Callbacks.end())
		{
			// CRITICAL FIX: Erase the iterator from memory FIRST, THEN call Shutdown.
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
		__try
		{
			func(cb);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			gEngfuncs.Con_Printf("[IMGUIExtension] Crash caught in consumer callback: %s (code 0x%08X)\n",
				cb->GetModuleName(), GetExceptionCode());
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

		// 2. Process deferred shutdowns
		if (!m_PendingShutdowns.empty())
		{
			// Copy the queue to iterate safely in case Shutdown() appends more elements
			auto pending = m_PendingShutdowns;
			m_PendingShutdowns.clear();
			
			for (size_t i = 0; i < pending.size(); ++i)
			{
				if (pending[i]) pending[i]->Shutdown();
			}
		}

		// 3. Process deferred additions
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
		std::sort(m_Callbacks.begin(), m_Callbacks.end(), [](IImGuiExtensionCallbacks* a, IImGuiExtensionCallbacks* b) {
			if (!a) return false;
			if (!b) return true;
			return a->GetAltitude() > b->GetAltitude();
		});
	}

	std::vector<IImGuiExtensionCallbacks*> m_Callbacks;
	std::vector<IImGuiExtensionCallbacks*> m_PendingAdditions;
	std::vector<IImGuiExtensionCallbacks*> m_PendingShutdowns;
	int m_IterationDepth = 0;

	// State tracking
	bool m_bInitialized = false;
	bool m_bStarted = false;
	cl_enginefunc_t* m_pEngineFuncs = nullptr;
	int m_iInterfaceVersion = 0;
};