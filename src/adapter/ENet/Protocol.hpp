#pragma once

#include "../../port/ServerOutput/IClientNotifier.hpp" // RoomId, ClientId, FrameNumber

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// Wire format for the ENet adapter. Deliberately free of any ENet type so the
// codec can be unit-tested without a socket.
//
//   client -> server: CreateRoom, JoinRoom, ListRooms, ReadyToStart, SubmitFrameData
//   server -> client: LobbyUpdated, MatchStarted, ConfirmedFrame, OpponentDisconnected
//
// Every integer is little-endian. Every read is bounds-checked: a truncated or
// hostile packet just fails to decode instead of reading past the buffer.
namespace net {

enum class MsgType : std::uint8_t {
    CreateRoom      = 0x01,
    JoinRoom        = 0x02,
    ListRooms       = 0x03,
    ReadyToStart    = 0x04,
    SubmitFrameData = 0x05,

    LobbyUpdated         = 0x81,
    MatchStarted         = 0x82,
    ConfirmedFrame       = 0x83,
    OpponentDisconnected = 0x84
};

using FrameInputs = std::vector<std::pair<port::ClientId, std::vector<std::uint8_t>>>;

// ─── Little-endian writer ────────────────────────────────────
class Writer {
public:
    void U8(std::uint8_t value);
    void U16(std::uint16_t value);
    void U32(std::uint32_t value);
    void U64(std::uint64_t value);
    void Blob(const std::vector<std::uint8_t>& bytes); // u16 length + bytes

    const std::vector<std::uint8_t>& Data() const { return _buffer; }

private:
    std::vector<std::uint8_t> _buffer;
};

// ─── Little-endian reader ────────────────────────────────────
class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size);

    bool U8(std::uint8_t& out);
    bool U16(std::uint16_t& out);
    bool U32(std::uint32_t& out);
    bool U64(std::uint64_t& out);
    bool Blob(std::vector<std::uint8_t>& out);

    bool Ok() const { return _ok; }
    bool AtEnd() const { return _ok && _pos == _size; }

private:
    bool Need(std::size_t count);

    const std::uint8_t* _data = nullptr;
    std::size_t _size = 0;
    std::size_t _pos = 0;
    bool _ok = true;
};

// ─── Client -> Server ────────────────────────────────────────
std::vector<std::uint8_t> EncodeCreateRoom();
std::vector<std::uint8_t> EncodeJoinRoom(port::RoomId room);
std::vector<std::uint8_t> EncodeListRooms();
std::vector<std::uint8_t> EncodeReadyToStart(port::RoomId room);
std::vector<std::uint8_t> EncodeSubmitFrameData(port::RoomId room, std::uint8_t frame,
                                                const std::vector<std::uint8_t>& payload);

struct ClientRequest {
    MsgType type = MsgType::ListRooms;
    port::RoomId room = static_cast<port::RoomId>(0);
    std::uint8_t frame = 0;
    std::vector<std::uint8_t> payload;
};

bool DecodeClientRequest(const std::uint8_t* data, std::size_t size, ClientRequest& out);

// ─── Server -> Client ────────────────────────────────────────
std::vector<std::uint8_t> EncodeLobbyUpdated(const std::vector<port::RoomId>& rooms);
std::vector<std::uint8_t> EncodeMatchStarted(std::uint8_t slot);
std::vector<std::uint8_t> EncodeConfirmedFrame(port::FrameNumber frame, const FrameInputs& inputs);
std::vector<std::uint8_t> EncodeOpponentDisconnected();

bool DecodeLobbyUpdated(const std::uint8_t* data, std::size_t size,
                        std::vector<port::RoomId>& rooms);
bool DecodeMatchStarted(const std::uint8_t* data, std::size_t size, std::uint8_t& slot);
bool DecodeConfirmedFrame(const std::uint8_t* data, std::size_t size,
                          port::FrameNumber& frame, FrameInputs& inputs);
bool DecodeOpponentDisconnected(const std::uint8_t* data, std::size_t size);

bool PeekType(const std::uint8_t* data, std::size_t size, MsgType& type);

} // namespace net
