// THUG2 story co-op: dinput8.dll proxy that patches the game at load time.
// F10 (host) and F11 (join; the keys are set in coop.ini's [keys]) start the co-op scripts in coop.qb; the script functions below are the bridge.
#include <algorithm>
#include <cstdarg>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <set>
#include <string>
#include <vector>
#include <windows.h>

#include "automation.h"
#include "game.h"
#include "gamepad.h"
#include "link.h"
#include "patch.h"

namespace {

// --- log ----------------------------------------------------------------------------------------------------

FILE* g_log = nullptr;

void Log(const char* format, ...)
{
    if (!g_log)
        return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::fprintf(g_log, "%02d:%02d:%02d.%03d [%lu] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds,
                 GetCurrentProcessId());
    va_list args;
    va_start(args, format);
    std::vfprintf(g_log, format, args);
    va_end(args);
    std::fputc('\n', g_log);
    std::fflush(g_log);
}

// --- settings (coop.ini next to the exe) --------------------------------------------------------------------

struct Settings {
    char hostAddress[64] = "127.0.0.1";
    char name[16] = "";
    int port = 5153;
    bool logPrintf = false;
    bool windowed = false;
    bool multiInstance = false;
    bool alwaysActive = false;
    bool debugHotkeys = false;
    bool mirrorCutscenes = false;
    bool mirrorGoalCameras = true;
    bool sharedGoals = true;
    bool automation = false;
    int hostKey = VK_F10;     // [keys] host, join, teleport: virtual key codes, optionally with modifier bits
    int joinKey = VK_F11;
    int teleportKey = VK_F12;
    int leaveKey = VK_F9;
    int playerCollision = -1; // skaters knock each other down: 1 on, 0 off, -1 the game's network option
    int screenWidth = 0;      // [display] resolution (desktop: the monitor's); 0: Launcher.exe's choice
    int screenHeight = 0;
    bool gamepad = true;      // [gamepad] enabled: XInput pads (gamepad.cpp)
    int displayMode = 1;      // [display] mode: 0 fullscreen, 1 borderless (a window over the whole screen), 2 windowed
} g_settings;

// The modifier bits of a key setting (above the virtual key code).
constexpr int kWithCtrl = 0x100, kWithShift = 0x200, kWithAlt = 0x400;

// A key from coop.ini: "F10", "Ctrl+F10", "Shift+Q", "Num5", "Insert", "0x7A"... -1 for "none" or empty, the
// default when it isn't understood.
int ParseKey(const char* text, int fallback)
{
    std::string rest;
    for (const char* c = text; *c; ++c)
        if (*c != ' ' && *c != '\t')
            rest += static_cast<char>(std::toupper(static_cast<uint8_t>(*c)));
    if (rest.empty() || rest == "NONE" || rest == "0")
        return -1;
    int modifiers = 0;
    for (;;) {
        size_t plus = rest.find('+');
        if (plus == std::string::npos || plus + 1 == rest.size())
            break;
        std::string mod = rest.substr(0, plus);
        if (mod == "CTRL" || mod == "CONTROL")
            modifiers |= kWithCtrl;
        else if (mod == "SHIFT")
            modifiers |= kWithShift;
        else if (mod == "ALT")
            modifiers |= kWithAlt;
        else
            return fallback;
        rest = rest.substr(plus + 1);
    }
    int vk = -1;
    if (rest.size() == 1 && std::isalnum(static_cast<uint8_t>(rest[0])))
        vk = rest[0]; // letters and digits are their own codes
    else if (rest[0] == 'F' && rest.size() <= 3 && std::isdigit(static_cast<uint8_t>(rest[1])) && std::atoi(&rest[1]) >= 1 &&
             std::atoi(&rest[1]) <= 24)
        vk = VK_F1 + std::atoi(&rest[1]) - 1;
    else if (rest.size() == 4 && rest.compare(0, 3, "NUM") == 0 && std::isdigit(static_cast<uint8_t>(rest[3])))
        vk = VK_NUMPAD0 + (rest[3] - '0');
    else if (rest.compare(0, 2, "0X") == 0)
        vk = static_cast<int>(std::strtol(rest.c_str() + 2, nullptr, 16));
    else {
        static const struct {
            const char* name;
            int vk;
        } named[] = {{"INSERT", VK_INSERT}, {"INS", VK_INSERT},   {"DELETE", VK_DELETE},   {"DEL", VK_DELETE},
                     {"HOME", VK_HOME},     {"END", VK_END},       {"PAGEUP", VK_PRIOR},    {"PGUP", VK_PRIOR},
                     {"PAGEDOWN", VK_NEXT}, {"PGDN", VK_NEXT},     {"PAUSE", VK_PAUSE},     {"SCROLLLOCK", VK_SCROLL},
                     {"TAB", VK_TAB},       {"BACKSPACE", VK_BACK}, {"`", VK_OEM_3},        {"~", VK_OEM_3},
                     {"-", VK_OEM_MINUS},   {"=", VK_OEM_PLUS},    {"[", VK_OEM_4},         {"]", VK_OEM_6},
                     {"NUM*", VK_MULTIPLY}, {"NUM+", VK_ADD},      {"NUM-", VK_SUBTRACT},   {"NUM/", VK_DIVIDE}};
        for (const auto& k : named)
            if (rest == k.name)
                vk = k.vk;
    }
    if (vk <= 0 || vk > 0xFE)
        return fallback;
    return vk | modifiers;
}

int KeySetting(const char* name, int fallback, const char* path)
{
    char text[64];
    if (!GetPrivateProfileStringA("keys", name, "\x01", text, sizeof(text), path) || std::strcmp(text, "\x01") == 0)
        return fallback; // not in coop.ini
    return ParseKey(text, fallback);
}

void LoadSettings()
{
    char path[MAX_PATH];
    GetFullPathNameA("coop.ini", MAX_PATH, path, nullptr);
    GetPrivateProfileStringA("coop", "host_address", "127.0.0.1", g_settings.hostAddress,
                             sizeof(g_settings.hostAddress), path);
    g_settings.port = GetPrivateProfileIntA("coop", "port", 5153, path);
    GetPrivateProfileStringA("coop", "name", "", g_settings.name, sizeof(g_settings.name), path);
    g_settings.logPrintf = GetPrivateProfileIntA("debug", "log_printf", 0, path) != 0;
    g_settings.windowed = GetPrivateProfileIntA("debug", "windowed", 0, path) != 0;
    g_settings.multiInstance = GetPrivateProfileIntA("debug", "multi_instance", 0, path) != 0;
    g_settings.alwaysActive = GetPrivateProfileIntA("debug", "always_active", 0, path) != 0;
    g_settings.debugHotkeys = GetPrivateProfileIntA("debug", "hotkeys", 0, path) != 0;
    g_settings.mirrorCutscenes = GetPrivateProfileIntA("coop", "mirror_cutscenes", 0, path) != 0;
    g_settings.mirrorGoalCameras = GetPrivateProfileIntA("coop", "mirror_goal_cameras", 1, path) != 0;
    g_settings.sharedGoals = GetPrivateProfileIntA("coop", "shared_goals", 1, path) != 0;
    g_settings.automation = GetPrivateProfileIntA("debug", "automation", 0, path) != 0;
    g_settings.playerCollision = GetPrivateProfileIntA("coop", "player_collision", -1, path);
    g_settings.gamepad = GetPrivateProfileIntA("gamepad", "enabled", 1, path) != 0;
    g_settings.hostKey = KeySetting("host", VK_F10, path);
    g_settings.joinKey = KeySetting("join", VK_F11, path);
    g_settings.teleportKey = KeySetting("teleport", VK_F12, path);
    g_settings.leaveKey = KeySetting("leave", VK_F9, path);
    char text[32];
    GetPrivateProfileStringA("display", "mode", g_settings.windowed ? "windowed" : "borderless", text, sizeof(text), path);
    g_settings.displayMode = _stricmp(text, "fullscreen") == 0 ? 0 : _stricmp(text, "windowed") == 0 ? 2 : 1;
    g_settings.windowed = g_settings.displayMode == 2; // ([debug] windowed=1 is the old name of mode=windowed)
    GetPrivateProfileStringA("display", "resolution", "desktop", text, sizeof(text), path);
    int width = 0, height = 0;
    char x = 0;
    if (_stricmp(text, "desktop") == 0) {
        width = GetSystemMetrics(SM_CXSCREEN); // (real pixels: the process is DPI aware, DllMain)
        height = GetSystemMetrics(SM_CYSCREEN);
    } else if (std::sscanf(text, " %d %c %d", &width, &x, &height) != 3 || (x != 'x' && x != 'X' && x != '*')) {
        width = height = 0; // "launcher", empty or not understood
    }
    if (width >= 320 && width <= 7680 && height >= 240 && height <= 4320) {
        g_settings.screenWidth = width;
        g_settings.screenHeight = height;
    } else {
        g_settings.screenWidth = g_settings.screenHeight = 0;
    }
}

// --- co-op state ----------------------------------------------------------------------------------------------

enum class Role { None, Host, Client };
Role g_role = Role::None;
bool g_remote = false;     // applying the peer's event: don't send it back
bool g_coopSession = false;  // co-op ran in this level (also after the host is gone, until the main menu)
void* g_deferredSkater = nullptr; // the friend's skater, its removal put off to the level's cleanup
bool g_inCleanup = false;
bool g_leavingServer = false;
uint32_t g_friendSwap[2] = {0, 0}; // the friend's skater's model swap here: skater id + 1, other object id
bool g_haveCareer = false; // client: the host's career arrived
bool g_joinNow = false;    // client: the host said its game is up
bool g_clientJoined = false; // host: the client has joined the game
bool g_selfJoined = false;   // client: JoinServer sent (story events apply from then on)
bool g_hostInLevel = false;  // host: its last LaunchLevel was a level, not the main menu
bool g_inMainMenu = true;    // the last LaunchLevel was Load_MainMenu (the widescreen code draws it 4:3)
bool g_hadFriendInLevel = false; // host: a friend's skater was in this level already (a rejoin reloads it)

// --- script functions -------------------------------------------------------------------------------------------

// InNetGame / InMultiplayerGame as the scripts see them: "no" while co-op runs, so a level loads its story
// content (peds, cutscenes, story goals) even though a network session is up. The engine's own checks are untouched.
bool __cdecl ScriptInNetGame(game::CStruct*, game::CScript*)
{
    return g_role == Role::None && game::ManagerInNetGame();
}

bool __cdecl ScriptInMultiplayerGame(game::CStruct*, game::CScript*)
{
    return g_role == Role::None && game::SkateInMultiplayerGame() != 0;
}

// OkToJoin's "game in progress" test asks for the game mode's name: the co-op host answers "net lobby", so the
// friend joins as a player in the middle of the story instead of being offered to observe.
uint32_t __fastcall GameModeNameForJoin(void* mode, void*)
{
    if (g_role == Role::Host)
        return game::kNetLobby;
    return reinterpret_cast<uint32_t(__thiscall*)(void*)>(game::kGameModeName)(mode);
}

// The engine's own "network game?" test (GameNet::Manager::InNetGame) at call sites that decide a level's content.
// In co-op they see a single-player game, so the story's pedestrians and level objects are loaded and created.
uint32_t __fastcall ContentInNetGame(void* manager, void*)
{
    if (g_role != Role::None)
        return 0;
    return reinterpret_cast<bool(__thiscall*)(void*)>(game::kManagerInNetGame)(manager);
}
const uintptr_t kContentInNetGameCalls[] = {
    0x005AB321, // PreloadPedestrians: no pedestrians in a network game
    0x00565BC8, // Skate::SkipNode: which level nodes this game creates (also NodeCommand's create / kill / hide)
    0x0056610C, // the skater's restart node: a network game's Multiplayer ones (in the training level, behind a wall)
    // The goal manager and the career (0x00550040-0x0056C920): in a network game won goals weren't kept in the
    // career (a level reload lost them), gaps weren't awarded, the run's end and rewards took the network path.
    0x00551AE0, 0x00553AEF, 0x00553BB4, 0x00555599, 0x005555EA, 0x00555939, 0x00555E31, 0x0055750A, 0x00557723,
    0x005577F4, 0x005579AA, 0x00557A55, 0x005580CB, 0x005583FB, 0x0055C0B1, 0x0055DFCE, 0x0055DFF9, 0x00560D5C,
    0x005804E5, // CSkater::GetStat: the player's own stats (a network game uses fixed ones)
};

// Skate::GetRestartNode-for-skater (0x00566100, thiscall, the skater's number): where a skater starts or restarts
// without a node of its own. Player1 restart number n for skater n: in co-op both start at the level's first.
constexpr uintptr_t kSkaterRestartNode = 0x00566100;
const uintptr_t kSkaterRestartNodeCalls[] = {0x00566381, 0x00566A25};
uint32_t __fastcall SkaterRestartNode(void* skate, void*, int number)
{
    if (g_role != Role::None)
        number = 0;
    return reinterpret_cast<uint32_t(__thiscall*)(void*, int)>(kSkaterRestartNode)(skate, number);
}

// CoopLog "<text>": a line in coop.log.
bool __cdecl CoopLog(game::CStruct* params, game::CScript*)
{
    const char* text = nullptr;
    if (params && game::GetString(params, &text) && text)
        Log("script: %s", text);
    return true;
}

// Printf "<text>" (a stub in the retail exe): into coop.log when [debug] log_printf=1.
bool __cdecl ScriptPrintf(game::CStruct* params, game::CScript*)
{
    const char* text = nullptr;
    if (g_settings.logPrintf && params && game::GetString(params, &text) && text)
        Log("printf: %s", text);
    return true;
}

// CoopSetRole <0 none | 1 host | 2 client>.
bool __cdecl CoopSetRole(game::CStruct* params, game::CScript*)
{
    int role = 0;
    if (params)
        game::GetInteger(params, &role);
    g_role = role == 1 ? Role::Host : role == 2 ? Role::Client : Role::None;
    if (g_role != Role::None)
        g_coopSession = true;
    if (g_role != Role::None)
        *reinterpret_cast<uint32_t*>(game::kMaxPlayers) = 2; // co-op is for two (also InitSkaterHeaps' count)
    if (g_role == Role::Host && !link::Listen(g_settings.port + 1))
        Log("link: can't listen on %d", g_settings.port + 1);
    if (g_role == Role::None)
        link::Close();
    Log("role -> %d", role);
    return true;
}

// A second skater needs its own heaps: coop.qb makes them with InitSkaterHeaps (as the two-player start does),
// and every level change's cleanup would delete them again - not in co-op.
void __fastcall CleanupDeleteExtraSkaterHeaps(void* manager, void*)
{
    if (g_role != Role::None)
        return;
    reinterpret_cast<void(__thiscall*)(void*)>(game::kDeleteExtraSkaterHeaps)(manager);
}

bool __cdecl CoopIsActive(game::CStruct*, game::CScript*) { return g_role != Role::None; }
// CoopWasActive: co-op ran in this level (the host may be gone already).
bool __cdecl CoopWasActive(game::CStruct*, game::CScript*) { return g_coopSession; }
bool __cdecl CoopIsHost(game::CStruct*, game::CScript*) { return g_role == Role::Host; }

// CoopJoin: JoinServer "<host_address>" <port> from coop.ini.
bool __cdecl CoopJoin(game::CStruct* params, game::CScript* script)
{
    Log("joining %s:%d", g_settings.hostAddress, g_settings.port);
    game::AddString(params, g_settings.hostAddress);
    game::AddInteger(params, g_settings.port);
    bool result = reinterpret_cast<game::CFunc>(game::kCFuncJoinServer)(params, script);
    g_selfJoined = true;
    link::Send(link::kJoined, nullptr, 0);
    return result;
}

// --- story sync ----------------------------------------------------------------------------------------------------

// Both games run the same story from the same state: the client takes the host's career when it joins, and every
// goal either player wins is won on the other side too (so its rewards, unlocks and world changes happen in both).
// The host's cutscenes are played on the client.

uint8_t g_buffer[0x20000];

// The goal manager's own saved state (won goals, their flags: what the story's save keeps as GoalManagerParams,
// thug2-decomp src/game/mc/save_contents.cpp) goes with the career. Its structure: the goal manager's +0x14
// (0x004A2BA0), the goal manager from 0x005591B0.
constexpr uint32_t kCoopGoalManager = 0x833DDE20; // coop_goal_manager
game::CStruct* GoalManagerParams()
{
    void* goalManager = reinterpret_cast<void*(__cdecl*)()>(0x005591B0)();
    return goalManager ? reinterpret_cast<game::CStruct*(__thiscall*)(void*)>(0x004A2BA0)(goalManager) : nullptr;
}
void AddGoalManagerParams(game::CStruct* s)
{
    if (game::CStruct* params = GoalManagerParams())
        reinterpret_cast<void(__thiscall*)(game::CStruct*, uint32_t, game::CStruct*)>(0x00478670)(s, kCoopGoalManager, params);
}
void ReadGoalManagerParams(game::CStruct* s)
{
    game::CStruct* saved = nullptr;
    game::CStruct* own = GoalManagerParams();
    if (!own || !game::GetStructureNamed(s, kCoopGoalManager, &saved) || !saved)
        return;
    reinterpret_cast<void(__thiscall*)(game::CStruct*)>(game::kStructClear)(own);
    reinterpret_cast<game::CStruct*(__thiscall*)(game::CStruct*, game::CStruct*)>(0x00478480)(own, saved);
}

void SendCareer()
{
    game::CStruct* s = game::NewStruct();
    reinterpret_cast<void(__thiscall*)(void*, game::CStruct*)>(game::kCareerWriteToStructure)(game::Career(), s);
    reinterpret_cast<void(__thiscall*)(void*, game::CStruct*)>(game::kWriteChapterInfo)(nullptr, s);
    AddGoalManagerParams(s);
    uint32_t size = game::WriteToBuffer(s, g_buffer, sizeof(g_buffer));
    game::FreeStruct(s);
    Log("career -> client (%u bytes)", size);
    if (size)
        link::Send(link::kCareer, g_buffer, size);
}

// The client's own story, kept from before the first host career and put back when co-op ends.
std::vector<uint8_t> g_ownCareer;

uint32_t WriteCareer(uint8_t* buffer, uint32_t size)
{
    game::CStruct* s = game::NewStruct();
    reinterpret_cast<void(__thiscall*)(void*, game::CStruct*)>(game::kCareerWriteToStructure)(game::Career(), s);
    reinterpret_cast<void(__thiscall*)(void*, game::CStruct*)>(game::kWriteChapterInfo)(nullptr, s);
    AddGoalManagerParams(s);
    uint32_t written = game::WriteToBuffer(s, buffer, size);
    game::FreeStruct(s);
    return written;
}

void ReadCareer(const uint8_t* data)
{
    game::CStruct* s = game::NewStruct();
    game::ReadFromBuffer(s, data);
    reinterpret_cast<void(__thiscall*)(void*, game::CStruct*)>(game::kCareerReadFromStructure)(game::Career(), s);
    reinterpret_cast<void(__thiscall*)(void*, game::CStruct*)>(game::kReadChapterInfo)(nullptr, s);
    ReadGoalManagerParams(s);
    game::FreeStruct(s);
}

void RestoreOwnCareer()
{
    if (g_ownCareer.empty())
        return;
    ReadCareer(g_ownCareer.data());
    g_ownCareer.clear();
    Log("client: own story put back");
}

void ApplyCareer(const uint8_t* data)
{
    if (g_ownCareer.empty()) {
        static uint8_t own[0x20000];
        uint32_t size = WriteCareer(own, sizeof(own));
        g_ownCareer.assign(own, own + size);
    }
    game::CStruct* s = game::NewStruct();
    game::ReadFromBuffer(s, data);
    reinterpret_cast<void(__thiscall*)(void*, game::CStruct*)>(game::kCareerReadFromStructure)(game::Career(), s);
    reinterpret_cast<void(__thiscall*)(void*, game::CStruct*)>(game::kReadChapterInfo)(nullptr, s);
    ReadGoalManagerParams(s);
    game::FreeStruct(s);
    g_haveCareer = true;
    Log("career <- host applied");
}

// Scripts spawned for the peer (kSpawn): what they do came from the other game, so hooks don't send it back.
struct RemoteScript {
    game::CScript* script;
    uint32_t id;
} g_remoteScripts[16];
int g_remoteNext = 0;

void RememberRemote(game::CScript* script)
{
    if (!script)
        return;
    g_remoteScripts[g_remoteNext] = {script, game::ScriptUniqueId(script)};
    g_remoteNext = (g_remoteNext + 1) % 16;
}

bool IsRemote(game::CScript* script)
{
    if (g_remote)
        return true;
    if (!script)
        return false;
    for (const RemoteScript& r : g_remoteScripts)
        if (r.script == script && r.id == game::ScriptUniqueId(script))
            return true;
    return false;
}

// Asks the peer to spawn the script `name` (checksum) with these params.
void SendSpawn(uint32_t name, game::CStruct* params)
{
    std::memcpy(g_buffer, &name, 4);
    uint32_t size = params ? game::WriteToBuffer(params, g_buffer + 4, sizeof(g_buffer) - 4) : 0;
    if (!size)
        g_buffer[4] = 0, size = 1; // an empty structure
    link::Send(link::kSpawn, g_buffer, 4 + size);
}

// CGoalManager::WinGoal, through a trampoline (its first 8 bytes run there, then back into the original).
using WinGoalFn = bool(__thiscall*)(void*, uint32_t);
WinGoalFn g_winGoal = nullptr;

// Shared goals ([coop] shared_goals=1): a goal one player takes from an NPC starts for the other too
// (goal_accept_trigger in coop.qb), and it's won only once both have done it. The first one's win waits: the goal
// ends there with a message, the other is told; the second one's win wins it for both.
constexpr uint32_t kGoalIdParam = 0x9982E501; // goal_id
std::set<uint32_t> g_sharedGoals; // started for both
std::set<uint32_t> g_localDone;   // done here, waiting for the other player
std::set<uint32_t> g_peerDone;    // done by the other player

bool InCoopSession() { return g_role != Role::None && link::Connected() && (g_clientJoined || g_selfJoined); }

void ForgetSharedGoal(uint32_t name)
{
    g_sharedGoals.erase(name);
    g_localDone.erase(name);
    g_peerDone.erase(name);
}

void ForgetSharedGoals()
{
    g_sharedGoals.clear();
    g_localDone.clear();
    g_peerDone.clear();
}

void SpawnGoalScript(const char* script, uint32_t goal)
{
    game::CStruct* params = game::NewStruct();
    game::AddChecksumNamed(params, kGoalIdParam, goal);
    game::SpawnScriptChecksum(game::Checksum(script), params);
    game::FreeStruct(params);
}

// A goal's next stage (its `child`, as in the training's chains) starts by itself when the goal is won, not from
// its NPC: coop_goal_won_chain makes it shared here once it runs. Each player's game does this on its own win.
void ShareNextStage(uint32_t goal)
{
    if (g_settings.sharedGoals && InCoopSession())
        SpawnGoalScript("coop_goal_won_chain", goal);
}

bool __fastcall WinGoalHook(void* manager, void*, uint32_t name)
{
    if (!g_remote && g_settings.sharedGoals && InCoopSession() && g_sharedGoals.count(name)) {
        if (g_localDone.count(name))
            return true; // already waiting (the goal ends next frame)
        if (!g_peerDone.count(name)) {
            g_localDone.insert(name);
            Log("shared goal %08X done here -> peer, waiting", name);
            link::Send(link::kGoalDone, &name, sizeof(name));
            SpawnGoalScript("coop_goal_done_wait", name);
            return true;
        }
        Log("shared goal %08X done by both", name);
        ForgetSharedGoal(name);
    }
    bool won = g_winGoal(manager, name);
    if (g_role != Role::None && !g_remote && link::Connected() && (g_clientJoined || g_selfJoined)) {
        Log("goal %08X won -> peer", name);
        link::Send(link::kGoalWon, &name, sizeof(name));
        ShareNextStage(name);
    }
    if (InCoopSession())
        game::SpawnScript("coop_after_goal_won");
    return won;
}

bool InstallWinGoalHook()
{
    const uint8_t prologue[] = {0x83, 0xEC, 0x28, 0xA1, 0x14, 0xCE, 0x7C, 0x00}; // sub esp,28h; mov eax,[7CCE14h]
    if (!patch::Matches(game::kGoalManagerWinGoal, prologue, sizeof(prologue)))
        return false;
    g_winGoal = reinterpret_cast<WinGoalFn>(
        patch::Detour(game::kGoalManagerWinGoal, sizeof(prologue), reinterpret_cast<void*>(&WinGoalHook)));
    return true;
}

// PreMgr::LoadPre 0x005B8EE0, logged (a missing archive ends in the disc error screen, which crashes windowed).
using LoadPreFn = void(__thiscall*)(void*, const char*, int, int, int);
LoadPreFn g_loadPre = nullptr;

void __fastcall LoadPreHook(void* manager, void*, const char* name, int async, int reportMissing, int bottomUp)
{
    Log("pre: %s", name ? name : "(null)");
    g_loadPre(manager, name, async, reportMissing, bottomUp);
}

bool InstallLoadPreLog()
{
    const uint8_t prologue[] = {0x81, 0xEC, 0x1C, 0x01, 0x00, 0x00, 0xA1, 0x38, 0xEC, 0x68, 0x00};
    if (!patch::Matches(0x005B8EE0, prologue, sizeof(prologue)))
        return false;
    g_loadPre = reinterpret_cast<LoadPreFn>(patch::Detour(0x005B8EE0, sizeof(prologue), reinterpret_cast<void*>(&LoadPreHook)));
    return true;
}

// PlayCutscene: the host's are sent to the client. A cutscene both games start on their own (a goal both won)
// isn't played twice: the client skips a remote one it started itself in the last seconds, and the other way round.
struct RecentCutscene {
    char name[128] = "";
    DWORD time = 0;
} g_localCutscene, g_remoteCutscene;

bool Recent(const RecentCutscene& c, const char* name)
{
    return c.name[0] && std::strcmp(c.name, name) == 0 && GetTickCount() - c.time < 8000;
}
void Remember(RecentCutscene& c, const char* name)
{
    std::snprintf(c.name, sizeof(c.name), "%s", name);
    c.time = GetTickCount();
}

bool __cdecl PlayCutsceneHook(game::CStruct* params, game::CScript* script)
{
    const char* name = "";
    if (params)
        game::GetStringNamed(params, game::kNameChecksum, &name);
    if (g_role == Role::Client) {
        if (IsRemote(script)) {
            if (Recent(g_localCutscene, name)) {
                Log("cutscene %s from host: already playing", name);
                return true;
            }
            Remember(g_remoteCutscene, name);
        } else {
            if (Recent(g_remoteCutscene, name)) {
                Log("cutscene %s: the host's already played", name);
                return true;
            }
            Remember(g_localCutscene, name);
        }
    }
    // Both games run the same story, so they start the same cutscenes themselves; sending the host's ones too
    // played two at once (and one during the client's level load). Off unless [coop] mirror_cutscenes=1.
    if (g_settings.mirrorCutscenes && g_role == Role::Host && link::Connected() && g_clientJoined) {
        Log("cutscene %s -> client", name);
        SendSpawn(game::Checksum("coop_play_cutscene"), params);
    }
    return reinterpret_cast<game::CFunc>(game::kCFuncPlayCutscene)(params, script);
}

// After a level change the other player's skater kept playing back its movement against the old level's times
// (CSkaterNonLocalNetLogicComponent, type 0x850EEF66: no state reached, a zero time span -> NaN positions). Its
// playback starts over: the sync frame (+0x18), the update count (+0x298) and its state history's count (+0x2A4 ->
// +0x18) cleared, so it waits for fresh states and syncs to them.
DWORD g_resetPlaybackAt[2] = {0, 0};

void Component2(void* object, uint32_t type, void** out);
void ResetRemotePlayback()
{
    void* local = game::LocalSkater();
    for (uint32_t id = 0; id < 2; ++id) {
        void* o = game::ObjectById(id);
        if (!o || o == local || o == g_deferredSkater)
            continue;
        void* c = nullptr;
        Component2(o, 0x850EEF66, &c);
        if (!c)
            continue;
        auto* comp = static_cast<uint8_t*>(c);
        *reinterpret_cast<uint32_t*>(comp + 0x18) = 0;
        *reinterpret_cast<int*>(comp + 0x298) = 0;
        if (auto* history = *reinterpret_cast<uint8_t**>(comp + 0x2A4))
            *reinterpret_cast<int*>(history + 0x18) = 0;
        Log("playback of skater %u restarted", id);
    }
}

// LaunchLevel: before the host loads a level (a chapter moved on, a level picked), the client gets the career again,
// so its game starts the new level from the same story state.
void UndoFriendSwap();

// Cleanup (table 0x0067F5C8, cfunc 0x0059FE50): the level's objects go - the friend's skater gives back a ped's model
// first (it keeps living across levels; a dead ped's model in it crashed the game later).
void RemoveDeferredSkater();
bool __cdecl CleanupHook(game::CStruct* params, game::CScript* script)
{
    UndoFriendSwap();
    g_inCleanup = true;
    RemoveDeferredSkater();
    bool result = reinterpret_cast<game::CFunc>(0x0059FE50)(params, script);
    g_inCleanup = false;
    return result;
}

bool __cdecl LaunchLevelHook(game::CStruct* params, game::CScript* script)
{
    g_friendSwap[0] = 0; // (undone at the level's Cleanup already)
    if (g_role != Role::None) {
        g_resetPlaybackAt[0] = GetTickCount() + 4000;
        g_resetPlaybackAt[1] = GetTickCount() + 12000;
    }
    uint32_t level = 0;
    if (params)
        game::GetChecksumNamed(params, 0x651533EC, &level); // level=<load script>
    g_hostInLevel = level != 0 && level != 0xE92ECAFE;     // Load_MainMenu
    g_inMainMenu = !g_hostInLevel;
    if (g_role == Role::Client && g_selfJoined && !g_hostInLevel) {
        // the friend went back to the main menu (quit, or the host is gone): co-op over for them (F11 joins again)
        Log("client: back to the main menu, co-op ended");
        g_role = Role::None;
        RestoreOwnCareer();
        g_selfJoined = false;
        link::Close();
        // Pause > Quit: in co-op the pause menu really pauses the game (the scripts see no network game), and the
        // main menu's load waited for the pause to end, forever.
        game::SpawnScript("coop_client_quit_unpause");
    }
    if (g_role == Role::None && !g_hostInLevel) {
        RestoreOwnCareer(); // back at the main menu after co-op as the friend
        g_coopSession = false;
    }
    Log("launch level %08X", level);
    if (g_role == Role::Host && !g_hostInLevel && g_clientJoined && link::Connected()) {
        Log("host: back to the main menu - the friend leaves too");
        link::Send(link::kHostLeft, nullptr, 0);
        g_clientJoined = false;
    }
    if (g_role == Role::Host) // a fresh level: the friend (if any) loads it now too
        g_hadFriendInLevel = link::Connected() && g_clientJoined;
    if (g_role == Role::Host && link::Connected()) {
        SendCareer();
        if (!g_clientJoined && g_hostInLevel && game::ManagerInNetGame()) {
            Log("lobby: the host's game starts -> client joins");
            link::Send(link::kJoinNow, nullptr, 0);
        }
    }
    return reinterpret_cast<game::CFunc>(game::kCFuncLaunchLevel)(params, script);
}

// SetNetworkMode <0|1|2>: in co-op "no network" (as chosen_leave_server and the story's start set it) stays LAN, so
// the story's own StartServer makes a server the friend can join.
bool __cdecl SetNetworkModeHook(game::CStruct* params, game::CScript* script)
{
    int mode = 0;
    if (params)
        game::GetInteger(params, &mode);
    if (g_role != Role::None && mode == 0) {
        reinterpret_cast<void(__thiscall*)(void*, int)>(game::kManagerSetNetworkMode)(game::Deref(game::kGameNetManager), 1);
        return true;
    }
    return reinterpret_cast<game::CFunc>(game::kCFuncSetNetworkMode)(params, script);
}

void EndRemoteMovie(uint32_t name);
void SwapRemote(uint32_t skaterId, uint32_t otherId);

void* MovieManager();
uint32_t PlayingMovie(void* manager);

// PlaySkaterCamAnim (table 0x0067FE28, cfunc 0x005AD830): a camera (a goal taken or won, the chapter's goal tour)
// plays for the friend too, without the skaterId (theirs is the local skater there). One both games start at once
// (a goal won on both) isn't doubled: one already playing under that name is left alone. The client doesn't run
// the chapter's goal tour itself (PlayIntroCameras in coop.qb): it shows the host's.
constexpr uintptr_t kPlaySkaterCamAnimEntry = 0x0067FE28, kCFuncPlaySkaterCamAnim = 0x005AD830;
constexpr uint32_t kSkaterIdParam = 0x5B24FAAA, kStopFlag = 0x46A9E949;

// Whether the structure has a component with this name (any type): CStruct::GetPair-free check through
// the component search the struct getters share (0x00476DF0 returns the component or null).
bool HasParam(game::CStruct* params, uint32_t name)
{
    uint32_t value = 0;
    return game::GetChecksumNamed(params, name, &value);
}

struct PendingCam {
    uint32_t name = 0;
    uint32_t size = 0;
    uint8_t data[2048];
} g_pendingCams[16];

void QueueCamForPeer(uint32_t name, game::CStruct* params)
{
    for (PendingCam& c : g_pendingCams) {
        if (c.name == 0 || c.name == name) {
            c.name = name;
            c.size = game::WriteToBuffer(params, c.data, sizeof(c.data));
            if (!c.size)
                c.name = 0;
            return;
        }
    }
}

// Once a frame: a queued camera that has started playing here goes to the friend now.
void SendStartedCams()
{
    void* movies = MovieManager();
    uint32_t playing = movies ? PlayingMovie(movies) : 0;
    static uint32_t lastPlaying = 0;
    if (playing == lastPlaying)
        return;
    lastPlaying = playing;
    for (PendingCam& c : g_pendingCams) {
        if (c.name && c.name == playing) {
            uint32_t script = 0x7A8425F1; // coop_play_cam_anim
            std::memcpy(g_buffer, &script, 4);
            std::memcpy(g_buffer + 4, c.data, c.size);
            link::Send(link::kSpawn, g_buffer, 4 + c.size);
            Log("camera %08X -> peer", c.name);
            c.name = 0;
        }
    }
    if (!playing) // nothing plays: whatever is left was dropped
        for (PendingCam& c : g_pendingCams)
            c.name = 0;
}

bool __cdecl PlaySkaterCamAnimHook(game::CStruct* params, game::CScript* script)
{
    uint32_t name = 0;
    if (params)
        game::GetChecksumNamed(params, game::kNameChecksum, &name);
    if (IsRemote(script)) {
        void* movies = MovieManager();
        if (name && movies && PlayingMovie(movies) == name)
            return true; // already showing it
    } else if (g_settings.mirrorGoalCameras && name && g_role != Role::None && link::Connected() &&
               params && (g_clientJoined || g_selfJoined)) {
        // Queued here; sent when it starts playing (a goal tour queues all its cameras at once).
        game::CStruct* copy = game::NewStruct();
        reinterpret_cast<game::CStruct*(__thiscall*)(game::CStruct*, game::CStruct*)>(0x00478480)(copy, params);
        reinterpret_cast<void(__thiscall*)(game::CStruct*, uint32_t)>(0x004776B0)(copy, kSkaterIdParam);
        QueueCamForPeer(name, copy);
        game::FreeStruct(copy);
    }
    return reinterpret_cast<game::CFunc>(kCFuncPlaySkaterCamAnim)(params, script);
}

void OnLinkMessage(uint32_t type, const uint8_t* data, uint32_t size)
{
    switch (type) {
    case link::kHello:
        Log("client said hello");
        g_clientJoined = false;
        if (g_role == Role::Host) {
            SendCareer();
            // Already in a network game (F10 in a level): join now. Else (the lobby) at the next LaunchLevel.
            if (g_hostInLevel && game::ManagerInNetGame()) {
                if (g_hadFriendInLevel) {
                    // A friend left this level before: its skater's leftovers crash a new one. The level is
                    // loaded again and the friend joins as it loads (LaunchLevel), as from the lobby.
                    Log("host: friend back - reloading the level");
                    game::SpawnScriptChecksum(0xD60137D5, nullptr); // coop_host_reload
                } else {
                    link::Send(link::kJoinNow, nullptr, 0);
                }
            }
        }
        break;
    case link::kJoinNow:
        Log("host: join now");
        g_joinNow = true;
        break;
    case link::kMovieEnded:
        if (size == 4) {
            uint32_t name;
            std::memcpy(&name, data, 4);
            EndRemoteMovie(name);
        }
        break;
    case link::kSwapModels:
        if (size == 8) {
            uint32_t ids[2];
            std::memcpy(ids, data, 8);
            SwapRemote(ids[0], ids[1]);
        }
        break;
    case link::kHostLeft:
        if (g_role == Role::Client && g_selfJoined) {
            Log("client: the host left the story, co-op ended");
            g_role = Role::None;
            g_selfJoined = false;
            link::Close(); // (the host waits for this before it cleans its level up)
            game::SpawnScript("coop_client_host_gone");
        }
        break;
    case link::kJoined:
        Log("client joined");
        g_clientJoined = true;
        g_hadFriendInLevel = true;
        break;
    case link::kCareer:
        if (g_role == Role::Client)
            ApplyCareer(data);
        break;
    case link::kGoalWon:
        if (size == 4) {
            uint32_t name;
            std::memcpy(&name, data, 4);
            Log("goal %08X won by peer", name);
            ForgetSharedGoal(name);
            g_remote = true;
            g_winGoal(game::GoalManager(), name);
            g_remote = false;
            ShareNextStage(name);
            game::SpawnScript("coop_after_goal_won");
        }
        break;
    case link::kGoalDone:
        if (size == 4) {
            uint32_t name;
            std::memcpy(&name, data, 4);
            if (g_localDone.count(name)) {
                // both done (each waited for the other): won for both here, the peer wins it from kGoalWon
                Log("shared goal %08X done by peer too: won", name);
                ForgetSharedGoal(name);
                g_winGoal(game::GoalManager(), name);
                link::Send(link::kGoalWon, &name, sizeof(name));
                ShareNextStage(name);
                game::SpawnScript("coop_after_goal_won");
            } else {
                Log("shared goal %08X done by peer", name);
                g_peerDone.insert(name);
                SpawnGoalScript("coop_peer_goal_done", name);
            }
        }
        break;
    case link::kSpawn: {
        if (g_role == Role::Client && !g_selfJoined)
            break; // not in the host's game yet
        if (size < 5)
            break;
        uint32_t name;
        std::memcpy(&name, data, 4);
        game::CStruct* params = game::NewStruct();
        game::ReadFromBuffer(params, data + 4);
        Log("spawn %08X from peer", name);
        g_remote = true;
        RememberRemote(game::SpawnScriptChecksum(name, params));
        g_remote = false;
        game::FreeStruct(params);
        break;
    }
    default:
        Log("link: unknown message %u", type);
    }
}

// CoopConnect: the client's link to the host (host_address, port + 1); true when connected.
bool __cdecl CoopConnect(game::CStruct*, game::CScript*)
{
    g_haveCareer = false;
    g_joinNow = false;
    g_selfJoined = false;
    if (!link::Connect(g_settings.hostAddress, g_settings.port + 1, 4000)) {
        Log("link: can't reach %s:%d", g_settings.hostAddress, g_settings.port + 1);
        return false;
    }
    Log("link: connected");
    link::Send(link::kHello, nullptr, 0);
    return true;
}

bool __cdecl CoopHasCareer(game::CStruct*, game::CScript*) { return g_haveCareer; }
void UndoFriendSwap();
// CoopBeforeLevelChange: change_level (coop.qb) calls it first - the level's objects are still there.
bool __cdecl CoopBeforeLevelChange(game::CStruct*, game::CScript*)
{
    UndoFriendSwap();
    ForgetSharedGoals();
    return true;
}
// CoopSharedGoal goal_id=<goal> mode=start | joined | waiting: false when shared goals are off or no co-op session.
// start: this player took the goal from its NPC - it starts for the other player too (coop_remote_start_goal there).
// joined: both run it now (the other player's game started it too).
// waiting: whether this player has done it and waits for the other (taking it again wouldn't count).
bool __cdecl CoopSharedGoal(game::CStruct* params, game::CScript*)
{
    uint32_t goal = 0, mode = 0;
    if (!g_settings.sharedGoals || !InCoopSession() || !params || !game::GetChecksumNamed(params, kGoalIdParam, &goal))
        return false;
    game::GetChecksumNamed(params, game::Checksum("mode"), &mode);
    if (mode == game::Checksum("start")) {
        if (g_sharedGoals.count(goal))
            return true; // taken again while shared: the other player's run goes on
        game::CStruct* send = game::NewStruct();
        game::AddChecksumNamed(send, kGoalIdParam, goal);
        SendSpawn(game::Checksum("coop_remote_start_goal"), send);
        game::FreeStruct(send);
        Log("shared goal %08X started here -> peer", goal);
    } else if (mode == game::Checksum("joined")) {
        ForgetSharedGoal(goal);
        g_sharedGoals.insert(goal);
        Log("shared goal %08X: both run it", goal);
    } else if (mode == game::Checksum("waiting")) {
        return g_localDone.count(goal) != 0; // done here, the other player still at it
    }
    return true;
}

// CoopIsRemote: the running script was started by the other player's game.
bool __cdecl CoopIsRemote(game::CStruct*, game::CScript* script) { return IsRemote(script); }

void Component2(void* object, uint32_t type, void** out);
void* MovieManager();
uint32_t PlayingMovie(void* manager);

// CoopLogPositions: where skaters 0 and 1 (the players' objects) and the local skater are, into coop.log.
bool __cdecl CoopLogPositions(game::CStruct*, game::CScript*)
{
    void* local = game::LocalSkater();
    void* movies = MovieManager();
    Log("pos: movie playing %08X, paused %d", movies ? PlayingMovie(movies) : 0, *reinterpret_cast<bool*>(0x006FA7D4));
    for (uint32_t id = 0; id < 2; ++id) {
        void* o = game::ObjectById(id);
        if (!o) {
            Log("pos: object %u missing", id);
            continue;
        }
        const float* p = reinterpret_cast<const float*>(static_cast<uint8_t*>(o) + 0x4C);
        Log("pos: object %u%s at %.0f %.0f %.0f", id, o == local ? " (local)" : "", p[0], p[1], p[2]);
        void* c = nullptr;
        Component2(o, 0x850EEF66, &c);
        if (c) {
            auto* comp = static_cast<uint8_t*>(c);
            auto* history = *reinterpret_cast<uint8_t**>(comp + 0x2A4);
            Log("pos:   playback sync %u time %d entry %d updates %d history %d", *reinterpret_cast<uint32_t*>(comp + 0x18),
                *reinterpret_cast<int*>(comp + 0x1C), *reinterpret_cast<int*>(comp + 0x24),
                *reinterpret_cast<int*>(comp + 0x298), history ? *reinterpret_cast<int*>(history + 0x18) : -1);
        }
    }
    return true;
}

// CoopGetFriendPos: the other player's skater position as pos=(x, y, z) in the script's params (false if none).
// Both games see the same positions (the network game's own sync).
bool __cdecl CoopGetFriendPos(game::CStruct*, game::CScript* script)
{
    void* local = game::LocalSkater();
    for (uint32_t id = 0; id < 2; ++id) {
        void* o = game::ObjectById(id);
        if (!o || o == local || o == g_deferredSkater)
            continue;
        const float* p = reinterpret_cast<const float*>(static_cast<uint8_t*>(o) + 0x4C);
        auto* results = *reinterpret_cast<game::CStruct**>(reinterpret_cast<uint8_t*>(script) + 0x14);
        reinterpret_cast<void(__thiscall*)(game::CStruct*, uint32_t, float, float, float)>(0x00477E20)(
            results, 0x7F261953, p[0], p[1], p[2]); // "pos"
        return true;
    }
    return false;
}

// Puts the local skater at `pos` (+ dx on x) the way CSkater::MoveToNode does.
bool TeleportLocal(const float* pos, float dx)
{
    auto* local = static_cast<uint8_t*>(game::LocalSkater());
    if (!local)
        return false;
    float p[4] = {pos[0] + dx, pos[1], pos[2], 1.0f};
    std::memcpy(local + 0x4C, p, sizeof(p));
    std::memcpy(local + 0x5C, p, sizeof(p));
    if (auto* core = *reinterpret_cast<uint8_t**>(local + 0x280))
        std::memcpy(core + 0x98, p, sizeof(p));
    std::memset(local + 0xAC, 0, 12); // standing still there (CCompositeObject::m_vel)
    reinterpret_cast<void(__thiscall*)(void*, bool)>(0x0045E4C0)(local, true);
    return true;
}

// CoopTeleportToFriend: the local skater put next to the other player's (as CSkater::MoveToNode moves one: position,
// old position, the core physics' position at +0x98 of the component at +0x280, SetTeleported 0x0045E4C0).
bool __cdecl CoopTeleportToFriend(game::CStruct*, game::CScript*)
{
    auto* local = static_cast<uint8_t*>(game::LocalSkater());
    if (!local)
        return false;
    for (uint32_t id = 0; id < 2; ++id) {
        auto* o = static_cast<uint8_t*>(game::ObjectById(id));
        if (!o || o == local || o == g_deferredSkater)
            continue;
        const float* pos = reinterpret_cast<const float*>(o + 0x4C);
        TeleportLocal(pos, 60.0f); // beside, not inside
        Log("teleported to skater %u at %.0f %.0f %.0f", id, pos[0], pos[1], pos[2]);
        return true;
    }
    return false;
}

// CoopGetName: coop_name="<the player's name>" (coop.ini's name, or -NAME) in the script's params; false if none.
bool __cdecl CoopGetName(game::CStruct*, game::CScript* script)
{
    const char* name = reinterpret_cast<const char*>(0x007CCBC4);
    if (!name[0])
        return false;
    auto* results = *reinterpret_cast<game::CStruct**>(reinterpret_cast<uint8_t*>(script) + 0x14);
    reinterpret_cast<void(__thiscall*)(game::CStruct*, uint32_t, const char*)>(game::kStructAddString)(results, 0xC62B2CEA, name);
    return true;
}

// CoopSendSpawn call=<script> [params={...}]: the other player's game spawns that script with these params.
bool __cdecl CoopSendSpawn(game::CStruct* params, game::CScript*)
{
    uint32_t name = 0;
    game::CStruct* scriptParams = nullptr;
    if (!params || !game::GetChecksumNamed(params, 0x3371D0C1, &name) || !link::Connected()) // "call"
        return false;
    game::GetStructureNamed(params, 0x7031F10C, &scriptParams); // "params"
    SendSpawn(name, scriptParams);
    return true;
}

// The client plays the host's story: a story save (OptionsAndPros) in co-op saves the friend's own story (below), and
// none is loaded, so the synced one isn't replaced.
constexpr uint32_t kType = 0x7321A8D6, kOptionsAndPros = 0xB010F357;
bool ClientStoryFile(game::CStruct* params)
{
    uint32_t type = 0;
    return g_role == Role::Client && params && game::GetChecksumNamed(params, kType, &type) && type == kOptionsAndPros;
}
// The friend saves their own story: while the save is written, their own career and chapter info are put in (the
// host's go back right after, in the same frame), and the goal manager's params the save asks for are their own.
// Earlier the save was refused with a message, which looked like a save error.
game::CStruct* g_saveGoalParams = nullptr;
constexpr uintptr_t kSaveGoalParamsCall = 0x005A7B9C; // WriteSaveContents (OptionsAndPros): the goal manager's params

game::CStruct* __fastcall GoalParamsForSave(void* goalManager, void*)
{
    if (g_saveGoalParams)
        return g_saveGoalParams;
    return reinterpret_cast<game::CStruct*(__thiscall*)(void*)>(0x004A2BA0)(goalManager);
}

void ReadCareerAndChapters(game::CStruct* s)
{
    reinterpret_cast<void(__thiscall*)(void*, game::CStruct*)>(game::kCareerReadFromStructure)(game::Career(), s);
    reinterpret_cast<void(__thiscall*)(void*, game::CStruct*)>(game::kReadChapterInfo)(nullptr, s);
}

bool __cdecl SaveToMemoryCardHook(game::CStruct* params, game::CScript* script)
{
    auto save = reinterpret_cast<game::CFunc>(0x005A97E0);
    if (!ClientStoryFile(params) || g_ownCareer.empty())
        return save(params, script);
    static uint8_t hostStory[0x20000];
    if (!WriteCareer(hostStory, sizeof(hostStory))) {
        Log("client: save refused (the host's story couldn't be kept aside)");
        game::SpawnScript("coop_client_save_refused");
        return false;
    }
    game::CStruct* own = game::NewStruct();
    game::ReadFromBuffer(own, g_ownCareer.data());
    ReadCareerAndChapters(own);
    game::CStruct* ownGoals = nullptr;
    if (game::GetStructureNamed(own, kCoopGoalManager, &ownGoals))
        g_saveGoalParams = ownGoals;
    bool saved = save(params, script);
    g_saveGoalParams = nullptr;
    game::CStruct* host = game::NewStruct();
    game::ReadFromBuffer(host, hostStory);
    ReadCareerAndChapters(host);
    game::FreeStruct(host);
    game::FreeStruct(own);
    Log("client: own story saved: %s", saved ? "ok" : "failed");
    if (saved)
        game::SpawnScript("coop_client_saved_own");
    return saved;
}
bool __cdecl LoadFromMemoryCardHook(game::CStruct* params, game::CScript* script)
{
    if (ClientStoryFile(params)) {
        Log("client: story load refused in co-op");
        game::SpawnScript("coop_client_save_refused");
        return false;
    }
    const char* name = "";
    if (params)
        game::GetStringNamed(params, game::kNameChecksum, &name);
    bool loaded = reinterpret_cast<game::CFunc>(0x005A8730)(params, script);
    Log("loaded save '%s': %s", name ? name : "", loaded ? "ok" : "failed");
    return loaded;
}
bool __cdecl CoopShouldJoin(game::CStruct*, game::CScript*) { return g_joinNow && g_haveCareer; }
bool __cdecl CoopLinkUp(game::CStruct*, game::CScript*) { return link::Connected(); }

// --- script function table -------------------------------------------------------------------------------------

// Unused debug entries of the exe's script function table ({const char* name, CFunc}), all pointing at the
// retail stub 0x005B9760. Renamed before the game registers them, so the scripts can call the new names.
constexpr uintptr_t kStub = 0x005B9760;
struct TableSlot {
    uintptr_t entry;
    const char* oldName;
    const char* newName;
    game::CFunc function;
};
const TableSlot kSlots[] = {
    {0x00680CF8, "LaunchScriptDebugger", "CoopLog", &CoopLog},
    {0x0067FA48, "DumpTextures", "CoopJoin", &CoopJoin},
    {0x0067FB70, "ToggleMetricItem", "CoopSetRole", &CoopSetRole},
    {0x0067FB78, "ToggleMemMetrics", "CoopIsActive", &CoopIsActive},
    {0x00681968, "StartProfiler", "CoopIsHost", &CoopIsHost},
    {0x00681970, "StopProfiler", "CoopConnect", &CoopConnect},
    {0x00681880, "ProfileTime", "CoopHasCareer", &CoopHasCareer},
    {0x00680CA8, "DumpScreenElements", "CoopShouldJoin", &CoopShouldJoin},
    {0x00680BF8, "GetMemProfileSize", "CoopLinkUp", &CoopLinkUp},
    {0x0067FF10, "SkaterDebugOn", "CoopSendSpawn", &CoopSendSpawn},
    {0x0067FF18, "SkaterDebugOff", "CoopLogPositions", &CoopLogPositions},
    {0x006814B0, "PrintSkaterPosition", "CoopGetFriendPos", &CoopGetFriendPos},
    {0x006814A0, "PrintSkaterStats", "CoopTeleportToFriend", &CoopTeleportToFriend},
    {0x006814A8, "PrintSkaterStats2", "CoopIsRemote", &CoopIsRemote},
    {0x00681888, "PrintPrefixedNodes", "CoopGetName", &CoopGetName},
    {0x00681818, "ShowCamOffset", "CoopBeforeLevelChange", &CoopBeforeLevelChange},
    {0x00681830, "GetVramUsage", "CoopWasActive", &CoopWasActive},
    {0x0067F3E8, "PrintScriptInfo", "CoopSharedGoal", &CoopSharedGoal},
    {0x0067F3D0, "Printf", "Printf", &ScriptPrintf},
};

bool PatchTable()
{
    for (const TableSlot& slot : kSlots) {
        auto* entry = reinterpret_cast<uintptr_t*>(slot.entry);
        if (entry[1] != kStub || std::strcmp(reinterpret_cast<const char*>(entry[0]), slot.oldName) != 0) {
            Log("table slot %s at %08X doesn't match", slot.oldName, static_cast<unsigned>(slot.entry));
            return false;
        }
    }
    for (const TableSlot& slot : kSlots) {
        patch::WriteU32(slot.entry, reinterpret_cast<uint32_t>(slot.newName));
        patch::WriteU32(slot.entry + 4, reinterpret_cast<uint32_t>(slot.function));
    }
    return true;
}

// --- per frame ---------------------------------------------------------------------------------------------------

using VoidFn = void(__cdecl*)();
VoidFn g_readKeyboard = nullptr;
bool g_escapeTyped = false; // Esc typed (Sio::KeyInput): the chat box closes (coop_chat_escape) next frame

bool OurWindowHasFocus()
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

bool Pressed(int key, bool& wasDown)
{
    if (key < 0) // switched off in coop.ini
        return false;
    auto held = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
    bool down = held(key & 0xFF) && held(VK_CONTROL) == ((key & kWithCtrl) != 0) &&
                held(VK_SHIFT) == ((key & kWithShift) != 0) && held(VK_MENU) == ((key & kWithAlt) != 0);
    bool edge = down && !wasDown;
    wasDown = down;
    return edge;
}

void* MovieManager();
uint32_t PlayingMovie(void* manager);
void SendStartedCams();

void SpawnByName(const char* script) { game::SpawnScript(script); }

void Component2(void* object, uint32_t type, void** out)
{
    *out = reinterpret_cast<void*(__thiscall*)(void*, uint32_t)>(0x0045DB90)(object, type);
}

// Test bot: "goto <object name | friend> [radius]" drives the local skater there (W held, A / D to turn toward it).
struct Goto {
    bool active = false;
    uint32_t target = 0; // object id; 0xFFFFFFFF: the friend
    float radius = 120.0f;
    DWORD started = 0;
} g_goto;
constexpr int kKeyW = 0x11, kKeyA = 0x1E, kKeyD = 0x20;

// Co-op ends for this player: the friend goes back to the main menu with their own story; the host's level reloads
// for single player (the friend is told the host left).
void LeaveCoop()
{
    if (g_role == Role::Host) {
        Log("leave: the host ends co-op");
        // The friend goes first (its skater is then hidden and removed at the cleanup, as when a friend quits):
        // a level cleanup with the friend's skater still in the game crashed.
        if (link::Connected())
            link::Send(link::kHostLeft, nullptr, 0);
        game::SpawnScript("coop_host_leave");
    } else if (g_role == Role::Client) {
        Log("leave: the friend leaves");
        bool inGame = g_selfJoined;
        g_role = Role::None; // (its own story comes back at the main menu)
        g_selfJoined = false;
        link::Close();
        if (inGame)
            game::SpawnScript("coop_client_leave");
    }
}

bool AutomationCommand(const char* verb, const char* arg, int value)
{
    if (std::strcmp(verb, "pad") == 0) { // test only: "pad a+lx=0.8 <ms>", a gamepad's buttons and sticks held
        gamepad::Inject(arg, value);
        return true;
    }
    if (std::strcmp(verb, "leave") == 0) { // test only: as the leave key pressed twice
        LeaveCoop();
        return true;
    }
    if (std::strcmp(verb, "goto") == 0) {
        g_goto.active = true;
        g_goto.target = std::strcmp(arg, "friend") == 0 ? 0xFFFFFFFF : game::Checksum(arg);
        g_goto.radius = value > 0 ? static_cast<float>(value) : 120.0f;
        g_goto.started = GetTickCount();
        Log("bot: goto %s", arg);
        return true;
    }
    if (std::strcmp(verb, "warp") == 0) { // test only: next to an object
        auto* o = static_cast<uint8_t*>(game::ObjectById(game::Checksum(arg)));
        if (!o)
            Log("bot: warp %s: no such object", arg);
        else if (TeleportLocal(reinterpret_cast<const float*>(o + 0x4C), value ? static_cast<float>(value) : 70.0f))
            Log("bot: warped to %s at %.0f %.0f %.0f", arg, reinterpret_cast<const float*>(o + 0x4C)[0],
                reinterpret_cast<const float*>(o + 0x4C)[1], reinterpret_cast<const float*>(o + 0x4C)[2]);
        return true;
    }
    if (std::strcmp(verb, "face") == 0) { // test only: the local skater turned to the friend (as CSkater::MoveToNode)
        auto* self = static_cast<uint8_t*>(game::LocalSkater());
        uint8_t* other = nullptr;
        for (uint32_t id = 0; id < 2 && !other; ++id)
            if (auto* o = static_cast<uint8_t*>(game::ObjectById(id)); o && o != self && o != g_deferredSkater)
                other = o;
        if (!self || !other)
            return true;
        const float* p = reinterpret_cast<const float*>(self + 0x4C);
        const float* q = reinterpret_cast<const float*>(other + 0x4C);
        float fx = q[0] - p[0], fz = q[2] - p[2], length = std::sqrt(fx * fx + fz * fz);
        if (length < 1.0f)
            return true;
        fx /= length, fz /= length;
        const float m[16] = {fz, 0, -fx, 0, 0, 1, 0, 0, fx, 0, fz, 0, 0, 0, 0, 1}; // right, up, at
        std::memcpy(self + 0x6C, m, sizeof(m));
        std::memcpy(self + 0xC0, m, sizeof(m));
        if (auto* core = *reinterpret_cast<uint8_t**>(self + 0x280)) {
            std::memcpy(core + 0x18, m, sizeof(m));
            for (uintptr_t up : {0x58u, 0x68u, 0x78u})
                std::memcpy(core + up, m + 4, 16);
        }
        Log("bot: facing the friend");
        return true;
    }
    if (std::strcmp(verb, "loadsave") == 0) { // test only: load a save by name ('_' for a space) as the menu does
        std::string name = arg;
        for (char& c : name)
            if (c == '_')
                c = ' ';
        game::CStruct* params = game::NewStruct();
        reinterpret_cast<void(__thiscall*)(game::CStruct*, uint32_t, const char*)>(game::kStructAddString)(
            params, game::kNameChecksum, name.c_str());
        game::SpawnScriptChecksum(game::Checksum("coop_debug_load_save"), params);
        game::FreeStruct(params);
        return true;
    }
    if (std::strcmp(verb, "type") == 0) { // test only: characters into the game window, as typed (%XX: a byte)
        HWND window = *reinterpret_cast<HWND*>(0x007D6A28);
        for (const char* c = arg; *c; ++c) {
            unsigned ch = static_cast<uint8_t>(*c);
            if (*c == '%' && c[1] && c[2] && std::sscanf(c + 1, "%2x", &ch) == 1)
                c += 2;
            PostMessageA(window, WM_CHAR, ch, 1);
        }
        return true;
    }
    if (std::strcmp(verb, "stop") == 0) {
        g_goto.active = false;
        automation::Hold(kKeyW, false), automation::Hold(kKeyA, false), automation::Hold(kKeyD, false);
        return true;
    }
    return false;
}

void SteerBot()
{
    if (!g_goto.active)
        return;
    auto* self = static_cast<uint8_t*>(game::LocalSkater());
    uint8_t* target = nullptr;
    if (g_goto.target == 0xFFFFFFFF) {
        for (uint32_t id = 0; id < 2 && !target; ++id)
            if (auto* o = static_cast<uint8_t*>(game::ObjectById(id)); o && o != self && o != g_deferredSkater)
                target = o;
    } else {
        target = static_cast<uint8_t*>(game::ObjectById(g_goto.target));
    }
    auto stop = [](const char* why) {
        g_goto.active = false;
        automation::Hold(kKeyW, false), automation::Hold(kKeyA, false), automation::Hold(kKeyD, false);
        Log("bot: %s", why);
    };
    if (!self || !target)
        return stop("goto: no skater or target");
    const float* p = reinterpret_cast<const float*>(self + 0x4C);
    const float* q = reinterpret_cast<const float*>(target + 0x4C);
    const float* fwd = reinterpret_cast<const float*>(self + 0xC0 + 32); // matrix row 2
    float dx = q[0] - p[0], dz = q[2] - p[2];
    float dist = std::sqrt(dx * dx + dz * dz);
    if (dist < g_goto.radius)
        return stop("goto: arrived");
    if (GetTickCount() - g_goto.started > 30000)
        return stop("goto: gave up after 30 s");
    // the turn's sign: the cross product's y of forward x (to target)
    float cross = fwd[2] * dx - fwd[0] * dz;
    float dot = fwd[0] * dx + fwd[2] * dz;
    float angle = std::atan2(cross, dot) * 57.29578f;
    automation::Hold(kKeyW, true);
    automation::Hold(kKeyA, angle > 12.0f);
    automation::Hold(kKeyD, angle < -12.0f);
    static DWORD logged = 0;
    if (GetTickCount() - logged > 1000) {
        logged = GetTickCount();
        Log("bot: dist %.0f angle %.0f", dist, angle);
    }
}

// --- hang watch ---------------------------------------------------------------------------------------------------

// The host's game stood still for 30-40 s a few times in a real session (no frames, then Windows' "not responding").
// A thread watches the frames: when none came for 5 s it looks where the game's main thread is (its instruction
// pointer and the game's return addresses on its stack) and logs it, and how long the stall lasted once it's over.
HANDLE g_mainThread = nullptr;
volatile DWORD g_lastFrame = 0;

DWORD WINAPI HangWatch(void*)
{
    bool stalled = false;
    DWORD stallStart = 0;
    for (;;) {
        Sleep(1000);
        DWORD last = g_lastFrame, now = GetTickCount();
        if (!last)
            continue;
        if (now - last < 5000) {
            if (stalled)
                Log("hang watch: frames again after %lu ms", now - stallStart);
            stalled = false;
            continue;
        }
        if (stalled)
            continue;
        stalled = true;
        stallStart = last;
        uint32_t eip = 0, found = 0, trace[16];
        CONTEXT c = {};
        c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        if (SuspendThread(g_mainThread) != static_cast<DWORD>(-1)) {
            if (GetThreadContext(g_mainThread, &c)) {
                eip = c.Eip;
                const auto* stack = reinterpret_cast<const uint32_t*>(c.Esp);
                for (int i = 0; i < 2048 && found < 16; ++i) {
                    MEMORY_BASIC_INFORMATION mbi;
                    if (!VirtualQuery(stack + i, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
                        break;
                    uint32_t v = stack[i];
                    if (v >= 0x00401000 && v < 0x00645000)
                        trace[found++] = v;
                }
            }
            ResumeThread(g_mainThread);
        }
        char line[256] = "";
        int n = 0;
        for (uint32_t i = 0; i < found; ++i)
            n += std::snprintf(line + n, sizeof(line) - n, " %08X", trace[i]);
        char module[MAX_PATH] = "?";
        HMODULE owner = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(static_cast<uintptr_t>(eip)), &owner))
            GetModuleFileNameA(owner, module, sizeof(module));
        const char* base = std::strrchr(module, '\\');
        Log("hang watch: no frame for %lu ms (window %s); main thread at %08X in %s; game stack:%s", now - last,
            OurWindowHasFocus() ? "in front" : "in the background", eip, base ? base + 1 : module, line);
    }
}

void __cdecl OnFrame()
{
    g_lastFrame = GetTickCount();
    if (g_settings.automation) {
        automation::Update(&SpawnByName, &AutomationCommand);
        SteerBot();
    }
    g_readKeyboard();
    if (!g_sharedGoals.empty() && !InCoopSession()) {
        // The other player left: a shared goal done here (waiting for them) is won now.
        std::set<uint32_t> done = g_localDone;
        ForgetSharedGoals();
        for (uint32_t name : done) {
            Log("shared goal %08X: the other player left, won here", name);
            g_winGoal(game::GoalManager(), name);
        }
    }
    if (g_role == Role::Client && g_selfJoined && !link::Connected()) {
        // The host is gone: the game's own "lost connection" handling takes over as in any network game.
        Log("client: link to the host lost, co-op ended");
        g_role = Role::None; // (its own story comes back at the main menu: not under a running level)
        // Back to the main menu now, the level's objects frozen meanwhile: the game's own handling a few seconds
        // later crashed in the story's peds (Barcelona's skater peds).
        game::SpawnScript("coop_client_host_gone");
        g_selfJoined = false;
        link::Close();
    }
    static DWORD heartbeat = 0;
    if (g_role != Role::None && GetTickCount() - heartbeat > 15000) {
        heartbeat = GetTickCount();
        Log("state: role %d listening %d connected %d clientJoined %d selfJoined %d", static_cast<int>(g_role),
            link::Listening(), link::Connected(), g_clientJoined, g_selfJoined);
    }
    static bool wasConnected = false;
    if (link::Connected() != wasConnected) {
        wasConnected = link::Connected();
        Log("link: %s", wasConnected ? "peer connected" : "peer gone");
        if (!wasConnected) {
            game::SpawnScriptChecksum(0x454E4718, nullptr); // coop_friend_label_hide (their skater stops updating)
            UndoFriendSwap();
            if (g_role == Role::Host)
                g_clientJoined = false;
        }
    }
    if (g_role != Role::None) {
        link::Poll(&OnLinkMessage);
        if (link::Connected() && (g_clientJoined || g_selfJoined))
            SendStartedCams();
    }
    // The friend's skater is moved by the network, not by this game's story: whatever paused it here (a chapter
    // intro, a cutscene that ended early, the pro select) it runs again once no cutscene or camera plays.
    if (g_role != Role::None) {
        static DWORD checked = 0;
        if (GetTickCount() - checked > 250) {
            checked = GetTickCount();
            void* movies = MovieManager();
            if (!(movies && PlayingMovie(movies))) {
                void* local = game::LocalSkater();
                for (uint32_t id = 0; id < 2; ++id) {
                    auto* o = static_cast<uint8_t*>(game::ObjectById(id));
                    if (o && o != local && o != g_deferredSkater && (*reinterpret_cast<uint32_t*>(o + 0x104) & 1)) {
                        *reinterpret_cast<uint32_t*>(o + 0x104) &= ~1u; // COMPOSITE_FLAG_PAUSED
                        Log("friend's skater %u unpaused", id);
                    }
                }
            }
        }
    }
    for (DWORD& at : g_resetPlaybackAt) {
        if (at && GetTickCount() >= at) {
            at = 0;
            if (g_role != Role::None)
                ResetRemotePlayback();
        }
    }
    static bool f10, f11;
    bool focus = OurWindowHasFocus();
    if (Pressed(g_settings.hostKey, f10) && focus) {
        Log("host key: coop_host_start");
        game::SpawnScript("coop_host_start");
    }
    static bool f12;
    if (Pressed(g_settings.teleportKey, f12) && focus && g_role != Role::None) {
        Log("teleport key: coop_teleport_request");
        game::SpawnScript("coop_teleport_request");
    }
    static bool f8;
    if (Pressed(VK_F8, f8) && focus && g_settings.debugHotkeys) {
        Log("F8: coop_debug_f9");
        game::SpawnScript("coop_debug_f9");
    }
    if (g_escapeTyped) {
        g_escapeTyped = false;
        game::SpawnScript("coop_chat_escape");
    }
    // [keys] leave: co-op ends for this player (pressed twice within 4 s). The friend goes back to the main menu
    // with their own story; the host's level reloads for single player (the friend is told the host left).
    static bool leaveDown;
    static DWORD leaveArmed = 0;
    if (Pressed(g_settings.leaveKey, leaveDown) && focus && g_role != Role::None) {
        if (!leaveArmed || GetTickCount() - leaveArmed > 4000) {
            leaveArmed = GetTickCount();
            game::CStruct* params = game::NewStruct();
            reinterpret_cast<void(__thiscall*)(game::CStruct*, uint32_t, const char*)>(game::kStructAddString)(
                params, game::Checksum("text"), "Co-op: press the leave key again to leave");
            game::SpawnScriptChecksum(game::Checksum("coop_leave_confirm"), params);
            game::FreeStruct(params);
        } else {
            leaveArmed = 0;
            LeaveCoop();
        }
    }
    if (Pressed(g_settings.joinKey, f11) && focus) {
        Log("join key: coop_join_start");
        game::SpawnScript("coop_join_start");
    }
}

// --- null component guard ----------------------------------------------------------------------------------------

// Script::CallMemberFunction 0x00472980 looks the member function's component up on the object and calls it with
// no null check. A network skater has no story components (StatsManager...), so story scripts running on the
// client's skater crashed there. The guard makes a missing component's member function return false.
bool InstallComponentGuard()
{
    const uintptr_t site = 0x004729BA; // mov ecx,[esp+14h] (8B 4C 24 14); mov edx,[eax] (8B 10)
    const uint8_t expected[] = {0x8B, 0x4C, 0x24, 0x14, 0x8B, 0x10, 0x51};
    if (!patch::Matches(site, expected, sizeof(expected)))
        return false;
    auto* stub = static_cast<uint8_t*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    uint8_t code[] = {
        0x85, 0xC0,             // test eax, eax
        0x74, 0x0B,             // jz  none
        0x8B, 0x4C, 0x24, 0x14, // mov ecx, [esp+14h]
        0x8B, 0x10,             // mov edx, [eax]
        0xE9, 0, 0, 0, 0,       // jmp 0x004729C0
        0x32, 0xC0,             // none: xor al, al
        0x5E,                   // pop esi
        0xC3,                   // ret
    };
    int32_t back = static_cast<int32_t>(0x004729C0 - (reinterpret_cast<uintptr_t>(stub) + 15));
    std::memcpy(code + 11, &back, 4);
    std::memcpy(stub, code, sizeof(code));
    patch::Jump(site, stub);
    const uint8_t nop = 0x90;
    patch::Write(site + 5, &nop, 1);
    return true;
}

// --- cutscene end -------------------------------------------------------------------------------------------------

// CMovieManager::Update ends the playing cutscene when it's complete - finished, or skipped with a button - with
// EndMovies(false) (call at 0x0057AF21). The other game ends the same cutscene then, if it's still playing it: a skip
// skips it for both, and a cutscene chain stays in step.
constexpr uintptr_t kSkateMovieManager = 0x368;   // CMovieManager* at the skate module +0x368
constexpr uintptr_t kEndMovies = 0x0057A7E0;      // bool __thiscall CMovieManager::EndMovies(bool skippable)
constexpr uintptr_t kGetPlayingName = 0x0057B030; // u32 __thiscall CMovieManager::GetPlayingName()
constexpr uintptr_t kUpdateEndMoviesCall = 0x0057AF21;

void* MovieManager() { return *reinterpret_cast<void**>(static_cast<uint8_t*>(game::Deref(game::kSkateModule)) + kSkateMovieManager); }
uint32_t PlayingMovie(void* manager) { return reinterpret_cast<uint32_t(__thiscall*)(void*)>(kGetPlayingName)(manager); }
bool EndMovies(void* manager, int skippable)
{
    return reinterpret_cast<bool(__thiscall*)(void*, int)>(kEndMovies)(manager, skippable);
}

bool __fastcall UpdateEndMoviesHook(void* manager, void*, int skippable)
{
    uint32_t name = PlayingMovie(manager);
    bool ended = EndMovies(manager, skippable);
    if (name && !g_remote && g_role != Role::None && link::Connected() && (g_clientJoined || g_selfJoined)) {
        Log("movie %08X ended -> peer", name);
        link::Send(link::kMovieEnded, &name, sizeof(name));
    }
    return ended;
}

// KillSkaterCamAnim (also KillCutscene; table 0x0067FE50, cfunc 0x005ADD70): a skipped goal camera or cutscene
// ends on the other side too.
bool __cdecl KillSkaterCamAnimHook(game::CStruct* params, game::CScript* script)
{
    void* manager = MovieManager();
    uint32_t before = manager ? PlayingMovie(manager) : 0;
    bool result = reinterpret_cast<game::CFunc>(0x005ADD70)(params, script);
    uint32_t after = manager ? PlayingMovie(manager) : 0;
    // KillSkaterCamAnim All is a goal's start clearing cameras, not a skip: the other player's camera shown here as
    // a goal starts (both start a shared goal at once) ends without ending theirs.
    uint32_t which = 0;
    bool all = params && game::GetChecksumNamed(params, 0, &which) && which == game::Checksum("all");
    if (g_role != Role::None)
        Log("cam kill: %08X -> %08X%s%s", before, after, all ? " (all)" : "", IsRemote(script) ? " (remote)" : "");
    if (before && before != after && !all && !IsRemote(script) && g_role != Role::None && link::Connected() &&
        (g_clientJoined || g_selfJoined))
        link::Send(link::kMovieEnded, &before, sizeof(before));
    return result;
}

void EndRemoteMovie(uint32_t name)
{
    void* manager = MovieManager();
    uint32_t playing = manager ? PlayingMovie(manager) : 0;
    if (playing != name) {
        Log("peer ended %08X; playing here: %08X", name, playing);
        return;
    }
    Log("cutscene %08X ended by peer", name);
    // KillSkaterCamAnim Current: ends it the way a skip does, and gives the camera back to the skater after the last.
    game::CStruct* params = game::NewStruct();
    reinterpret_cast<void(__thiscall*)(game::CStruct*, uint32_t, uint32_t)>(0x00477D40)(params, 0, 0x230CCBF4); // Current
    g_remote = true;
    reinterpret_cast<game::CFunc>(0x005ADD70)(params, nullptr);
    g_remote = false;
    game::FreeStruct(params);
}

// --- guest characters ---------------------------------------------------------------------------------------------

// A guest character (or a pro) is played by swapping the local skater's model with the ped standing in the level
// (SwapObjectModels obj_1=skater obj_2=<ped>, from swap_skaters). The other game swaps the same ped with this
// player's skater as it sees it (same object ids on both: level nodes, and the skaters' player ids).
constexpr uintptr_t kSwapObjectModelsEntry = 0x00681950;
constexpr uintptr_t kCFuncSwapObjectModels = 0x005B34A0;
constexpr uint32_t kObj1 = 0x9A2DD8A1, kObj2 = 0x0324891B; // obj_1, obj_2

bool __cdecl SwapObjectModelsHook(game::CStruct* params, game::CScript* script)
{
    uint32_t id1 = 0, id2 = 0;
    game::GetChecksumNamed(params, kObj1, &id1);
    game::GetChecksumNamed(params, kObj2, &id2);
    void* a = game::ObjectById(id1);
    void* b = game::ObjectById(id2);
    bool swapped = reinterpret_cast<game::CFunc>(kCFuncSwapObjectModels)(params, script);
    void* local = game::LocalSkater();
    // (not during cutscenes and goal cameras: the pro intro swaps models back and forth for the show)
    void* movies = MovieManager();
    bool presenting = movies && PlayingMovie(movies);
    if (swapped && !presenting && !IsRemote(script) && local && a && b && g_role != Role::None && link::Connected() &&
        (g_clientJoined || g_selfJoined) && (a == local || b == local)) {
        uint32_t ids[2] = {game::ObjectId(local), game::ObjectId(a == local ? b : a)};
        Log("swap skater %u <-> %08X -> peer", ids[0], ids[1]);
        link::Send(link::kSwapModels, ids, sizeof(ids));
    }
    return swapped;
}

// The friend's skater's model swap (the guest / pro it wears here), so it can be put back before the game drops
// that skater: destroying a skater that wears a ped's model hung the game.

// The game's swap table (0x007CEDE0, five {used, id, current} entries; thug2-decomp model_swap_cfuncs.cpp) is for
// the local player's swaps: RestoreAllSwappedObjectModels walks it. The friend's mirrored swap is kept out of it
// and undone by the DLL (level cleanup, the friend leaving, a swap back).
void ForgetSwapRecords(uint32_t a, uint32_t b)
{
    auto* entry = reinterpret_cast<uint8_t*>(0x007CEDE0);
    for (int i = 0; i < 5; ++i, entry += 12) {
        uint32_t id = *reinterpret_cast<uint32_t*>(entry + 4);
        if (entry[0] && (id == a || id == b))
            entry[0] = 0;
    }
}

void UndoFriendSwap()
{
    if (!g_friendSwap[0])
        return;
    uint32_t skater = g_friendSwap[0] - 1, other = g_friendSwap[1];
    g_friendSwap[0] = 0;
    if (!game::ObjectById(skater) || !game::ObjectById(other)) {
        Log("friend's skater %u: swap with %08X can't be undone (object gone)", skater, other);
        return;
    }
    game::CStruct* params = game::NewStruct();
    game::AddChecksumNamed(params, kObj1, skater);
    game::AddChecksumNamed(params, kObj2, other);
    g_remote = true;
    reinterpret_cast<game::CFunc>(kCFuncSwapObjectModels)(params, nullptr);
    g_remote = false;
    game::FreeStruct(params);
    ForgetSwapRecords(skater, other);
    Log("friend's skater %u: swap with %08X undone", skater, other);
}

void SwapRemote(uint32_t skaterId, uint32_t otherId)
{
    if (!game::ObjectById(skaterId) || !game::ObjectById(otherId)) {
        Log("swap from peer: object missing (%u, %08X)", skaterId, otherId);
        return;
    }
    game::CStruct* params = game::NewStruct();
    game::AddChecksumNamed(params, kObj1, skaterId);
    game::AddChecksumNamed(params, kObj2, otherId);
    g_remote = true;
    bool ok = reinterpret_cast<game::CFunc>(kCFuncSwapObjectModels)(params, nullptr);
    g_remote = false;
    game::FreeStruct(params);
    ForgetSwapRecords(skaterId, otherId);
    Log("swap from peer: skater %u <-> %08X %s", skaterId, otherId, ok ? "done" : "failed");
    if (ok) { // a second swap with the same object puts it back
        if (g_friendSwap[0] == skaterId + 1 && g_friendSwap[1] == otherId)
            g_friendSwap[0] = 0;
        else
            g_friendSwap[0] = skaterId + 1, g_friendSwap[1] = otherId;
    }
}

// --- the friend's name -------------------------------------------------------------------------------------------

// CSkaterFloatingNameComponent::Update 0x00511870 puts each player's name over their skater with a pos3D text element
// - in career mode those land at the top of the screen. In co-op the DLL draws the friend's name itself: projected with
// the player's camera (skatercam0's Gfx::Camera: position +0x5C, matrix +0x6C - right, up, back -, drawn horizontal
// field of view +0xB0) onto the 640x480 screen, kept at the screen's edge when the friend is off screen or behind,
// with the distance; translucent and over everything, so it shows through walls. The local name isn't drawn.
using ComponentUpdateFn = void(__thiscall*)(void*);
ComponentUpdateFn g_nameUpdate = nullptr;
void* MovieManager();
uint32_t PlayingMovie(void* manager);
constexpr uint32_t kId = 0x40C698AF, kText = 0xC4745838, kPos = 0x7F261953;
constexpr uint32_t kDestroyObjectLabel = 0x2575B406, kFriendLabel = 0xA6053129, kSkaterCam0 = 0x967C138C, kCameraType = 0xC4E311FA;

void* Component(void* object, uint32_t type)
{
    return reinterpret_cast<void*(__thiscall*)(void*, uint32_t)>(0x0045DB90)(object, type);
}

void RemoveLabel(uint32_t label)
{
    game::CStruct* params = game::NewStruct();
    game::AddChecksumNamed(params, kId, label);
    game::SpawnScriptChecksum(kDestroyObjectLabel, params);
    game::FreeStruct(params);
}

// The friend's head on the screen (640x480): false if there's no camera.
bool ProjectFriend(const float* world, float* sx, float* sy, float* distance)
{
    void* camObject = game::ObjectById(kSkaterCam0);
    if (!camObject)
        return false;
    auto* component = static_cast<uint8_t*>(Component(camObject, kCameraType));
    auto* camera = component ? *reinterpret_cast<const uint8_t**>(component + 0x18) : nullptr;
    if (!camera)
        return false;
    const float* pos = reinterpret_cast<const float*>(camera + 0x5C);
    const float* m = reinterpret_cast<const float*>(camera + 0x6C); // rows of 4
    float d[3] = {world[0] - pos[0], world[1] + 75.0f - pos[1], world[2] - pos[2]};
    float x = d[0] * m[0] + d[1] * m[1] + d[2] * m[2];
    float y = d[0] * m[4] + d[1] * m[5] + d[2] * m[6];
    float z = -(d[0] * m[8] + d[1] * m[9] + d[2] * m[10]);
    *distance = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    float fov = *reinterpret_cast<const float*>(camera + 0xB0);
    static DWORD logged = 0;
    if (g_settings.logPrintf && GetTickCount() - logged > 2000) {
        logged = GetTickCount();
        Log("label: d=(%.0f %.0f %.0f) cam=(%.0f %.0f %.0f) rows r=(%.2f %.2f %.2f) u=(%.2f %.2f %.2f) b=(%.2f %.2f %.2f) "
            "xyz=(%.0f %.0f %.0f) fov=%.1f",
            d[0], d[1], d[2], pos[0], pos[1], pos[2], m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10], x, y, z, fov);
    }
    float focal = 320.0f / std::tan(fov * 0.5f * 3.14159265f / 180.0f);
    if (z < 1.0f) { // behind: push it to the edge on its side
        float side = x >= 0 ? 1.0f : -1.0f;
        *sx = side > 0 ? 565.0f : 75.0f;
        *sy = 240.0f - (y > 0 ? 120.0f : -120.0f);
        return true;
    }
    *sx = 320.0f + x / z * focal;
    *sy = 240.0f - y / z * focal;
    *sx = *sx < 75.0f ? 75.0f : *sx > 565.0f ? 565.0f : *sx; // (the text is centred, ~130 px wide)
    *sy = *sy < 40.0f ? 40.0f : *sy > 440.0f ? 440.0f : *sy;
    return true;
}

