#pragma once

#include <F4SE/F4SE.h>
#include <RE/Fallout.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <filesystem>

namespace RobCoMigrator
{
    enum ExportMode {
        MODE_CREATE_NEW = 0,
        MODE_OVERWRITE = 1,
        MODE_MERGE = 2
    };

    enum class LogLevel {
        Standard = 0, // Level 0: Silent / Core Operations
        Verbose = 1   // Level 1: Full scan details and matches
    };

    struct InjectionEntry {
        std::string formEditorID;
        std::string formRobCoID;
        std::string formName;
        std::uint16_t level;
        std::uint16_t count;
        std::int8_t chanceNone;
        std::string commentBlock;
    };

    struct TargetListData {
        std::string listEditorID;
        std::string listRobCoID;
        std::vector<InjectionEntry> entries;
    };

    extern bool g_bSafeToRevert;
    extern bool g_bUserUnlockedRevert;
    extern LogLevel g_logLevel;

    void Log(LogLevel a_level, const std::string& msg);

    std::int32_t GeneratePatch(std::monostate, RE::BSFixedString a_modFolderName, std::int32_t a_exportMode);
    void SetRevertUnlocked(std::monostate, bool a_unlocked);

    // New Native Interfaces for Papyrus
    std::int32_t GetRevertStatus(std::monostate);
    std::vector<RE::TESForm*> GetInjectedLists(std::monostate);

    bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm);
}
