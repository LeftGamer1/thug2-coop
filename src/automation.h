// Test automation ([debug] automation=1): keys injected into this game's own DirectInput keyboard and scripts
// spawned, from commands in coop_cmd_<pid>.txt next to the exe. No window focus needed, nothing reaches other
// programs. Commands, one per line: "key <name> [hold ms]", "wait <ms>", "spawn <script>".
#pragma once
#include <windows.h>

namespace automation {

// Wraps the DirectInput object the game gets from DirectInput8Create (keyboard reads see the injected keys).
void WrapDirectInput(void* directInput);
// Once a frame on the game's thread: reads new commands, runs them over time. `spawn` runs scripts through it.
// `command` handles verbs the module doesn't know (goto ...): true when it took the command.
void Update(void (*spawn)(const char* script), bool (*command)(const char* verb, const char* arg, int value));
// A key held down (or let go) by the caller, e.g. a steering bot. Scan codes as DirectInput's.
void Hold(int scan, bool down);

} // namespace automation
