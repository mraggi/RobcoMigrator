#include "LLMigrator.hpp"
#include <format>
#include <fstream>
#include <algorithm>
#include <regex>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <cctype>

namespace fs = std::filesystem;

namespace RobCoMigrator
{
	bool g_bSafeToRevert = false;
	bool g_bUserUnlockedRevert = false;
	std::int32_t g_lastFixCount = 0;
	static std::mutex logMutex;

	static constexpr const char* kModFolderName = "RobCoMigrator_Export";

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

		uint32_t loadIndex = (formID >> 24) & 0xFF;

		if (loadIndex == 0xFE) {
			return std::format("{}|{:X}", pluginName, formID & 0xFFF);
		} else {
			return std::format("{}|{:X}", pluginName, formID & 0xFFFFFF);
		}
	}

	RE::TESForm* LookupFormByRobCoID(const std::string& a_robCoID) {
		auto dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) return nullptr;
		auto pos = a_robCoID.find('|');
		if (pos == std::string::npos || pos == 0 || pos + 1 >= a_robCoID.size()) return nullptr;
		std::string pluginName = a_robCoID.substr(0, pos);
		std::string idHex = a_robCoID.substr(pos + 1);
		try {
			auto localID = static_cast<std::uint32_t>(std::stoul(idHex, nullptr, 16));
			return dataHandler->FindForm(localID, pluginName);
		} catch (...) {
			return nullptr;
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

	std::string GetCurrentPlayerNameRaw() {
		std::string playerName = "Player";
		if (auto player = RE::PlayerCharacter::GetSingleton()) {
			auto displayName = player->GetDisplayName();
			if (displayName && !displayName->empty()) {
				playerName = displayName->c_str();
			}
		}
		return playerName;
	}

	InjectionEntry BuildInjectionEntry(const std::string& a_robCoID, std::uint16_t a_level, std::uint16_t a_count, std::int8_t a_chanceNone) {
		InjectionEntry entry;
		entry.formRobCoID = a_robCoID;
		entry.level = a_level;
		entry.count = a_count;
		entry.chanceNone = a_chanceNone;

		auto form = LookupFormByRobCoID(a_robCoID);
		if (form) {
			entry.formEditorID = GetEditorID(form);
			entry.formName = GetFullName(form);
		} else {
			entry.formEditorID = a_robCoID;
			entry.formName = "";
		}

		std::string itemDisplay = entry.formEditorID;
		if (!entry.formName.empty()) {
			itemDisplay += std::format(" (\"{}\")", entry.formName);
		}
		if (!form) {
			itemDisplay += " [not currently loaded]";
		}
		entry.commentBlock = std::format("//     ╰─ [Lvl {}] {}\n", entry.level, itemDisplay);

		return entry;
	}

	struct ParsedFile {
		std::vector<TargetListData> lists;
		std::vector<std::string> preservedLines;
	};

	struct LenientParseResult {
		bool ok = false;
		bool wasFixed = false;
		InjectionEntry entry;
	};

	// Lenient single-entry parser:
	//   - trims whitespace
	//   - strips stray "addToLLs=" prefixes (handles user copy-paste mistakes like
	//     "filterByLLs=X:addToLLs=addToLLs=Plugin|ID")
	//   - if level/count/chanceNone are missing or unparseable, defaults to 1/1/0
	//   - wasFixed = true whenever any auto-correction was applied
	LenientParseResult ParseEntryLenient(std::string token) {
		LenientParseResult result;

		auto trim = [](std::string& s) {
			std::size_t start = s.find_first_not_of(" \t\r\n");
			std::size_t end = s.find_last_not_of(" \t\r\n");
			s = (start == std::string::npos) ? std::string{} : s.substr(start, end - start + 1);
		};
		trim(token);

		while (token.starts_with("addToLLs=")) {
			token = token.substr(9);
			trim(token);
			result.wasFixed = true;
		}

		if (token.empty()) return result;

		std::vector<std::string> parts;
		std::stringstream ss(token);
		std::string part;
		while (std::getline(ss, part, '~')) {
			trim(part);
			parts.push_back(std::move(part));
		}

		if (parts.empty() || parts[0].empty() || parts[0].find('|') == std::string::npos) {
			return result;
		}

		std::string robCoID = parts[0];
		std::int32_t level = 1, count = 1, chanceNone = 0;

		auto parseField = [&result](const std::string& s, std::int32_t defaultVal) -> std::int32_t {
			if (s.empty()) {
				result.wasFixed = true;
				return defaultVal;
			}
			try {
				std::size_t pos = 0;
				int v = std::stoi(s, &pos);
				for (std::size_t i = pos; i < s.size(); ++i) {
					if (!std::isspace(static_cast<unsigned char>(s[i]))) {
						result.wasFixed = true;
						return defaultVal;
					}
				}
				return v;
			} catch (...) {
				result.wasFixed = true;
				return defaultVal;
			}
		};

		if (parts.size() >= 2) { level = parseField(parts[1], 1); } else { result.wasFixed = true; }
		if (parts.size() >= 3) { count = parseField(parts[2], 1); } else { result.wasFixed = true; }
		if (parts.size() >= 4) { chanceNone = parseField(parts[3], 0); } else { result.wasFixed = true; }
		if (parts.size() > 4) { result.wasFixed = true; }

		result.entry = BuildInjectionEntry(robCoID,
			static_cast<std::uint16_t>(level),
			static_cast<std::uint16_t>(count),
			static_cast<std::int8_t>(chanceNone));
		result.ok = true;
		return result;
	}

	// Parse every filterByLLs line we can read. Each line is processed independently:
	//   - parseable entries are auto-corrected if needed (defaults applied, prefixes stripped)
	//   - entries we can't recognize at all cause the WHOLE line to be preserved verbatim
	//     so no user data is ever lost
	// Comments are dropped (will be regenerated). Other non-comment lines are preserved.
	ParsedFile ParseExistingINIFull(const fs::path& a_path) {
		ParsedFile result;
		std::ifstream ini(a_path);
		if (!ini.is_open()) return result;

		const std::regex linePattern(R"(\s*filterByLLs\s*=\s*([^:]+?)\s*:\s*addToLLs\s*=\s*(.+))");

		std::string line;
		while (std::getline(ini, line)) {
			if (!line.empty() && line.back() == '\r') line.pop_back();

			std::size_t firstNonWs = line.find_first_not_of(" \t");
			if (firstNonWs == std::string::npos) continue;

			std::string_view sv(line.data() + firstNonWs, line.size() - firstNonWs);
			if (sv.starts_with("//")) continue;

			std::smatch matches;
			if (!std::regex_match(line, matches, linePattern)) {
				result.preservedLines.push_back(line);
				++g_lastFixCount;
				continue;
			}

			std::string targetRobCoID = matches[1].str();
			std::string entriesStr = matches[2].str();

			std::vector<InjectionEntry> parsedEntries;
			bool lineSalvageable = true;

			std::stringstream ss(entriesStr);
			std::string token;
			while (std::getline(ss, token, ',')) {
				bool onlySpace = std::all_of(token.begin(), token.end(), [](char c) {
					return std::isspace(static_cast<unsigned char>(c));
				});
				if (token.empty() || onlySpace) continue;

				LenientParseResult er = ParseEntryLenient(token);
				if (!er.ok) {
					lineSalvageable = false;
					break;
				}
				if (er.wasFixed) {
					++g_lastFixCount;
				}
				parsedEntries.push_back(std::move(er.entry));
			}

			if (!lineSalvageable || parsedEntries.empty()) {
				result.preservedLines.push_back(line);
				++g_lastFixCount;
				continue;
			}

			TargetListData list;
			list.listRobCoID = targetRobCoID;
			auto targetForm = LookupFormByRobCoID(targetRobCoID);
			list.listEditorID = targetForm ? GetEditorID(targetForm) : targetRobCoID;
			list.entries = std::move(parsedEntries);
			result.lists.push_back(std::move(list));
		}
		return result;
	}

	std::vector<TargetListData> ConsolidateByTarget(std::vector<TargetListData> a_lists) {
		std::unordered_map<std::string, std::size_t> indexOf;
		std::vector<TargetListData> result;
		result.reserve(a_lists.size());

		for (auto& list : a_lists) {
			auto it = indexOf.find(list.listRobCoID);
			if (it == indexOf.end()) {
				indexOf[list.listRobCoID] = result.size();
				result.push_back(std::move(list));
			} else {
				auto& dest = result[it->second];
				std::unordered_set<std::string> haveIDs;
				for (const auto& e : dest.entries) haveIDs.insert(e.formRobCoID);
				for (auto& e : list.entries) {
					if (haveIDs.insert(e.formRobCoID).second) {
						dest.entries.push_back(std::move(e));
					}
				}
			}
		}
		return result;
	}

	std::vector<TargetListData> ScanLeveledLists() {
		std::vector<TargetListData> scanned;
		auto dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) return scanned;

		for (auto listForm : dataHandler->GetFormArray<RE::TESLevItem>()) {
			if (!listForm || listForm->scriptListCount <= 0 || !listForm->scriptAddedLists) continue;

			TargetListData listData;
			listData.listEditorID = GetEditorID(listForm);
			listData.listRobCoID = GetRobCoID(listForm);

			for (std::uint8_t i = 0; i < listForm->scriptListCount; ++i) {
				auto entry = listForm->scriptAddedLists[i];
				if (!entry || !entry->form) continue;

				InjectionEntry inj;
				inj.formRobCoID = GetRobCoID(entry->form);
				inj.formEditorID = GetEditorID(entry->form);
				inj.formName = GetFullName(entry->form);
				inj.level = entry->level;
				inj.count = entry->count;
				inj.chanceNone = entry->chanceNone;

				std::string itemDisplay = inj.formEditorID;
				if (!inj.formName.empty()) {
					itemDisplay += std::format(" (\"{}\")", inj.formName);
				}
				inj.commentBlock = std::format("//     ╰─ [Lvl {}] {}\n", inj.level, itemDisplay);

				listData.entries.push_back(std::move(inj));
			}

			if (!listData.entries.empty()) {
				scanned.push_back(std::move(listData));
			}
		}
		return scanned;
	}

	std::vector<TargetListData> MergeScanIntoExisting(std::vector<TargetListData> a_existing, const std::vector<TargetListData>& a_scan) {
		std::unordered_map<std::string, std::size_t> indexOf;
		for (std::size_t i = 0; i < a_existing.size(); ++i) {
			indexOf[a_existing[i].listRobCoID] = i;
		}

		for (const auto& scanList : a_scan) {
			auto it = indexOf.find(scanList.listRobCoID);
			if (it == indexOf.end()) {
				indexOf[scanList.listRobCoID] = a_existing.size();
				a_existing.push_back(scanList);
			} else {
				auto& dest = a_existing[it->second];
				std::unordered_set<std::string> haveIDs;
				for (const auto& e : dest.entries) haveIDs.insert(e.formRobCoID);
				for (const auto& e : scanList.entries) {
					if (haveIDs.insert(e.formRobCoID).second) {
						dest.entries.push_back(e);
					}
				}
			}
		}
		return a_existing;
	}

	void SortLists(std::vector<TargetListData>& a_lists) {
		for (auto& list : a_lists) {
			std::sort(list.entries.begin(), list.entries.end(), [](const InjectionEntry& a, const InjectionEntry& b) {
				if (a.level != b.level) return a.level < b.level;
				return a.formEditorID < b.formEditorID;
			});
		}
		std::sort(a_lists.begin(), a_lists.end(), [](const TargetListData& a, const TargetListData& b) {
			if (a.entries.size() != b.entries.size()) return a.entries.size() > b.entries.size();
			return a.listEditorID < b.listEditorID;
		});
	}

	void WriteINI(const fs::path& a_outPath, const std::vector<TargetListData>& a_lists, const std::vector<std::string>& a_preservedLines, const std::string& a_displayTimestamp) {
		std::ofstream ini(a_outPath, std::ios_base::trunc);
		if (!ini.is_open()) return;

		ini << std::format("// Created by RobCo Migrator on [{}]\n", a_displayTimestamp);
		ini << "// Comments are rebuilt on every run. Edit filterByLLs lines freely - your edits are preserved.\n\n";

		for (const auto& list : a_lists) {
			std::string block = std::format("// [{}]\n", list.listEditorID);
			std::string command = std::format("filterByLLs={}:addToLLs=", list.listRobCoID);
			for (std::size_t i = 0; i < list.entries.size(); ++i) {
				const auto& entry = list.entries[i];
				block += entry.commentBlock;
				command += std::format("{}~{}~{}~{}", entry.formRobCoID, entry.level, entry.count, entry.chanceNone);
				if (i + 1 < list.entries.size()) command += ",";
			}
			ini << block << command << "\n\n";
		}

		if (!a_preservedLines.empty()) {
			ini << "// --- Preserved lines (could not be parsed; kept verbatim so no data is lost) ---\n";
			for (const auto& line : a_preservedLines) {
				ini << line << "\n";
			}
		}
	}

	void WriteCSV(const fs::path& a_outPath, const std::vector<TargetListData>& a_lists) {
		std::ofstream csv(a_outPath, std::ios_base::trunc);
		if (!csv.is_open()) return;

		csv << "TargetList_EditorID,TargetList_PluginID,InjectedForm_EditorID,InjectedForm_Name,InjectedForm_PluginID,Level,Count,ChanceNone\n";
		for (const auto& data : a_lists) {
			for (const auto& entry : data.entries) {
				std::string safeName = entry.formName.empty() ? "N/A" : std::format("\"{}\"", entry.formName);
				csv << std::format("{},{},{},{},{},{},{},{}\n",
								   data.listEditorID, data.listRobCoID,
								   entry.formEditorID, safeName, entry.formRobCoID,
								   entry.level, entry.count, entry.chanceNone);
			}
		}
	}

	std::int32_t GeneratePatch(std::monostate, RE::BSFixedString a_modFolderName) {
		Log("GeneratePatch triggered.");
		g_lastFixCount = 0;

		if (g_bSafeToRevert) {
			Log("GeneratePatch blocked - user already generated a patch this session.");
			return 2;
		}

		auto dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler || a_modFolderName.empty()) return -1;

		fs::path targetDir = fs::path("Data") / "F4SE" / "Plugins" / "RobCo_Patcher" / "leveledList" / a_modFolderName.c_str();
		fs::create_directories(targetDir);

		std::string playerName = GetCurrentPlayerNameRaw();

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

		ParsedFile parsed;
		if (fs::exists(iniPath)) {
			std::string oldTimestamp = GetFileHeaderTimestamp(iniPath, fileTimestamp);
			fs::path backupPath = targetDir / std::format("{}_{}.bak", baseNameINI, oldTimestamp);
			try {
				fs::copy_file(iniPath, backupPath, fs::copy_options::overwrite_existing);
				Log(std::format("Backed up existing INI to {}", backupPath.filename().string()));
			} catch (const std::exception& e) {
				Log(std::format("ERROR: Could not back up existing INI: {} - aborting merge.", e.what()));
				return -1;
			}

			parsed = ParseExistingINIFull(iniPath);
			std::size_t totalBefore = parsed.lists.size();
			parsed.lists = ConsolidateByTarget(std::move(parsed.lists));
			Log(std::format("Parsed {} filterByLLs lines from existing INI; consolidated to {} unique targets ({} preserved lines, {} total fixes).",
							totalBefore, parsed.lists.size(), parsed.preservedLines.size(), g_lastFixCount));
		}

		auto scanned = ScanLeveledLists();
		Log(std::format("Scanned {} target lists with dynamic injections.", scanned.size()));

		auto merged = MergeScanIntoExisting(std::move(parsed.lists), scanned);
		std::erase_if(merged, [](const TargetListData& l) { return l.entries.empty(); });

		if (merged.empty() && parsed.preservedLines.empty()) {
			Log("Nothing to write - no existing entries and no live injections.");
			return 0;
		}

		SortLists(merged);
		WriteINI(iniPath, merged, parsed.preservedLines, displayTimestamp);
		WriteCSV(csvPath, merged);

		Log(std::format("Smart merge complete. {} target lists written; {} auto-fixes.", merged.size(), g_lastFixCount));
		g_bSafeToRevert = true;
		return 1;
	}

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

	std::int32_t GetLastFixCount(std::monostate) {
		return g_lastFixCount;
	}

	RE::BSFixedString GetCurrentPlayerName(std::monostate) {
		return RE::BSFixedString(GetCurrentPlayerNameRaw());
	}

	// Scans the target dir for Patch_<name>.ini files belonging to characters other
	// than the one whose name is passed in. Always safe to call - pure filesystem,
	// no game state access.
	struct PlayerFileMismatch {
		std::string filename;
		std::string playerName;
	};

	std::vector<PlayerFileMismatch> FindMismatchedPlayerFiles(const std::string& a_currentPlayerName) {
		std::vector<PlayerFileMismatch> mismatches;
		fs::path targetDir = fs::path("Data") / "F4SE" / "Plugins" / "RobCo_Patcher" / "leveledList" / kModFolderName;

		std::error_code ec;
		if (!fs::exists(targetDir, ec) || !fs::is_directory(targetDir, ec)) return mismatches;

		for (const auto& entry : fs::directory_iterator(targetDir, ec)) {
			if (ec) break;
			std::error_code ec2;
			if (!entry.is_regular_file(ec2) || ec2) continue;
			if (entry.path().extension() != ".ini") continue;

			std::string stem = entry.path().stem().string();
			if (!stem.starts_with("Patch_")) continue;

			std::string playerName = stem.substr(6);
			if (playerName.empty()) continue;
			if (playerName == a_currentPlayerName) continue;

			mismatches.push_back({ entry.path().filename().string(), std::move(playerName) });
		}
		return mismatches;
	}

	// Build the foreign-files warning message for the current player. Returns an
	// empty string if there are no mismatches. Called from Papyrus during MCM
	// interaction - never from a load-game callback (the VM isn't safe to call
	// into during kPostLoadGame).
	RE::BSFixedString GetForeignPlayerFileWarning(std::monostate) {
		std::string current = GetCurrentPlayerNameRaw();
		auto mismatches = FindMismatchedPlayerFiles(current);
		if (mismatches.empty()) return RE::BSFixedString("");

		std::string body = std::format(
			"You're playing as '{}', but the RobCo Patcher folder also contains patch file(s) belonging to other characters:\n\n",
			current);

		for (const auto& m : mismatches) {
			body += std::format("  - {}  (for character '{}')\n", m.filename, m.playerName);
		}

		body +=
			"\nRobCo Patcher loads EVERY .ini file in that folder. So those other characters' injections "
			"are being applied to your current save too - which is almost certainly not what you want.\n\n"
			"Fix: alt-tab out and either delete those files or move them somewhere else.\n\n"
			"Folder:\nData/F4SE/Plugins/RobCo_Patcher/leveledList/RobCoMigrator_Export/";

		Log(std::format("Foreign-file warning surfaced: {} mismatched file(s) for current player '{}'.",
						mismatches.size(), current));

		return RE::BSFixedString(body);
	}

	bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm) {
		a_vm->BindNativeMethod(new RE::BSScript::NativeFunction("RobCoMigrator", "GeneratePatch", GeneratePatch));
		a_vm->BindNativeMethod(new RE::BSScript::NativeFunction("RobCoMigrator", "SetRevertUnlocked", SetRevertUnlocked));
		a_vm->BindNativeMethod(new RE::BSScript::NativeFunction("RobCoMigrator", "GetRevertStatus", GetRevertStatus));
		a_vm->BindNativeMethod(new RE::BSScript::NativeFunction("RobCoMigrator", "GetInjectedLists", GetInjectedLists));
		a_vm->BindNativeMethod(new RE::BSScript::NativeFunction("RobCoMigrator", "GetLastFixCount", GetLastFixCount));
		a_vm->BindNativeMethod(new RE::BSScript::NativeFunction("RobCoMigrator", "GetCurrentPlayerName", GetCurrentPlayerName));
		a_vm->BindNativeMethod(new RE::BSScript::NativeFunction("RobCoMigrator", "GetForeignPlayerFileWarning", GetForeignPlayerFileWarning));
		return true;
	}
}
