#include "pch.h"
#include "EdoyunServer.h"

EdoyunServer::EdoyunServer(const std::string& ip, short port) {
    m_address.sin_family = AF_INET;
    m_address.sin_port = htons(port);
    m_valid = InetPtonA(AF_INET, ip.c_str(), &m_address.sin_addr) == 1;
}

EdoyunServer::~EdoyunServer() {
    if (m_thread.joinable()) {
        PostQueuedCompletionStatus(m_port, 0, 0, NULL);
        m_thread.join();
    }
    if (m_socket != INVALID_SOCKET) closesocket(m_socket);
    if (m_port) CloseHandle(m_port);
    if (m_wsaStarted) WSACleanup();
}

bool EdoyunServer::StartService() {
    if (!m_valid || m_wsaStarted) return false;
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
    m_wsaStarted = true;
    m_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (m_socket == INVALID_SOCKET) return false;
    if (bind(m_socket, reinterpret_cast<sockaddr*>(&m_address), sizeof(m_address)) == SOCKET_ERROR ||
        listen(m_socket, SOMAXCONN) == SOCKET_ERROR) return false;
    m_port = CreateIoCompletionPort(reinterpret_cast<HANDLE>(m_socket), NULL, 0, 1);
    if (!m_port || !NewAccept()) return false;
    m_thread = std::thread(&EdoyunServer::Run, this);
    return true;
}

bool EdoyunServer::NewAccept() {
    auto client = std::make_unique<Client>();
    client->socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (client->socket == INVALID_SOCKET) return false;
    DWORD bytes = 0;
    OVERLAPPED* key = &client->overlapped;
    BOOL ok = AcceptEx(m_socket, client->socket, client->buffer.data(), 0,
        sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16, &bytes, key);
    if (!ok && WSAGetLastError() != ERROR_IO_PENDING) return false;
    m_clients.emplace(key, std::move(client));
    return true;
}

bool EdoyunServer::Receive(Client& client) {
    client.overlapped = {};
    client.operation = Operation::Receive;
    WSABUF buffer = { static_cast<ULONG>(client.buffer.size()), client.buffer.data() };
    DWORD flags = 0, bytes = 0;
    int ret = WSARecv(client.socket, &buffer, 1, &bytes, &flags, &client.overlapped, NULL);
    return ret == 0 || WSAGetLastError() == WSA_IO_PENDING;
}

bool EdoyunServer::Send(Client& client) {
    if (client.sent == client.output.size()) {
        if (client.packets.empty()) return false;
        CPacket& packet = client.packets.front();
        client.output.assign(packet.Data(), packet.Size());
        client.packets.pop_front();
        client.sent = 0;
    }
    client.overlapped = {};
    client.operation = Operation::Send;
    WSABUF buffer = { static_cast<ULONG>(client.output.size() - client.sent),
        &client.output[client.sent] };
    DWORD bytes = 0;
    int ret = WSASend(client.socket, &buffer, 1, &bytes, 0, &client.overlapped, NULL);
    return ret == 0 || WSAGetLastError() == WSA_IO_PENDING;
}

void EdoyunServer::Run() {
    bool stopping = false;
    while (!stopping || !m_clients.empty()) {
        DWORD bytes = 0;
        ULONG_PTR completionKey = 0;
        OVERLAPPED* operation = NULL;
        BOOL ok = GetQueuedCompletionStatus(m_port, &bytes, &completionKey, &operation, INFINITE);
        if (!operation) {
            stopping = true;
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
            // Closing sockets cancels pending operations; retain their buffers until completion.
            for (auto& entry : m_clients) {
                closesocket(entry.second->socket);
                entry.second->socket = INVALID_SOCKET;
            }
            continue;
        }
        auto it = m_clients.find(operation);
        if (it == m_clients.end()) continue;
        Client& client = *it->second;
        if (stopping || !ok) {
            bool accept = client.operation == Operation::Accept;
            m_clients.erase(it);
            if (accept && !stopping) NewAccept();
            continue;
        }
        bool pending = false;
        if (client.operation == Operation::Accept) {
            NewAccept();
            setsockopt(client.socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                reinterpret_cast<const char*>(&m_socket), sizeof(m_socket));
            if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(client.socket), m_port, 0, 1))
                pending = Receive(client);
        } else if (client.operation == Operation::Receive && bytes > 0) {
            client.input.append(client.buffer.data(), bytes);
            // Requests are small. Reject impossible lengths rather than accumulating forever.
            if (client.input.size() <= 1024 * 1024) {
                size_t consumed = client.input.size();
                CPacket packet(reinterpret_cast<const BYTE*>(client.input.data()), consumed);
                if (consumed) {
                    if (m_command.ExcuteCommand(packet.sCmd, client.packets, packet) == 0)
                        pending = Send(client);
                } else {
                    pending = Receive(client);
                }
            }
        } else if (client.operation == Operation::Send && bytes > 0) {
            client.sent += bytes;
            pending = Send(client);
        }
        if (!pending) {
            shutdown(client.socket, SD_SEND);
            m_clients.erase(it);
        }
    }
}
