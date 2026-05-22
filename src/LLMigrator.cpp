#include "LLMigrator.hpp"
#include <format>
#include <fstream>
#include <algorithm>
#include <regex>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <mutex>

namespace fs = std::filesystem;

namespace RobCoMigrator
{
	bool g_bSafeToRevert = false;
	static std::mutex logMutex;

	void Log(const std::string& msg) {
		std::lock_guard<std::mutex> lock(logMutex);
		fs::path logPath = fs::path("Data") / "F4SE" / "Plugins" / "RobCoMigrator.log";
		std::ofstream logFile(logPath, std::ios_base::app);
		if (logFile.is_open()) {
			auto now = std::chrono::system_clock::now();
			auto time_t_now = std::chrono::system_clock::to_time_t(now);
			std::tm tm;
			localtime_s(&tm, &time_t_now);
			logFile << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " | " << msg << "\n";
		}
	}

	std::string GetEditorID(RE::TESForm* a_form) {
		if (!a_form) return "UNKNOWN";
		const char* edID = a_form->GetFormEditorID();
		if (edID && edID[0] != '\0') return std::string(edID);
		return std::format("{:08X}", a_form->GetFormID());
	}

	std::string GetFullName(RE::TESForm* a_form) {
		if (!a_form) return "";
		// FIX: C++23 Concepts bypass. TESFullName is a Mixin, not a direct TESForm descendant.
		auto fullNameComponent = dynamic_cast<RE::TESFullName*>(a_form);
		if (fullNameComponent) {
			const char* name = fullNameComponent->fullName.c_str();
			if (name && name[0] != '\0') return std::string(name);
		}
		return "";
	}

	std::string GetRobCoID(RE::TESForm* a_form) {
		if (!a_form) return "UNKNOWN|0";

		uint32_t formID = a_form->GetFormID();
		auto file = a_form->GetFile(0);
		std::string pluginName = file ? file->filename.data() : "UNKNOWN";

		// Determine if it's a light plugin by checking the load index (first byte)
		uint32_t loadIndex = (formID >> 24) & 0xFF;

		if (loadIndex == 0xFE) {
			// Light plugin (ESL): Local ID is the last 3 hex digits (12 bits)
			// RobCo Patcher expects exactly 3 digits for light plugins
			return std::format("{}|{:X}", pluginName, formID & 0xFFF);
		} else {
			// Standard plugin (ESM/ESP): Local ID is the last 6 hex digits (24 bits)
			// This captures the correct ID for Fallout4.esm and all standard mods
			return std::format("{}|{:X}", pluginName, formID & 0xFFFFFF);
		}
	}

	std::string GetFileHeaderTimestamp(const fs::path& a_path, const std::string& a_fallbackTimestamp) {
		std::ifstream file(a_path);
		std::string line;
		if (std::getline(file, line)) {
			std::regex pattern(R"(\[([0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2})\])");
			std::smatch match;
			if (std::regex_search(line, match, pattern) && match.size() > 1) {
				std::string ts = match[1].str();
				std::replace(ts.begin(), ts.end(), ':', '-');
				std::replace(ts.begin(), ts.end(), ' ', '_');
				return ts;
			}
		}
		return a_fallbackTimestamp;
	}

	std::map<std::string, std::set<std::string>> ParseExistingINI(const fs::path& a_path) {
		std::map<std::string, std::set<std::string>> existingInjections;
		std::ifstream ini(a_path);
		if (!ini.is_open()) return existingInjections;

		std::string line;
		const std::regex pattern(R"(filterByLLs=([^:]+):addToLLs=(.*))");
		std::smatch matches;

		while (std::getline(ini, line)) {
			if (std::regex_search(line, matches, pattern) && matches.size() == 3) {
				std::string targetList = matches[1].str();
				std::string injectionsStr = matches[2].str();
				std::stringstream ss(injectionsStr);
				std::string token;
				while (std::getline(ss, token, ',')) {
					auto tildePos = token.find('~');
					if (tildePos != std::string::npos) {
						existingInjections[targetList].insert(token.substr(0, tildePos));
					} else {
						existingInjections[targetList].insert(token);
					}
				}
			}
		}
		return existingInjections;
	}