void __fastcall NameUpdateHook(void* component, void*)
{
    if (g_role == Role::None) {
        g_nameUpdate(component);
        return;
    }
    auto* object = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(component) + 0x0C);
    uint32_t label = *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(component) + 0x18);
    static uint32_t removed[8];
    uint32_t slot = object ? *reinterpret_cast<uint32_t*>(object + 0x14) & 7 : 0;
    if (removed[slot] != label) { // the game's own label for this skater, once
        removed[slot] = label;
        RemoveLabel(label);
    }
    if (!object || object == game::LocalSkater())
        return;
    // not over cutscenes, goal cameras or the pause menu, nor for a friend who left (their skater hidden till cleanup)
    static bool hidden = false;
    if (object == g_deferredSkater || !link::Connected()) {
        if (!hidden) {
            hidden = true;
            game::SpawnScriptChecksum(0x454E4718, nullptr); // coop_friend_label_hide
        }
        return;
    }
    void* movies = MovieManager();
    if ((movies && PlayingMovie(movies)) || *reinterpret_cast<bool*>(0x006FA7D4)) {
        if (!hidden) {
            hidden = true;
            game::SpawnScriptChecksum(0x454E4718, nullptr); // coop_friend_label_hide
        }
        return;
    }
    hidden = false;
    static DWORD last = 0;
    if (GetTickCount() - last < 33) // ~30 updates a second is plenty
        return;
    last = GetTickCount();
    float sx, sy, distance;
    if (!ProjectFriend(reinterpret_cast<const float*>(object + 0x4C), &sx, &sy, &distance))
        return;
    if (auto* self = static_cast<const uint8_t*>(game::LocalSkater())) { // distance between the skaters
        const float* a = reinterpret_cast<const float*>(object + 0x4C);
        const float* b = reinterpret_cast<const float*>(self + 0x4C);
        distance = std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
    }
    const char* name = reinterpret_cast<const char*(__thiscall*)(void*)>(0x00581580)(object);
    char text[96];
    std::snprintf(text, sizeof(text), "%s  %dm", name ? name : "friend", static_cast<int>(distance * 0.0254f));
    game::CStruct* params = game::NewStruct();
    game::AddString(params, text); // (unnamed: the script reads it as text)
    reinterpret_cast<void(__thiscall*)(game::CStruct*, uint32_t, const char*)>(game::kStructAddString)(params, kText, text);
    reinterpret_cast<void(__thiscall*)(game::CStruct*, uint32_t, float, float)>(0x00477FC0)(params, kPos, sx, sy);
    game::SpawnScriptChecksum(kFriendLabel, params);
    game::FreeStruct(params);
}

