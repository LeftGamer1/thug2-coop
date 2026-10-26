#include "link.h"

#include <cstring>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>

namespace link {
namespace {

SOCKET g_listen = INVALID_SOCKET;
SOCKET g_peer = INVALID_SOCKET;
std::vector<uint8_t> g_in;
std::vector<uint8_t> g_out;

bool Startup()
{
    static bool started = false;
    if (!started) {
        WSADATA data;
        started = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    return started;
}

void NonBlocking(SOCKET s)
{
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
    BOOL noDelay = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
}

void DropPeer()
{
    if (g_peer != INVALID_SOCKET)
        closesocket(g_peer);
    g_peer = INVALID_SOCKET;
    g_in.clear();
    g_out.clear();
}

void Flush()
{
    while (g_peer != INVALID_SOCKET && !g_out.empty()) {
        int sent = send(g_peer, reinterpret_cast<const char*>(g_out.data()), static_cast<int>(g_out.size()), 0);
        if (sent > 0) {
            g_out.erase(g_out.begin(), g_out.begin() + sent);
        } else {
            if (WSAGetLastError() != WSAEWOULDBLOCK)
                DropPeer();
            return;
        }
    }
}

} // namespace

bool Listen(int port)
{
    if (!Startup())
        return false;
    if (g_listen != INVALID_SOCKET)
        return true;
    g_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    BOOL reuse = TRUE;
    setsockopt(g_listen, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<u_short>(port));
    if (bind(g_listen, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(g_listen, 1) != 0) {
        closesocket(g_listen);
        g_listen = INVALID_SOCKET;
        return false;
    }
    NonBlocking(g_listen);
    return true;
}

bool Connect(const char* address, int port, int timeoutMs)
{
    if (!Startup())
        return false;
    DropPeer();
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (getaddrinfo(address, nullptr, &hints, &found) != 0 || !found)
        return false;
    sockaddr_in addr = *reinterpret_cast<sockaddr_in*>(found->ai_addr);
    freeaddrinfo(found);
    addr.sin_port = htons(static_cast<u_short>(port));
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    NonBlocking(s);
    connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    fd_set writable;
    FD_ZERO(&writable);
    FD_SET(s, &writable);
    timeval wait = {timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    int error = 0;
    int length = sizeof(error);
    if (select(0, nullptr, &writable, nullptr, &wait) != 1 ||
        getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) != 0 || error != 0) {
        closesocket(s);
        return false;
    }
    g_peer = s;
    return true;
}

void Close()
{
    DropPeer();
    if (g_listen != INVALID_SOCKET)
        closesocket(g_listen);
    g_listen = INVALID_SOCKET;
}

bool Connected() { return g_peer != INVALID_SOCKET; }
bool Listening() { return g_listen != INVALID_SOCKET; }

void Send(uint32_t type, const void* data, uint32_t size)
{
    if (g_peer == INVALID_SOCKET)
        return;
    const uint32_t header[2] = {type, size};
    const auto* h = reinterpret_cast<const uint8_t*>(header);
    g_out.insert(g_out.end(), h, h + sizeof(header));
    const auto* d = static_cast<const uint8_t*>(data);
    g_out.insert(g_out.end(), d, d + size);
    Flush();
}

void Poll(Handler handler)
{
    if (g_listen != INVALID_SOCKET && g_peer == INVALID_SOCKET) {
        SOCKET s = accept(g_listen, nullptr, nullptr);
        if (s != INVALID_SOCKET) {
            NonBlocking(s);
            g_peer = s; // one friend: further connections wait in the backlog until this one drops
        }
    }
    if (g_peer == INVALID_SOCKET)
        return;
    Flush();
    uint8_t chunk[16384];
    for (;;) {
        int got = recv(g_peer, reinterpret_cast<char*>(chunk), sizeof(chunk), 0);
        if (got > 0) {
            g_in.insert(g_in.end(), chunk, chunk + got);
            continue;
        }
        if (got == 0 || WSAGetLastError() != WSAEWOULDBLOCK)
            DropPeer();
        break;
    }
    size_t at = 0;
    while (g_in.size() - at >= 8) {
        uint32_t header[2];
        std::memcpy(header, g_in.data() + at, sizeof(header));
        if (g_in.size() - at - 8 < header[1])
            break;
        handler(header[0], g_in.data() + at + 8, header[1]);
        at += 8 + header[1];
        if (g_peer == INVALID_SOCKET)
            return;
    }
    g_in.erase(g_in.begin(), g_in.begin() + at);
}

} // namespace link
