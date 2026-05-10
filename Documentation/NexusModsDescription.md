[center][size=6][b][color=#00ffff]RobCo Migrator[/color][/b][/size]
[i]Dynamically convert Leveled List injections into safe, conflict-free RobCo Patcher files![/i][/center]

[line]

[size=5][b][color=#ffff00]What does this mod do?[/color][/b][/size]
For years, mod authors have used Papyrus scripts to dynamically inject weapons, armor, and items into the game's Leveled Lists. While this avoids direct plugin conflicts, it bakes that data directly into your save file. Over time, this bloats the save, makes uninstalling mods dangerous, and if you don't like some mod having injected their stuff everywhere, too bad. Some mod authors are "too eager" and inject their weapon into every list basically. Yes, your gun is cool, but I don't want every single raider/gunner/minuteman/bos soldier/vendor to have it!

[b]RobCo Migrator[/b] is an F4SE plugin that solves this. It safely scans your currently loaded save file in RAM, finds every single script-injected item, and automatically generates a clean, readable [b]RobCo Patcher INI file[/b] for them. It then gives you the option to safely purge the injections from your save file. 

The result? All your modded loot stays exactly where it belongs, but your save file is completely clean, and you can easily edit the ini files to remove unwanted items!

[line]

[size=5][b][color=#ff0000]⚠️ IMPORTANT WARNINGS ⚠️[/color][/b][/size]
[list]
[*][b]FOLLOW THE INSTRUCTIONS:[/b] This tool modifies active memory. You [u]must[/u] follow the step-by-step instructions below exactly as written to avoid saving duplicate items into your Leveled Lists.
[*][b]MULTIPLE CHARACTERS:[/b] You probably should [b]NOT[/b] use this tool if you are actively playing multiple characters simultaneously on the same mod setup. RobCo Patcher INI files apply globally to your entire game. If Character A has different quest-locked injections than Character B, migrating them to a global file may cause loot to appear on the wrong character. Maybe one day I'll add a ini-manager-per-character, but today is not that day.
[/list]

[line]

[size=5][b][color=#00ff00]Requirements[/color][/b][/size]
[list]
[*][url=https://f4se.silverlock.org/]Fallout 4 Script Extender (F4SE)[/url]
[*][url=https://www.nexusmods.com/fallout4/mods/21497]Mod Configuration Menu (MCM)[/url]
[*][url=https://www.nexusmods.com/fallout4/mods/69798]RobCo Patcher[/url]
[/list]

[line]

[size=5][b][color=#ffff00]Step-by-Step Instructions[/color][/b][/size]
[i]Read carefully before using the tool![/i]

[b]1.[/b] Load your save game and go to a safe, interior location. Make a [b]HARD SAVE[/b] right now.
[b]2.[/b] Open the [b]Mod Configuration Menu (MCM)[/b] and select [b]RobCo Migrator[/b].
[b]3.[/b] Click ONE of the "Generate" buttons. (If this is your first time, use [b]"Generate: Update previous"[/b]).
[b]4.[/b] A message will pop up confirming success. [b]Alt-Tab[/b] out of Fallout 4.
[b]5.[/b] Navigate to your Fallout 4 folder: [font=Courier New]Data/F4SE/Plugins/RobCo_Patcher/leveledList/RobCoMigrator_Export[/font]
[b]6.[/b] Open the newly generated [b].ini[/b] file. Verify that it looks correct and contains your modded items. (A .csv file is also generated for spreadsheet viewing!)
[b]7.[/b] Tab back into Fallout 4. In the MCM, click [b]"I've reviewed the ini file and am ready to revert"[/b]. This disables the safety lock.
[b]8.[/b] Click [b]"Revert ALL lists (DANGER)"[/b]. This purges the injections from your save file's RAM.
[b]9.[/b] Exit the MCM and [b]IMMEDIATELY[/b] save your game into a [b]NEW SLOT[/b]. 
[b]10.[/b] [b]EXIT FALLOUT 4 COMPLETELY TO DESKTOP.[/b] Do not keep playing on this session.
[b]11.[/b] Launch Fallout 4 and load your new save normally. You're done!

[line]

[size=5][b][color=#00ffff]Frequently Asked Questions (FAQ)[/color][/b][/size]

[b]Q: What happens if a new mod (or an old mod via a quest) injects into a leveled list AFTER I've already migrated?[/b]
[b]A:[/b] Nothing bad! It will work seamlessly alongside your generated RobCo patch. The new items will just exist as standard Papyrus injections again. If you want to clean your save again later, simply load up the MCM, click [b]"Generate: Update previous"[/b] to append the new items to your INI file, and run the revert process again.

[b]Q: Can I safely uninstall RobCo Migrator after I generate my ini files?[/b]
[b]A:[/b] Yes. The generated INI files belong to RobCo Patcher. Once the lists are reverted and saved, RobCo Migrator has done its job and can be safely disabled if you wish.

[b]Q: Do I uninstall the weapon/armor mods after doing this?[/b]
[b]A:[/b] [b]NO![/b] Keep your weapon and armor mods installed. The INI files just tell RobCo Patcher how to distribute the items. If you uninstall the original mods, the items will disappear.

[line]

[size=5][b][color=#ffff00]Credits & Special Thanks[/color][/b][/size]
This tool stands on the shoulders of giants. Massive thanks to:
[list]
[*][b]The libxse Team[/b] for F4SE and CommonLibF4. Without their reverse-engineering, none of this would be possible.
[*][b]Zzyxzz[/b] for the incredible [b]RobCo Patcher[/b].
[*][b]Neanka[/b] and the [b]MCM team[/b] for the Mod Configuration Menu.
[/list]