bool InstallNameHook()
{
    const uint8_t prologue[] = {0x6A, 0xFF, 0x68, 0x26, 0xBE, 0x63, 0x00}; // push -1; push 0x0063BE26
    if (!patch::Matches(0x00511870, prologue, sizeof(prologue)))
        return false;
    g_nameUpdate = reinterpret_cast<ComponentUpdateFn>(
        patch::Detour(0x00511870, sizeof(prologue), reinterpret_cast<void*>(&NameUpdateHook)));
    return true;
}

// --- typing in Cyrillic ---------------------------------------------------------------------------------------

// Sio::KeyInput (0x005BDE70, cdecl key, flags): a typed character from the window's WM_CHAR - the chat box, names.
// The window is ANSI, so a Russian layout types code page bytes (cp1251) and the game's fonts have no Cyrillic:
// the letters showed as Western accented ones or boxes. They're typed as the Latin character on the same key of
// an English keyboard instead (й -> q, Ж -> :), as if the layout were switched.
using KeyInputFn = void(__cdecl*)(int, int);
KeyInputFn g_keyInput = nullptr;

// The character the key that types c in the input language gives on a US keyboard, 0 if none (AltGr, not on it).
char UsKeyCharacter(wchar_t c)
{
    const SHORT key = VkKeyScanExW(c, GetKeyboardLayout(0));
    if (key == -1 || (HIBYTE(key) & ~1))
        return 0;
    const uint8_t vk = LOBYTE(key);
    const bool shift = (HIBYTE(key) & 1) != 0;
    if (vk >= 'A' && vk <= 'Z')
        return static_cast<char>(shift ? vk : vk + ('a' - 'A'));
    if (vk >= '0' && vk <= '9')
        return shift ? ")!@#$%^&*("[vk - '0'] : static_cast<char>(vk);
    switch (vk) {
    case VK_OEM_1: return shift ? ':' : ';';
    case VK_OEM_PLUS: return shift ? '+' : '=';
    case VK_OEM_COMMA: return shift ? '<' : ',';
    case VK_OEM_MINUS: return shift ? '_' : '-';
    case VK_OEM_PERIOD: return shift ? '>' : '.';
    case VK_OEM_2: return shift ? '?' : '/';
    case VK_OEM_3: return shift ? '~' : '`';
    case VK_OEM_4: return shift ? '{' : '[';
    case VK_OEM_5: return shift ? '|' : '\\';
    case VK_OEM_6: return shift ? '}' : ']';
    case VK_OEM_7: return shift ? '"' : '\'';
    }
    return 0;
}

