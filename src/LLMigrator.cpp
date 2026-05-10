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
    bool g_bUserUnlockedRevert = false;
    LogLevel g_logLevel = LogLevel::Standard; // Default log level
    static std::mutex logMutex;

    void Log(LogLevel a_level, const std::string& msg) {
        if (static_cast<int>(a_level) > static_cast<int>(g_logLevel)) return;

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
        auto file = a_form->GetFile(0);
        std::string pluginName = file ? file->filename : "UNKNOWN";
        return std::format("{}|{:X}", pluginName, a_form->GetLocalFormID());
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

    std::map<std::string, TargetListData> ParseExistingINI(const fs::path& a_path) {
        std::map<std::string, TargetListData> parsedData;
        std::ifstream ini(a_path);
        if (!ini.is_open()) return parsedData;

        std::string line;
        TargetListData currentList;
        std::vector<std::string> currentComments;

        const std::regex pattern(R"(filterByLLs=([^:]+):addToLLs=(.*))");
        std::smatch matches;

        while (std::getline(ini, line)) {
            if (line.empty() || line.starts_with("// ---") || line.starts_with("// Created") || line.starts_with("// Updated")) continue;

            if (line.starts_with("// [") && line.find("[Lvl ") == std::string::npos) {
                currentList.listEditorID = line.substr(4, line.length() - 5);
            }
            else if (line.starts_with("//") && line.find("[Lvl ") != std::string::npos) {
                currentComments.push_back(line + "\n");
            }
            else if (std::regex_search(line, matches, pattern) && matches.size() == 3) {
                currentList.listRobCoID = matches[1].str();
                std::string injectionsStr = matches[2].str();

                std::stringstream ss(injectionsStr);
                std::string token;
                size_t commentIdx = 0;

                while (std::getline(ss, token, ',')) {
                    InjectionEntry entry;
                    std::stringstream tokenSS(token);
                    std::string part;
                    std::vector<std::string> parts;

                    while (std::getline(tokenSS, part, '~')) parts.push_back(part);

                    if (parts.size() >= 4) {
                        entry.formRobCoID = parts[0];
                        entry.level = static_cast<std::uint16_t>(std::stoi(parts[1]));
                        entry.count = static_cast<std::uint16_t>(std::stoi(parts[2]));
                        entry.chanceNone = static_cast<std::int8_t>(std::stoi(parts[3]));
                    } else if (!parts.empty()) {
                        entry.formRobCoID = parts[0];
                    }

                    if (commentIdx < currentComments.size()) {
                        entry.commentBlock = currentComments[commentIdx];
                    } else {
                        entry.commentBlock = std::format("//     ╰─ [Lvl {}] {}\n", entry.level, entry.formRobCoID);
                    }

                    currentList.entries.push_back(entry);
                    commentIdx++;
                }

                parsedData[currentList.listRobCoID] = currentList;
                currentList = TargetListData();
                currentComments.clear();
            }
        }
        return parsedData;
    }

    void WriteINI(const fs::path& a_outPath, const std::vector<TargetListData>& a_sortedData, bool a_wasMerged, const std::string& a_displayTimestamp) {
        std::ofstream ini(a_outPath, std::ios_base::trunc);
        if (!ini.is_open()) return;

        if (a_wasMerged) {
            ini << std::format("// Updated & Merged by RobCo Migrator on [{}]\n\n", a_displayTimestamp);
        } else {
            ini << std::format("// Created by RobCo Migrator on [{}]\n\n", a_displayTimestamp);
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

    void WriteCSV(const fs::path& a_outPath, const std::vector<TargetListData>& a_sortedData) {
        std::ofstream csv(a_outPath, std::ios_base::trunc);
        if (!csv.is_open()) return;

        csv << "TargetList_EditorID,TargetList_PluginID,InjectedForm_EditorID,InjectedForm_Name,InjectedForm_PluginID,Level,Count,ChanceNone\n";

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
        Log(LogLevel::Standard, std::format("GeneratePatch Triggered. Mode: {}", a_exportMode));

        if (g_bSafeToRevert) {
            Log(LogLevel::Verbose, "GeneratePatch blocked - user already generated a patch this session.");
            return 2;
        }

        auto dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler || a_modFolderName.empty()) return -1;

        ExportMode mode = static_cast<ExportMode>(a_exportMode);
        fs::path targetDir = fs::path("Data") / "F4SE" / "Plugins" / "RobCo_Patcher" / "leveledList" / a_modFolderName.c_str();
        fs::create_directories(targetDir);

        std::string playerName = "UnknownPlayer";
        auto player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            const char* name = player->GetDisplayFullName();
            if (name && name[0] != '\0') playerName = name;
        }

        std::replace_if(playerName.begin(), playerName.end(), [](char c) {
            return c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || c == ' ';
        }, '_');

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

        std::map<std::string, TargetListData> mergedData;

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
                mergedData = ParseExistingINI(iniPath);
                Log(LogLevel::Verbose, std::format("Parsed {} existing lists for merging.", mergedData.size()));
            }
        }

        std::vector<TargetListData> groupedInjections;

        for (auto listForm : dataHandler->GetFormArray<RE::TESLevItem>()) {
            if (!listForm || listForm->scriptListCount <= 0 || !listForm->scriptAddedLists) continue;

            TargetListData listData;
            listData.listEditorID = GetEditorID(listForm);
            listData.listRobCoID = GetRobCoID(listForm);

            Log(LogLevel::Verbose, std::format("Scanning list: {}", listData.listRobCoID));

            for (std::int8_t i = 0; i < listForm->scriptListCount; ++i) {
                auto entry = listForm->scriptAddedLists[i];
                if (!entry || !entry->form) continue;

                std::string injectRobCoID = GetRobCoID(entry->form);
                std::string itemDisplay = GetEditorID(entry->form);
                std::string name = GetFullName(entry->form);
                if (!name.empty()) itemDisplay += std::format(" (\"{}\")", name);
                std::string commentStr = std::format("//     ╰─ [Lvl {}] {}\n", entry->level, itemDisplay);

                InjectionEntry injectObj = {
                    GetEditorID(entry->form),
                    injectRobCoID,
                    GetFullName(entry->form),
                    entry->level,
                    entry->count,
                    entry->chanceNone,
                    commentStr
                };

                if (mode == MODE_MERGE) {
                    auto& existingList = mergedData[listData.listRobCoID];
                    bool alreadyExists = false;
                    for (const auto& exEntry : existingList.entries) {
                        if (exEntry.formRobCoID == injectRobCoID && exEntry.level == entry->level) {
                            alreadyExists = true;
                            Log(LogLevel::Verbose, std::format("Skipped existing entry: {}", injectRobCoID));
                            break;
                        }
                    }
                    if (!alreadyExists) {
                        if (existingList.listEditorID.empty()) existingList.listEditorID = listData.listEditorID;
                        if (existingList.listRobCoID.empty()) existingList.listRobCoID = listData.listRobCoID;
                        existingList.entries.push_back(injectObj);
                        Log(LogLevel::Verbose, std::format("Merged new entry: {}", injectRobCoID));
                    }
                } else {
                    listData.entries.push_back(injectObj);
                }
            }

            if (mode != MODE_MERGE && !listData.entries.empty()) {
                groupedInjections.push_back(std::move(listData));
            }
        }

        if (mode == MODE_MERGE) {
            for (auto& [robCoID, listData] : mergedData) {
                if (!listData.entries.empty()) {
                    groupedInjections.push_back(std::move(listData));
                }
            }
        }

        if (groupedInjections.empty()) return 0;

        for (auto& list : groupedInjections) {
            std::sort(list.entries.begin(), list.entries.end(), [](const InjectionEntry& a, const InjectionEntry& b) {
                if (a.level != b.level) return a.level < b.level;
                return a.formRobCoID < b.formRobCoID;
            });
        }

        std::sort(groupedInjections.begin(), groupedInjections.end(), [](const TargetListData& a, const TargetListData& b) {
            if (a.entries.size() != b.entries.size()) return a.entries.size() > b.entries.size();
            return a.listEditorID < b.listEditorID;
        });

        WriteINI(iniPath, groupedInjections, (mode == MODE_MERGE), displayTimestamp);
        WriteCSV(csvPath, groupedInjections);

        Log(LogLevel::Standard, "GeneratePatch finished successfully.");
        g_bSafeToRevert = true;
        return 1;
    }

    void SetRevertUnlocked(std::monostate, bool a_unlocked) {
        g_bUserUnlockedRevert = a_unlocked;
        Log(LogLevel::Standard, std::format("User UI Toggle set to: {}", a_unlocked ? "true" : "false"));
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

        Log(LogLevel::Standard, std::format("Handed off {} injected Leveled Items to Papyrus for Revert().", result.size()));
        return result;
    }

    bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm) {
        a_vm->RegisterFunction("GeneratePatch", "RobCoMigrator", GeneratePatch);
        a_vm->RegisterFunction("SetRevertUnlocked", "RobCoMigrator", SetRevertUnlocked);
        a_vm->RegisterFunction("GetRevertStatus", "RobCoMigrator", GetRevertStatus);
        a_vm->RegisterFunction("GetInjectedLists", "RobCoMigrator", GetInjectedLists);
        return true;
    }
}