	void WriteINI(const fs::path& a_outPath, const std::vector<TargetListData>& a_sortedData, bool a_append, const std::string& a_displayTimestamp) {
		auto mode = a_append ? std::ios_base::app : std::ios_base::trunc;
		std::ofstream ini(a_outPath, mode);
		if (!ini.is_open()) return;

		if (!a_append) {
			ini << std::format("// Created by RobCo Migrator on [{}]\n\n", a_displayTimestamp);
		} else if (a_append && !a_sortedData.empty()) {
			ini << "\n// --- MERGED ADDITIONS ---\n";
			ini << std::format("// Merged on [{}]\n\n", a_displayTimestamp);
		}

		for (const auto& data : a_sortedData) {
			std::string block = std::format("// [{}]\n", data.listEditorID);
			std::string command = std::format("filterByLLs={}:addToLLs=", data.listRobCoID);

			for (size_t i = 0; i < data.entries.size(); ++i) {
				const auto& entry = data.entries[i];
				block += entry.commentBlock;
				command += std::format("{}~{}~{}~{}", entry.formRobCoID, entry.level, entry.count, entry.chanceNone);
				if (i < data.entries.size() - 1) command += ",";
			}
			ini << block << command << "\n\n";
		}
	}

	void WriteCSV(const fs::path& a_outPath, const std::vector<TargetListData>& a_sortedData, bool a_append) {
		auto mode = a_append ? std::ios_base::app : std::ios_base::trunc;
		std::ofstream csv(a_outPath, mode);
		if (!csv.is_open()) return;

		if (!a_append) {
			csv << "TargetList_EditorID,TargetList_PluginID,InjectedForm_EditorID,InjectedForm_Name,InjectedForm_PluginID,Level,Count,ChanceNone\n";
		}

		for (const auto& data : a_sortedData) {
			for (const auto& entry : data.entries) {
				std::string safeName = entry.formName.empty() ? "N/A" : std::format("\"{}\"", entry.formName);
				csv << std::format("{},{},{},{},{},{},{},{}\n",
								   data.listEditorID, data.listRobCoID,
					   entry.formEditorID, safeName, entry.formRobCoID,
					   entry.level, entry.count, entry.chanceNone);
			}
		}
	}

	std::int32_t GeneratePatch(std::monostate, RE::BSFixedString a_modFolderName, std::int32_t a_exportMode) {
		Log(std::format("GeneratePatch Triggered. Mode: {}", a_exportMode));

		if (g_bSafeToRevert) {
			Log("GeneratePatch blocked - user already generated a patch this session.");
			return 2;
		}

		auto dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler || a_modFolderName.empty()) return -1;

		ExportMode mode = static_cast<ExportMode>(a_exportMode);
		fs::path targetDir = fs::path("Data") / "F4SE" / "Plugins" / "RobCo_Patcher" / "leveledList" / a_modFolderName.c_str();
		fs::create_directories(targetDir);

		// FIX: Hardcode to avoid virtual table walk of stripped PlayerCharacter methods
		std::string playerName = "Player";
		if (auto player = RE::PlayerCharacter::GetSingleton()) {
			auto displayName = player->GetDisplayName();
			if (displayName && !displayName->empty()) {
				playerName = displayName->c_str();
			}
		}

		auto now = std::chrono::system_clock::now();
		auto time_t_now = std::chrono::system_clock::to_time_t(now);
		std::tm tm;
		localtime_s(&tm, &time_t_now);

		std::ostringstream fileTsOss;
		fileTsOss << std::put_time(&tm, "%Y%m%d_%H%M%S");
		std::string fileTimestamp = fileTsOss.str();

		std::ostringstream displayTsOss;
		displayTsOss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
		std::string displayTimestamp = displayTsOss.str();

		std::string baseNameINI = std::format("Patch_{}", playerName);
		std::string baseNameCSV = std::format("Data_{}", playerName);

		fs::path iniPath = targetDir / std::format("{}.ini", baseNameINI);
		fs::path csvPath = targetDir / std::format("{}.csv", baseNameCSV);

		std::map<std::string, std::set<std::string>> existingCache;
		bool shouldAppend = false;

		if (fs::exists(iniPath)) {
			if (mode == MODE_CREATE_NEW) {
				iniPath = targetDir / std::format("{}_{}.ini", baseNameINI, fileTimestamp);
				csvPath = targetDir / std::format("{}_{}.csv", baseNameCSV, fileTimestamp);
			}
			else if (mode == MODE_OVERWRITE) {
				std::string oldTimestamp = GetFileHeaderTimestamp(iniPath, fileTimestamp);
				fs::path backupINI = targetDir / std::format("{}_{}.old", baseNameINI, oldTimestamp);
				fs::path backupCSV = targetDir / std::format("{}_{}.old", baseNameCSV, oldTimestamp);
				fs::rename(iniPath, backupINI);
				if (fs::exists(csvPath)) fs::rename(csvPath, backupCSV);
			}
			else if (mode == MODE_MERGE) {
				existingCache = ParseExistingINI(iniPath);
				shouldAppend = true;
			}
		}

