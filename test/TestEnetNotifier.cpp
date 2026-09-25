#include "catch.hpp"

#include "EnetTestHelpers.hpp"

#include "../src/adapter/ENet/Protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

using namespace enettest;

// The outbound half of the port: every Notify* must turn into a packet the
// client can decode again.

TEST_CASE("EnetManager: NotifyMatchStarted reaches the client") {
    EnetManager server;
    TestClient client;
    server.Init(34620);

    const port::ClientId id = ConnectClient(server, client, 34620);
    server.NotifyMatchStarted(id, 1);

    std::uint8_t slot = 0;
    REQUIRE(Settle(server, client, [&] {
        return client.HasPackets() &&
               net::DecodeMatchStarted(client.Packet(0).data(), client.Packet(0).size(), slot);
    }));
    REQUIRE(slot == 1);
}

TEST_CASE("EnetManager: NotifyLobbyUpdated reaches the client") {
    EnetManager server;
    TestClient client;
    server.Init(34621);

    const port::ClientId id = ConnectClient(server, client, 34621);
    server.NotifyLobbyUpdated(id, {AsRoom(4), AsRoom(5)});

    std::vector<port::RoomId> rooms;
    REQUIRE(Settle(server, client, [&] {
        return client.HasPackets() &&
               net::DecodeLobbyUpdated(client.Packet(0).data(), client.Packet(0).size(), rooms);
    }));
    REQUIRE(rooms.size() == 2);
    REQUIRE(static_cast<std::size_t>(rooms[0]) == 4);
    REQUIRE(static_cast<std::size_t>(rooms[1]) == 5);
}

TEST_CASE("EnetManager: NotifyConfirmedFrame reaches the client") {
    EnetManager server;
    TestClient client;
    server.Init(34622);

    const port::ClientId id = ConnectClient(server, client, 34622);

    const net::FrameInputs inputs{
        {AsClient(1), {0x01}},
        {AsClient(2), {0x02, 0x03}}};
    server.NotifyConfirmedFrame(id, 47, inputs);

    port::FrameNumber frame = 0;
    net::FrameInputs decoded;
    REQUIRE(Settle(server, client, [&] {
        return client.HasPackets() &&
               net::DecodeConfirmedFrame(client.Packet(0).data(), client.Packet(0).size(),
                                         frame, decoded);
    }));
    REQUIRE(frame == 47);
    REQUIRE(decoded.size() == 2);
    REQUIRE(static_cast<std::size_t>(decoded[0].first) == 1);
    REQUIRE(decoded[1].second == std::vector<std::uint8_t>({0x02, 0x03}));
}

TEST_CASE("EnetManager: NotifyOpponentDisconnected reaches the client") {
    EnetManager server;
    TestClient client;
    server.Init(34623);

    const port::ClientId id = ConnectClient(server, client, 34623);
    server.NotifyOpponentDisconnected(id);

    REQUIRE(Settle(server, client, [&] {
        return client.HasPackets() &&
               net::DecodeOpponentDisconnected(client.Packet(0).data(), client.Packet(0).size());
    }));
}

TEST_CASE("EnetManager: notifying a client that is not connected is harmless") {
    EnetManager server;
    server.Init(34624);

    REQUIRE_NOTHROW(server.NotifyMatchStarted(AsClient(1234), 0));
    REQUIRE_NOTHROW(server.NotifyLobbyUpdated(AsClient(1234), {AsRoom(1)}));
    REQUIRE_NOTHROW(server.NotifyConfirmedFrame(AsClient(1234), 1, {}));
    REQUIRE_NOTHROW(server.NotifyOpponentDisconnected(AsClient(1234)));
}
