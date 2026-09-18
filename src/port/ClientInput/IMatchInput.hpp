// things the user can ask the servr for

#include <vector>
#include <cstdint>


using RoomId = size_t ;
using ClientId = size_t ;

class IMatchInput {

public:
    virtual RoomId CreateRoom() = 0;
    virtual bool JoinRoom(RoomId room, ClientId client) = 0;
    virtual std::vector<RoomId> ListRooms() = 0;
    virtual void PlayerReadyToStart(RoomId room, ClientId client) = 0;
    virtual void SubmitFrameData(RoomId room, ClientId client, uint8_t frameNumber, std::vector<uint8_t> payload) = 0;
};