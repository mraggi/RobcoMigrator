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
#include <excpt.h>  // EXCEPTION_EXECUTE_HANDLER (MSVC SEH guard in ScanLeveledLists)

namespace fs = std::filesystem;

namespace RobCoMigrator
{
	bool g_bSafeToRevert = false;
	bool g_bUserUnlockedRevert = false;
	std::int32_t g_lastFixCount = 0;
	LogLevel g_logLevel = LogLevel::Standard;
	static std::mutex logMutex;

	static constexpr const char* kModFolderName = "RobCoMigrator_Export";
	static std::string g_modFolderName = kModFolderName;

	// Lists whose (baseListCount + migrated entries) would exceed this are capped:
	// the surplus is left script-injected instead of migrated, keeping the list
	// RobCo Patcher rebuilds clear of the engine's hard 255-entry ceiling.
	static constexpr int kMaxListEntries = 245;

	// Plugin-name patterns (lowercased; a trailing '*' is a prefix wildcard) read
	// from RobCoMigrator_ExcludedMods.ini. Injections owned-by or sourced-from one
	// of these are neither migrated nor reverted - see LoadExcludedMods().
	static std::vector<std::string> g_excludedPatterns;

	// Filled by ScanLeveledLists(): per scanned target list, the exact injected
	// LEVELED_OBJECTs that were migrated into the .ini - the only ones
	// RevertMigratedEntries() may remove. Pointers stay valid for the
	// Generate->Revert window (same session, no reload in between).
	static std::unordered_map<RE::TESLevItem*, std::vector<RE::LEVELED_OBJECT*>> g_revertPlan;

	// Filled by ScanLeveledLists(): per target list (keyed by its RobCoID), every
	// form RobCoID currently live in its scriptAddedLists - INCLUDING excluded and
	// over-cap ones. Used when updating an existing .ini to safely strip excluded
	// entries: we only remove an excluded line if the game is actually re-injecting
	// it right now (otherwise removing it would lose data - e.g. WSFW disabled).
	static std::unordered_map<std::string, std::unordered_set<std::string>> g_liveInjectionIndex;

	void Log(const std::string& msg) {
		std::lock_guard<std::mutex> lock(logMutex);
		// F4SE::GetLogDirectoryPath() returns Documents/My Games/Fallout4/F4SE.
		// It is populated during F4SE::Init(), so Log() must not be called before that.
		static const fs::path logPath = F4SE::GetLogDirectoryPath() / "RobCoMigrator.log";
		std::ofstream logFile(logPath, std::ios_base::app);
		if (logFile.is_open()) {
			auto now = std::chrono::system_clock::now();
			auto time_t_now = std::chrono::system_clock::to_time_t(now);
			std::tm tm;
			localtime_s(&tm, &time_t_now);
			logFile << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " | " << msg << "\n";
		}
	}

	std::string GetRobCoID(RE::TESForm* a_form);  // defined below; used as the no-EditorID fallback

