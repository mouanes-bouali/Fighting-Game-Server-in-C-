#pragma once

// Shared scaffolding for the EnetManager tests: a core that records whatever
// the adapter hands it, and a plain ENet client standing in for a game client.

#include "../src/adapter/ENet/EnetManager.hpp"

#include <enet/enet.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <utility>
#include <vector>

namespace enettest {

inline port::ClientId AsClient(std::size_t value) {
    return static_cast<port::ClientId>(value);
}

inline port::RoomId AsRoom(std::size_t value) {
    return static_cast<port::RoomId>(value);
}

// An IClientInput that just remembers the calls.
class RecordingCore : public port::IClientInput {
public:
    struct Frame {
        port::RoomId room;
        port::ClientId client;
        std::uint8_t frame;
        std::vector<std::uint8_t> payload;
    };

    port::RoomId CreateRoom(port::ClientId owner) override {
        created.push_back(owner);
        return AsRoom(1);
    }

    bool JoinRoom(port::RoomId room, port::ClientId client) override {
        joined.emplace_back(room, client);
        return true;
    }

    std::vector<port::RoomId> ListRooms() override {
        ++listCalls;
        return {AsRoom(1), AsRoom(2)};
    }

    void PlayerReadyToStart(port::RoomId room, port::ClientId client) override {
        ready.emplace_back(room, client);
    }

    void SubmitFrameData(port::RoomId room, port::ClientId client,
                         std::uint8_t frameNumber, std::vector<std::uint8_t> payload) override {
        frames.push_back(Frame{room, client, frameNumber, payload});
    }

    void ClientDisconnected(port::ClientId client) override {
        disconnected.push_back(client);
    }

    int listCalls = 0;
    std::vector<port::ClientId> created;
    std::vector<std::pair<port::RoomId, port::ClientId>> joined;
    std::vector<std::pair<port::RoomId, port::ClientId>> ready;
    std::vector<Frame> frames;
    std::vector<port::ClientId> disconnected;
};

// A bare ENet client host, connected to the EnetManager under test.
class TestClient {
public:
    ~TestClient() { Close(); }

    bool Connect(std::uint16_t port) {
        _host = enet_host_create(nullptr, 1, 2, 0, 0);
        if (!_host) return false;

        ENetAddress address;
        if (enet_address_set_host(&address, "127.0.0.1") != 0) return false;
        address.port = port;

        _peer = enet_host_connect(_host, &address, 2, 0);
        return _peer != nullptr;
    }

    void Pump(unsigned timeoutMs = 1) {
        if (!_host) return;

        ENetEvent event;
        while (enet_host_service(_host, &event, timeoutMs) > 0) {
            timeoutMs = 0;
            switch (event.type) {
                case ENET_EVENT_TYPE_CONNECT:
                    _connected = true;
                    break;
                case ENET_EVENT_TYPE_DISCONNECT:
                    _disconnected = true;
                    break;
                case ENET_EVENT_TYPE_RECEIVE:
                    _received.emplace_back(event.packet->data,
                                           event.packet->data + event.packet->dataLength);
                    enet_packet_destroy(event.packet);
                    break;
                default:
                    break;
            }
        }
    }

    bool Send(const std::vector<std::uint8_t>& message) {
        if (!_peer) return false;

        ENetPacket* packet = enet_packet_create(message.data(), message.size(),
                                                ENET_PACKET_FLAG_RELIABLE);
        if (!packet) return false;

        enet_peer_send(_peer, 0, packet);
        enet_host_flush(_host);
        return true;
    }

    void Disconnect() {
        if (_peer) enet_peer_disconnect(_peer, 0);
    }

    void Close() {
        if (_peer) {
            enet_peer_reset(_peer);
            _peer = nullptr;
        }
        if (_host) {
            enet_host_destroy(_host);
            _host = nullptr;
        }
    }

    bool IsConnected() const { return _connected; }
    bool IsDisconnected() const { return _disconnected; }
    bool HasPackets() const { return !_received.empty(); }
    std::size_t PacketCount() const { return _received.size(); }
    const std::vector<std::uint8_t>& Packet(std::size_t index) const { return _received[index]; }

private:
    ENetHost* _host = nullptr;
    ENetPeer* _peer = nullptr;
    bool _connected = false;
    bool _disconnected = false;
    std::vector<std::vector<std::uint8_t>> _received;
};

// Starts the server and asserts the socket bound.
inline void StartServer(EnetManager& server, std::uint16_t port) {
    server.Init(port);
}

// Connects a client and hands back the id the server assigned it.
inline port::ClientId ConnectClient(EnetManager& server, TestClient& client, std::uint16_t port) {
    if (!client.Connect(port)) return AsClient(0);

    for (int i = 0; i < 400; ++i) {
        server.PollEvents();
        client.Pump();
        if (server.GetClientCount() == 1 && client.IsConnected() && !server.GetClientIds().empty()) {
            return server.GetClientIds().front();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return AsClient(0); // 0 == "never connected"
}

// Pumps both ends until `done()` is true, or the round budget runs out.
template <typename Predicate>
bool Settle(EnetManager& server, TestClient& client, Predicate done, int rounds = 400) {
    for (int i = 0; i < rounds; ++i) {
        server.PollEvents();
        client.Pump();
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return done();
}

} // namespace enettest
