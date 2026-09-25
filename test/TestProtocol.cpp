#include "catch.hpp"

#include "../src/adapter/ENet/Protocol.hpp"

#include <cstdint>
#include <vector>

namespace {

port::RoomId AsRoom(std::size_t value) { return static_cast<port::RoomId>(value); }
port::ClientId AsClient(std::size_t value) { return static_cast<port::ClientId>(value); }

} // namespace

TEST_CASE("Protocol: CreateRoom round-trips") {
    const auto bytes = net::EncodeCreateRoom();

    net::ClientRequest request;
    REQUIRE(net::DecodeClientRequest(bytes.data(), bytes.size(), request));
    REQUIRE(request.type == net::MsgType::CreateRoom);
}

TEST_CASE("Protocol: ListRooms round-trips") {
    const auto bytes = net::EncodeListRooms();

    net::ClientRequest request;
    REQUIRE(net::DecodeClientRequest(bytes.data(), bytes.size(), request));
    REQUIRE(request.type == net::MsgType::ListRooms);
}

TEST_CASE("Protocol: JoinRoom carries the room id") {
    const auto bytes = net::EncodeJoinRoom(AsRoom(42));

    net::ClientRequest request;
    REQUIRE(net::DecodeClientRequest(bytes.data(), bytes.size(), request));
    REQUIRE(request.type == net::MsgType::JoinRoom);
    REQUIRE(static_cast<std::size_t>(request.room) == 42);
}

TEST_CASE("Protocol: ReadyToStart carries the room id") {
    const auto bytes = net::EncodeReadyToStart(AsRoom(7));

    net::ClientRequest request;
    REQUIRE(net::DecodeClientRequest(bytes.data(), bytes.size(), request));
    REQUIRE(request.type == net::MsgType::ReadyToStart);
    REQUIRE(static_cast<std::size_t>(request.room) == 7);
}

TEST_CASE("Protocol: SubmitFrameData carries room, frame and payload") {
    const std::vector<std::uint8_t> payload{0xDE, 0xAD, 0xBE, 0xEF};
    const auto bytes = net::EncodeSubmitFrameData(AsRoom(3), 99, payload);

    net::ClientRequest request;
    REQUIRE(net::DecodeClientRequest(bytes.data(), bytes.size(), request));
    REQUIRE(request.type == net::MsgType::SubmitFrameData);
    REQUIRE(static_cast<std::size_t>(request.room) == 3);
    REQUIRE(request.frame == 99);
    REQUIRE(request.payload == payload);
}

TEST_CASE("Protocol: SubmitFrameData with an empty payload") {
    const auto bytes = net::EncodeSubmitFrameData(AsRoom(1), 0, {});

    net::ClientRequest request;
    REQUIRE(net::DecodeClientRequest(bytes.data(), bytes.size(), request));
    REQUIRE(request.frame == 0);
    REQUIRE(request.payload.empty());
}

TEST_CASE("Protocol: LobbyUpdated round-trips") {
    const std::vector<port::RoomId> rooms{AsRoom(1), AsRoom(2), AsRoom(300)};
    const auto bytes = net::EncodeLobbyUpdated(rooms);

    std::vector<port::RoomId> decoded;
    REQUIRE(net::DecodeLobbyUpdated(bytes.data(), bytes.size(), decoded));
    REQUIRE(decoded.size() == 3);
    REQUIRE(static_cast<std::size_t>(decoded[0]) == 1);
    REQUIRE(static_cast<std::size_t>(decoded[2]) == 300);
}

TEST_CASE("Protocol: LobbyUpdated with no rooms round-trips") {
    const auto bytes = net::EncodeLobbyUpdated({});

    std::vector<port::RoomId> decoded;
    REQUIRE(net::DecodeLobbyUpdated(bytes.data(), bytes.size(), decoded));
    REQUIRE(decoded.empty());
}

TEST_CASE("Protocol: MatchStarted carries the slot") {
    const auto bytes = net::EncodeMatchStarted(1);

    std::uint8_t slot = 0;
    REQUIRE(net::DecodeMatchStarted(bytes.data(), bytes.size(), slot));
    REQUIRE(slot == 1);
}

