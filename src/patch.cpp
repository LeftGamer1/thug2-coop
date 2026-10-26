#include "patch.h"

#include <cstring>
#include <windows.h>

namespace patch {

bool Matches(uintptr_t address, const uint8_t* expected, size_t size)
{
    return std::memcmp(reinterpret_cast<const void*>(address), expected, size) == 0;
}

void Write(uintptr_t address, const void* bytes, size_t size)
{
    DWORD old = 0;
    VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(reinterpret_cast<void*>(address), bytes, size);
    VirtualProtect(reinterpret_cast<void*>(address), size, old, &old);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), size);
}

void WriteU32(uintptr_t address, uint32_t value) { Write(address, &value, sizeof(value)); }

void Jump(uintptr_t address, const void* target)
{
    uint8_t code[5] = {0xE9};
    int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - (address + 5));
    std::memcpy(code + 1, &rel, 4);
    Write(address, code, sizeof(code));
}

void* Detour(uintptr_t address, size_t length, const void* hook)
{
    auto* tramp = static_cast<uint8_t*>(VirtualAlloc(nullptr, length + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    std::memcpy(tramp, reinterpret_cast<const void*>(address), length);
    tramp[length] = 0xE9;
    int32_t back = static_cast<int32_t>((address + length) - (reinterpret_cast<uintptr_t>(tramp) + length + 5));
    std::memcpy(tramp + length + 1, &back, 4);
    Jump(address, hook);
    return tramp;
}

uintptr_t RetargetCall(uintptr_t address, const void* target)
{
    auto* p = reinterpret_cast<const uint8_t*>(address);
    if (p[0] != 0xE8)
        return 0;
    int32_t oldRel;
    std::memcpy(&oldRel, p + 1, 4);
    uintptr_t old = address + 5 + oldRel;
    int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - (address + 5));
    Write(address + 1, &rel, 4);
    return old;
}

} // namespace patch
