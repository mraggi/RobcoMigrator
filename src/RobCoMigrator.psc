ScriptName RobCoMigrator Native Hidden

; ---------------------------------------------
; C++ NATIVE BINDINGS
; ---------------------------------------------

; Returns: -1 = Error, 0 = Nothing to write, 1 = Success, 2 = Already generated this session
Int Function GeneratePatch(String a_modFolderName) global native

; Updates the live RAM lock inside the DLL
Function SetRevertUnlocked(Bool a_unlocked) global native

; Returns: -2 = User Locked, -1 = Not Generated, 1 = Ready
Int Function GetRevertStatus() global native

; Grabs the raw forms directly from the DataHandler scanner
Form[] Function GetInjectedLists() global native

; Number of auto-corrections + preserved lines from the LAST GeneratePatch run
Int Function GetLastFixCount() global native

; Current player display name (matches how patch file is named)
String Function GetCurrentPlayerName() global native

; Returns a warning string if there are Patch_<name>.ini files in the folder
; that belong to OTHER characters, or "" if all the patch files match the current
; player. Safe to call from any MCM button - it's pure filesystem + display name.
String Function GetForeignPlayerFileWarning() global native


; ---------------------------------------------
; MCM BUTTON HOOKS
; ---------------------------------------------

Function RunMigration() global
    String folderName = "RobCoMigrator_Export"

    String foreignWarning = GetForeignPlayerFileWarning()
    If foreignWarning != ""
        Debug.MessageBox("HEADS UP - other characters' files detected\n\n" + foreignWarning + "\n\n(Closing this will continue with patch generation.)")
    EndIf

    Debug.Notification("RobCo Migrator: Scanning leveled lists...")

    Int result = GeneratePatch(folderName)

    If result == 2
        Debug.MessageBox("ALREADY GENERATED\n\nYou already generated a patch this session. Review the .ini, click 'I've reviewed the .ini - unlock revert', click 'REVERT ALL LISTS', save to a new slot, then exit to desktop.")
        Return
    EndIf

    If result == -1
        Debug.MessageBox("ERROR\n\nSomething went wrong on the C++ side. Check:\nData/F4SE/Plugins/RobCoMigrator.log")
        Return
    EndIf

    String playerName = GetCurrentPlayerName()
    String filePath = "Data/F4SE/Plugins/RobCo_Patcher/leveledList/" + folderName + "/Patch_" + playerName + ".ini"
    Int fixCount = GetLastFixCount()

    String fixNote = ""
    If fixCount > 0
        fixNote = "\n\nHEADS UP: " + fixCount + " line(s) needed auto-correction. Check the bottom of the file for any lines that couldn't be parsed (listed under 'Preserved lines')."
    EndIf

    If result == 0
        Debug.MessageBox("NOTHING TO DO\n\nNo dynamic injections found and no existing patch file. Either no injection mods are loaded, or you've already migrated." + fixNote)
        Return
    EndIf

    String msg = "PATCH WRITTEN\n\n" + filePath + "\n\n"
    msg += "Next steps (in order):\n"
    msg += "  1. Alt-tab and open the file. Skim it.\n"
    msg += "  2. Back here: click 'I've reviewed the .ini - unlock revert'.\n"
    msg += "  3. Click 'REVERT ALL LISTS'.\n"
    msg += "  4. Save to a NEW slot.\n"
    msg += "  5. Exit to desktop. RobCo Patcher applies the patch on the next load."
    msg += fixNote

    Debug.MessageBox(msg)
EndFunction

Function RunMigrationUnlock() global
    SetRevertUnlocked(True)
    Debug.MessageBox("REVERT UNLOCKED\n\nThe 'REVERT ALL LISTS' button is now active.\n\nFile is at:\nData/F4SE/Plugins/RobCo_Patcher/leveledList/RobCoMigrator_Export/Patch_" + GetCurrentPlayerName() + ".ini")
EndFunction

Function RunMigrationRevert() global
    Int status = GetRevertStatus()

    If status == -2
        Debug.MessageBox("REVERT LOCKED\n\nClick 'I've reviewed the .ini - unlock revert' first.")
        Return
    ElseIf status == -1
        Debug.MessageBox("REVERT BLOCKED\n\nGenerate a patch first (click '>>> [Smart] Generate / Update ini').")
        Return
    EndIf

    Form[] listsToRevert = GetInjectedLists()
    If listsToRevert.Length == 0
        Debug.MessageBox("NOTHING TO REVERT\n\nNo live script-injected entries found. You likely already reverted in a previous session.")
        Return
    EndIf

    Int revertedCount = 0
    Int i = 0
    While i < listsToRevert.Length
        LeveledItem lItem = listsToRevert[i] as LeveledItem
        If lItem
            lItem.Revert()
            revertedCount += 1
        EndIf
        i += 1
    EndWhile

    SetRevertUnlocked(false)

    String msg = "REVERTED " + revertedCount + " LEVELED LIST(S)\n\n"
    msg += "Now:\n"
    msg += "  1. Save to a NEW slot.\n"
    msg += "  2. Exit to desktop.\n"
    msg += "  3. Load the new save. RobCo Patcher reads the .ini on startup and re-injects everything cleanly."
    Debug.MessageBox(msg)
EndFunction
