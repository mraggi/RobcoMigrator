#include "LLMigrator.hpp"
#include "EditorIDLoader.hpp"
#include <F4SE/F4SE.hpp>

#ifndef DLLEXPORT
#define DLLEXPORT __declspec(dllexport)
#endif

namespace {
    // Fallout 4 keeps EditorIDs in memory only for a few form types; for the
    // rest they are discarded at load. We need them for the .ini/.csv output,
    // so unless Baka Framework or Hydra is doing the job we install our own
    // EditorID-loading hooks. Hooks MUST go in at kPostLoad (before plugins
    // load); the kGameDataReady probe just verifies the result afterwards.
    void MessageCallback(F4SE::MessagingInterface::Message* a_msg) {
        if (!a_msg) return;

        switch (a_msg->GetType()) {
        case F4SE::MessagingInterface::MessageType::kPostLoad:
            RobCoMigrator::EditorIDLoader::InstallIfNeeded();
            break;

        case F4SE::MessagingInterface::MessageType::kGameDataReady:
            if (RobCoMigrator::EditorIDLoader::IsWorking()) {
                RobCoMigrator::Log("[EditorIDLoader] EditorID probe SUCCESS "
                                   "('LL_Vendor_Weapon_GunSpecialty' resolved). EditorIDs are available.");
            } else {
                RobCoMigrator::Log("[EditorIDLoader] WARNING: EditorID probe FAILED "
                                   "('LL_Vendor_Weapon_GunSpecialty' did not resolve). Output will fall back "
                                   "to bare form IDs. Install Baka Framework or Hydra if this persists.");
            }
            break;

        case F4SE::MessagingInterface::MessageType::kPostLoadGame:
            // DEBUG: fires the instant a save finishes loading - AFTER the engine
            // applies its change forms, but BEFORE Papyrus OnPlayerLoadGame scripts
            // run. So this snapshot is "what the SAVE itself contains." If it shows
            // injections right after a clean revert+save, the entries are baked into
            // the save's change forms; if it shows 0 here but they reappear once you
            // open the MCM, a still-active source mod is re-injecting on load.
            RobCoMigrator::LogInjectedListSizes(std::monostate{}, "kPostLoadGame (save just finished loading)");
            break;

        default:
            break;
        }
    }
}

extern "C" DLLEXPORT bool F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se) {
    // F4SE::Init() must run first: it populates F4SE::GetLogDirectoryPath(),
    // which RobCoMigrator::Log() depends on. Never call Log() before this.
    F4SE::Init(a_f4se);

    F4SE::GetMessagingInterface()->RegisterListener(MessageCallback);
    F4SE::GetPapyrusInterface()->Register(RobCoMigrator::RegisterPapyrus);

    return true;
}