TEST_CASE("Protocol: ConfirmedFrame round-trips both players' inputs") {
    const net::FrameInputs inputs{
        {AsClient(1), {0x01, 0x02}},
        {AsClient(2), {0x0A}}};
    const auto bytes = net::EncodeConfirmedFrame(47, inputs);

    port::FrameNumber frame = 0;
    net::FrameInputs decoded;
    REQUIRE(net::DecodeConfirmedFrame(bytes.data(), bytes.size(), frame, decoded));
    REQUIRE(frame == 47);
    REQUIRE(decoded.size() == 2);
    REQUIRE(static_cast<std::size_t>(decoded[0].first) == 1);
    REQUIRE(decoded[0].second == std::vector<std::uint8_t>({0x01, 0x02}));
    REQUIRE(static_cast<std::size_t>(decoded[1].first) == 2);
    REQUIRE(decoded[1].second == std::vector<std::uint8_t>({0x0A}));
}

TEST_CASE("Protocol: OpponentDisconnected round-trips") {
    const auto bytes = net::EncodeOpponentDisconnected();
    REQUIRE(net::DecodeOpponentDisconnected(bytes.data(), bytes.size()));
}

TEST_CASE("Protocol: PeekType reads the first byte") {
    const auto bytes = net::EncodeConfirmedFrame(1, {});

    net::MsgType type{};
    REQUIRE(net::PeekType(bytes.data(), bytes.size(), type));
    REQUIRE(type == net::MsgType::ConfirmedFrame);
}

TEST_CASE("Protocol: an empty buffer decodes to nothing") {
    net::ClientRequest request;
    REQUIRE_FALSE(net::DecodeClientRequest(nullptr, 0, request));

    net::MsgType type{};
    REQUIRE_FALSE(net::PeekType(nullptr, 0, type));
}

TEST_CASE("Protocol: a server message is not a client request") {
    const auto bytes = net::EncodeMatchStarted(0);

    net::ClientRequest request;
    REQUIRE_FALSE(net::DecodeClientRequest(bytes.data(), bytes.size(), request));
}

TEST_CASE("Protocol: an unknown id is rejected") {
    const std::vector<std::uint8_t> bytes{0x7F};

    net::ClientRequest request;
    REQUIRE_FALSE(net::DecodeClientRequest(bytes.data(), bytes.size(), request));
}

TEST_CASE("Protocol: a truncated JoinRoom is rejected") {
    const auto bytes = net::EncodeJoinRoom(AsRoom(1));

    net::ClientRequest request;
    REQUIRE_FALSE(net::DecodeClientRequest(bytes.data(), bytes.size() - 1, request));
}

TEST_CASE("Protocol: a truncated SubmitFrameData is rejected") {
    const auto bytes = net::EncodeSubmitFrameData(AsRoom(1), 1, {0x01, 0x02, 0x03});

    net::ClientRequest request;
    REQUIRE_FALSE(net::DecodeClientRequest(bytes.data(), bytes.size() - 1, request));
}

TEST_CASE("Protocol: a huge declared blob length is rejected") {
    // id(1) + room(8) + frame(1) + length(2) claiming 5000 bytes that are not there.
    const std::vector<std::uint8_t> bytes{0x05, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0x88, 0x13};

    net::ClientRequest request;
    REQUIRE_FALSE(net::DecodeClientRequest(bytes.data(), bytes.size(), request));
}

TEST_CASE("Protocol: a huge room count is rejected") {
    const std::vector<std::uint8_t> bytes{0x81, 0xFF, 0xFF, 0xFF, 0x7F}; // count ~2 billion

    std::vector<port::RoomId> rooms;
    REQUIRE_FALSE(net::DecodeLobbyUpdated(bytes.data(), bytes.size(), rooms));
}

TEST_CASE("Protocol: the per-message decoders reject the wrong id") {
    const auto bytes = net::EncodeMatchStarted(0);

    std::vector<port::RoomId> rooms;
    REQUIRE_FALSE(net::DecodeLobbyUpdated(bytes.data(), bytes.size(), rooms));

    port::FrameNumber frame = 0;
    net::FrameInputs inputs;
    REQUIRE_FALSE(net::DecodeConfirmedFrame(bytes.data(), bytes.size(), frame, inputs));
    REQUIRE_FALSE(net::DecodeOpponentDisconnected(bytes.data(), bytes.size()));
}