// The US key character for a typed byte: read in the input language's code page (Windows converts WM_CHAR for an
// ANSI window with it), else in the system's.
char UsKeyCharacterOf(char byte)
{
    char cp[8] = "";
    LCID locale = MAKELCID(LOWORD(reinterpret_cast<uintptr_t>(GetKeyboardLayout(0))), SORT_DEFAULT);
    UINT pages[2] = {GetLocaleInfoA(locale, LOCALE_IDEFAULTANSICODEPAGE, cp, sizeof(cp)) ? static_cast<UINT>(std::atoi(cp)) : 0u,
                     CP_ACP};
    for (UINT page : pages) {
        wchar_t wide = 0;
        if (page != 0 && MultiByteToWideChar(page, 0, &byte, 1, &wide, 1) == 1)
            if (char latin = UsKeyCharacter(wide))
                return latin;
    }
    return 0;
}

void __cdecl KeyInputHook(int key, int flags)
{
    if (key == 0x1B && flags >= 0 && !(flags & 0x40000000))
        g_escapeTyped = true;
    if (key >= 0x80 && key <= 0xFF) {
        if (char latin = UsKeyCharacterOf(static_cast<char>(key))) {
            g_keyInput(static_cast<uint8_t>(latin), flags);
            return;
        }
    }
    g_keyInput(key, flags);
}

