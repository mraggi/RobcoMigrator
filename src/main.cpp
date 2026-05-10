#include "LLMigrator.hpp"
#include <F4SE/F4SE.h>

#ifndef DLLEXPORT
#define DLLEXPORT __declspec(dllexport)
#endif

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se) {
    F4SE::Init(a_f4se);

    // Register the Papyrus Native binding
    F4SE::GetPapyrusInterface()->Register(RobCoMigrator::RegisterPapyrus);

    return true;
}
