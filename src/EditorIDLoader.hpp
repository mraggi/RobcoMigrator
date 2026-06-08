#pragma once

// =============================================================================
// EditorIDLoader.hpp
//
// Populates the engine's EditorID-to-Form map at form-load time, so that
// form->GetFormEditorID() actually returns something for the form types whose
// EDIDs the engine would otherwise discard.
//
// Fallout 4 does NOT keep EditorIDs in memory for most form types. We restore
// them by hooking GetFormEditorID/SetFormEditorID and feeding the engine's
// global EditorID map, so the .ini/.csv comments and target-list headers can
// show real EditorIDs instead of bare form IDs.
//
// The one case we step aside for is Hydra: our hook (and the Baka-derived
// approach in general) silently fails to load NPC_ EditorIDs for reasons still
// unknown, whereas Hydra loads them more completely. When Hydra is present,
// doing nothing gives better coverage than running our own loader. We do NOT
// defer to any other loader: the engine map uses insert-if-absent emplace, so
// multiple loaders writing identical EDIDs is harmless (first writer wins).
//
// HEAVILY ADAPTED FROM:
//   Baka Framework by shad0wshayd3-FO4
//   https://github.com/shad0wshayd3-FO4/BakaFramework
//   src/Patches/LoadEditorIDs.h
//   Licensed under GPL-3.0
//
// This file is offered under the same GPL-3.0 license as the original.
//
// Adapted for the LucaDotGit fork of CommonLibF4 (this project's submodule),
// which exposes vtable hooking via REL::Relocation<uintptr_t>::WriteVirtualCall
// rather than write_vfunc.
// =============================================================================

#include "LLMigrator.hpp"

#include <REL/Relocation.hpp>

#include <string>
#include <unordered_map>

#include <windows.h>

namespace RobCoMigrator::EditorIDLoader
{
	namespace detail
	{
		// Reverse map: formID -> EDID. For most form types the engine's
		// per-form EDID slot is zero-sized; the only place EDIDs live is the
		// global map returned by TESForm::GetAllFormsByEditorID(). When code
		// calls form->GetFormEditorID() the default vfunc returns the empty
		// per-form slot, so we override that vfunc to consult this rmap.
		inline std::unordered_map<RE::TESFormID, std::string> g_edidMap;

		inline void AddToGameMap(RE::TESForm* a_this, const char* a_edid)
		{
			const auto& [map, lock] = RE::TESForm::GetAllFormsByEditorID();
			const RE::BSAutoWriteLock locker{ lock.get() };
			if (!map) return;
			map->emplace(a_edid, a_this);
			g_edidMap.emplace(a_this->GetFormID(), a_edid);
		}

		template <class T>
		class Hook
		{
		public:
			static void Install()
			{
				REL::Relocation<std::uintptr_t> vtable{ T::VTABLE[0] };
				_GetFormEditorID = vtable.WriteVirtualCall(0x3A, GetFormEditorID_Hook);
				_SetFormEditorID = vtable.WriteVirtualCall(0x3B, SetFormEditorID_Hook);
			}

		private:
			static const char* GetFormEditorID_Hook(RE::TESForm* a_this)
			{
				auto it = g_edidMap.find(a_this->GetFormID());
				if (it != g_edidMap.end()) return it->second.c_str();
				return _GetFormEditorID(a_this);
			}

			static bool SetFormEditorID_Hook(RE::TESForm* a_this, const char* a_edid)
			{
				if (a_edid && a_edid[0] != '\0' && a_this->GetFormID() < 0xFF000000) {
					AddToGameMap(a_this, a_edid);
				}
				return _SetFormEditorID(a_this, a_edid);
			}

			inline static REL::Relocation<decltype(&GetFormEditorID_Hook)> _GetFormEditorID;
			inline static REL::Relocation<decltype(&SetFormEditorID_Hook)> _SetFormEditorID;
		};
	} // namespace detail