bool InstallKeyInputHook()
{
    const uint8_t prologue[] = {0xA0, 0x6E, 0xE4, 0x7C, 0x00, 0x84, 0xC0}; // mov al, [0x007CE46E]; test al, al
    if (!patch::Matches(0x005BDE70, prologue, sizeof(prologue)))
        return false;
    g_keyInput = reinterpret_cast<KeyInputFn>(
        patch::Detour(0x005BDE70, sizeof(prologue), reinterpret_cast<void*>(&KeyInputHook)));
    return true;
}

// --- player collision ---------------------------------------------------------------------------------------------

// GameNet::Manager::PlayerCollisionEnabled (0x00534340): whether the skaters bump into each other (and the friend's
// skater is extrapolated ahead). The game takes it from the host's network option player_collision; in co-op
// coop.ini's player_collision decides when set.
using PlayerCollisionFn = bool(__thiscall*)(void*);
PlayerCollisionFn g_playerCollision = nullptr;

bool __fastcall PlayerCollisionHook(void* manager, void*)
{
    bool original = g_playerCollision(manager);
    bool result = (g_role != Role::None && g_settings.playerCollision >= 0) ? g_settings.playerCollision != 0 : original;
    static int logged = -1;
    int state = (original ? 1 : 0) | (result ? 2 : 0);
    if (g_role != Role::None && state != logged) {
        logged = state;
        Log("player collision: game %s, co-op %s", original ? "on" : "off", result ? "on" : "off");
    }
    return result;
}

