#pragma once




#include "../../port/INetworkManager.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

struct _ENetHost;
using ENetHost = _ENetHost;

struct _ENetPeer;
using ENetPeer = _ENetPeer;

struct _ENetPacket;
using ENetPacket = _ENetPacket;

// ENet adapter. Implements port::INetworkManager.
// Holds the ENet host (implementation detail) and a pointer to whoever
// receives the incoming client commands.
class EnetManager : public port::INetworkManager {
public:
    EnetManager() = default;
    ~EnetManager() override;

    // ─── Lifecycle ────────────────────────────────────────────
    void Init(std::uint16_t port) override;
    void Shutdown() override;

    // ─── Pump ─────────────────────────────────────────────────
    void PollEvents() override;

    // ─── Getter / Setter ──────────────────────────────────────
    void SetInputHandler(port::IClientInput* handler) override;
    port::IClientInput* GetInputHandler() const { return _inputHandler; }
    bool IsRunning() const { return _host != nullptr; }
    std::size_t GetClientCount() const { return _clients.size(); }
    std::vector<port::ClientId> GetClientIds() const;

    // ─── IClientNotifier (Server -> Client) ───────────────────
    void NotifyLobbyUpdated(port::ClientId client,
                            const std::vector<port::RoomId>& rooms) override;

    void NotifyMatchStarted(port::ClientId client,
                            uint8_t assignedSlot) override;

    void NotifyConfirmedFrame(
        port::ClientId client,
        port::FrameNumber frameNumber,
        const std::vector<std::pair<port::ClientId, std::vector<uint8_t>>>& inputs) override;

    void NotifyOpponentDisconnected(port::ClientId client) override;

private:
    struct Client {
        port::ClientId id = static_cast<port::ClientId>(0);
        ENetPeer* peer = nullptr;
    };

    void OnConnect(ENetPeer* peer);
    void OnReceive(ENetPeer* peer, ENetPacket* packet);
    void OnDisconnect(ENetPeer* peer);

    Client* FindByPeer(ENetPeer* peer);
    ENetPeer* FindPeer(port::ClientId client) const;
    void SendTo(port::ClientId client, const std::vector<std::uint8_t>& message,
                std::size_t channel);

    ENetHost* _host = nullptr;
    bool _initialized = false;
    std::size_t _nextClientId = 1;
    std::vector<Client> _clients;
    port::IClientInput* _inputHandler = nullptr;
};
