#include <algorithm>
#include <cstdint>
#include <istream>
#include <sstream>
#include <vector>

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/helpers/Workspace.hpp>

#include "globals.hpp"


const std::string k_workspaceCount = "plugin:split-monitor-workspaces:count";
const std::string k_keepFocused = "plugin:split-monitor-workspaces:keep_focused";
const CColor s_pluginColor = {0x61 / 255.0f, 0xAF / 255.0f, 0xEF / 255.0f, 1.0f};

const uint32_t COUNT_PER_MONITOR = 10;
const uint32_t SPECIAL_BASE = 1e5;
const uint32_t WORKSPACE_BASE = 1;
const uint32_t SPECIAL_COUNT = 5;

void notify(std::string msg) {
	 HyprlandAPI::addNotification(PHANDLE, msg, s_pluginColor, 5000);
}

/*
 *	 Hyprland Abstraction layer
 */

typedef int workspaceId;

struct Workspace {
	bool isSpecial;
	uint64_t id;

};

class Monitor {
private:
	CMonitor* ptr;

	Monitor(CMonitor* _ptr): ptr(_ptr) {

	}

	auto ID() {
		return ptr->ID;
	}

	workspaceId toWorkspaceId(Workspace workspace) {
		if (workspace.isSpecial) {
			if (workspace.id >= SPECIAL_COUNT)
				throw std::invalid_argument("Invalid special workspace id");

			return SPECIAL_BASE + workspace.id * SPECIAL_COUNT + ID();
		} else {
			if (workspace.id >= COUNT_PER_MONITOR)
				throw std::invalid_argument("Invalid workspace id");

			return WORKSPACE_BASE + ID() * COUNT_PER_MONITOR + workspace.id;
		}
	}

	bool owns(workspaceId wId) {
		if (wId >= SPECIAL_BASE) {
			if (wId - SPECIAL_BASE < ID()) return false;
			return (wId - SPECIAL_BASE - ID()) % SPECIAL_COUNT == 0;
		} else {
			return (wId - WORKSPACE_BASE) / COUNT_PER_MONITOR == ID();
		}
	}

	Workspace toWorkspace(workspaceId workspaceId) {
		if (workspaceId >= SPECIAL_BASE) {
			return {
				.isSpecial = true,
				.id = (workspaceId - SPECIAL_BASE - ID()) / SPECIAL_COUNT,
			};
		} else {
			return {
				.isSpecial = false,
				.id = workspaceId - WORKSPACE_BASE - ID() * COUNT_PER_MONITOR,
			};
		}
	}

public:
	Monitor(): ptr(nullptr) {};

	static Monitor current() {
		return Monitor(g_pCompositor->getMonitorFromCursor());
	}

	static Monitor fromId(int id) {
		auto ptr = g_pCompositor->getMonitorFromID(id);
		if (!ptr)
			throw std::invalid_argument("No monitor with requested id");
		return Monitor(ptr);
	}

	Workspace getActiveWorkspace() {
		workspaceId active = ptr->activeWorkspace;
		return toWorkspace(active);
	}

	std::vector<Workspace> getWorkspaces() {
		std::vector<Workspace> workspaces;
		for (const std::unique_ptr<CWorkspace>& w_ptr: g_pCompositor->m_vWorkspaces) {
			if (owns(w_ptr->m_iID)) {
				workspaces.push_back(toWorkspace(w_ptr->m_iID));
			}
		}
		std::sort(workspaces.begin(), workspaces.end(), [](auto a, auto b) { return a.id < b.id; });;
		return workspaces;
	}

	void setActiveWorkspace(Workspace w) {
		auto w_id = toWorkspaceId(w);

		auto w_ptr = g_pCompositor->getWorkspaceByID(w_id);
		if (!w_ptr) {
			w_ptr = g_pCompositor->createNewWorkspace(w_id, ID(), std::to_string(w.id + 1));
		}

		ptr->changeWorkspace(w_ptr);
	}
};

struct UWID {
	Monitor monitor;
	Workspace workspace;

	// (monitor_id | c) (a workspace_id |s special_id|(e|r) delta_id)
	UWID(std::istream& UWIDs) {
		auto token = [&UWIDs]() {
			std::string t;
			if (!std::getline(UWIDs, t, ' '))
				throw std::invalid_argument("Could not parse UWID");
			return t;
		};
		auto number = [&token]() {
			return std::stoi(token());
		};
		auto deltaMod = [](int a, int b, int mod) {
			return (a + (b % mod) + mod) % mod;
		};

		auto monitor_selector = token();
		if (monitor_selector == "c") {
			monitor = Monitor::current();
		} else {
			throw std::logic_error("Monitor selection uninplemented");
		}

		auto workspace_selection_method = token();
		if (workspace_selection_method.size() > 1)
			throw std::invalid_argument("Invalid workspace selection method");

		switch (workspace_selection_method[0]) {
			case 's': {
				workspace.isSpecial = true;
				int id = number() - 1;
				if (id < 0 || id > SPECIAL_BASE)
					throw std::invalid_argument("Invalid special id");
				workspace.id = id;
			}
			break;
			case 'a': {
				workspace.isSpecial = false;
				int id = number() - 1;
				if (id < 0 || id > COUNT_PER_MONITOR)
					throw std::invalid_argument("Invalid workspace id");
				workspace.id = id;
			}
			break;
			case 'e': {
				Workspace cur = monitor.getActiveWorkspace();
				if (cur.isSpecial)
					throw std::logic_error("Cannot use relative selector on special workspace");
				workspace.isSpecial = false;
				int delta = number();
				workspace.id = deltaMod(cur.id, delta, COUNT_PER_MONITOR);
			}
			break;
			case 'r': {
				Workspace cur = monitor.getActiveWorkspace();
				if (cur.isSpecial)
					throw std::logic_error("Cannot use relative selector on special workspace");
				workspace.isSpecial = false;
				int delta = number();
				std::vector<Workspace> workspaces = monitor.getWorkspaces();
				int i = 0;
				for (; i < workspaces.size(); i++) {
					if (workspaces[i].id == cur.id) {
						break;
					}
				}
				workspace.id = workspaces[deltaMod(i, delta, (int) workspaces.size())].id;
			}
			break;
		}
	}

	UWID(std::string in) {
		std::stringstream UWIDs(in);
		this->~UWID();
		new (this) UWID(UWIDs);
	}
};

// (window_id | c) UWID
void moveWindowToWorkspace() {
	throw std::logic_error("Not implemented");
}

// UWID
void focusWorkspace(std::string in) {
	UWID uwid(in);
	if (uwid.workspace.isSpecial)
		throw std::invalid_argument("Cannot focus a special workspace on a single monitor");
	uwid.monitor.setActiveWorkspace(uwid.workspace);
}


// Do NOT change this function.
APICALL EXPORT std::string PLUGIN_API_VERSION()
{
	return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle)
{
	PHANDLE = handle;

	HyprlandAPI::addConfigValue(PHANDLE, k_workspaceCount, SConfigValue{.intValue = 10});
	HyprlandAPI::addConfigValue(PHANDLE, k_keepFocused, SConfigValue{.intValue = 0});

	HyprlandAPI::addDispatcher(PHANDLE, "change_workspace", focusWorkspace);
	return {"split-monitor-workspaces", "Split monitor workspace namespaces", "Duckonaut", "1.1.0"};
}

APICALL EXPORT void PLUGIN_EXIT()
{
}
