# svdt
## tdvs, but backwards (whatever that means)
svdt is a save explorer and manager for the 3DS. It is heavily based on smealum's 3ds_hb_menu/browser and sploit_installer in many parts, and the author is indebted to smea for making all of his code open-source.

### Installation and startup
Put svdt.3dsx, svdt.smdh, and svdt.xml in sd:/3ds/svdt/. Run from the homebrew launcher. Make sure you can select a target app, since svdt will want to access its save data.

Since some games don't handle gfxInitDefault() well for some reason (Animal Crossing: New Leaf is a prominent example), svdt has some emergency operations available for all games. Since the homebrew launcher doesn't care about the L/R shoulder buttons, these are ideal for binding to functions that need to be executed before the application gets to gfxInitDefault().
* svdt automatically backs up all save data at startup into a time-stamped folder in the SD card (ex. sd:/svdt/Cubic Ninja/20151021_060942/). Hold down the left shoulder button while starting svdt to skip this.
* Hold down the right shoulder button while starting svdt to dump the contents of sd:/svdt_inject/ into the target save data. If no directory named svdt_inject exists in the SD root, nothing is attempted.

When svdt starts up, it checks for whether the target game is on a gamecard or the SD card.
* If the target is a gamecard, svdt automatically fetches the game title (the short English title in exeFS:/icon).
* If the target is a digital game, svdt asks you to pick the game title from a list of all titles on the device. This is because at present, svdt does not have an automatic way to fetch the title ID. Use left/right buttons on the D-pad to browse through the title list, and press A to select a title. (You can also press B to skip this step.)

If the selected title uses anti-savegame restore, svdt has some built-in measures to automatically use the secure value currently in save data when restoring older save files. svdt will prompt you to enable this if it starts up successfully, but will attempt to automatically work against anti-savegame restore in emergency mode.

While the game title is otherwise unnecessary for actually accessing data, svdt uses it for naming backups of all save data. Emergency backups always go to sd:/svdt/[title]/[timestamp], while interactive ones can also go to sd:/[current working directory]/[title]_[timestamp].

### Brief UI guide
* There are two panes in view. The left pane lists files and directories in the target app's save data. The right pane does the same for the SD card. Both listings start at root.
* A line along the bottom of the upper screen usually shows the target title. If an action selected by the user needs confirmation, this is indicated instead in this line.
* A cursor to the left of the active listing indicates the current selected entry. Use up/down on the D-pad to move the cursor up and down the listing. Use L/R D-pad or shoulder buttons to switch which listing is selected (SD versus save data).
* The top listing of each pane is the current working directory path, either in full or truncated. (The full path is always shown in the debug output when the directory is first selected.)
 * Selecting this line with the cursor and pressing A refreshes the whole listing.
 * Selecting this line with the cursor and pressing Y dumps the contents of the working directory onto a subdirectory in the other working directory (save to SD, or vice versa). If / is dumped (only possible from save to SD), the subdirectory is named with a timestamp. If a non-root directory is dumped, the subdirectory just takes on the name of the directory.
* The second listing of each pane is a dummy entry for the parent directory. Selecting this with A or pressing B at any time navigates to the parent directory.
* All further listings are files and directories in the working directory.
 * Press A to navigate inside a subdirectory.
 * Press X to delete the selected file or directory (recursively). svdt will ask for confirmation by pressing SELECT.
 * Press Y to copy the selected file or directory (recursively) across. svdt will ask for confirmation if it sees that the file you are trying to copy may overwrite a file in the destination directory.
 * If there are more files and directories in the folder than can fit on the screen, you actually can scroll up and down past the last on-screen item. There is no visual indicator for this, however.
* Press SELECT to see a set of instructions on the lower screen. Normally, the screen is full of moderately useful debug output. Pressing SELECT also switches how SD and save data listings are sorted (by date by default, by name if desired).
* Press START to exit back to the homebrew launcher.

### Known issues
* Some games don't let svdt start up, leaving it hanging on an incoherent screen. This is also an issue with profi200's save_manager (and perhaps could be correlated with regionFOUR incompatibility?), but save_manager is designed simply enough that it will still work if you press the right buttons. This needs to be entered into consideration for svdt as well.
* The homebrew launcher may hang while trying to start svdt, on a blue or white or otherwise abstract screen. I swear it's not my fault.
* The homebrew launcher doesn't always show the target app selection screen. svdt has no mechanism in place at the moment to check whether there is a target app, but this should be fairly straightforward to implement (check for target app name?). If svdt starts without a target, then the output is garbled slightly at first, but it functions for the most part as a SD data browser. There are better SD card browsers, so using svdt in this way is not recommended.
* svdt does not handle running out of space gracefully. *This is because if the save data does run out of space, then trying to continue writing to save data (even after reinitialising FS handles and archives) may corrupt it.* For now, if svdt detects any problem at all with manipulating files, it just throws a fatal error and asks you to quit out. There may not really be a better option.
* The code is a mess, and I just need to comment it.
