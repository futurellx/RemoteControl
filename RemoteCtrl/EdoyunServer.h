#pragma once
#include <MSWSock.h>
#include <WS2tcpip.h>
#include <array>
#include <map>
#include <memory>
#include <thread>
#include "Command.h"

// One outstanding operation per connection. Its storage stays alive until IOCP completes it.
class EdoyunServer {
public:
    EdoyunServer(const std::string& ip = "0.0.0.0", short port = 9527);
    ~EdoyunServer();
    bool StartService();
private:
    enum class Operation { Accept, Receive, Send };
    struct Client {
        OVERLAPPED overlapped = {};
        SOCKET socket = INVALID_SOCKET;
        Operation operation = Operation::Accept;
        std::array<char, 64 * 1024> buffer;
        std::string input, output;
        std::list<CPacket> packets;
        size_t sent = 0;
        ~Client() { if (socket != INVALID_SOCKET) closesocket(socket); }
    };
    bool NewAccept();
    bool Receive(Client& client);
    bool Send(Client& client);
    void Run();
    SOCKET m_socket = INVALID_SOCKET;
    HANDLE m_port = NULL;
    sockaddr_in m_address = {};
    bool m_valid = false, m_wsaStarted = false;
    std::thread m_thread;
    std::map<OVERLAPPED*, std::unique_ptr<Client>> m_clients;
    CCommand m_command;
};
