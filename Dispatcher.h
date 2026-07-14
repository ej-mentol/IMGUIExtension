#pragma once

#include <vector>
#include <algorithm>
#include "IImGuiExtension.h"

class CImGuiDispatcher
{
public:
	void Initialize()
	{
		m_bInitialized = true;
		for (auto cb : m_Callbacks)
		{
			if (cb) cb->Initialize();
		}
	}

	void Start(cl_enginefunc_t* engineFuncs, int interfaceVersion)
	{
		m_bStarted = true;
		m_pEngineFuncs = engineFuncs;
		m_iInterfaceVersion = interfaceVersion;
		for (auto cb : m_Callbacks)
		{
			if (cb) cb->Start(engineFuncs, interfaceVersion);
		}
	}

	void Shutdown()
	{
		for (auto cb : m_Callbacks)
		{
			if (cb) cb->Shutdown();
		}
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

		if (m_bIsIterating)
		{
			m_PendingAdditions.push_back(cb);
			return;
		}

		auto it = std::find(m_Callbacks.begin(), m_Callbacks.end(), cb);
		if (it == m_Callbacks.end())
		{
			if (m_bInitialized)
			{
				cb->Initialize();
			}
			if (m_bStarted)
			{
				cb->Start(m_pEngineFuncs, m_iInterfaceVersion);
			}

			m_Callbacks.push_back(cb);
			Sort();
		}
	}

	void Unregister(IImGuiExtensionCallbacks* cb)
	{
		if (!cb) return;

		if (m_bIsIterating)
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
			m_PendingAdditions.erase(
				std::remove(m_PendingAdditions.begin(), m_PendingAdditions.end(), cb),
				m_PendingAdditions.end()
			);
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
			cb->Shutdown();
			m_Callbacks.erase(it);
		}
	}

	bool AnyWantsInputCapture() const
	{
		for (auto cb : m_Callbacks)
		{
			if (cb && cb->WantsInputCapture())
				return true;
		}
		return false;
	}

	bool AnyAllowsKeyPassthrough(int keynum, const char* pszCurrentBinding) const
	{
		for (auto cb : m_Callbacks)
		{
			if (cb && cb->AllowKeyPassthrough(keynum, pszCurrentBinding))
				return true;
		}
		return false;
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
		m_bIsIterating = true;
		for (size_t i = 0; i < m_Callbacks.size(); ++i)
		{
			auto cb = m_Callbacks[i];
			if (cb)
			{
				func(cb);
			}
		}
		m_bIsIterating = false;

		// Clean up marked nullptrs
		m_Callbacks.erase(
			std::remove(m_Callbacks.begin(), m_Callbacks.end(), nullptr),
			m_Callbacks.end()
		);

		// Handle deferred shutdowns
		if (!m_PendingShutdowns.empty())
		{
			for (auto cb : m_PendingShutdowns)
			{
				if (cb) cb->Shutdown();
			}
			m_PendingShutdowns.clear();
		}

		// Handle deferred additions
		if (!m_PendingAdditions.empty())
		{
			for (auto cb : m_PendingAdditions)
			{
				auto it = std::find(m_Callbacks.begin(), m_Callbacks.end(), cb);
				if (it == m_Callbacks.end())
				{
					if (m_bInitialized)
					{
						cb->Initialize();
					}
					if (m_bStarted)
					{
						cb->Start(m_pEngineFuncs, m_iInterfaceVersion);
					}

					m_Callbacks.push_back(cb);
				}
			}
			m_PendingAdditions.clear();
			Sort();
		}
	}

	const std::vector<IImGuiExtensionCallbacks*>& GetCallbacks() const
	{
		return m_Callbacks;
	}

private:
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
	bool m_bIsIterating = false;

	// State tracking
	bool m_bInitialized = false;
	bool m_bStarted = false;
	cl_enginefunc_t* m_pEngineFuncs = nullptr;
	int m_iInterfaceVersion = 0;
};