bool InstallPlayerCollisionHook()
{
    const uint8_t prologue[] = {0x56, 0x8B, 0xF1, 0x8A, 0x46, 0x3A}; // push esi; mov esi, ecx; mov al, [esi+0x3A]
    if (!patch::Matches(0x00534340, prologue, sizeof(prologue)))
        return false;
    g_playerCollision = reinterpret_cast<PlayerCollisionFn>(
        patch::Detour(0x00534340, sizeof(prologue), reinterpret_cast<void*>(&PlayerCollisionHook)));
    return true;
}

// --- skater 0 is me ------------------------------------------------------------------------------------------------

// Script commands take "skater 0" (Skate::GetSkater(0) 0x005665C0, the default skater=0) to mean the player at this
// machine - in a network game it's the first in the list, the host's on the client. Called from the game's script
// commands (0x0059F610-0x005B6560), index 0 is the local skater and 1 the friend's in co-op; the engine's own
// callers keep the list order. (The client's goal cameras - PlaySkaterCamAnim on a skater that isn't local - did
// nothing before.)
using GetSkaterFn = void*(__thiscall*)(void*, int);
GetSkaterFn g_getSkater = nullptr;

void* __fastcall GetSkaterHook(void* skate, void*, int index)
{
    if (g_role != Role::None && (index == 0 || index == 1)) {
        auto from = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
        if (from >= 0x0059F610 && from < 0x005B6560) {
            void* local = game::LocalSkater();
            if (local) {
                if (index == 0)
                    return local;
                void* first = g_getSkater(skate, 0);
                return first == local ? g_getSkater(skate, 1) : first;
            }
        }
    }
    return g_getSkater(skate, index);
}

bool InstallSkaterIndexHook()
{
    const uint8_t prologue[] = {0x8B, 0x41, 0x34, 0x83, 0xC1, 0x28}; // mov eax,[ecx+34h]; add ecx,28h
    if (!patch::Matches(0x005665C0, prologue, sizeof(prologue)))
        return false;
    g_getSkater = reinterpret_cast<GetSkaterFn>(
        patch::Detour(0x005665C0, sizeof(prologue), reinterpret_cast<void*>(&GetSkaterHook)));
    return true;
}

// --- the friend's skater leaving -----------------------------------------------------------------------------------

// Skate::RemoveSkater 0x00565790 (thiscall, the skater): when the friend leaves (or the host is gone) the game removes
// their skater at once. With the story's peds around that crashed the game a frame later (the level's skater peds,
// e.g. Barcelona's TRG_BA_Ped_Skater02/03, in an animation). In co-op the skater is hidden and frozen instead, and
// removed with the level's other objects at its Cleanup.
using RemoveSkaterFn = void(__thiscall*)(void*, void*);
RemoveSkaterFn g_removeSkater = nullptr;

void __fastcall RemoveSkaterHook(void* skate, void*, void* skater)
{
    if (skater && g_role == Role::Host && !g_inCleanup && !g_leavingServer && skater != game::LocalSkater() &&
        !g_deferredSkater) {
        auto* o = static_cast<uint8_t*>(skater);
        *reinterpret_cast<uint32_t*>(o + 0x104) |= 1; // COMPOSITE_FLAG_PAUSED
        reinterpret_cast<void(__thiscall*)(void*, bool)>(0x0045D990)(skater, true); // Hide
        g_deferredSkater = skater;
        Log("friend's skater %u: hidden, removed at the level's cleanup", *reinterpret_cast<uint32_t*>(o + 0x14));
        return;
    }
    g_removeSkater(skate, skater);
}

void RemoveDeferredSkater()
{
    if (!g_deferredSkater)
        return;
    void* skater = g_deferredSkater;
    g_deferredSkater = nullptr;
    *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(skater) + 0x104) &= ~1u;
    g_removeSkater(game::Deref(game::kSkateModule), skater);
    Log("friend's skater removed");
}

// LeaveServer (table 0x00680148, cfunc 0x005A08D0): this game leaves its network session - every skater goes now,
// the friend's put-off one first.
bool __cdecl LeaveServerHook(game::CStruct* params, game::CScript* script)
{
    if (g_role == Role::Client && g_selfJoined) {
        // The friend quits (pause > Quit: quit_network_game > chosen_leave_server): out of co-op now, before its
        // SetNetworkMode 0, which co-op would have kept LAN - the main menu then never finished loading.
        Log("client: leaving the host's game");
        g_role = Role::None;
        g_selfJoined = false;
        link::Close();
        game::SpawnScript("coop_client_quit_unpause"); // (the pause menu really paused the game in co-op)
    }
    g_leavingServer = true;
    RemoveDeferredSkater();
    bool result = reinterpret_cast<game::CFunc>(0x005A08D0)(params, script);
    g_leavingServer = false;
    return result;
}

bool InstallRemoveSkaterHook()
{
    const uint8_t prologue[] = {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x0D, 0xB8, 0x0F, 0x6F, 0x00, 0x6A, 0xFF};
    if (!patch::Matches(0x00565790, prologue, sizeof(prologue)))
        return false;
    g_removeSkater = reinterpret_cast<RemoveSkaterFn>(
        patch::Detour(0x00565790, sizeof(prologue), reinterpret_cast<void*>(&RemoveSkaterHook)));
    if (*reinterpret_cast<uint32_t*>(0x00680148 + 4) == 0x005A08D0)
        patch::WriteU32(0x00680148 + 4, reinterpret_cast<uint32_t>(&LeaveServerHook));
    else
        Log("leave server hook: unexpected table entry");
    return true;
}

// --- connection watch ---------------------------------------------------------------------------------------------

// GameNet::Manager::ConnectionTask 0x0053EB70 drops a player (host) or leaves the game (client) after some seconds of
// silence. Cutscenes are that silent. While the co-op link is up it answers for the connection instead.
using TaskFn = void(__cdecl*)(void*);
TaskFn g_connectionTask = nullptr;

void __cdecl ConnectionTaskHook(void* task)
{
    if (g_role != Role::None && link::Connected() && (g_clientJoined || g_selfJoined))
        return;
    g_connectionTask(task);
}

bool InstallConnectionWatch()
{
    const uint8_t prologue[] = {0x6A, 0xFF, 0x68, 0x00, 0xD0, 0x63, 0x00}; // push -1; push 0x0063D000
    if (!patch::Matches(0x0053EB70, prologue, sizeof(prologue)))
        return false;
    g_connectionTask = reinterpret_cast<TaskFn>(
        patch::Detour(0x0053EB70, sizeof(prologue), reinterpret_cast<void*>(&ConnectionTaskHook)));
    return true;
}

// --- lost host --------------------------------------------------------------------------------------------------

// Net::Client::EnqueueMessageToServer 0x00459E50 (thiscall, one argument): after the host's game is gone the
// client's skater still sent its sounds and states through a client that no longer exists (null). Dropped then.
using EnqueueFn = void(__thiscall*)(void*, void*);
EnqueueFn g_enqueueToServer = nullptr;
void __fastcall EnqueueToServerHook(void* client, void*, void* desc)
{
    if (!client)
        return;
    g_enqueueToServer(client, desc);
}

bool InstallEnqueueGuard()
{
    const uint8_t prologue[] = {0x6A, 0xFF, 0x68, 0x78, 0x82, 0x63, 0x00}; // push -1; push 0x00638278
    if (!patch::Matches(0x00459E50, prologue, sizeof(prologue)))
        return false;
    g_enqueueToServer = reinterpret_cast<EnqueueFn>(
        patch::Detour(0x00459E50, sizeof(prologue), reinterpret_cast<void*>(&EnqueueToServerHook)));
    return true;
}

// --- cutscene stand-in guards -------------------------------------------------------------------------------------

// A cutscene object can stand in for a level object (found by name with Obj::FindObject 0x00460AD0). On the client
// that object can be missing, and CCutsceneData::UpdateStandIn / DestroyObjects used the null result. The guards
// skip the stand-in work when it's missing.
struct Stub {
    uint8_t* code;
    size_t size = 0;
    void Byte(uint8_t b) { code[size++] = b; }
    void Bytes(std::initializer_list<uint8_t> list) { for (uint8_t b : list) Byte(b); }
    void Rel32(uintptr_t target)
    {
        int32_t rel = static_cast<int32_t>(target - (reinterpret_cast<uintptr_t>(code) + size + 4));
        std::memcpy(code + size, &rel, 4);
        size += 4;
    }
    void Jmp(uintptr_t target) { Byte(0xE9); Rel32(target); }
};

// At `site`: mov edi,eax (8B F8); push 0x286A8D26 (68 26 8D 6A 28) - replaced by a jump to a stub that goes on at
// site + 7 when eax is an object, else runs `missing` (bytes) and jumps to `missingTarget`.
bool GuardFoundObject(uintptr_t site, std::initializer_list<uint8_t> missing, uintptr_t missingTarget)
{
    const uint8_t expected[] = {0x8B, 0xF8, 0x68, 0x26, 0x8D, 0x6A, 0x28};
    if (!patch::Matches(site, expected, sizeof(expected)))
        return false;
    Stub s{static_cast<uint8_t*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE))};
    s.Bytes({0x85, 0xC0});        // test eax, eax
    s.Bytes({0x74, 0x0C});        // jz +12 (missing)
    s.Bytes({0x8B, 0xF8});        // mov edi, eax
    s.Bytes({0x68, 0x26, 0x8D, 0x6A, 0x28}); // push "model"
    s.Jmp(site + 7);
    s.Bytes(missing);
    s.Jmp(missingTarget);
    patch::Jump(site, s.code);
    const uint8_t nops[2] = {0x90, 0x90};
    patch::Write(site + 5, nops, 2);
    return true;
}