		std::vector<TargetListData> groupedInjections;

		for (auto listForm : dataHandler->GetFormArray<RE::TESLevItem>()) {
			if (!listForm || listForm->scriptListCount <= 0 || !listForm->scriptAddedLists) continue;

			TargetListData listData;
			listData.listEditorID = GetEditorID(listForm);
			listData.listRobCoID = GetRobCoID(listForm);

			for (std::int8_t i = 0; i < listForm->scriptListCount; ++i) {
				auto entry = listForm->scriptAddedLists[i];
				if (!entry || !entry->form) continue;

				std::string injectRobCoID = GetRobCoID(entry->form);

				if (mode == MODE_MERGE && existingCache.contains(listData.listRobCoID)) {
					if (existingCache.at(listData.listRobCoID).contains(injectRobCoID)) {
						continue;
					}
				}

				std::string itemDisplay = GetEditorID(entry->form);
				std::string name = GetFullName(entry->form);
				if (!name.empty()) itemDisplay += std::format(" (\"{}\")", name);
				std::string commentStr = std::format("//     ╰─ [Lvl {}] {}\n", entry->level, itemDisplay);

				listData.entries.push_back({
					GetEditorID(entry->form),
										   injectRobCoID,
										   GetFullName(entry->form),
										   entry->level,
										   entry->count,
										   entry->chanceNone,
										   commentStr
				});
			}

			if (!listData.entries.empty()) {
				std::sort(listData.entries.begin(), listData.entries.end(), [](const InjectionEntry& a, const InjectionEntry& b) {
					if (a.level != b.level) return a.level < b.level;
					return a.formEditorID < b.formEditorID;
				});
				groupedInjections.push_back(std::move(listData));
			}
		}

		if (groupedInjections.empty()) return 0;

		std::sort(groupedInjections.begin(), groupedInjections.end(), [](const TargetListData& a, const TargetListData& b) {
			if (a.entries.size() != b.entries.size()) return a.entries.size() > b.entries.size();
			return a.listEditorID < b.listEditorID;
		});

		WriteINI(iniPath, groupedInjections, shouldAppend, displayTimestamp);
		WriteCSV(csvPath, groupedInjections, shouldAppend);

		Log("GeneratePatch finished successfully.");
		g_bSafeToRevert = true;
		return 1;
	}

	bool g_bUserUnlockedRevert = false;

	void SetRevertUnlocked(std::monostate, bool a_unlocked) {
		g_bUserUnlockedRevert = a_unlocked;
		Log(std::format("User UI Toggle set to: {}", a_unlocked ? "true" : "false"));
	}

	std::int32_t GetRevertStatus(std::monostate) {
		if (!g_bUserUnlockedRevert) return -2;
		if (!g_bSafeToRevert) return -1;
		return 1;
	}

	std::vector<RE::TESForm*> GetInjectedLists(std::monostate) {
		std::vector<RE::TESForm*> result;
		auto dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) return result;

		for (auto listForm : dataHandler->GetFormArray<RE::TESLevItem>()) {
			if (listForm && listForm->scriptListCount > 0) {
				result.push_back(listForm);
			}
		}

		g_bSafeToRevert = false;
		g_bUserUnlockedRevert = false;

		Log(std::format("Handed off {} injected Leveled Items to Papyrus for Revert().", result.size()));
		return result;
	}

	bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm) {
		// FIX: Decoupled native binding signature
		a_vm->BindNativeMethod(new RE::BSScript::NativeFunction("RobCoMigrator", "GeneratePatch", GeneratePatch));
		a_vm->BindNativeMethod(new RE::BSScript::NativeFunction("RobCoMigrator", "SetRevertUnlocked", SetRevertUnlocked));
		a_vm->BindNativeMethod(new RE::BSScript::NativeFunction("RobCoMigrator", "GetRevertStatus", GetRevertStatus));
		a_vm->BindNativeMethod(new RE::BSScript::NativeFunction("RobCoMigrator", "GetInjectedLists", GetInjectedLists));
		return true;
	}
}
