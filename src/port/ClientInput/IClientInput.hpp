#pragma once

#include "../ServerOutput/IClientNotifier.hpp" // RoomId, ClientId

#include <cstdint>
#include <vector>

namespace port {

// things the client can ask the server for (inbound)
class IClientInput {
public:
    virtual ~IClientInput() = default;

    virtual RoomId CreateRoom(ClientId owner) = 0;
    virtual bool JoinRoom(RoomId room, ClientId client) = 0;
    virtual std::vector<RoomId> ListRooms() = 0;
    virtual void PlayerReadyToStart(RoomId room, ClientId client) = 0;
    virtual void SubmitFrameData(RoomId room, ClientId client,
                                 uint8_t frameNumber,
                                 std::vector<uint8_t> payload) = 0;
    virtual void ClientDisconnected(ClientId client) = 0;
};

} // namespace port
