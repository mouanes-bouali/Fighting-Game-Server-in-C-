

// things the user can ask the servr for

#include <cstdint>
#include <vector>

using RoomId = size_t ;
using ClientId = size_t ;
class IMatchOutput {
public:
    virtual void NotifyLobbyUpdated(ClientId client, std::vector<RoomId> rooms) = 0;
    virtual void NotifyMatchStarted(ClientId client, uint8_t assignedSlot) = 0;
    virtual void NotifyConfirmedFrame(ClientId client, uint32_t frameNumber, std::vector<uint8_t> p1Payload, std::vector<uint8_t> p2Payload) = 0;
    virtual void NotifyOpponentDisconnected(ClientId client) = 0;
     virtual void ReciveFrameData(RoomId room, ClientId client, uint8_t frameNumber, std::vector<uint8_t> payload) = 0;
};