	std::string GetEditorID(RE::TESForm* a_form) {
		if (!a_form) return "UNKNOWN";
		const char* edID = a_form->GetFormEditorID();
		if (edID && edID[0] != '\0') return std::string(edID);
		// No EditorID available (e.g. our hooks didn't load it and Hydra isn't
		// present). Fall back to the same Plugin|LocalID we'd write to the patch
		// rather than the full {:08X} FormID, so comments/CSV stay load-order
		// stable and match the filterByLLs/addToLLs lines.
		return GetRobCoID(a_form);
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

	static std::string SanitizeFilename(std::string name) {
		static constexpr std::string_view kIllegal = R"(\/:*?"<>|)";
		for (char& c : name) {
			if (static_cast<unsigned char>(c) < 32 || kIllegal.find(c) != std::string_view::npos)
				c = '_';
		}
		return name;
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

	// RobCo Patcher doesn't need leading zeros in the local form ID (e.g.
	// "RPD.esp|00B9E7" and "RPD.esp|B9E7" are equivalent), and they're ugly.
	// Live-scanned IDs already come out clean (GetRobCoID uses {:X}); this
	// strips leading zeros from IDs read back out of existing .ini files - which
	// older versions of this plugin zero-padded - so re-saving cleans them up.
	std::string NormalizeRobCoID(const std::string& a_robCoID) {
		auto pos = a_robCoID.find('|');
		if (pos == std::string::npos) return a_robCoID;
		std::string plugin = a_robCoID.substr(0, pos);
		std::string id = a_robCoID.substr(pos + 1);
		std::size_t firstNonZero = id.find_first_not_of('0');
		id = (firstNonZero == std::string::npos) ? "0" : id.substr(firstNonZero);
		return plugin + "|" + id;
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

	// Plugin token of a "Plugin|LocalID" RobCoID (everything before the '|').
	static std::string PluginFromRobCoID(const std::string& a_robCoID) {
		auto pos = a_robCoID.find('|');
		return (pos == std::string::npos) ? a_robCoID : a_robCoID.substr(0, pos);
	}

	static std::string ToLowerCopy(std::string s) {
		std::transform(s.begin(), s.end(), s.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return s;
	}

	static void AddExcludePattern(std::string a_line) {
		if (!a_line.empty() && a_line.back() == '\r') a_line.pop_back();
		std::size_t start = a_line.find_first_not_of(" \t");
		if (start == std::string::npos) return;
		std::size_t end = a_line.find_last_not_of(" \t");
		a_line = a_line.substr(start, end - start + 1);
		if (a_line.empty() || a_line.starts_with("//") || a_line[0] == ';' || a_line[0] == '#') return;
		g_excludedPatterns.push_back(ToLowerCopy(std::move(a_line)));
	}

	// Written to RobCoMigrator_ExcludedMods.ini when it's missing. Kept in sync with
	// the shipped src/RobCoMigrator_ExcludedMods.ini; this string is the runtime
	// fallback so the feature still works if the file can't be read.
	static constexpr const char* kDefaultExcludedMods =
R"EXCL(// RobCo Migrator - Excluded Mods
//
// Leveled-list injections coming from the plugins listed here are treated as
// "transient": they are NOT written into the RobCo Patcher .ini and they are
// NOT reverted (erased) from your save. Use this for mods that re-inject their
// leveled lists on EVERY game load (e.g. Workshop Framework, Sim Settlements 2).
// Migrating those would cause double-injection - the mod re-adds them AND RobCo
// Patcher re-adds them - which on large lists trips the engine's 255-entries
// limit and crashes the game on save.
//
// FORMAT
//   - One plugin name per line (e.g. WorkshopFramework.esm).
//   - Case-insensitive.
//   - A trailing '*' is a prefix wildcard: "SS2*" matches SS2.esm,
//     SS2Extended.esp, SS2_XPAC_Chapter2.esm, etc.
//   - Lines starting with //  ;  or  #  are comments.
//
// HOW MATCHING IS APPLIED (both directions)
//   - If a target leveled list is OWNED by an excluded plugin (e.g. the
//     WSFW_InjectableItemHolder_* lists owned by WorkshopFramework.esm), the
//     WHOLE list is skipped.
//   - Otherwise, individual injected items whose SOURCE plugin is excluded are
//     skipped (left in place) and the rest of that list migrates normally.

WorkshopFramework.esm
SS2*
)EXCL";

	static fs::path ExcludedModsPath() {
		return fs::path("Data") / "F4SE" / "Plugins" / "RobCoMigrator_ExcludedMods.ini";
	}

	// (Re)loads the exclusion patterns. Called at the start of every GeneratePatch
	// so edits take effect without a game restart. Auto-creates a documented default
	// file if none exists; falls back to the built-in defaults if it can't be read.
	void LoadExcludedMods() {
		g_excludedPatterns.clear();
		fs::path path = ExcludedModsPath();

		std::error_code ec;
		if (!fs::exists(path, ec)) {
			fs::create_directories(path.parent_path(), ec);
			std::ofstream out(path, std::ios_base::trunc);
			if (out.is_open()) {
				out << kDefaultExcludedMods;
				Log(std::format("Created default excluded-mods file: {}", path.string()));
			}
		}

		std::ifstream in(path);
		if (in.is_open()) {
			std::string line;
			while (std::getline(in, line)) AddExcludePattern(line);
			Log(std::format("Loaded {} excluded-mod pattern(s) from RobCoMigrator_ExcludedMods.ini.", g_excludedPatterns.size()));
		} else {
			std::stringstream ss(kDefaultExcludedMods);
			std::string line;
			while (std::getline(ss, line)) AddExcludePattern(line);
			Log(std::format("Could not read excluded-mods file; using {} built-in default pattern(s).", g_excludedPatterns.size()));
		}
	}

	static bool IsPluginExcluded(const std::string& a_plugin) {
		if (a_plugin.empty() || g_excludedPatterns.empty()) return false;
		std::string p = ToLowerCopy(a_plugin);
		for (const auto& pat : g_excludedPatterns) {
			if (!pat.empty() && pat.back() == '*') {
				std::size_t n = pat.size() - 1;
				if (p.size() >= n && p.compare(0, n, pat, 0, n) == 0) return true;
			} else if (p == pat) {
				return true;
			}
		}
		return false;
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

	// Reads the player's name off the base actor. We deliberately do NOT call
	// TESObjectREFR::GetDisplayName() - its ExtraTextDisplayData engine routine
	// faults on this runtime (a dangling extra-data pointer in NG+ saves). The
	// player's chosen name lives in TESFullName's sparse map, not the static
	// `fullName` member, so we use the map-aware static GetFormFullName()
	// accessor - the same path GetDisplayName() itself falls back to. Guarded
	// because a stale base pointer could still fault - see ScanOneListGuarded.
	static void GetPlayerNameImpl(RE::PlayerCharacter* a_player, std::string* a_out) {
		auto* base = a_player->GetBaseObject();
		if (!base) return;
		auto fullName = RE::TESFullName::GetFormFullName(base);
		if (fullName && !fullName->empty()) *a_out = fullName->c_str();
	}

	// SEH guard around GetPlayerNameImpl. Holds no C++ objects needing unwinding,
	// so __try is legal here (avoids C2712) - see ScanOneListGuarded.
	static bool GetPlayerNameGuarded(RE::PlayerCharacter* a_player, std::string* a_out) noexcept {
		__try {
			GetPlayerNameImpl(a_player, a_out);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	std::string GetCurrentPlayerNameRaw() {
		std::string playerName;
		if (auto player = RE::PlayerCharacter::GetSingleton()) {
			GetPlayerNameGuarded(player, &playerName);
		}
		if (playerName.empty()) playerName = "Player";
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
		int fixCount = 0;
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

		// Strip any leading zeros older versions wrote (e.g. "Plugin|00B9E7" ->
		// "Plugin|B9E7"). Silent cleanup - not counted as a fix - and it also lets
		// these old entries dedup against freshly-scanned IDs, which never pad.
		std::string robCoID = NormalizeRobCoID(parts[0]);
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
			static_cast<std::uint16_t>(std::clamp(level, 0, 65535)),
			static_cast<std::uint16_t>(std::clamp(count, 0, 65535)),
			static_cast<std::int8_t>(std::clamp(chanceNone, 0, 100)));
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
			// Strip stray NUL bytes first. An unclean shutdown (crash, force-kill)
			// can leave a block of NULs zero-filled into the file by the filesystem;
			// without this we'd "preserve" that garbage verbatim and carry it forward,
			// growing it on every rewrite. Real content glued to it is kept.
			std::erase(line, '\0');
			if (!line.empty() && line.back() == '\r') line.pop_back();

			std::size_t firstNonWs = line.find_first_not_of(" \t");
			if (firstNonWs == std::string::npos) continue;

			std::string_view sv(line.data() + firstNonWs, line.size() - firstNonWs);
			if (sv.starts_with("//")) continue;

			std::smatch matches;
			if (!std::regex_match(line, matches, linePattern)) {
				result.preservedLines.push_back(line);
				++result.fixCount;
				continue;
			}

			std::string targetRobCoID = NormalizeRobCoID(matches[1].str());
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
					++result.fixCount;
				}
				parsedEntries.push_back(std::move(er.entry));
			}

			if (!lineSalvageable || parsedEntries.empty()) {
				result.preservedLines.push_back(line);
				++result.fixCount;
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

	// Collapses an existing file's multiple lines for the same target onto one
	// line. We do NOT dedup entries here: a leveled list can intentionally list
	// the same item several times to weight its drop chance, and RobCo Patcher
	// already applies every line additively, so concatenating preserves the
	// file's exact in-game effect. The only place we drop anything is the
	// scan-vs-file step in MergeScanIntoExisting.
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
				dest.entries.insert(dest.entries.end(),
					std::make_move_iterator(list.entries.begin()),
					std::make_move_iterator(list.entries.end()));
			}
		}
		return result;
	}

	// Reads the script-added entries of a single leveled list. This is the only place
	// we chase pointers that other mods inject at runtime (entry->form), so it is also
	// the only place that can fault: an injected entry can point at a stale/dangling
	// TESForm (e.g. a mod-edited override, a partial save state, or an NG+ reset). A
	// plain "entry->form != nullptr" check does NOT catch a dangling pointer - we only
	// find out it's bad once we dereference it, which is too late. See ScanOneListGuarded.
	static bool ScanOneListImpl(RE::TESLevItem* a_listForm, TargetListData* a_out,
		std::vector<RE::LEVELED_OBJECT*>* a_migratedPtrs,
		std::vector<std::string>* a_allLiveForms, bool* a_listExcluded) {
		a_out->listEditorID = GetEditorID(a_listForm);
		a_out->listRobCoID = GetRobCoID(a_listForm);

		// A list OWNED by an excluded mod (e.g. a WSFW_InjectableItemHolder_* owned
		// by WorkshopFramework.esm) is migrated as a whole-list skip: nothing of it
		// goes into the .ini or the revert plan. We still record its live forms
		// (below) so an existing .ini can be safely cleaned of those lines.
		const bool listExcluded = IsPluginExcluded(PluginFromRobCoID(a_out->listRobCoID));
		*a_listExcluded = listExcluded;

		// Cap the rebuilt list (static base + entries RobCo Patcher will re-add)
		// below the engine's 255 ceiling; the surplus stays script-injected.
		const int headroom = kMaxListEntries - static_cast<int>(a_listForm->baseListCount);
		int migrated = 0;

		for (std::uint8_t i = 0; i < a_listForm->scriptListCount; ++i) {
			auto entry = a_listForm->scriptAddedLists[i];
			if (!entry || !entry->form) continue;

			std::string formRobCoID = GetRobCoID(entry->form);
			a_allLiveForms->push_back(formRobCoID);  // every live entry, for safe pruning

			if (listExcluded) continue;                                   // whole list left injected
			if (IsPluginExcluded(PluginFromRobCoID(formRobCoID))) continue;  // excluded source: left injected
			if (migrated >= headroom) continue;                           // over cap: left injected

			InjectionEntry inj;
			inj.formRobCoID = std::move(formRobCoID);
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

			a_out->entries.push_back(std::move(inj));
			a_migratedPtrs->push_back(entry);
			++migrated;
		}
		return true;
	}

	// SEH guard around ScanOneListImpl. If a list holds a dangling injected form,
	// dereferencing it raises an access violation; we catch it, skip just that list,
	// and keep going instead of crashing the whole game. This frame deliberately holds
	// no C++ objects that need unwinding, so __try is legal here (avoids C2712).
	static bool ScanOneListGuarded(RE::TESLevItem* a_listForm, TargetListData* a_out,
		std::vector<RE::LEVELED_OBJECT*>* a_migratedPtrs,
		std::vector<std::string>* a_allLiveForms, bool* a_listExcluded) noexcept {
		__try {
			return ScanOneListImpl(a_listForm, a_out, a_migratedPtrs, a_allLiveForms, a_listExcluded);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	std::vector<TargetListData> ScanLeveledLists() {
		g_revertPlan.clear();
		g_liveInjectionIndex.clear();

		std::vector<TargetListData> scanned;
		auto dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) return scanned;

		int skipped = 0;
		int excludedLists = 0;
		for (auto listForm : dataHandler->GetFormArray<RE::TESLevItem>()) {
			if (!listForm || listForm->scriptListCount <= 0 || !listForm->scriptAddedLists) continue;

			TargetListData listData;
			std::vector<RE::LEVELED_OBJECT*> migratedPtrs;
			std::vector<std::string> allLiveForms;
			bool listExcluded = false;
			if (!ScanOneListGuarded(listForm, &listData, &migratedPtrs, &allLiveForms, &listExcluded)) {
				++skipped;
				Log(std::format("WARNING: Skipped leveled list '{}' - access violation while reading its "
								"script-added entries (likely a dangling/invalid injected form left by another mod). "
								"scriptListCount={}. This list was not migrated; everything else continues normally.",
								GetEditorID(listForm), static_cast<int>(listForm->scriptListCount)));
				continue;
			}

			// Index every live-injected form (incl. excluded/over-cap) so an existing
			// .ini can be cleaned of excluded lines only when they're truly live.
			if (!allLiveForms.empty()) {
				auto& set = g_liveInjectionIndex[listData.listRobCoID];
				for (auto& f : allLiveForms) set.insert(std::move(f));
			}

			if (listExcluded) {
				++excludedLists;
				continue;
			}

			if (!listData.entries.empty()) {
				g_revertPlan.emplace(listForm, std::move(migratedPtrs));
				scanned.push_back(std::move(listData));
			}
		}

		if (excludedLists > 0) {
			Log(std::format("Skipped {} leveled list(s) owned by excluded mods (left untouched).", excludedLists));
		}
		if (skipped > 0) {
			Log(std::format("Scan finished with {} leveled list(s) skipped due to bad injected data. "
							"The rest were migrated normally.", skipped));
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
				// Per-form count reconciliation: the result keeps max(file, scan)
				// copies of each form. Duplicate entries are intentional leveled-
				// list weighting, so we never trim what's already in the file; we
				// only top it up with the scan's surplus. A form the scan sees no
				// more often than the file already lists adds nothing (lists weren't
				// reverted before re-running, or it's a test pass); a form the scan
				// sees 3x while the file has 2x adds 1; a brand-new form adds all.
				std::unordered_map<std::string, int> fileCount;
				for (const auto& e : dest.entries) ++fileCount[e.formRobCoID];
				std::unordered_map<std::string, int> seen;
				for (const auto& e : scanList.entries) {
					if (++seen[e.formRobCoID] > fileCount[e.formRobCoID]) {
						dest.entries.push_back(e);
					}
				}
			}
		}
		return a_existing;
	}

	// When updating an existing .ini we strip lines that now belong to excluded
	// mods - but ONLY where the game is currently re-injecting that exact form into
	// that exact list (per g_liveInjectionIndex, which ScanLeveledLists just built).
	// If a form isn't live (e.g. the user disabled WSFW, or it's a persistent entry
	// that legitimately lives in the file), we keep it so nothing is silently lost.
	// Returns the number of entries removed.
	int PruneExcludedLiveEntries(std::vector<TargetListData>& a_lists) {
		int removed = 0;
		for (auto& list : a_lists) {
			const bool ownerExcluded = IsPluginExcluded(PluginFromRobCoID(list.listRobCoID));
			auto liveIt = g_liveInjectionIndex.find(list.listRobCoID);
			const std::unordered_set<std::string>* live =
				(liveIt != g_liveInjectionIndex.end()) ? &liveIt->second : nullptr;

			std::size_t before = list.entries.size();
			std::erase_if(list.entries, [&](const InjectionEntry& e) {
				const bool excluded = ownerExcluded || IsPluginExcluded(PluginFromRobCoID(e.formRobCoID));
				return excluded && live && live->count(e.formRobCoID) > 0;
			});
			removed += static_cast<int>(before - list.entries.size());
		}
		return removed;
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
				std::string safeName;
				if (entry.formName.empty()) {
					safeName = "N/A";
				} else {
					std::string escaped;
					for (char c : entry.formName) {
						if (c == '"') escaped += '"';
						escaped += c;
					}
					safeName = std::format("\"{}\"", escaped);
				}
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

		g_modFolderName = a_modFolderName.c_str();
		fs::path targetDir = fs::path("Data") / "F4SE" / "Plugins" / "RobCo_Patcher" / "leveledList" / g_modFolderName;
		fs::create_directories(targetDir);

		std::string playerName = SanitizeFilename(GetCurrentPlayerNameRaw());

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
		bool hadExistingFile = fs::exists(iniPath);
		if (hadExistingFile) {
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
			g_lastFixCount = parsed.fixCount;
			std::size_t totalBefore = parsed.lists.size();
			parsed.lists = ConsolidateByTarget(std::move(parsed.lists));
			Log(std::format("Parsed {} filterByLLs lines from existing INI; consolidated to {} unique targets ({} preserved lines, {} total fixes).",
							totalBefore, parsed.lists.size(), parsed.preservedLines.size(), g_lastFixCount));
		}

		LoadExcludedMods();

		auto scanned = ScanLeveledLists();
		Log(std::format("Scanned {} target lists with dynamic injections.", scanned.size()));

		// Updating an existing file: drop excluded-mod lines, but only the ones the
		// game is actually re-injecting right now (safe to remove). Anything not
		// confirmed live is kept untouched so we never lose a user's data.
		if (!parsed.lists.empty()) {
			int pruned = PruneExcludedLiveEntries(parsed.lists);
			if (pruned > 0) {
				Log(std::format("Cleaned {} excluded-mod entr(ies) from the existing INI "
								"(confirmed live in-game, so safe to remove).", pruned));
			}
		}

		auto merged = MergeScanIntoExisting(std::move(parsed.lists), scanned);
		std::erase_if(merged, [](const TargetListData& l) { return l.entries.empty(); });

		// Only bail when there's genuinely nothing on disk and nothing to add. If a
		// file already exists we always rewrite it - even with no live injections -
		// so older files get cleaned up (leading zeros stripped, header/comments
		// rebuilt) just by running a scan.
		if (merged.empty() && parsed.preservedLines.empty() && !hadExistingFile) {
			Log("Nothing to write - no existing file and no live injections.");
			return 0;
		}

		SortLists(merged);

		for (const auto& list : merged) {
			if (list.entries.size() > static_cast<std::size_t>(kMaxListEntries)) {
				Log(std::format("WARNING: target list '{}' has {} entries in the patch, above the {} "
								"safety cap. Likely an older or hand-edited file; trim it to stay clear of "
								"the engine's 255-entry save crash.",
								list.listEditorID, list.entries.size(), kMaxListEntries));
			}
		}

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

	// SEH-guarded surgical removal: drops exactly the migrated LEVELED_OBJECTs from
	// one list and deletes them (mirroring ClearScriptLevObjects' ownership). We
	// re-find each pointer by identity every time because RemoveNthScriptLevObject
	// reflows the array. This frame holds only trivially-destructible locals, so
	// __try is legal here (avoids C2712). Returns the number removed.
	static int RemoveMigratedFromListGuarded(RE::TESLevItem* a_list,
		RE::LEVELED_OBJECT* const* a_targets, std::size_t a_count) noexcept {
		int removed = 0;
		__try {
			for (std::size_t k = 0; k < a_count; ++k) {
				RE::LEVELED_OBJECT* target = a_targets[k];
				int idx = -1;
				for (std::uint8_t i = 0; i < a_list->scriptListCount; ++i) {
					if (a_list->scriptAddedLists[i] == target) { idx = static_cast<int>(i); break; }
				}
				if (idx < 0) continue;  // already gone
				a_list->RemoveNthScriptLevObject(static_cast<std::uint8_t>(idx));
				delete target;
				++removed;
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
		}
		return removed;
	}

	// Surgically removes ONLY the entries the last GeneratePatch migrated into the
	// .ini, using the plan ScanLeveledLists built. Excluded-mod and over-cap entries
	// are left injected (untouched). Resets the revert lock so a second click can't
	// double-revert. Returns the total number of entries removed.
	std::int32_t RevertMigratedEntries(std::monostate) {
		int totalRemoved = 0;
		int listsTouched = 0;
		for (auto& [listForm, ptrs] : g_revertPlan) {
			if (!listForm || ptrs.empty()) continue;
			int r = RemoveMigratedFromListGuarded(listForm, ptrs.data(), ptrs.size());
			totalRemoved += r;
			if (r > 0) ++listsTouched;
		}

		g_revertPlan.clear();
		g_liveInjectionIndex.clear();
		g_bSafeToRevert = false;
		g_bUserUnlockedRevert = false;

		Log(std::format("Surgical revert: removed {} migrated entr(ies) from {} list(s); "
						"excluded/over-cap entries left injected.", totalRemoved, listsTouched));
		return totalRemoved;
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
		fs::path targetDir = fs::path("Data") / "F4SE" / "Plugins" / "RobCo_Patcher" / "leveledList" / g_modFolderName;

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
		std::string currentRaw = GetCurrentPlayerNameRaw();
		auto mismatches = FindMismatchedPlayerFiles(SanitizeFilename(currentRaw));
		if (mismatches.empty()) return RE::BSFixedString("");

		std::string body = std::format(
			"You're playing as '{}', but the RobCo Patcher folder also contains patch file(s) belonging to other characters:\n\n",
			currentRaw);

		for (const auto& m : mismatches) {
			body += std::format("  - {}  (for character '{}')\n", m.filename, m.playerName);
		}

		body +=
			"\nRobCo Patcher loads EVERY .ini file in that folder. So those other characters' injections "
			"are being applied to your current save too - which is almost certainly not what you want.\n\n"
			"Fix: alt-tab out and either delete those files or move them somewhere else.\n\n"
			+ std::format("Folder:\nData/F4SE/Plugins/RobCo_Patcher/leveledList/{}/", g_modFolderName);

		Log(std::format("Foreign-file warning surfaced: {} mismatched file(s) for current player '{}'.",
						mismatches.size(), currentRaw));

		return RE::BSFixedString(body);
	}

	bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm) {
		a_vm->BindNativeMethod(new RE::BSScript::NativeFunction("RobCoMigrator", "GeneratePatch", GeneratePatch));
		a_vm->BindNativeMethod(new RE::BSScript::NativeFunction("RobCoMigrator", "SetRevertUnlocked", SetRevertUnlocked));
		a_vm->BindNativeMethod(new RE::BSScript::NativeFunction("RobCoMigrator", "GetRevertStatus", GetRevertStatus));
		a_vm->BindNativeMethod(new RE::BSScript::NativeFunction("RobCoMigrator", "GetLastFixCount", GetLastFixCount));
		a_vm->BindNativeMethod(new RE::BSScript::NativeFunction("RobCoMigrator", "GetCurrentPlayerName", GetCurrentPlayerName));
		a_vm->BindNativeMethod(new RE::BSScript::NativeFunction("RobCoMigrator", "GetForeignPlayerFileWarning", GetForeignPlayerFileWarning));
		a_vm->BindNativeMethod(new RE::BSScript::NativeFunction("RobCoMigrator", "RevertMigratedEntries", RevertMigratedEntries));
		return true;
	}
}
