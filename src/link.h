// The co-op link: one TCP connection between the host's and the client's DLL (port + 1), next to the game's own
// network session. Non-blocking, serviced once a frame on the game's thread. Messages: [u32 type][u32 size][data].
#pragma once
#include <cstdint>

namespace link {

enum Type : uint32_t {
    kHello = 1,  // client -> host: send me your story state
    kCareer = 2, // host -> client: the career (Script::WriteToBuffer of CSkaterCareer::WriteToStructure)
    kGoalWon = 3, // either way: u32 goal name; the other side wins it too
    kSpawn = 4,  // either way: u32 script name checksum, then its params (WriteToBuffer)
    kJoinNow = 5, // host -> client: the host's network game is up (the career was just sent): join it
    kJoined = 6,  // client -> host: JoinServer sent
    kMovieEnded = 7, // either way: u32 cutscene name that just ended (finished or skipped) here
    kSwapModels = 8, // either way: u32 skater object id, u32 other object id (a guest character / pro swap)
    kHostLeft = 9,   // host -> client: the host left the story (back at its main menu)
    kGoalDone = 10,  // either way: u32 goal name; a shared goal done here, waiting for the other player
};

using Handler = void (*)(uint32_t type, const uint8_t* data, uint32_t size);

bool Listen(int port);                                  // host
bool Connect(const char* address, int port, int timeoutMs); // client (blocking up to timeoutMs)
void Close();
bool Connected();
bool Listening();
void Send(uint32_t type, const void* data, uint32_t size);
void Poll(Handler handler); // accepts / reads; calls handler for each whole message

} // namespace link
