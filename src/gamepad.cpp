#include "gamepad.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include "patch.h"

namespace gamepad {
namespace {

void (*g_log)(const char*, ...) = nullptr;

// XInput (xinput1_4 on Windows 8+, else 1_3 or 9_1_0), loaded when first needed.
struct PadState {
    DWORD packet;
    WORD buttons;
    BYTE leftTrigger, rightTrigger;
    SHORT lx, ly, rx, ry;
};
using GetStateFn = DWORD(WINAPI*)(DWORD, PadState*);
GetStateFn g_getState = nullptr;
constexpr WORD kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008, kStart = 0x0010, kBack = 0x0020,
               kLeftStick = 0x0040, kRightStick = 0x0080, kLeftShoulder = 0x0100, kRightShoulder = 0x0200,
               kA = 0x1000, kB = 0x2000, kX = 0x4000, kY = 0x8000;

bool LoadXInput()
{
    static bool tried = false;
    if (!tried) {
        tried = true;
        for (const char* name : {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"}) {
            if (HMODULE module = LoadLibraryA(name)) {
                g_getState = reinterpret_cast<GetStateFn>(GetProcAddress(module, "XInputGetState"));
                if (g_getState) {
                    g_log("gamepad: %s", name);
                    break;
                }
            }
        }
    }
    return g_getState != nullptr;
}

// The first connected pad's state (polled at most every 2 s while none is connected: a disconnected slot is slow).
bool FirstPad(PadState* out)
{
    static DWORD nextTry = 0;
    static int slot = -1;
    if (!LoadXInput())
        return false;
    if (slot >= 0 && g_getState(slot, out) == ERROR_SUCCESS)
        return true;
    if (slot >= 0) {
        g_log("gamepad: pad %d disconnected", slot);
        slot = -1;
    }
    if (GetTickCount() < nextTry)
        return false;
    nextTry = GetTickCount() + 2000;
    for (DWORD i = 0; i < 4; ++i) {
        if (g_getState(i, out) == ERROR_SUCCESS) {
            slot = static_cast<int>(i);
            g_log("gamepad: pad %d connected", slot);
            return true;
        }
    }
    return false;
}

PadState g_injected = {};
DWORD g_injectedUntil = 0;

// A stick axis as the PS2 data has it (0..255, 128 the middle), with a dead zone.
uint8_t Axis(SHORT value, SHORT deadZone, bool invert)
{
    int v = value;
    if (v > -deadZone && v < deadZone)
        v = 0;
    if (invert)
        v = -v - 1;
    int byte = (v >> 8) + 128;
    return static_cast<uint8_t>(byte < 0 ? 0 : byte > 255 ? 255 : byte);
}

int Distance(uint8_t axis) { return axis >= 128 ? axis - 128 : 128 - axis; }

// Sio::Device (thug2-decomp; PARTYMOD's input.c names the fields): +0x0C port, +0x14 valid, +0x1C the control data
// (32 bytes, PS2 format: [2] [3] button bits, 1 = up; [4..7] right stick x y, left stick x y; [8..19] pressures:
// right left up down, triangle circle cross square, L1 R1 L2 R2; [20] the Xbox black (1) and white (2) buttons),
// +0xE0 plugged in, +0xEC pressed, +0xF0 start or A pressed.
void Merge(uint8_t* device, const PadState& pad)
{
    uint8_t* data = device + 0x1C;
    uint8_t up2 = 0, up3 = 0; // bits pressed (cleared in the data)
    auto button = [&](bool down, uint8_t& bits, int bit, int pressure) {
        if (!down)
            return;
        bits |= static_cast<uint8_t>(1 << bit);
        if (pressure >= 0)
            data[pressure] = 0xFF;
    };
    WORD b = pad.buttons;
    button(b & kBack, up2, 0, -1);        // camera toggle (select)
    button(b & kLeftStick, up2, 1, -1);   // focus (L3)
    button(b & kRightStick, up2, 2, -1);  // camera swivel lock (R3)
    button(b & kStart, up2, 3, -1);       // pause
    button(b & kUp, up2, 4, 10);
    button(b & kRight, up2, 5, 8);
    button(b & kDown, up2, 6, 11);
    button(b & kLeft, up2, 7, 9);
    button(b & kY, up3, 4, 12);           // grind (triangle)
    button(b & kB, up3, 5, 13);           // grab (circle)
    button(b & kA, up3, 6, 14);           // ollie (cross)
    button(b & kX, up3, 7, 15);           // flip (square)
    bool leftTrigger = pad.leftTrigger > 0x40, rightTrigger = pad.rightTrigger > 0x40;
    button(leftTrigger, up3, 2, 16);      // nollie: L1 and L2, as the PC game reads the Xbox triggers
    button(leftTrigger, up3, 0, 18);
    button(rightTrigger, up3, 3, 17);     // switch / revert: R1 and R2
    button(rightTrigger, up3, 1, 19);
    if (b & kLeftShoulder)
        data[20] |= 2;                    // spin left (white)
    if (b & kRightShoulder)
        data[20] |= 1;                    // spin right (black)
    data[2] &= static_cast<uint8_t>(~up2);
    data[3] &= static_cast<uint8_t>(~up3);
    const uint8_t sticks[4] = {Axis(pad.rx, 8689, false), Axis(pad.ry, 8689, true), Axis(pad.lx, 7849, false),
                               Axis(pad.ly, 7849, true)};
    for (int i = 0; i < 4; ++i)
        if (Distance(sticks[i]) > Distance(data[4 + i]))
            data[4 + i] = sticks[i];
    device[0x14] = 1;
    device[0xE0] = 1;
    if (up2 || up3 || data[20])
        *reinterpret_cast<uint32_t*>(device + 0xEC) = 1;
    if ((b & kStart) || (b & kA))
        *reinterpret_cast<uint32_t*>(device + 0xF0) = 1;
}

// Sio::Device::Process 0x005BDCC0 (thiscall, one argument): the game's own reading, then the pad on port 1.
using ProcessFn = void(__thiscall*)(void*, void*);
ProcessFn g_process = nullptr;
bool g_padSeen = false;

void __fastcall ProcessHook(void* device, void*, void* arg)
{
    g_process(device, arg);
    auto* d = static_cast<uint8_t*>(device);
    if (*reinterpret_cast<uint32_t*>(d + 0x0C) != 0)
        return;
    PadState pad = {};
    if (GetTickCount() < g_injectedUntil)
        pad = g_injected;
    else if (!FirstPad(&pad))
        return;
    g_padSeen = true;
    Merge(d, pad);
}

// IDirectInput8A::EnumDevices (slot 4): game controllers left out while an XInput pad is connected.
using EnumCallback = BOOL(CALLBACK*)(const void*, void*);
using EnumDevicesFn = HRESULT(WINAPI*)(void*, DWORD, EnumCallback, void*, DWORD);
EnumDevicesFn g_enumDevices = nullptr;
struct EnumContext {
    EnumCallback callback;
    void* ref;
};

BOOL CALLBACK FilterDevice(const void* instance, void* ref)
{
    auto* context = static_cast<EnumContext*>(ref);
    DWORD type = *reinterpret_cast<const DWORD*>(static_cast<const uint8_t*>(instance) + 36) & 0xFF; // dwDevType
    if (type >= 0x14 && type <= 0x18) { // joystick, gamepad, driving, flight, first person
        g_log("gamepad: a DirectInput game controller hidden from the game (read through XInput)");
        return TRUE; // DIENUM_CONTINUE
    }
    return context->callback(instance, context->ref);
}

HRESULT WINAPI EnumDevicesHook(void* input, DWORD type, EnumCallback callback, void* ref, DWORD flags)
{
    PadState pad;
    if (!callback || !FirstPad(&pad))
        return g_enumDevices(input, type, callback, ref, flags);
    EnumContext context = {callback, ref};
    return g_enumDevices(input, type, &FilterDevice, &context, flags);
}

} // namespace

bool Install(void (*log)(const char*, ...))
{
    g_log = log;
    const uint8_t prologue[] = {0x8B, 0x81, 0xD4, 0x00, 0x00, 0x00, 0x48}; // mov eax,[ecx+0D4h]; dec eax
    if (!patch::Matches(0x005BDCC0, prologue, sizeof(prologue)))
        return false;
    g_process = reinterpret_cast<ProcessFn>(patch::Detour(0x005BDCC0, sizeof(prologue), reinterpret_cast<void*>(&ProcessHook)));
    return true;
}

void WrapDirectInput(void* directInput)
{
    if (!directInput)
        return;
    void** vtable = *static_cast<void***>(directInput);
    if (vtable[4] == reinterpret_cast<void*>(&EnumDevicesHook))
        return;
    g_enumDevices = reinterpret_cast<EnumDevicesFn>(vtable[4]);
    patch::WriteU32(reinterpret_cast<uintptr_t>(&vtable[4]), reinterpret_cast<uint32_t>(&EnumDevicesHook));
}

void Inject(const char* buttons, int ms)
{
    PadState pad = {};
    std::string all = buttons;
    size_t at = 0;
    while (at < all.size()) {
        size_t end = all.find_first_of(" ,+", at);
        std::string word = all.substr(at, end == std::string::npos ? std::string::npos : end - at);
        at = end == std::string::npos ? all.size() : end + 1;
        static const struct {
            const char* name;
            WORD bit;
        } names[] = {{"a", kA}, {"b", kB}, {"x", kX}, {"y", kY}, {"lb", kLeftShoulder}, {"rb", kRightShoulder},
                     {"back", kBack}, {"start", kStart}, {"ls", kLeftStick}, {"rs", kRightStick}, {"up", kUp},
                     {"down", kDown}, {"left", kLeft}, {"right", kRight}};
        for (const auto& n : names)
            if (word == n.name)
                pad.buttons |= n.bit;
        if (word == "lt")
            pad.leftTrigger = 255;
        if (word == "rt")
            pad.rightTrigger = 255;
        size_t eq = word.find('=');
        if (eq != std::string::npos) {
            double v = std::atof(word.c_str() + eq + 1);
            SHORT axis = static_cast<SHORT>(v >= 1 ? 32767 : v <= -1 ? -32768 : v * 32767);
            std::string key = word.substr(0, eq);
            if (key == "lx") pad.lx = axis;
            if (key == "ly") pad.ly = axis;
            if (key == "rx") pad.rx = axis;
            if (key == "ry") pad.ry = axis;
        }
    }
    g_injected = pad;
    g_injectedUntil = GetTickCount() + static_cast<DWORD>(ms > 0 ? ms : 200);
    g_log("gamepad: test input '%s' for %d ms", buttons, ms);
}

} // namespace gamepad
