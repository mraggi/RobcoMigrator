#pragma once

#include <F4SE/F4SE.hpp>
#include <RE/Game.hpp>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <filesystem>

namespace RobCoMigrator
{
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
    extern std::int32_t g_lastFixCount;

    void Log(const std::string& msg);

    // Native bindings
    std::int32_t GeneratePatch(std::monostate, RE::BSFixedString a_modFolderName);
    void SetRevertUnlocked(std::monostate, bool a_unlocked);
    std::int32_t GetRevertStatus(std::monostate);
    std::vector<RE::TESForm*> GetInjectedLists(std::monostate);
    std::int32_t GetLastFixCount(std::monostate);
    RE::BSFixedString GetCurrentPlayerName(std::monostate);
    RE::BSFixedString GetForeignPlayerFileWarning(std::monostate);

    bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm);
}
