// Writing into the game's code: byte checks, raw writes, jumps and call-target swaps.
#pragma once
#include <cstddef>
#include <cstdint>

namespace patch {

// True when the bytes at `address` are `expected` (a guard against an unknown exe).
bool Matches(uintptr_t address, const uint8_t* expected, size_t size);
void Write(uintptr_t address, const void* bytes, size_t size);
void WriteU32(uintptr_t address, uint32_t value);
// A 5-byte `jmp target` at address.
void Jump(uintptr_t address, const void* target);
// Hook a function whose first `length` bytes are whole, position-independent instructions: they are copied into
// a trampoline (followed by a jump back), the start becomes `jmp hook`. Returns the trampoline (the original).
void* Detour(uintptr_t address, size_t length, const void* hook);
// Retarget the `call rel32` at address; returns the old target (0 if it isn't an E8 call).
uintptr_t RetargetCall(uintptr_t address, const void* target);

} // namespace patch
