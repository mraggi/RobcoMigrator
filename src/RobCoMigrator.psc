ScriptName RobCoMigrator Native Hidden

; ---------------------------------------------
; C++ NATIVE BINDINGS
; ---------------------------------------------
; Returns: -1 = Error, 0 = Clean Save, 1 = Success, 2 = Already Generated This Session
Int Function GeneratePatch(String a_modFolderName, Int a_exportMode) global native

; Updates the live RAM lock inside the DLL
Function SetRevertUnlocked(Bool a_unlocked) global native

; Returns: -2 = User Locked, -1 = Not Generated, 1 = Success
Int Function GetRevertStatus() global native

; Grabs the raw forms directly from the DataHandler scanner
Form[] Function GetInjectedLists() global native

; ---------------------------------------------
; MCM BUTTON HOOKS
; ---------------------------------------------
Function RunMigrationCreateNew() global
    DoMigration(0)
EndFunction

Function RunMigrationOverwrite() global
    DoMigration(1)
EndFunction

Function RunMigrationMerge() global
    DoMigration(2)
EndFunction

Function RunMigrationUnlock() global
    SetRevertUnlocked(True)
    Debug.MessageBox("UNLOCKED:\n\nThe Revert feature is now unlocked. You may now click the 'Revert ALL lists' button.")
EndFunction

Function RunMigrationRevert() global
    Int status = GetRevertStatus()

    If status == -2
        Debug.MessageBox("LOCKED:\n\nYou must click 'I've reviewed the ini file...' in the MCM before clicking this button.")
        Return
    ElseIf status == -1
        Debug.MessageBox("STOP:\n\nYou must generate and review your patch before reverting the lists!")
        Return
    EndIf

    Form[] listsToRevert = GetInjectedLists()
    If listsToRevert.Length == 0
        Debug.MessageBox("ERROR:\n\nNo injected lists found to revert, or extraction failed.")
        Return
    EndIf

    Int revertedCount = 0
    Int i = 0
    While i < listsToRevert.Length
        ; Strictly cast and revert LeveledItem
        LeveledItem lItem = listsToRevert[i] as LeveledItem
        If lItem
            lItem.Revert()
            revertedCount += 1
        EndIf
        i += 1
    EndWhile

    Debug.MessageBox("SUCCESS:\n\n" + revertedCount + " Leveled Item Lists successfully purged via Papyrus!\n\nIMMEDIATELY save your game into a NEW slot and EXIT TO DESKTOP. Do not continue playing on this session.")
EndFunction

; ---------------------------------------------
; INTERNAL LOGIC
; ---------------------------------------------
Function DoMigration(Int exportMode) global
    String folderName = "RobCoMigrator_Export"

    Debug.Notification("RobCo Migrator: Scanning Leveled Lists...")

    Int result = GeneratePatch(folderName, exportMode)

    If result == 2
        Debug.MessageBox("STOP:\n\nA patch has already been generated during this session!\n\nTo prevent duplicate data and file clutter, you cannot generate multiple times in a row. Please review your generated INI file, unlock the revert feature, save into a NEW slot, and fully exit the game.")
    ElseIf result == 1
        String msg = "SUCCESS!\n\n"
        If exportMode == 0
            msg += "WARNING: New timestamped files were created alongside your existing files. If you do not manually delete the old .ini files or revert the lists, you will have duplicate injections in your game.\n\n"
        ElseIf exportMode == 1
            msg += "Old INI files were safely backed up as '.old' (with their original creation timestamps) so RobCo Patcher ignores them. Fresh data was written to a new INI file.\n\n"
        ElseIf exportMode == 2
            msg += "New data safely merged and sorted into your existing file. No duplicate entries were added.\n\n"
        EndIf

        msg += "Please tab out and check: Data/F4SE/Plugins/RobCo_Patcher/leveledList/" + folderName + "/\n\nIf the file looks correct, return to the MCM, click 'I've reviewed the ini file and am ready to revert', and then click 'Revert ALL lists'."
        Debug.MessageBox(msg)

    ElseIf result == 0
        Debug.MessageBox("SCAN COMPLETE:\n\nNo dynamic leveled list injections were found in your save file! Your game is completely clean and no files were generated.")

    Else
        Debug.MessageBox("ERROR:\n\nMigration failed. The Data Handler could not be loaded. Please check the F4SE logs.")
    EndIf
EndFunction
