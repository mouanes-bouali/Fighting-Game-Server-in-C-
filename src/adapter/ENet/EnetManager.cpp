#include "EnetManager.hpp"

#include "Protocol.hpp"

#include <enet/enet.h>

#include <algorithm>

namespace {

constexpr std::size_t MaxClients = 32;
constexpr std::size_t ChannelCount = 2;
constexpr std::size_t ControlChannel = 0; // lobby / match control
constexpr std::size_t FrameChannel = 1;   // confirmed frames

} // namespace

EnetManager::~EnetManager() {
    Shutdown();
}

// ─── Lifecycle ───────────────────────────────────────────────
void EnetManager::Init(std::uint16_t port) {
    if (_initialized) return;
    if (enet_initialize() != 0) return;
    _initialized = true;

    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = port;

    _host = enet_host_create(&address, MaxClients, ChannelCount, 0, 0);
}

void EnetManager::Shutdown() {
    _clients.clear();
    _nextClientId = 1;

    if (_host) {
        enet_host_destroy(_host);
        _host = nullptr;
    }

    if (_initialized) {
        enet_deinitialize();
        _initialized = false;
    }
}

// ─── Pump ────────────────────────────────────────────────────
void EnetManager::PollEvents() {
    if (!_host) return;

    ENetEvent event;
    while (enet_host_service(_host, &event, 0) > 0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:
                OnConnect(event.peer);
                break;

            case ENET_EVENT_TYPE_RECEIVE:
                OnReceive(event.peer, event.packet);
                enet_packet_destroy(event.packet);
                break;

            case ENET_EVENT_TYPE_DISCONNECT:
                OnDisconnect(event.peer);
                break;

            default:
                break;
        }
    }
}

// ─── Getter / Setter ─────────────────────────────────────────
void EnetManager::SetInputHandler(port::IClientInput* handler) {
    _inputHandler = handler;
}

std::vector<port::ClientId> EnetManager::GetClientIds() const {
    std::vector<port::ClientId> ids;
    ids.reserve(_clients.size());
    for (const auto& client : _clients) {
        ids.push_back(client.id);
    }
    return ids;
}

// ─── IClientNotifier ─────────────────────────────────────────
void EnetManager::NotifyLobbyUpdated(port::ClientId client, const std::vector<port::RoomId>& rooms) {
    SendTo(client, net::EncodeLobbyUpdated(rooms), ControlChannel);
}

void EnetManager::NotifyMatchStarted(port::ClientId client, uint8_t assignedSlot) {
    SendTo(client, net::EncodeMatchStarted(assignedSlot), ControlChannel);
}

void EnetManager::NotifyConfirmedFrame(
    port::ClientId client, port::FrameNumber frameNumber,
    const std::vector<std::pair<port::ClientId, std::vector<uint8_t>>>& inputs) {
    SendTo(client, net::EncodeConfirmedFrame(frameNumber, inputs), FrameChannel);
}

void EnetManager::NotifyOpponentDisconnected(port::ClientId client) {
    SendTo(client, net::EncodeOpponentDisconnected(), ControlChannel);
}

// ─── Peer events ─────────────────────────────────────────────
void EnetManager::OnConnect(ENetPeer* peer) {
    if (!peer) return;

    Client client;
    client.id = static_cast<port::ClientId>(_nextClientId++);
    client.peer = peer;
    _clients.push_back(client);
}

void EnetManager::OnReceive(ENetPeer* peer, ENetPacket* packet) {
    if (!peer || !packet || !_inputHandler) return;

    const Client* client = FindByPeer(peer);
    if (!client) return;

    net::ClientRequest request;
    if (!net::DecodeClientRequest(packet->data, packet->dataLength, request)) return;

    // The handle is known here, so requests that hand back a value
    // (ListRooms and the room list after create/join) are answered by the
    // adapter; everything else is an event the core raises itself.
    switch (request.type) {
        case net::MsgType::CreateRoom:
            _inputHandler->CreateRoom(client->id);
            NotifyLobbyUpdated(client->id, _inputHandler->ListRooms());
            break;

        case net::MsgType::JoinRoom:
            _inputHandler->JoinRoom(request.room, client->id);
            NotifyLobbyUpdated(client->id, _inputHandler->ListRooms());
            break;

        case net::MsgType::ListRooms:
            NotifyLobbyUpdated(client->id, _inputHandler->ListRooms());
            break;

        case net::MsgType::ReadyToStart:
            _inputHandler->PlayerReadyToStart(request.room, client->id);
            break;

        case net::MsgType::SubmitFrameData:
            _inputHandler->SubmitFrameData(request.room, client->id, request.frame, request.payload);
            break;

        default:
            break; // server-side ids are meaningless inbound
    }
}

void EnetManager::OnDisconnect(ENetPeer* peer) {
    const auto it = std::find_if(_clients.begin(), _clients.end(),
                                 [peer](const Client& client) { return client.peer == peer; });
    if (it == _clients.end()) return;

    const port::ClientId id = it->id;
    _clients.erase(it);

    if (_inputHandler) {
        _inputHandler->ClientDisconnected(id);
    }
}

// ─── Helpers ─────────────────────────────────────────────────
EnetManager::Client* EnetManager::FindByPeer(ENetPeer* peer) {
    for (auto& client : _clients) {
        if (client.peer == peer) return &client;
    }
    return nullptr;
}

ENetPeer* EnetManager::FindPeer(port::ClientId client) const {
    for (const auto& entry : _clients) {
        if (entry.id == client) return entry.peer;
    }
    return nullptr;
}

void EnetManager::SendTo(port::ClientId client, const std::vector<std::uint8_t>& message,
                         std::size_t channel) {
    if (!_host || message.empty()) return;

    ENetPeer* peer = FindPeer(client);
    if (!peer) return; // not connected (anymore) -- drop the notification

    ENetPacket* packet = enet_packet_create(message.data(), message.size(), ENET_PACKET_FLAG_RELIABLE);
    if (!packet) return;

    enet_peer_send(peer, static_cast<enet_uint8>(channel), packet);
    enet_host_flush(_host);
}