	// -------------------------------------------------------------------------
	// Sanity probe. Call after kGameDataReady, once plugins are loaded.
	//
	// "LL_Vendor_Weapon_GunSpecialty" is a vanilla Fallout4.esm TESLevItem that
	// always exists - exactly the kind of form whose EditorID this mod relies
	// on. If this resolves, EditorIDs are working (whether from Baka/Hydra or
	// from our own hooks). If not, output falls back to bare form IDs.
	// -------------------------------------------------------------------------
	inline bool IsWorking()
	{
		const auto& [map, lock] = RE::TESForm::GetAllFormsByEditorID();
		if (!map) return false;
		const RE::BSAutoReadLock locker{ lock.get() };
		return map->contains("LL_Vendor_Weapon_GunSpecialty");
	}

	// -------------------------------------------------------------------------
	// Install hooks unless Hydra is present. Call at kPostLoad, BEFORE plugin
	// loading begins - that's the only window in which the engine still calls
	// SetFormEditorID for the forms it's about to discard the EDIDs of. We
	// can't probe the map here (no forms loaded yet, so it's empty for everyone
	// including when Hydra is present); module presence is the only signal
	// available this early.
	//
	// The hooked form-type list mirrors Baka Framework's. Types the engine
	// keeps EDIDs for natively (keywords, globals, ...) are omitted; types it
	// discards (NPCs, weapons, leveled lists, OMODs, ...) are hooked.
	// -------------------------------------------------------------------------
	inline void InstallIfNeeded()
	{
		static bool installed = false;
		if (installed) return;

		// Hydra is present - it loads EditorIDs more completely than we do
		// (notably NPC_ records, which our hook fails to capture for reasons
		// still unknown). Skip our install entirely and let Hydra handle it.
		if (GetModuleHandleA("Hydra.dll") != nullptr) {
			Log("[EditorIDLoader] Hydra detected; deferring EditorID loading to Hydra.");
			return;
		}

		Log("[EditorIDLoader] No Hydra detected. Installing our own hooks "
			"(adapted from Baka Framework, GPL-3.0) so EditorIDs are available.");

		using namespace detail;

		Hook<RE::BGSTransform>::Install();
		Hook<RE::BGSComponent>::Install();
		Hook<RE::BGSTextureSet>::Install();
		Hook<RE::BGSDamageType>::Install();
		Hook<RE::TESClass>::Install();
		Hook<RE::TESFaction>::Install();
		Hook<RE::TESEyes>::Install();
		Hook<RE::BGSAcousticSpace>::Install();
		Hook<RE::EffectSetting>::Install();
		Hook<RE::Script>::Install();
		Hook<RE::TESLandTexture>::Install();
		Hook<RE::EnchantmentItem>::Install();
		Hook<RE::SpellItem>::Install();
		Hook<RE::ScrollItem>::Install();
		Hook<RE::TESObjectACTI>::Install();
		Hook<RE::BGSTalkingActivator>::Install();
		Hook<RE::TESObjectARMO>::Install();
		Hook<RE::TESObjectBOOK>::Install();
		Hook<RE::TESObjectCONT>::Install();
		Hook<RE::TESObjectDOOR>::Install();
		Hook<RE::IngredientItem>::Install();
		Hook<RE::TESObjectLIGH>::Install();
		Hook<RE::TESObjectMISC>::Install();
		Hook<RE::TESObjectSTAT>::Install();
		Hook<RE::TESObjectREFR>::Install();
		Hook<RE::TESObjectCELL>::Install();
		Hook<RE::TESGrass>::Install();
		Hook<RE::TESObjectTREE>::Install();
		Hook<RE::TESFlora>::Install();
		Hook<RE::TESFurniture>::Install();
		Hook<RE::TESObjectWEAP>::Install();
		Hook<RE::TESAmmo>::Install();
		Hook<RE::TESNPC>::Install();
		Hook<RE::TESLevCharacter>::Install();
		Hook<RE::TESLeveledList>::Install();
		Hook<RE::TESKey>::Install();
		Hook<RE::AlchemyItem>::Install();
		Hook<RE::BGSIdleMarker>::Install();
		Hook<RE::BGSNote>::Install();
		Hook<RE::BGSProjectile>::Install();
		Hook<RE::BGSHazard>::Install();
		Hook<RE::BGSBendableSpline>::Install();
		Hook<RE::TESSoulGem>::Install();
		Hook<RE::BGSTerminal>::Install();
		Hook<RE::TESLevItem>::Install();
		Hook<RE::TESWeather>::Install();
		Hook<RE::TESClimate>::Install();
		Hook<RE::BGSShaderParticleGeometryData>::Install();
		Hook<RE::BGSReferenceEffect>::Install();
		Hook<RE::TESRegion>::Install();
		Hook<RE::Explosion>::Install();
		Hook<RE::Projectile>::Install();
		Hook<RE::Actor>::Install();
		Hook<RE::PlayerCharacter>::Install();
		Hook<RE::MissileProjectile>::Install();
		Hook<RE::ArrowProjectile>::Install();
		Hook<RE::GrenadeProjectile>::Install();
		Hook<RE::BeamProjectile>::Install();
		Hook<RE::FlameProjectile>::Install();
		Hook<RE::ConeProjectile>::Install();
		Hook<RE::BarrierProjectile>::Install();
		Hook<RE::Hazard>::Install();
		Hook<RE::TESTopicInfo>::Install();
		Hook<RE::TESPackage>::Install();
		Hook<RE::AlarmPackage>::Install();
		Hook<RE::DialoguePackage>::Install();
		Hook<RE::FleePackage>::Install();
		Hook<RE::SpectatorPackage>::Install();
		Hook<RE::TrespassPackage>::Install();
		Hook<RE::TESCombatStyle>::Install();
		Hook<RE::TESLoadScreen>::Install();
		Hook<RE::TESLevSpell>::Install();
		Hook<RE::TESWaterForm>::Install();
		Hook<RE::TESEffectShader>::Install();
		Hook<RE::BGSExplosion>::Install();
		Hook<RE::BGSDebris>::Install();
		Hook<RE::TESImageSpace>::Install();
		Hook<RE::BGSListForm>::Install();
		Hook<RE::BGSPerk>::Install();
		Hook<RE::BGSBodyPartData>::Install();
		Hook<RE::BGSAddonNode>::Install();
		Hook<RE::BGSCameraShot>::Install();
		Hook<RE::BGSCameraPath>::Install();
		Hook<RE::BGSMaterialType>::Install();
		Hook<RE::BGSImpactData>::Install();
		Hook<RE::BGSImpactDataSet>::Install();
		Hook<RE::TESObjectARMA>::Install();
		Hook<RE::BGSEncounterZone>::Install();
		Hook<RE::BGSLocation>::Install();
		Hook<RE::BGSMessage>::Install();
		Hook<RE::BGSLightingTemplate>::Install();
		Hook<RE::BGSFootstep>::Install();
		Hook<RE::BGSFootstepSet>::Install();
		Hook<RE::BGSDialogueBranch>::Install();
		Hook<RE::BGSMusicTrackFormWrapper>::Install();
		Hook<RE::TESWordOfPower>::Install();
		Hook<RE::TESShout>::Install();
		Hook<RE::BGSEquipSlot>::Install();
		Hook<RE::BGSRelationship>::Install();
		Hook<RE::BGSScene>::Install();
		Hook<RE::BGSAssociationType>::Install();
		Hook<RE::BGSOutfit>::Install();
		Hook<RE::BGSArtObject>::Install();
		Hook<RE::BGSMaterialObject>::Install();
		Hook<RE::BGSMovementType>::Install();
		Hook<RE::BGSDualCastData>::Install();
		Hook<RE::BGSSoundCategory>::Install();
		Hook<RE::BGSSoundOutput>::Install();
		Hook<RE::BGSCollisionLayer>::Install();
		Hook<RE::BGSColorForm>::Install();
		Hook<RE::BGSReverbParameters>::Install();
		Hook<RE::BGSAimModel>::Install();
		Hook<RE::BGSConstructibleObject>::Install();
		Hook<RE::BGSMod::Attachment::Mod>::Install();
		Hook<RE::BGSMaterialSwap>::Install();
		Hook<RE::BGSZoomData>::Install();
		Hook<RE::BGSInstanceNamingRules>::Install();
		Hook<RE::BGSSoundKeywordMapping>::Install();
		Hook<RE::BGSAudioEffectChain>::Install();
		Hook<RE::BGSSoundCategorySnapshot>::Install();
		Hook<RE::BGSSoundTagSet>::Install();
		Hook<RE::BGSLensFlare>::Install();
		Hook<RE::BGSGodRays>::Install();

		installed = true;
		Log("[EditorIDLoader] Hooks installed.");
	}
} // namespace RobCoMigrator::EditorIDLoader
