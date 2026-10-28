// Gamepads through XInput (Xbox pads, and others through Steam Input or DS4Windows), added to the game's own pad 1
// (keyboard and DirectInput) in its PS2-style control data. The data's format and the layout follow PARTYMOD for
// THUG2 (https://github.com/PARTYMANX/partymod-thug2, src/input.c, MIT License, Copyright (c) 2024 PARTYMANX; see
// THIRD_PARTY_NOTICES.txt).
#pragma once
#include <windows.h>

namespace gamepad {

// Hooks the pad's per-frame processing; false if the exe's bytes aren't the expected ones.
bool Install(void (*log)(const char*, ...));
// Wraps the game's DirectInput object: game controllers are hidden from the game while an XInput pad is connected
// (it read the same pad again through DirectInput, with its own mapping).
void WrapDirectInput(void* directInput);
// Test only: a pad state used instead of XInput's ("a b x y lb rb lt rt back start ls rs up down left right",
// sticks "lx=-1..1 ly= rx= ry="), for `ms` milliseconds.
void Inject(const char* buttons, int ms);

} // namespace gamepad
