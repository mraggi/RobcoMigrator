#include "LLMigrator.hpp"
#include <F4SE/F4SE.hpp>

#ifndef DLLEXPORT
#define DLLEXPORT __declspec(dllexport)
#endif

static void F4SEMessageHandler(F4SE::MessagingInterface::Message* a_msg) {
    if (!a_msg) return;
    if (a_msg->GetType() == F4SE::MessagingInterface::MessageType::kPostLoadGame) {
        RobCoMigrator::OnGameLoaded();
    }
}

extern "C" DLLEXPORT bool F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se) {
    F4SE::Init(a_f4se);

    F4SE::GetPapyrusInterface()->Register(RobCoMigrator::RegisterPapyrus);
    F4SE::GetMessagingInterface()->RegisterListener(F4SEMessageHandler);

    return true;
}
