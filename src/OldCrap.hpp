#pragma once

// =====================================================================================
// OldCrap.hpp - Snapshot of the legacy export modes (CREATE_NEW and OVERWRITE).
//
// Before the smart-merge rewrite, GeneratePatch had three modes chosen by separate
// MCM buttons:
//   - MODE_CREATE_NEW: leave the existing Patch_<player>.ini alone and write a
//                      fresh Patch_<player>_<timestamp>.ini alongside it. RobCo
//                      Patcher then loaded BOTH files (often causing duplicate
//                      injections - which is exactly why this mode was confusing).
//   - MODE_OVERWRITE:  rename the current .ini/.csv to .old so RobCo Patcher ignores
//                      them, then write a fresh scan as if the player had never run
//                      the tool.
//   - MODE_MERGE:      naive append-only merge.
//
// The new smart merge supersedes all three: it parses the existing file, fixes any
// syntax errors, consolidates duplicate target lists, preserves user edits, and
// unions in anything new from the live save. So the two non-merge buttons were
// removed because they were error-prone and never the right choice.
//
// This file captures the per-mode rewiring helpers in case someone ever wants the
// old behavior back. It is NOT INCLUDED ANYWHERE on purpose - dead code preserved
// for reference, no compile-time cost.
// =====================================================================================

#include "LLMigrator.hpp"
#include <filesystem>
#include <format>

namespace RobCoMigrator::Legacy
{
	namespace fs = std::filesystem;

	// MODE_CREATE_NEW path-rewiring step.
	// Used to be called after the scan, before WriteINI, to redirect output to a
	// fresh timestamped file so the existing one stayed untouched.
	inline void RewireForCreateNew(
		fs::path& a_iniPath, fs::path& a_csvPath,
		const fs::path& a_targetDir,
		const std::string& a_baseNameINI, const std::string& a_baseNameCSV,
		const std::string& a_fileTimestamp)
	{
		a_iniPath = a_targetDir / std::format("{}_{}.ini", a_baseNameINI, a_fileTimestamp);
		a_csvPath = a_targetDir / std::format("{}_{}.csv", a_baseNameCSV, a_fileTimestamp);
	}

	// MODE_OVERWRITE path-rewiring step.
	// Renames the existing .ini and .csv to .old (with their original creation
	// timestamp embedded in the new name) so RobCo Patcher stops loading them.
	// After this, WriteINI runs against the original paths, producing a fresh file.
	inline void RewireForOverwrite(
		const fs::path& a_iniPath, const fs::path& a_csvPath,
		const fs::path& a_targetDir,
		const std::string& a_baseNameINI, const std::string& a_baseNameCSV,
		const std::string& a_oldTimestamp)
	{
		fs::path backupINI = a_targetDir / std::format("{}_{}.old", a_baseNameINI, a_oldTimestamp);
		fs::path backupCSV = a_targetDir / std::format("{}_{}.old", a_baseNameCSV, a_oldTimestamp);
		if (fs::exists(a_iniPath)) fs::rename(a_iniPath, backupINI);
		if (fs::exists(a_csvPath)) fs::rename(a_csvPath, backupCSV);
	}
}
