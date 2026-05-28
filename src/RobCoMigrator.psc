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


; ---------------------------------------------
; MCM BUTTON HOOKS
; ---------------------------------------------

Function RunMigration() global
    String folderName = "RobCoMigrator_Export"

    Debug.Notification("RobCo Migrator: Scanning leveled lists...")

    Int result = GeneratePatch(folderName)

    If result == 2
        Debug.MessageBox("STOP\n\nYou already generated a patch this session.\n\nWhy: generating twice in a row would re-scan the same RAM state and end up duplicating effort. After the FIRST generate of a session you should:\n  - Open and skim the .ini\n  - Click the Revert button\n  - Save into a NEW slot\n  - EXIT TO DESKTOP\n\nThen, on the next session, generate again if you need to.")
        Return
    EndIf

    If result == -1
        Debug.MessageBox("ERROR\n\nSomething went wrong inside the C++ side. Check the log file for details:\n\nData/F4SE/Plugins/RobCoMigrator.log")
        Return
    EndIf

    String playerName = GetCurrentPlayerName()
    String filePath = "Data/F4SE/Plugins/RobCo_Patcher/leveledList/" + folderName + "/Patch_" + playerName + ".ini"
    Int fixCount = GetLastFixCount()

    String fixNote = ""
    If fixCount > 0
        fixNote = "\n\n----------\nHEADS UP: " + fixCount + " line(s) in your existing .ini needed fixing.\nThis happens when something is malformed (missing values, typos, etc.). I auto-corrected what I could; anything I couldn't parse is preserved verbatim at the BOTTOM of the file under a 'Preserved lines' header.\n\nOpen the file and skim it before reverting - just to be safe."
    EndIf

    If result == 0
        Debug.MessageBox("NOTHING TO DO\n\nYour save has no dynamic leveled-list injections AND no existing patch file was found.\n\nThat means either:\n  - You haven't actually loaded any mods that inject leveled lists yet, OR\n  - You already migrated everything and reverted - in which case, you're done. RobCo Patcher is handling it from here." + fixNote)
        Return
    EndIf

    String msg = "PATCH WRITTEN\n\nYour patch file is here:\n" + filePath + "\n\n"
    msg += "WHAT THIS DOES:\nThe file you just wrote tells RobCo Patcher to re-inject every entry into the right leveled lists, every time the game loads. That's the goal: replace 'these injections live in my save file' (fragile, bloats saves) with 'these injections come from a patch file' (stable, save-clean).\n\n"
    msg += "DO THESE NEXT STEPS IN ORDER:\n\n"
    msg += "  1. ALT-TAB out of Fallout 4 and OPEN the file.\n     Just skim it. Make sure the list of injections looks like what you expect.\n\n"
    msg += "  2. Come back to this MCM and click 'I've reviewed the .ini'.\n     That unlocks the Revert button. (Safety: if you didn't actually look, you skip this whole point.)\n\n"
    msg += "  3. Click 'REVERT ALL LISTS'.\n     This wipes the live injections from your save's RAM. Without this step, when you reload you'd have BOTH the old in-save injections AND RobCo Patcher's, which means duplicates.\n\n"
    msg += "  4. SAVE INTO A NEW SLOT.\n     Not an overwrite. A new slot. (The papyrus VM is in a weird transitional state after a revert; saving over an existing slot can corrupt it.)\n\n"
    msg += "  5. EXIT TO DESKTOP. Do not keep playing.\n     Load the new save you just made. RobCo Patcher reapplies your patch fresh on load - clean state."
    msg += fixNote

    Debug.MessageBox(msg)
EndFunction

Function RunMigrationUnlock() global
    SetRevertUnlocked(True)
    Debug.MessageBox("REVERT UNLOCKED\n\nThe 'REVERT ALL LISTS' button is now active.\n\nIf you didn't actually open and review the .ini file yet, please do that FIRST before clicking revert. Reverting wipes the live injections from RAM - if your patch file is wrong or missing entries, you'd lose them.\n\nThe file is at:\nData/F4SE/Plugins/RobCo_Patcher/leveledList/RobCoMigrator_Export/Patch_" + GetCurrentPlayerName() + ".ini")
EndFunction

Function RunMigrationRevert() global
    Int status = GetRevertStatus()

    If status == -2
        Debug.MessageBox("REVERT LOCKED\n\nYou need to click 'I've reviewed the .ini' first.\n\nThe two-button gate exists because reverting is destructive - it wipes the live injections from RAM. The MCM wants to be sure you actually looked at the patch file before nuking the live state.")
        Return
    ElseIf status == -1
        Debug.MessageBox("REVERT BLOCKED\n\nYou haven't generated a patch this session. Click 'Generate / Update Patch' first.")
        Return
    EndIf

    Form[] listsToRevert = GetInjectedLists()
    If listsToRevert.Length == 0
        Debug.MessageBox("NOTHING TO REVERT\n\nThe scanner didn't find any leveled lists with live script-injected entries.\n\nThat usually means you already reverted in a previous session and the file you generated this time just represents what RobCo Patcher is already maintaining. You're fine - no action needed.")
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

    String msg = "REVERTED " + revertedCount + " LEVELED LIST(S)\n\nThe live script-injected entries are gone from RAM.\n\n"
    msg += "DO NOT KEEP PLAYING. Now:\n\n"
    msg += "  1. SAVE INTO A NEW SLOT. Not an overwrite. (The papyrus VM is mid-mutation; overwriting an existing save can corrupt it.)\n\n"
    msg += "  2. EXIT TO DESKTOP. Fully close the game.\n\n"
    msg += "  3. Launch Fallout 4 again and LOAD the new save you just made.\n     RobCo Patcher will reapply your patch file from scratch. You'll see exactly the same items in your leveled lists - but this time they're coming from disk, not bloating your save."
    Debug.MessageBox(msg)
EndFunction

; Called from C++ on game load when other-character patch files are detected
Function ShowLoadWarning(String msg) global
    Debug.MessageBox(msg)
EndFunction
