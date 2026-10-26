// Addresses in THUG2 (THEMOD 1.3 exe / PartyMod THUG2PM layout) and thin wrappers to call into the game.
// Names follow thug2-decomp (https://github.com/CanuckAussieKev/thug2-decomp), which documents each one.
#pragma once
#include <cstdint>

namespace game {

// Globals.
constexpr uintptr_t kGameNetManager = 0x007CCE14; // GameNet::Manager*
constexpr uintptr_t kSkateModule = 0x007CE478;    // Mdl::Skate*
constexpr uintptr_t kMaxPlayers = 0x0067EFD0;     // host option "Players" (uint32, 2..8)

// Functions.
constexpr uintptr_t kManagerInNetGame = 0x0044E4F0;      // bool __thiscall GameNet::Manager::InNetGame()
constexpr uintptr_t kSkateInMultiplayerGame = 0x005651F0; // int __thiscall Mdl::Skate::InMultiplayerGame()
constexpr uintptr_t kSpawnScriptByName = 0x00475670;     // CScript* __cdecl Script::SpawnScript(const char*, ...)
constexpr uintptr_t kReadKeyboard = 0x005BDD60;          // void __cdecl Sio::ReadKeyboard()
constexpr uintptr_t kCFuncJoinServer = 0x005A08E0;       // bool __cdecl CFunc_JoinServer(CStruct*, CScript*)
constexpr uintptr_t kStructGetString = 0x00476590;       // bool __thiscall CStruct::GetString(u32, const char**, int)
constexpr uintptr_t kStructGetInteger = 0x00476630;      // bool __thiscall CStruct::GetInteger(u32, int*, int)
constexpr uintptr_t kStructAddString = 0x004779D0;       // void __thiscall CStruct::AddString(u32, const char*)
constexpr uintptr_t kStructAddInteger = 0x00477B80;      // void __thiscall CStruct::AddInteger(u32, int)

constexpr uintptr_t kMemManager = 0x007D1800;            // Mem::Manager*
constexpr uintptr_t kInitExtraSkaterHeaps = 0x005BB820;  // void __thiscall Mem::Manager::InitExtraSkaterHeaps(int)
constexpr uintptr_t kDeleteExtraSkaterHeaps = 0x005BACF0; // void __thiscall Mem::Manager::DeleteExtraSkaterHeaps()
constexpr uintptr_t kSkaterRegions = 0x160;              // Mem::Manager::m_skaterRegions[8]

// Script structures (thug2-decomp src/game/script/struct.cpp, struct_buffer.cpp).
constexpr uintptr_t kNewStruct = 0x00409030;       // CStruct* __cdecl Script::NewStruct() (pool memory)
constexpr uintptr_t kDeleteStruct = 0x00409070;    // void __cdecl Script::DeleteStruct(CStruct*)
constexpr uintptr_t kStructInit = 0x00476410;      // CStruct* __thiscall CStruct::Init()
constexpr uintptr_t kStructClear = 0x00477090;     // void __thiscall CStruct::Clear()
constexpr uintptr_t kWriteToBuffer = 0x00479E20;   // u32 __cdecl Script::WriteToBuffer(CStruct*, u8*, u32, int, cb)
constexpr uintptr_t kReadFromBuffer = 0x0047A0C0;  // const u8* __cdecl Script::ReadFromBuffer(CStruct*, const u8*)

// Story (thug2-decomp src/game/skate/career.h; goal manager addresses from the exe).
constexpr uintptr_t kSkateCareer = 0x20;                 // CSkaterCareer* at the skate module +0x20
constexpr uintptr_t kCareerWriteToStructure = 0x00585550; // void __thiscall CSkaterCareer::WriteToStructure(CStruct*)
constexpr uintptr_t kCareerReadFromStructure = 0x00584D00; // void __thiscall CSkaterCareer::ReadFromStructure(CStruct*)
constexpr uintptr_t kWriteChapterInfo = 0x005801B0; // CSkater::WriteChapterInfo(CStruct*): CHAPTER_STATUS / _INTROS_ / _OUTTROS_VIEWED (no this)
constexpr uintptr_t kReadChapterInfo = 0x00580210;  // CSkater::ReadChapterInfo(CStruct*)
constexpr uintptr_t kGetGoalManager = 0x00564D90;        // CGoalManager* __thiscall Mdl::Skate::GetGoalManager()
constexpr uintptr_t kGoalManagerWinGoal = 0x00558030;    // bool __thiscall CGoalManager::WinGoal(u32 name)
constexpr uintptr_t kPlayCutsceneEntry = 0x0067FE98;     // script function table: PlayCutscene
constexpr uintptr_t kCFuncPlayCutscene = 0x005AE010;
constexpr uintptr_t kLaunchLevelEntry = 0x0067FD90;     // script function table: LaunchLevel
constexpr uintptr_t kCFuncLaunchLevel = 0x005654B0;
constexpr uintptr_t kSetNetworkModeEntry = 0x00680150;  // script function table: SetNetworkMode
constexpr uintptr_t kCFuncSetNetworkMode = 0x005A0A00;
constexpr uintptr_t kManagerSetNetworkMode = 0x00533F20; // void __thiscall GameNet::Manager::SetNetworkMode(int)
constexpr uint32_t kNameChecksum = 0xA1DC81F9;           // "name"

// Patch sites.
constexpr uintptr_t kCleanupDeleteSkaterHeapsCall = 0x005A0017; // level cleanup: DeleteExtraSkaterHeaps
constexpr uintptr_t kCFuncInNetGame = 0x005A0D80;        // mov ecx,[kGameNetManager]; jmp kManagerInNetGame
constexpr uintptr_t kCFuncInMultiplayerGame = 0x005A11B0; // mov ecx,[kSkateModule]; jmp kSkateInMultiplayerGame
constexpr uintptr_t kMainLoopReadKeyboardCall = 0x0044EF8E; // call Sio::ReadKeyboard, once per frame
constexpr uintptr_t kOkToJoinGameModeCall = 0x0052F672; // GameNet::Manager::OkToJoin: "game in progress" test
constexpr uintptr_t kJoinRequestGameModeCall = 0x0052FB4B; // HandleJoinRequest: in progress -> pending observer
constexpr uintptr_t kGameModeName = 0x005532A0;         // uint32 __thiscall GameMode::GetName() (this = [0x007CE470])
constexpr uint32_t kNetLobby = 0x1C471C60;              // checksum of netlobby

struct CStruct;
struct CScript;
using CFunc = bool(__cdecl*)(CStruct*, CScript*);

inline void* Deref(uintptr_t address) { return *reinterpret_cast<void**>(address); }

inline bool ManagerInNetGame()
{
    return reinterpret_cast<bool(__thiscall*)(void*)>(kManagerInNetGame)(Deref(kGameNetManager));
}
inline int SkateInMultiplayerGame()
{
    return reinterpret_cast<int(__thiscall*)(void*)>(kSkateInMultiplayerGame)(Deref(kSkateModule));
}
inline void SpawnScript(const char* name, CStruct* params = nullptr)
{
    using Fn = void*(__cdecl*)(const char*, CStruct*, uint32_t, void*, int, uint32_t, int, int, int, int);
    // no callback, node -1, id 0, not net, not permanent, not session specific, no object pause
    reinterpret_cast<Fn>(kSpawnScriptByName)(name, params, 0, nullptr, -1, 0, 0, 0, 1, 0);
}
inline bool GetStringNamed(CStruct* s, uint32_t name, const char** out)
{
    return reinterpret_cast<bool(__thiscall*)(CStruct*, uint32_t, const char**, int)>(kStructGetString)(s, name, out, 0);
}
inline bool GetChecksumNamed(CStruct* s, uint32_t name, uint32_t* out)
{
    return reinterpret_cast<bool(__thiscall*)(CStruct*, uint32_t, uint32_t*, int)>(0x00476950)(s, name, out, 0);
}
inline void AddChecksumNamed(CStruct* s, uint32_t name, uint32_t value)
{
    reinterpret_cast<void(__thiscall*)(CStruct*, uint32_t, uint32_t)>(0x00477D40)(s, name, value);
}
// Objects: the composite object manager (*0x006F0FB8) by id; the local skater (Mdl::Skate 0x00566520).
inline void* ObjectById(uint32_t id)
{
    return reinterpret_cast<void*(__thiscall*)(void*, uint32_t)>(0x004619F0)(Deref(0x006F0FB8), id);
}
inline void* LocalSkater() { return reinterpret_cast<void*(__thiscall*)(void*)>(0x00566520)(Deref(kSkateModule)); }
inline uint32_t ObjectId(void* object) { return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(object) + 0x14); }
inline uint32_t Checksum(const char* text) { return reinterpret_cast<uint32_t(__cdecl*)(const char*)>(0x00401B90)(text); }
inline bool GetStructureNamed(CStruct* s, uint32_t name, CStruct** out)
{
    return reinterpret_cast<bool(__thiscall*)(CStruct*, uint32_t, CStruct**, int)>(0x004767C0)(s, name, out, 0);
}
inline CScript* SpawnScriptChecksum(uint32_t name, CStruct* params)
{
    using Fn = CScript*(__cdecl*)(uint32_t, CStruct*, uint32_t, void*, int, uint32_t, int, int, int, int);
    return reinterpret_cast<Fn>(0x00475420)(name, params, 0, nullptr, -1, 0, 0, 0, 1, 0);
}
inline uint32_t ScriptUniqueId(CScript* s) { return *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(s) + 0xB0); }
inline CStruct* NewStruct()
{
    CStruct* s = reinterpret_cast<CStruct*(__cdecl*)()>(kNewStruct)();
    return s ? reinterpret_cast<CStruct*(__thiscall*)(CStruct*)>(kStructInit)(s) : nullptr;
}
inline void FreeStruct(CStruct* s)
{
    if (!s)
        return;
    reinterpret_cast<void(__thiscall*)(CStruct*)>(kStructClear)(s);
    reinterpret_cast<void(__cdecl*)(CStruct*)>(kDeleteStruct)(s);
}
inline uint32_t WriteToBuffer(CStruct* s, uint8_t* buffer, uint32_t size)
{
    using Fn = uint32_t(__cdecl*)(CStruct*, uint8_t*, uint32_t, int, void (*)());
    return reinterpret_cast<Fn>(kWriteToBuffer)(s, buffer, size, 0, nullptr);
}
inline void ReadFromBuffer(CStruct* s, const uint8_t* buffer)
{
    reinterpret_cast<const uint8_t*(__cdecl*)(CStruct*, const uint8_t*)>(kReadFromBuffer)(s, buffer);
}
inline void* Career() { return *reinterpret_cast<void**>(static_cast<uint8_t*>(Deref(kSkateModule)) + kSkateCareer); }
inline void* GoalManager()
{
    return reinterpret_cast<void*(__thiscall*)(void*)>(kGetGoalManager)(Deref(kSkateModule));
}
inline bool GetString(CStruct* s, const char** out)
{
    return reinterpret_cast<bool(__thiscall*)(CStruct*, uint32_t, const char**, int)>(kStructGetString)(s, 0, out, 0);
}
inline bool GetInteger(CStruct* s, int* out)
{
    return reinterpret_cast<bool(__thiscall*)(CStruct*, uint32_t, int*, int)>(kStructGetInteger)(s, 0, out, 0);
}
inline void AddString(CStruct* s, const char* value)
{
    reinterpret_cast<void(__thiscall*)(CStruct*, uint32_t, const char*)>(kStructAddString)(s, 0, value);
}
inline void AddInteger(CStruct* s, int value)
{
    reinterpret_cast<void(__thiscall*)(CStruct*, uint32_t, int)>(kStructAddInteger)(s, 0, value);
}

} // namespace game
