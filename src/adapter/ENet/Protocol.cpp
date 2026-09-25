#include "Protocol.hpp"

#include <algorithm>

namespace net {
namespace {

// Guards against a hostile/garbled length field allocating gigabytes.
constexpr std::size_t MaxBlob = 1024;
constexpr std::size_t MaxCount = 1024;

} // namespace

// ─── Writer ──────────────────────────────────────────────────
void Writer::U8(std::uint8_t value) {
    _buffer.push_back(value);
}

void Writer::U16(std::uint16_t value) {
    _buffer.push_back(static_cast<std::uint8_t>(value & 0xFF));
    _buffer.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void Writer::U32(std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        _buffer.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
    }
}

void Writer::U64(std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        _buffer.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
    }
}

void Writer::Blob(const std::vector<std::uint8_t>& bytes) {
    const std::size_t size = std::min(bytes.size(), MaxBlob);
    U16(static_cast<std::uint16_t>(size));
    _buffer.insert(_buffer.end(), bytes.begin(), bytes.begin() + size);
}

// ─── Reader ──────────────────────────────────────────────────
Reader::Reader(const std::uint8_t* data, std::size_t size)
    : _data(data)
    , _size(size) {}

bool Reader::Need(std::size_t count) {
    if (!_ok) return false;
    if (_size - _pos < count) {
        _ok = false;
        return false;
    }
    return true;
}

bool Reader::U8(std::uint8_t& out) {
    if (!Need(1)) return false;
    out = _data[_pos++];
    return true;
}

bool Reader::U16(std::uint16_t& out) {
    if (!Need(2)) return false;
    out = static_cast<std::uint16_t>(_data[_pos] | (_data[_pos + 1] << 8));
    _pos += 2;
    return true;
}

bool Reader::U32(std::uint32_t& out) {
    if (!Need(4)) return false;
    out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        out |= static_cast<std::uint32_t>(_data[_pos++]) << shift;
    }
    return true;
}

bool Reader::U64(std::uint64_t& out) {
    if (!Need(8)) return false;
    out = 0;
    for (int shift = 0; shift < 64; shift += 8) {
        out |= static_cast<std::uint64_t>(_data[_pos++]) << shift;
    }
    return true;
}

bool Reader::Blob(std::vector<std::uint8_t>& out) {
    std::uint16_t size = 0;
    if (!U16(size)) return false;
    if (size > MaxBlob) {
        _ok = false;
        return false;
    }
    if (!Need(size)) return false;

    out.assign(_data + _pos, _data + _pos + size);
    _pos += size;
    return true;
}

// ─── Client -> Server ────────────────────────────────────────
std::vector<std::uint8_t> EncodeCreateRoom() {
    Writer writer;
    writer.U8(static_cast<std::uint8_t>(MsgType::CreateRoom));
    return writer.Data();
}

std::vector<std::uint8_t> EncodeJoinRoom(port::RoomId room) {
    Writer writer;
    writer.U8(static_cast<std::uint8_t>(MsgType::JoinRoom));
    writer.U64(static_cast<std::uint64_t>(room));
    return writer.Data();
}

std::vector<std::uint8_t> EncodeListRooms() {
    Writer writer;
    writer.U8(static_cast<std::uint8_t>(MsgType::ListRooms));
    return writer.Data();
}

std::vector<std::uint8_t> EncodeReadyToStart(port::RoomId room) {
    Writer writer;
    writer.U8(static_cast<std::uint8_t>(MsgType::ReadyToStart));
    writer.U64(static_cast<std::uint64_t>(room));
    return writer.Data();
}

std::vector<std::uint8_t> EncodeSubmitFrameData(port::RoomId room, std::uint8_t frame,
                                                const std::vector<std::uint8_t>& payload) {
    Writer writer;
    writer.U8(static_cast<std::uint8_t>(MsgType::SubmitFrameData));
    writer.U64(static_cast<std::uint64_t>(room));
    writer.U8(frame);
    writer.Blob(payload);
    return writer.Data();
}