bool InstallStandInGuards()
{
    return GuardFoundObject(0x00465AFF, {0x5D, 0x5B}, 0x00465BD0) && // UpdateStandIn: pop ebp; pop ebx; to the end
           GuardFoundObject(0x00463CBD, {}, 0x00463D3C);                // DestroyObjects: on to deleting the stand-in
}

// --- widescreen -----------------------------------------------------------------------------------------------

// From PARTYMOD for THUG2 (https://github.com/PARTYMANX/partymod-thug2, src/gfx.c, MIT License, Copyright (c) 2024
// PARTYMANX; see THIRD_PARTY_NOTICES.txt): the same addresses (THUG2PM.exe has this exe's layout). On a wider screen than 4:3 the 3D view widens
// (the screen angle factor) instead of stretching; the main menu stays 4:3 with black bars at the sides, since its
// wheel's labels are placed for 4:3 and drifted off their slices when stretched; screen flashes cover the screen.
constexpr uintptr_t kLetterboxActive = 0x00786CBE;
float& ConvX() { return *reinterpret_cast<float*>(0x00786D80); }
float& ConvY() { return *reinterpret_cast<float*>(0x00786D84); }
uint32_t& ConvXOffset() { return *reinterpret_cast<uint32_t*>(0x00786D88); }
uint32_t& ConvYOffset() { return *reinterpret_cast<uint32_t*>(0x00786D8C); }
uint32_t BackBufferWidth() { return *reinterpret_cast<uint32_t*>(0x00786D6C); }
uint32_t BackBufferHeight() { return *reinterpret_cast<uint32_t*>(0x00786D70); }

float DesiredAspect()
{
    if (g_inMainMenu || !BackBufferHeight())
        return 4.0f / 3.0f;
    return static_cast<float>(BackBufferWidth()) / static_cast<float>(BackBufferHeight());
}

// 0x004A07C0: the 3D view's horizontal factor (the game's own value, 1.0 unless changed).
float __cdecl ScreenAngleFactor()
{
    float aspect = DesiredAspect();
    *reinterpret_cast<float*>(0x00701340) = aspect; // the screen's aspect ratio
    float own = *reinterpret_cast<float*>(0x0067E544);
    if (own != 1.0f)
        return own;
    return aspect > 4.0f / 3.0f ? aspect / (4.0f / 3.0f) : 1.0f; // (narrower: the vertical view grows instead)
}

// 0x004A0780: the aspect ratio set by the game.
void __cdecl SetAspectRatio(float) { *reinterpret_cast<float*>(0x00701340) = DesiredAspect(); }

// 0x004B2440: the 2D screen's scale and offsets for the desired aspect (bars where the screen is wider or taller).
void __cdecl SetLetterbox(int letterboxed)
{
    float backBuffer = static_cast<float>(BackBufferWidth()) / static_cast<float>(BackBufferHeight());
    float desired = DesiredAspect();
    if (backBuffer > desired) {
        ConvX() = (desired / backBuffer) * (BackBufferWidth() / 640.0f);
        ConvY() = BackBufferHeight() / 480.0f;
        uint32_t width = static_cast<uint32_t>(ConvX() * 640);
        ConvXOffset() = (BackBufferWidth() - width) / 2;
        ConvYOffset() = static_cast<uint32_t>(16 * ConvY());
    } else {
        ConvX() = BackBufferWidth() / 640.0f;
        ConvY() = (backBuffer / desired) * (BackBufferHeight() / 480.0f);
        uint32_t height = static_cast<uint32_t>(ConvY() * 480);
        ConvXOffset() = 0;
        ConvYOffset() = static_cast<uint32_t>(16 * ConvY()) + (BackBufferHeight() - height) / 2;
    }
    *reinterpret_cast<uint8_t*>(kLetterboxActive) = static_cast<uint8_t>(letterboxed);
}

// In the 3D and 2D viewport setup (letterboxed): the viewport is the desired aspect's region, centred.
void __cdecl SetDisplayRegion()
{
    auto* display = reinterpret_cast<uint32_t*>(0x00786C90); // x, y, width, height
    uint32_t width = static_cast<uint32_t>(ConvX() * 640), height = static_cast<uint32_t>(ConvY() * 480);
    display[0] = (BackBufferWidth() - width) / 2;
    display[2] = width;
    display[1] = (BackBufferHeight() - height) / 2;
    display[3] = height;
}

// The main loop's world render (0x0048C330): letterboxed (4:3) in the main menu only. (Left on, split screen drew
// both views on top of each other.)
void __cdecl RenderWorldWrapper()
{
    reinterpret_cast<void(__cdecl*)()>(0x0048C330)();
    uint8_t want = g_inMainMenu ? 1 : 0;
    if (*reinterpret_cast<uint8_t*>(kLetterboxActive) != want)
        SetLetterbox(want);
}

struct BarVertex {
    float x, y, z, w;
    uint32_t color;
    float u, v;
};

void DrawBar(float x0, float y0, float x1, float y1)
{
    BarVertex bar[4] = {{x0 - 0.5f, y0 - 0.5f, 0, 1, 0xFF000000, 0, 0},
                        {x0 - 0.5f, y1 + 0.5f, 0, 1, 0xFF000000, 0, 0},
                        {x1 + 0.5f, y1 + 0.5f, 0, 1, 0xFF000000, 0, 0},
                        {x1 + 0.5f, y0 - 0.5f, 0, 1, 0xFF000000, 0, 0}};
    void* device = *reinterpret_cast<void**>(0x007D69E0);
    using DrawPrimitiveUP = long(__stdcall*)(void*, int, unsigned, const void*, unsigned);
    reinterpret_cast<DrawPrimitiveUP>((*reinterpret_cast<void***>(device))[83])(device, 6, 2, bar, sizeof(BarVertex)); // fan
}

// After the 2D draw (0x004DFB90): black bars over what the 3D drew outside the desired aspect.
void __cdecl Draw2DWrapper()
{
    reinterpret_cast<void(__cdecl*)()>(0x004DFB90)();
    float w = static_cast<float>(BackBufferWidth()), h = static_cast<float>(BackBufferHeight());
    if (!h)
        return;
    float desired = DesiredAspect();
    if (w / h > desired) {
        float width = std::round(ConvX() * 640.0f), bar = std::floor((w - width) / 2);
        reinterpret_cast<void(__cdecl*)(int, int, int)>(0x004DA730)(0, 0, 0); // no texture
        DrawBar(0, 0, bar, h);
        DrawBar(bar + width, 0, w, h);
    } else if (w / h < desired) {
        float height = std::round(ConvY() * 480.0f), bar = std::floor((h - height) / 2);
        reinterpret_cast<void(__cdecl*)(int, int, int)>(0x004DA730)(0, 0, 0);
        DrawBar(0, 0, w, bar);
        DrawBar(0, bar + height, w, h);
    }
}

// A screen flash's vertices come in the wrong order (it covered part of the screen): reordered before the draw.
void __fastcall FlashDraw(void*, void** vtable, void* device, int primitive, unsigned count, BarVertex* vertices, unsigned stride)
{
    BarVertex first = vertices[0];
    vertices[0] = vertices[1];
    vertices[1] = vertices[2];
    vertices[2] = first;
    using DrawPrimitiveUP = long(__stdcall*)(void*, int, unsigned, const void*, unsigned);
    reinterpret_cast<DrawPrimitiveUP>(vtable[83])(device, primitive, count, vertices, stride);
}

void ApplyWidescreen()
{
    const uint8_t aspect[] = {0x8B, 0x44, 0x24, 0x04, 0xA3, 0x40, 0x13, 0x70, 0x00, 0xC3};
    const uint8_t angle[] = {0xD9, 0x05, 0x44, 0xE5, 0x67, 0x00, 0xC3};
    const uint8_t letterbox[] = {0x8A, 0x44, 0x24, 0x04, 0x84, 0xC0, 0xA0, 0xBE, 0x6C, 0x78, 0x00};
    const uint8_t fixJe[] = {0x74, 0x5E};
    const uint8_t view3d[] = {0x74, 0x3A, 0x8B, 0x0D, 0x70, 0x6D, 0x78, 0x00};
    const uint8_t view2d[] = {0x74, 0x23, 0x8B, 0xC1, 0x99};
    const uint8_t flash[] = {0xFF, 0x92, 0x4C, 0x01, 0x00, 0x00};
    const uint8_t call[] = {0xE8};
    if (!patch::Matches(0x004A0780, aspect, sizeof(aspect)) || !patch::Matches(0x004A07C0, angle, sizeof(angle)) ||
        !patch::Matches(0x004B2440, letterbox, sizeof(letterbox)) || !patch::Matches(0x004DF63A, fixJe, 2) ||
        !patch::Matches(0x004B2E63, view3d, sizeof(view3d)) || !patch::Matches(0x004B33C8, view2d, sizeof(view2d)) ||
        !patch::Matches(0x004B91E7, flash, sizeof(flash)) || !patch::Matches(0x0044F1C0, call, 1) ||
        !patch::Matches(0x004B341D, call, 1)) {
        Log("widescreen: unexpected bytes, left as it was");
        return;
    }
    patch::Jump(0x004A0780, reinterpret_cast<void*>(&SetAspectRatio));
    patch::Jump(0x004A07C0, reinterpret_cast<void*>(&ScreenAngleFactor));
    patch::Jump(0x004B2440, reinterpret_cast<void*>(&SetLetterbox));
    const uint8_t jmp = 0xEB; // the letterboxed display's fix
    patch::Write(0x004DF63A, &jmp, 1);
    auto callAt = [](uintptr_t site, size_t size, void* target) { // `call target`, the rest of the bytes NOPs
        std::vector<uint8_t> code(size, 0x90);
        code[0] = 0xE8;
        int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - (site + 5));
        std::memcpy(&code[1], &rel, 4);
        patch::Write(site, code.data(), size);
    };
    callAt(0x004B2E65, 58, reinterpret_cast<void*>(&SetDisplayRegion));
    callAt(0x004B33CA, 35, reinterpret_cast<void*>(&SetDisplayRegion));
    callAt(0x004B91E7, 6, reinterpret_cast<void*>(&FlashDraw));
    patch::RetargetCall(0x0044F1C0, reinterpret_cast<void*>(&RenderWorldWrapper));
    patch::RetargetCall(0x004B341D, reinterpret_cast<void*>(&Draw2DWrapper));
    Log("widescreen: on (main menu 4:3)");
}

// --- install ---------------------------------------------------------------------------------------------------

// Two copies on one PC read the same archives at once; the game opens them without sharing, so one of them failed
// (the disc error screen) when both loaded a level together. With multi_instance, reads share.
using CreateFileFn = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
CreateFileFn g_createFile = nullptr;
HANDLE WINAPI SharedCreateFile(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disposition,
                               DWORD flags, HANDLE templ)
{
    if (!(access & GENERIC_WRITE))
        share |= FILE_SHARE_READ;
    return g_createFile(name, access, share, sa, disposition, flags, templ);
}

