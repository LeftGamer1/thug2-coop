THUG2 Story Co-op (for THEMOD 1.3)
==================================

Tony Hawk's Underground 2 Story Mode for two over the network: the host plays their own Story Mode (their own
save), the friend joins the host's game and skates alongside in the same level, with NPCs, cutscenes and goals.

INSTALL (both players)
----------------------
The game folder is the one with THUG2.exe (usually ...\Tony Hawk's Underground 2\Game).
The mod runs on top of THEMOD 1.3; it doesn't have to be installed first - the archive has everything for it.
  1. Have THEMOD already? Keep a copy of your Data\pre\thethug2mod.prx somewhere.
  2. Copy from the archive into the game folder (replace):
       dinput8.dll, coop.ini, patch_themod.bat, patch_themod.ps1
       Data\pre\thethug2mod.prx  -> Data\pre\
     No "THEMOD 1.3.exe"? Run patch_themod.bat: it makes it from your THUG2.exe (THUG2.exe stays as it is).
     That's what THEMOD 1.3's installer does, and the installer can't be found anywhere now.
     Start the game with "THEMOD 1.3.exe".
  3. In coop.ini set name= (both) and host_address= (the friend only), see SETTINGS below.
  4. Network: the host needs UDP 5153 (the game) and TCP 5154 (story sync) reachable.
     Easiest: Radmin VPN / ZeroTier on both PCs, host_address = the host's IP in that network.
     Or Porthole (Steam, free): the host shares ports UDP 5153 and TCP 5154 in its lobby, the friend accepts
     them and sets host_address=127.0.0.1.
     Windows may ask about the firewall for "THEMOD 1.3.exe" - allow it.
  5. Updating the mod? Update it on both PCs: an old and a new version don't go well together.

SETTINGS (coop.ini)
-------------------
  host_address=     the host's IP. Only the friend fills it in; over Porthole 127.0.0.1.
  port=5153         the game's port (the story sync's TCP port is the next one, 5154). The same on both PCs.
  name=             your name over your skater (15 letters at most); empty: the game's.
  shared_goals=1    shared goals (see below); 0: a goal is won for both when either one does it.
  player_collision= 1: the skaters knock each other down when they collide; 0 or empty: they ride through.
                    Set it the same on both PCs.
  [keys] host=F10 / join=F11 / teleport=F12 - the keys: host co-op, join the host, teleport to your friend.
                    F1-F24, A-Z, 0-9, Num0-Num9, Insert, Delete, Home, End, PageUp, PageDown, Pause,
                    ScrollLock, with Ctrl+ / Shift+ / Alt+ in front if you like (Ctrl+F10); none: no key.
                    Avoid F1-F4 (taunts) and Enter (chat).
  [debug] windowed=1 - the game in a 640x480 window (if it crashes on the splash screen, see IF IT DOESN'T WORK).
                    The rest of [debug] is for debugging, leave it alone.

HOW TO PLAY
-----------
  At start the game loads the latest save by itself (story, skater, volume and other options).
  Changed an option? Save, or the next start brings back the old one.
  Host:   F10 in the main menu (lobby open), then Story Mode -> Continue / Start New / Load Game Progress.
  Friend: F11 in the main menu - connects and waits until the host starts the story.
  Also later: the host presses F10 in a story level, the friend F11 - the friend joins that level.
  F12 - teleport to your friend (not while a goal is running).
  (F10/F11/F12 are the default keys; change them in coop.ini, section [keys].)
  Enter - chat.

WHAT IS SYNCED
--------------
  - The host's story: chapters, won goals, world flags, cutscenes seen. The friend gets it on joining
    and on every level change.
  - A goal won by either player is won for both: points, rewards, unlocks, world changes.
  - Cutscenes play for both; Esc (cutscenes) and num2 (goal cameras) from either player skip them for both.
  - Goal cameras (goal taken, goal won, the chapter's goal tour) show for the other player too, with their
    captions and lines; while the other player's camera plays, your skater holds still and the HUD is hidden.
  - Both players take goals: skate up to an NPC and press the talk button as usual.
  - Shared goals (shared_goals=1 in coop.ini, the default): a goal one player takes from an NPC starts for both
    and is won once both have done it. Whoever finishes first waits for the friend; a player who fails tries
    again (Retry) and the other's finish is kept. Goals in stages (the training's, say) are shared stage by stage.
    A player busy with a goal of their own isn't pulled into it.
    If the friend leaves the game, the waiting player gets the goal right away.
    shared_goals=0 - as before: won for both when either one does it.
  - Guest characters and the pro teammate: a swap shows for the other player. The host picks the teammate.
  - Your friend's name and distance over their skater, visible through walls.
  - The skaters don't knock each other down. For that, set player_collision=1 in coop.ini on both PCs.
  - Chat (Enter): the game's fonts have no Cyrillic, so Russian letters are typed transliterated by themselves
    (Привет -> Privet). No need to switch the keyboard layout.
  - Your own skater's progression (stat challenges) is your own.

LIMITS
------
  - Two players at most.
  - Story decisions (moving on to the next chapter, changing level, picking the teammate) are the host's.
  - Only the host saves the story (saving works while the friend skates). In co-op the friend neither saves
    nor loads their own story; when the friend leaves co-op, their own story comes back as it was.
  - If the friend leaves and comes back into the same level, the host's level reloads.
  - Quitting: friend - pause -> Quit (the host keeps playing, the friend can rejoin with F11).
    Host - pause -> Quit: the friend goes back to the main menu too. The host's lobby stays open.
    If the host's game closes or crashes, the friend is taken back to the main menu with a message.

IF IT DOESN'T WORK
------------------
  - The game crashes on the splash screen (Windows 11, 2K resolution): not the mod, plain THEMOD 1.3 does it
    too. Run it in a window: in coop.ini, section [debug], set windowed=1 (a 640x480 window).
  - The game crashes right at start: keep the game folder on a path in Latin letters, not in OneDrive or
    Program Files. Before unpacking: the zip's Properties -> "Unblock". Check that Defender didn't remove
    dinput8.dll. Check that the game starts without dinput8.dll - if not, it isn't the mod.
  - The friend presses F11 and nothing happens: check host_address and that the host's UDP 5153 and TCP 5154
    are reachable (in Porthole: both ports shared and accepted).
  - 1920x1080: some copies of "THEMOD 1.3.exe" have 1920x1080 in place of
    800x600 - pick "Medium (800x600)" in Launcher.exe.
  - Music or sound gone: the volume is kept in the save. Set it in Options and save.

TURN THE MOD OFF
----------------
  Delete dinput8.dll from the game folder and put your thethug2mod.prx back.

Debug log: coop.log in the game folder. If something breaks, send it.