bool DecodeClientRequest(const std::uint8_t* data, std::size_t size, ClientRequest& out) {
    if (!data || size == 0) return false;

    Reader reader(data, size);
    std::uint8_t rawType = 0;
    if (!reader.U8(rawType)) return false;

    out = ClientRequest{};
    out.type = static_cast<MsgType>(rawType);

    switch (out.type) {
        case MsgType::CreateRoom:
        case MsgType::ListRooms:
            return true;

        case MsgType::JoinRoom:
        case MsgType::ReadyToStart: {
            std::uint64_t room = 0;
            if (!reader.U64(room)) return false;
            out.room = static_cast<port::RoomId>(room);
            return true;
        }

        case MsgType::SubmitFrameData: {
            std::uint64_t room = 0;
            if (!reader.U64(room)) return false;
            if (!reader.U8(out.frame)) return false;
            if (!reader.Blob(out.payload)) return false;
            out.room = static_cast<port::RoomId>(room);
            return true;
        }

        default:
            return false; // not a client request
    }
}

// ─── Server -> Client ────────────────────────────────────────
std::vector<std::uint8_t> EncodeLobbyUpdated(const std::vector<port::RoomId>& rooms) {
    Writer writer;
    writer.U8(static_cast<std::uint8_t>(MsgType::LobbyUpdated));
    writer.U32(static_cast<std::uint32_t>(rooms.size()));
    for (const auto room : rooms) {
        writer.U64(static_cast<std::uint64_t>(room));
    }
    return writer.Data();
}

std::vector<std::uint8_t> EncodeMatchStarted(std::uint8_t slot) {
    Writer writer;
    writer.U8(static_cast<std::uint8_t>(MsgType::MatchStarted));
    writer.U8(slot);
    return writer.Data();
}

std::vector<std::uint8_t> EncodeConfirmedFrame(port::FrameNumber frame, const FrameInputs& inputs) {
    Writer writer;
    writer.U8(static_cast<std::uint8_t>(MsgType::ConfirmedFrame));
    writer.U32(frame);
    writer.U32(static_cast<std::uint32_t>(inputs.size()));
    for (const auto& input : inputs) {
        writer.U64(static_cast<std::uint64_t>(input.first));
        writer.Blob(input.second);
    }
    return writer.Data();
}

std::vector<std::uint8_t> EncodeOpponentDisconnected() {
    Writer writer;
    writer.U8(static_cast<std::uint8_t>(MsgType::OpponentDisconnected));
    return writer.Data();
}

bool DecodeLobbyUpdated(const std::uint8_t* data, std::size_t size,
                        std::vector<port::RoomId>& rooms) {
    rooms.clear();

    Reader reader(data, size);
    std::uint8_t rawType = 0;
    if (!reader.U8(rawType) || static_cast<MsgType>(rawType) != MsgType::LobbyUpdated) return false;

    std::uint32_t count = 0;
    if (!reader.U32(count) || count > MaxCount) return false;

    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint64_t room = 0;
        if (!reader.U64(room)) return false;
        rooms.push_back(static_cast<port::RoomId>(room));
    }
    return true;
}

bool DecodeMatchStarted(const std::uint8_t* data, std::size_t size, std::uint8_t& slot) {
    Reader reader(data, size);
    std::uint8_t rawType = 0;
    if (!reader.U8(rawType) || static_cast<MsgType>(rawType) != MsgType::MatchStarted) return false;
    return reader.U8(slot);
}

bool DecodeConfirmedFrame(const std::uint8_t* data, std::size_t size,
                          port::FrameNumber& frame, FrameInputs& inputs) {
    inputs.clear();

    Reader reader(data, size);
    std::uint8_t rawType = 0;
    if (!reader.U8(rawType) || static_cast<MsgType>(rawType) != MsgType::ConfirmedFrame) return false;

    if (!reader.U32(frame)) return false;

    std::uint32_t count = 0;
    if (!reader.U32(count) || count > MaxCount) return false;

    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint64_t client = 0;
        std::vector<std::uint8_t> payload;
        if (!reader.U64(client)) return false;
        if (!reader.Blob(payload)) return false;
        inputs.emplace_back(static_cast<port::ClientId>(client), payload);
    }
    return true;
}

bool DecodeOpponentDisconnected(const std::uint8_t* data, std::size_t size) {
    Reader reader(data, size);
    std::uint8_t rawType = 0;
    if (!reader.U8(rawType)) return false;
    return static_cast<MsgType>(rawType) == MsgType::OpponentDisconnected;
}

bool PeekType(const std::uint8_t* data, std::size_t size, MsgType& type) {
    Reader reader(data, size);
    std::uint8_t rawType = 0;
    if (!reader.U8(rawType)) return false;
    type = static_cast<MsgType>(rawType);
    return true;
}

} // namespace net