// [display] resolution: the device setup (0x004D870B) picks one of 4 modes (1280x1024 ... 640x480) by
// Launcher.exe's setting; it always takes the last one here, and that one gets the resolution from coop.ini.
// A wider picture than 1920 crashed the game half a second after it started (the same with 2560x1080 as 3840x2160):
// 0x004B6B80 fills a table at 0x00701688 (12-byte entries, room for 1280 up to 0x00705288) with one entry per
// some fraction of the screen's width squared, and the table ran over the globals after it (the loading screen's
// colours, its thread's event, the object deleted at 0x004B6FA7). Nothing reads the table, so the count (the second
// float-to-int at 0x004B6C1B) is capped at 1280.
void CapWideScreenTable()
{
    const uint8_t call[] = {0xE8};
    if (!patch::Matches(0x004B6C1B, call, 1)) {
        Log("resolution: unexpected bytes at the table's count");
        return;
    }
    auto* stub = static_cast<uint8_t*>(VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    uint8_t code[] = {0xE8, 0, 0, 0, 0,             // call the float-to-int (0x006236FC)
                      0x3D, 0x00, 0x05, 0x00, 0x00, // cmp eax, 1280
                      0x7E, 0x05,                   // jle done
                      0xB8, 0x00, 0x05, 0x00, 0x00, // mov eax, 1280
                      0xC3};                        // done: ret
    int32_t rel = static_cast<int32_t>(0x006236FC - (reinterpret_cast<uintptr_t>(stub) + 5));
    std::memcpy(code + 1, &rel, 4);
    std::memcpy(stub, code, sizeof(code));
    if (patch::RetargetCall(0x004B6C1B, stub) != 0x006236FC)
        Log("resolution: unexpected call at the table's count");
}

void ApplyResolution()
{
    if (!g_settings.screenWidth)
        return;
    CapWideScreenTable();
    const uint8_t select[] = {0xBE, 0x03, 0x00, 0x00, 0x00, 0x2B, 0xF2};        // mov esi,3; sub esi,[setting]
    const uint8_t width[] = {0xC7, 0x44, 0x24, 0x38, 0x80, 0x02, 0x00, 0x00};  // mov [esp+38h],640
    const uint8_t height[] = {0xC7, 0x44, 0x24, 0x28, 0xE0, 0x01, 0x00, 0x00}; // mov [esp+28h],480
    if (!patch::Matches(0x004D870B, select, sizeof(select)) || !patch::Matches(0x004D8772, width, sizeof(width)) ||
        !patch::Matches(0x004D878E, height, sizeof(height))) {
        Log("resolution: unexpected bytes, Launcher.exe's is used");
        return;
    }
    const uint8_t nops[2] = {0x90, 0x90};
    patch::Write(0x004D8710, nops, 2);
    patch::WriteU32(0x004D8776, static_cast<uint32_t>(g_settings.screenWidth));
    patch::WriteU32(0x004D8792, static_cast<uint32_t>(g_settings.screenHeight));
    Log("resolution %dx%d", g_settings.screenWidth, g_settings.screenHeight);
}

// In a window (windowed or borderless) the game keeps running when another window is in front, and isn't minimised:
// on WM_ACTIVATE (0x004E1705) it minimised itself when it lost focus, flagged its Direct3D device to be reset, and
// maximised the window back when it got focus again. In a window that lost the window (it was gone after Alt+Tab),
// and each reset dropped OBS's game capture. Now WM_ACTIVATE only keeps the game active and goes on to
// DefWindowProc as the other messages do (0x004E17E1 with the message in eax).
void PatchActivateForWindow()
{
    const uint8_t handler[] = {0x8B, 0x44, 0x24, 0x0C, 0x25, 0xFF, 0xFF, 0x00, 0x00};
    if (!patch::Matches(0x004E1705, handler, sizeof(handler))) {
        Log("display: unexpected WM_ACTIVATE handler");
        return;
    }
    uint8_t code[] = {0xC6, 0x05, 0xE4, 0xCB, 0x7C, 0x00, 0x01, // mov byte [0x007CCBE4], 1 (active)
                      0xB8, 0x06, 0x00, 0x00, 0x00,             // mov eax, WM_ACTIVATE
                      0xE9, 0, 0, 0, 0};                        // jmp 0x004E17E1 (DefWindowProc)
    int32_t rel = static_cast<int32_t>(0x004E17E1 - (0x004E1705 + sizeof(code)));
    std::memcpy(code + 13, &rel, 4);
    patch::Write(0x004E1705, code, sizeof(code));
}

// After the device is made, SetWindowPos(0, 0, the back buffer's size) would put a window at the screen's corner at
// the back buffer's size: in a window it stays as made (as PartyMod does).
void KeepWindowAsMade()
{
    const uint8_t moveWindow[] = {0x6A, 0x04, 0x50, 0x51, 0x53, 0x53, 0x53, 0x52, 0xFF, 0x15, 0x20, 0x52, 0x64, 0x00};
    if (patch::Matches(0x004D8891, moveWindow, sizeof(moveWindow))) {
        uint8_t nops[sizeof(moveWindow)];
        std::memset(nops, 0x90, sizeof(nops));
        patch::Write(0x004D8891, nops, sizeof(nops));
    }
}

// [display] mode=borderless: the game's own full-screen window (a popup over the whole primary screen, 0x005F595F),
// not topmost, with Direct3D in windowed mode: the picture (the resolution's size) is stretched over it. Alt+Tab,
// OBS's window or game capture, Discord and overlays work as with any window; no display mode change.
void ApplyBorderless()
{
    const uint8_t present[] = {0x89, 0x1D};
    const uint8_t topmost[] = {0xC7, 0x44, 0x24, 0x70, 0x08, 0x00, 0x00, 0x00}; // mov [esp+70h], WS_EX_TOPMOST
    if (!patch::Matches(0x004D871F, present, 2) || !patch::Matches(0x005F596D, topmost, sizeof(topmost))) {
        Log("display: unexpected bytes, full screen");
        return;
    }
    const uint8_t windowedFlag = 0x05; // mov [pp.Windowed], eax (= 1)
    patch::Write(0x004D8720, &windowedFlag, 1);
    const uint8_t noExStyle = 0x00;
    patch::Write(0x005F5971, &noExStyle, 1);
    KeepWindowAsMade();
    PatchActivateForWindow();
    Log("display: borderless %dx%d on a %dx%d screen", g_settings.screenWidth, g_settings.screenHeight,
        GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
}

void ApplyDebugPatches()
{
    if (g_settings.displayMode == 1)
        ApplyBorderless();
    // mode=windowed: a window with a frame (also for running two copies on one PC).
    if (g_settings.windowed) {
        const uint8_t present[] = {0x89, 0x1D};
        const uint8_t style[] = {0x0F, 0x85};
        if (patch::Matches(0x004D871F, present, 2) && patch::Matches(0x005F58D3, style, 2)) {
            const uint8_t windowedFlag = 0x05; // mov [pp.Windowed], eax (= 1)
            patch::Write(0x004D8720, &windowedFlag, 1);
            const uint8_t nops[6] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90}; // bordered window, not full screen
            patch::Write(0x005F58D3, nops, 6);
            Log("display: windowed");
        }
        // The window (made 640x480 at 100,100 in WinMain): the resolution's size, centred; scaled down to fit the
        // screen's work area if it doesn't (the picture keeps its resolution, Direct3D stretches it to the window).
        const uint8_t makeWindow[] = {0x68, 0xE0, 0x01, 0x00, 0x00, 0x68, 0x80, 0x02, 0x00, 0x00, 0x6A, 0x64, 0x6A, 0x64};
        if (g_settings.screenWidth && patch::Matches(0x004E274C, makeWindow, sizeof(makeWindow))) {
            int width = g_settings.screenWidth, height = g_settings.screenHeight;
            RECT area = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
            SystemParametersInfoA(SPI_GETWORKAREA, 0, &area, 0);
            RECT frame = {0, 0, width, height};
            AdjustWindowRect(&frame, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE);
            const int roomX = (area.right - area.left) - (frame.right - frame.left - width);
            const int roomY = (area.bottom - area.top) - (frame.bottom - frame.top - height);
            if (width > roomX || height > roomY) {
                const double scale = std::min(static_cast<double>(roomX) / width, static_cast<double>(roomY) / height);
                width = static_cast<int>(width * scale);
                height = static_cast<int>(height * scale);
            }
            patch::WriteU32(0x004E274D, static_cast<uint32_t>(height));
            patch::WriteU32(0x004E2752, static_cast<uint32_t>(width));
            const uint8_t centred[4] = {0x6A, 0xFF, 0x6A, 0xFF}; // x, y -1: centred
            patch::Write(0x004E2756, centred, 4);
            KeepWindowAsMade();
            Log("display: window %dx%d", width, height);
        }
        if (!g_settings.alwaysActive)
            PatchActivateForWindow();
    }
    if (g_settings.multiInstance) {
        const uint8_t je[] = {0x74, 0x2E};
        if (patch::Matches(0x004E270B, je, 2)) {
            const uint8_t jmp = 0xEB; // skip the "THUG2 is running" window check
            patch::Write(0x004E270B, &jmp, 1);
            g_createFile = *reinterpret_cast<CreateFileFn*>(0x006450D0); // the exe's import of CreateFileA
            patch::WriteU32(0x006450D0, reinterpret_cast<uint32_t>(&SharedCreateFile));
            Log("debug: multi instance");
        }
    }
    if (g_settings.alwaysActive) {
        const uint8_t handler[] = {0x8B, 0x44, 0x24, 0x0C};
        if (patch::Matches(0x004E1705, handler, 4)) {
            // WM_ACTIVATE: active flag on, never minimise
            const uint8_t code[] = {0xC6, 0x05, 0xE4, 0xCB, 0x7C, 0x00, 0x01, 0x33, 0xC0, 0xC2, 0x10, 0x00};
            patch::Write(0x004E1705, code, sizeof(code));
            Log("debug: always active");
        }
    }
}

bool Install()
{
    const uint8_t inNetGame[] = {0x8B, 0x0D, 0x14, 0xCE, 0x7C, 0x00, 0xE9};
    const uint8_t inMultiplayer[] = {0x8B, 0x0D, 0x78, 0xE4, 0x7C, 0x00, 0xE9};
    const uint8_t frameCall[] = {0xE8};
    if (!patch::Matches(game::kCFuncInNetGame, inNetGame, sizeof(inNetGame)) ||
        !patch::Matches(game::kCFuncInMultiplayerGame, inMultiplayer, sizeof(inMultiplayer)) ||
        !patch::Matches(game::kMainLoopReadKeyboardCall, frameCall, 1)) {
        Log("unknown exe: co-op disabled");
        return false;
    }
    if (!PatchTable())
        return false;
    patch::Jump(game::kCFuncInNetGame, reinterpret_cast<void*>(&ScriptInNetGame));
    patch::Jump(game::kCFuncInMultiplayerGame, reinterpret_cast<void*>(&ScriptInMultiplayerGame));
    g_readKeyboard = reinterpret_cast<VoidFn>(
        patch::RetargetCall(game::kMainLoopReadKeyboardCall, reinterpret_cast<void*>(&OnFrame)));
    for (uintptr_t site : {game::kOkToJoinGameModeCall, game::kJoinRequestGameModeCall}) {
        if (patch::RetargetCall(site, reinterpret_cast<void*>(&GameModeNameForJoin)) != game::kGameModeName)
            Log("join hook at %08X: unexpected old target", static_cast<unsigned>(site));
    }
    if (patch::RetargetCall(game::kCleanupDeleteSkaterHeapsCall, reinterpret_cast<void*>(&CleanupDeleteExtraSkaterHeaps)) !=
        game::kDeleteExtraSkaterHeaps)
        Log("heap hook: unexpected old target");
    for (uintptr_t site : kContentInNetGameCalls) {
        if (patch::RetargetCall(site, reinterpret_cast<void*>(&ContentInNetGame)) != game::kManagerInNetGame)
            Log("content hook at %08X: unexpected old target", static_cast<unsigned>(site));
    }
    for (uintptr_t site : kSkaterRestartNodeCalls) {
        if (patch::RetargetCall(site, reinterpret_cast<void*>(&SkaterRestartNode)) != kSkaterRestartNode)
            Log("restart hook at %08X: unexpected old target", static_cast<unsigned>(site));
    }
    if (reinterpret_cast<uintptr_t>(g_readKeyboard) != game::kReadKeyboard)
        Log("frame hook: unexpected old target %p", reinterpret_cast<void*>(g_readKeyboard));
    if (g_settings.logPrintf && !InstallLoadPreLog())
        Log("pre log: unexpected bytes");
    if (!InstallWinGoalHook())
        Log("goal hook: unexpected bytes");
    if (*reinterpret_cast<uint32_t*>(game::kPlayCutsceneEntry + 4) == game::kCFuncPlayCutscene)
        patch::WriteU32(game::kPlayCutsceneEntry + 4, reinterpret_cast<uint32_t>(&PlayCutsceneHook));
    else
        Log("cutscene hook: unexpected table entry");
    if (*reinterpret_cast<uint32_t*>(game::kSetNetworkModeEntry + 4) == game::kCFuncSetNetworkMode)
        patch::WriteU32(game::kSetNetworkModeEntry + 4, reinterpret_cast<uint32_t>(&SetNetworkModeHook));
    else
        Log("network mode hook: unexpected table entry");
    if (*reinterpret_cast<uint32_t*>(0x0067F5C8 + 4) == 0x0059FE50)
        patch::WriteU32(0x0067F5C8 + 4, reinterpret_cast<uint32_t>(&CleanupHook));
    else
        Log("cleanup hook: unexpected table entry");
    if (*reinterpret_cast<uint32_t*>(game::kLaunchLevelEntry + 4) == game::kCFuncLaunchLevel)
        patch::WriteU32(game::kLaunchLevelEntry + 4, reinterpret_cast<uint32_t>(&LaunchLevelHook));
    else
        Log("launch hook: unexpected table entry");
    if (*reinterpret_cast<uint32_t*>(0x0067F860 + 4) == 0x005A97E0 && *reinterpret_cast<uint32_t*>(0x0067F868 + 4) == 0x005A8730) {
        patch::WriteU32(0x0067F860 + 4, reinterpret_cast<uint32_t>(&SaveToMemoryCardHook));
        patch::WriteU32(0x0067F868 + 4, reinterpret_cast<uint32_t>(&LoadFromMemoryCardHook));
        if (patch::RetargetCall(kSaveGoalParamsCall, reinterpret_cast<void*>(&GoalParamsForSave)) != 0x004A2BA0)
            Log("save goal params hook: unexpected old target");
    } else {
        Log("save hooks: unexpected table entries");
    }
    if (*reinterpret_cast<uint32_t*>(0x0067FE50 + 4) == 0x005ADD70)
        patch::WriteU32(0x0067FE50 + 4, reinterpret_cast<uint32_t>(&KillSkaterCamAnimHook));
    else
        Log("cam kill hook: unexpected table entry");
    if (*reinterpret_cast<uint32_t*>(kPlaySkaterCamAnimEntry + 4) == kCFuncPlaySkaterCamAnim)
        patch::WriteU32(kPlaySkaterCamAnimEntry + 4, reinterpret_cast<uint32_t>(&PlaySkaterCamAnimHook));
    else
        Log("cam anim hook: unexpected table entry");
    if (*reinterpret_cast<uint32_t*>(kSwapObjectModelsEntry + 4) == kCFuncSwapObjectModels)
        patch::WriteU32(kSwapObjectModelsEntry + 4, reinterpret_cast<uint32_t>(&SwapObjectModelsHook));
    else
        Log("swap hook: unexpected table entry");
    if (patch::RetargetCall(kUpdateEndMoviesCall, reinterpret_cast<void*>(&UpdateEndMoviesHook)) != kEndMovies)
        Log("cutscene end hook: unexpected old target");
    if (!InstallSkaterIndexHook())
        Log("skater index hook: unexpected bytes");
    if (!InstallNameHook())
        Log("name hook: unexpected bytes");
    if (!InstallKeyInputHook())
        Log("key input hook: unexpected bytes");
    if (!InstallPlayerCollisionHook())
        Log("player collision hook: unexpected bytes");
    if (!InstallEnqueueGuard())
        Log("enqueue guard: unexpected bytes");
    if (!InstallRemoveSkaterHook())
        Log("remove skater hook: unexpected bytes");
    if (!InstallConnectionWatch())
        Log("connection watch: unexpected bytes");
    if (!InstallStandInGuards())
        Log("stand-in guards: unexpected bytes");
    if (!InstallComponentGuard())
        Log("component guard: unexpected bytes");
    if (g_settings.name[0]) // the player's network name (as -NAME on the command line): shown over their skater
        std::memcpy(reinterpret_cast<void*>(0x007CCBC4), g_settings.name, sizeof(g_settings.name));
    if (g_settings.gamepad && !gamepad::Install(&Log))
        Log("gamepad: unexpected bytes, no XInput pads");
    ApplyResolution();
    ApplyWidescreen();
    ApplyDebugPatches();
    Log("installed (host %s:%d; keys host %X join %X teleport %X)", g_settings.hostAddress, g_settings.port,
        g_settings.hostKey, g_settings.joinKey, g_settings.teleportKey);
    return true;
}

// --- crash report ------------------------------------------------------------------------------------------------

// Logs faults (address, registers, the game's return addresses on the stack); the game then crashes as before.
LONG CALLBACK OnException(EXCEPTION_POINTERS* info)
{
    static int reported = 0;
    static bool busy = false; // IsBadReadPtr below faults on purpose; don't report those
    if (busy)
        return EXCEPTION_CONTINUE_SEARCH;
    DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_STACK_OVERFLOW)
        return EXCEPTION_CONTINUE_SEARCH;
    if (reported++ >= 3)
        return EXCEPTION_CONTINUE_SEARCH;
    const CONTEXT* c = info->ContextRecord;
    Log("CRASH %08lX at %08lX (access %lu %08lX) eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX esi=%08lX edi=%08lX "
        "ebp=%08lX esp=%08lX",
        code, c->Eip, info->ExceptionRecord->NumberParameters > 0 ? info->ExceptionRecord->ExceptionInformation[0] : 0,
        info->ExceptionRecord->NumberParameters > 1 ? info->ExceptionRecord->ExceptionInformation[1] : 0, c->Eax,
        c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    char line[512] = "";
    int n = 0, found = 0;
    const auto* stack = reinterpret_cast<const uint32_t*>(c->Esp);
    for (int i = 0; i < 1024 && found < 24; ++i) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(stack + i, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
            break;
        uint32_t v = stack[i];
        if (v >= 0x00401000 && v < 0x00645000) {
            n += std::snprintf(line + n, sizeof(line) - n, " %08X", v);
            ++found;
        }
    }
    Log("CRASH stack:%s", line);
    // A CScript's running script name is at +0xC8: try the registers that may hold the current script.
    busy = true;
    const DWORD candidates[] = {c->Edi, c->Esi, c->Ebx, c->Ebp, c->Ecx};
    for (DWORD r : candidates) {
        if (r > 0x10000 && !IsBadReadPtr(reinterpret_cast<void*>(r + 0xC8), 4))
            Log("CRASH reg %08lX +C8 = %08X +14 = %08X", r, *reinterpret_cast<const uint32_t*>(r + 0xC8),
                *reinterpret_cast<const uint32_t*>(r + 0x14));
    }
    // objects on the stack: a vtable in .rdata at +0 -> their id (+0x14) and vtable
    char objs[512] = "";
    int on = 0, count = 0;
    for (int i = 0; i < 160 && count < 10; ++i) {
        uint32_t v = 0;
        if (IsBadReadPtr(stack + i, 4))
            break;
        v = stack[i];
        if (v < 0x00800000 || IsBadReadPtr(reinterpret_cast<void*>(v), 0x18))
            continue;
        uint32_t vt = *reinterpret_cast<uint32_t*>(v);
        if (vt >= 0x00645000 && vt < 0x00690000) {
            on += std::snprintf(objs + on, sizeof(objs) - on, " %08X(vt %08X id %08X)", v, vt, *reinterpret_cast<uint32_t*>(v + 0x14));
            ++count;
        }
    }
    Log("CRASH objects:%s", objs);
    busy = false;
    return EXCEPTION_CONTINUE_SEARCH;
}

// --- dinput8 proxy -----------------------------------------------------------------------------------------------

HMODULE g_realDinput = nullptr;

} // namespace

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE instance, DWORD version, REFIID iid, LPVOID* out,
                                              LPUNKNOWN outer)
{
    if (!g_realDinput) {
        char path[MAX_PATH];
        GetSystemDirectoryA(path, MAX_PATH);
        std::strcat(path, "\\dinput8.dll");
        g_realDinput = LoadLibraryA(path);
    }
    using Fn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    auto real = reinterpret_cast<Fn>(GetProcAddress(g_realDinput, "DirectInput8Create"));
    HRESULT result = real ? real(instance, version, iid, out, outer) : E_FAIL;
    if (SUCCEEDED(result) && out && g_settings.automation)
        automation::WrapDirectInput(*out);
    if (SUCCEEDED(result) && out && g_settings.gamepad)
        gamepad::WrapDirectInput(*out);
    return result;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        // coop.log grows across sessions; past 4 MB it becomes coop.prev.log (one old one kept) and a new one starts.
        WIN32_FILE_ATTRIBUTE_DATA info;
        if (GetFileAttributesExA("coop.log", GetFileExInfoStandard, &info) && (info.nFileSizeHigh || info.nFileSizeLow > 4u << 20))
            MoveFileExA("coop.log", "coop.prev.log", MOVEFILE_REPLACE_EXISTING);
        g_log = std::fopen("coop.log", "a");
        SetProcessDPIAware(); // real pixels on a scaled desktop (125%, 150%): the resolution, the window's size
        SYSTEMTIME t;
        GetLocalTime(&t);
        if (g_log)
            std::fprintf(g_log, "\n=== %04d-%02d-%02d game started ===\n", t.wYear, t.wMonth, t.wDay);
        LoadSettings();
        AddVectoredExceptionHandler(0, &OnException);
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_mainThread,
                        THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
        if (Install() && g_mainThread)
            CloseHandle(CreateThread(nullptr, 0, &HangWatch, nullptr, 0, nullptr));
    }
    return TRUE;
}
