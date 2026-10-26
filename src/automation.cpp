#include "automation.h"

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "patch.h"

#ifndef DI_OK
#define DI_OK S_OK
#endif

namespace automation {
namespace {

// DirectInput 8 vtable slots.
constexpr int kCreateDevice = 3;      // IDirectInput8A::CreateDevice(REFGUID, device**, IUnknown*)
constexpr int kGetDeviceState = 9;    // IDirectInputDevice8A::GetDeviceState(DWORD size, void* data)
constexpr int kGetDeviceData = 10;    // IDirectInputDevice8A::GetDeviceData(DWORD size, DIDEVICEOBJECTDATA*, DWORD*, DWORD)
const GUID kSysKeyboard = {0x6F1D2B61, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

using CreateDeviceFn = HRESULT(WINAPI*)(void*, REFGUID, void**, void*);
using GetDeviceStateFn = HRESULT(WINAPI*)(void*, DWORD, void*);
using GetDeviceDataFn = HRESULT(WINAPI*)(void*, DWORD, void*, DWORD*, DWORD);
CreateDeviceFn g_createDevice = nullptr;
GetDeviceStateFn g_getDeviceState = nullptr;
GetDeviceDataFn g_getDeviceData = nullptr;
void* g_keyboard = nullptr;

bool g_down[256] = {};
struct Event {
    uint8_t scan;
    bool down;
};
std::deque<Event> g_events; // for buffered reads

void PatchSlot(void* object, int slot, void* hook, void** original)
{
    void** vtable = *static_cast<void***>(object);
    if (vtable[slot] == hook)
        return;
    *original = vtable[slot];
    patch::WriteU32(reinterpret_cast<uintptr_t>(&vtable[slot]), reinterpret_cast<uint32_t>(hook));
}

HRESULT WINAPI GetDeviceStateHook(void* device, DWORD size, void* data)
{
    HRESULT result = g_getDeviceState(device, size, data);
    if (device != g_keyboard || size != 256)
        return result;
    bool any = false;
    for (bool d : g_down)
        any |= d;
    if (!any)
        return result;
    if (FAILED(result))
        std::memset(data, 0, size); // not focused: still the injected keys
    auto* keys = static_cast<uint8_t*>(data);
    for (int i = 0; i < 256; ++i)
        if (g_down[i])
            keys[i] = 0x80;
    return DI_OK;
}

HRESULT WINAPI GetDeviceDataHook(void* device, DWORD size, void* data, DWORD* count, DWORD flags)
{
    DWORD capacity = count ? *count : 0; // in: the array's size; out: how many were written
    HRESULT result = g_getDeviceData(device, size, data, count, flags);
    if (device != g_keyboard || g_events.empty() || !count || !data || (flags & 1 /* DIGDD_PEEK */))
        return result;
    DWORD have = SUCCEEDED(result) ? *count : 0;
    while (!g_events.empty() && have < capacity) {
        auto* entry = static_cast<uint8_t*>(data) + have * size;
        std::memset(entry, 0, size);
        reinterpret_cast<DWORD*>(entry)[0] = g_events.front().scan;               // dwOfs
        reinterpret_cast<DWORD*>(entry)[1] = g_events.front().down ? 0x80 : 0x00; // dwData
        reinterpret_cast<DWORD*>(entry)[2] = GetTickCount();                     // dwTimeStamp
        g_events.pop_front();
        ++have;
    }
    *count = have;
    return DI_OK;
}

HRESULT WINAPI CreateDeviceHook(void* input, REFGUID guid, void** device, void* outer)
{
    HRESULT result = g_createDevice(input, guid, device, outer);
    if (SUCCEEDED(result) && device && *device && IsEqualGUID(guid, kSysKeyboard)) {
        g_keyboard = *device;
        PatchSlot(*device, kGetDeviceState, reinterpret_cast<void*>(&GetDeviceStateHook),
                  reinterpret_cast<void**>(&g_getDeviceState));
        PatchSlot(*device, kGetDeviceData, reinterpret_cast<void*>(&GetDeviceDataHook),
                  reinterpret_cast<void**>(&g_getDeviceData));
    }
    return result;
}

// Commands.
struct Command {
    std::string verb, arg;
    int value = 0;
};
std::deque<Command> g_queue;
DWORD g_busyUntil = 0;
int g_releaseKey = -1;
DWORD g_releaseAt = 0;
DWORD g_nextPoll = 0;

int ScanCode(const std::string& name)
{
    static const struct {
        const char* name;
        int scan;
    } keys[] = {{"esc", 0x01},   {"enter", 0x1C}, {"space", 0x39}, {"up", 0xC8},   {"down", 0xD0},  {"left", 0xCB},
                {"right", 0xCD}, {"tab", 0x0F},   {"bksp", 0x0E},  {"kp8", 0x48},  {"kp2", 0x50},   {"kp4", 0x4B},
                {"kp6", 0x4D},   {"kp5", 0x4C},   {"w", 0x11},     {"a", 0x1E},    {"s", 0x1F},     {"d", 0x20},
                {"q", 0x10},     {"e", 0x12},     {"r", 0x13},     {"f", 0x21},    {"z", 0x2C},     {"x", 0x2D},
                {"c", 0x2E},     {"v", 0x2F},     {"1", 0x02},     {"2", 0x03},    {"3", 0x04},     {"kp7", 0x47},
                {"kp9", 0x49},   {"kp1", 0x4F},   {"kp3", 0x51},   {"kp0", 0x52},  {"kpdot", 0x53}};
    for (const auto& k : keys)
        if (name == k.name)
            return k.scan;
    return -1;
}

void ReadCommands()
{
    char name[64], path[64];
    std::snprintf(name, sizeof(name), "coop_cmd_%lu.txt", GetCurrentProcessId());
    std::snprintf(path, sizeof(path), "coop_cmd_%lu.work", GetCurrentProcessId());
    if (!MoveFileA(name, path)) // taken first, so lines written meanwhile go to a new file
        return;
    FILE* f = std::fopen(path, "r");
    if (!f)
        return;
    char line[256];
    while (std::fgets(line, sizeof(line), f)) {
        char verb[32] = "", arg[200] = "";
        int value = 0;
        int n = std::sscanf(line, "%31s %199s %d", verb, arg, &value);
        if (n >= 1)
            g_queue.push_back({verb, arg, value});
    }
    std::fclose(f);
    std::remove(path);
}

} // namespace

void WrapDirectInput(void* directInput)
{
    if (directInput)
        PatchSlot(directInput, kCreateDevice, reinterpret_cast<void*>(&CreateDeviceHook),
                  reinterpret_cast<void**>(&g_createDevice));
}

void Hold(int scan, bool down)
{
    if (scan < 0 || scan > 255 || g_down[scan] == down)
        return;
    g_down[scan] = down;
    g_events.push_back({static_cast<uint8_t>(scan), down});
}

void Update(void (*spawn)(const char*), bool (*command)(const char*, const char*, int))
{
    DWORD now = GetTickCount();
    if (g_releaseKey >= 0 && now >= g_releaseAt) {
        g_down[g_releaseKey] = false;
        g_events.push_back({static_cast<uint8_t>(g_releaseKey), false});
        g_releaseKey = -1;
    }
    if (now >= g_nextPoll) {
        g_nextPoll = now + 250;
        ReadCommands();
    }
    while (!g_queue.empty() && now >= g_busyUntil && g_releaseKey < 0) {
        Command c = g_queue.front();
        g_queue.pop_front();
        if (c.verb == "wait") {
            g_busyUntil = now + static_cast<DWORD>(std::atoi(c.arg.c_str()));
        } else if (c.verb == "key") {
            int scan = ScanCode(c.arg);
            if (scan < 0)
                continue;
            int hold = c.value > 0 ? c.value : 120;
            g_down[scan] = true;
            g_events.push_back({static_cast<uint8_t>(scan), true});
            g_releaseKey = scan;
            g_releaseAt = now + static_cast<DWORD>(hold);
            g_busyUntil = g_releaseAt + 150; // a gap before the next command
        } else if (c.verb == "spawn") {
            spawn(c.arg.c_str());
        } else if (command) {
            command(c.verb.c_str(), c.arg.c_str(), c.value);
        }
    }
}

} // namespace automation
