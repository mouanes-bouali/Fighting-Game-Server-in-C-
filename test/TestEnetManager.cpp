#include "catch.hpp"

#include "EnetTestHelpers.hpp"

#include "../src/adapter/ENet/Protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

using namespace enettest;

TEST_CASE("EnetManager: Init binds the port") {
    EnetManager server;
    server.Init(34600);

    REQUIRE(server.IsRunning());
    REQUIRE(server.GetClientCount() == 0);
    REQUIRE(server.GetClientIds().empty());

    server.Shutdown();
    REQUIRE_FALSE(server.IsRunning());
}

TEST_CASE("EnetManager: Init twice is a no-op") {
    EnetManager server;
    server.Init(34601);
    REQUIRE(server.IsRunning());

    server.Init(34601);
    REQUIRE(server.IsRunning());

    server.Shutdown();
}

TEST_CASE("EnetManager: Shutdown twice is safe") {
    EnetManager server;
    server.Init(34602);
    server.Shutdown();
    server.Shutdown();

    REQUIRE_FALSE(server.IsRunning());
}

TEST_CASE("EnetManager: PollEvents before Init does nothing") {
    EnetManager server;
    REQUIRE_NOTHROW(server.PollEvents());
}

TEST_CASE("EnetManager: a connecting client is given a ClientId") {
    EnetManager server;   // declared first so it outlives the client host
    TestClient client;
    server.Init(34603);

    const port::ClientId id = ConnectClient(server, client, 34603);

    REQUIRE(static_cast<std::size_t>(id) == 1); // 0 would mean "never connected"
    REQUIRE(server.GetClientCount() == 1);
    REQUIRE(server.GetClientIds().size() == 1);
}

TEST_CASE("EnetManager: two clients get distinct ids") {
    EnetManager server;
    TestClient first;
    TestClient second;
    server.Init(34604);

    const port::ClientId firstId = ConnectClient(server, first, 34604);
    REQUIRE(static_cast<std::size_t>(firstId) == 1);

    REQUIRE(second.Connect(34604));
    REQUIRE(Settle(server, second, [&] { return server.GetClientCount() == 2; }));

    const std::vector<port::ClientId> ids = server.GetClientIds();
    REQUIRE(ids.size() == 2);
    REQUIRE(ids[0] != ids[1]);
}

TEST_CASE("EnetManager: CreateRoom reaches the core and is answered") {
    EnetManager server;
    TestClient client;
    RecordingCore core;
    server.Init(34605);
    server.SetInputHandler(&core);

    const port::ClientId id = ConnectClient(server, client, 34605);
    REQUIRE(static_cast<std::size_t>(id) == 1);

    REQUIRE(client.Send(net::EncodeCreateRoom()));
    REQUIRE(Settle(server, client, [&] { return !core.created.empty() && client.HasPackets(); }));

    REQUIRE(core.created.size() == 1);
    REQUIRE(core.created[0] == id);

    std::vector<port::RoomId> rooms;
    REQUIRE(net::DecodeLobbyUpdated(client.Packet(0).data(), client.Packet(0).size(), rooms));
    REQUIRE(rooms.size() == 2); // whatever RecordingCore::ListRooms said
}

TEST_CASE("EnetManager: ListRooms is turned into a LobbyUpdated") {
    EnetManager server;
    TestClient client;
    RecordingCore core;
    server.Init(34606);
    server.SetInputHandler(&core);

    ConnectClient(server, client, 34606);
    REQUIRE(client.Send(net::EncodeListRooms()));
    REQUIRE(Settle(server, client, [&] { return core.listCalls >= 1 && client.HasPackets(); }));

    std::vector<port::RoomId> rooms;
    REQUIRE(net::DecodeLobbyUpdated(client.Packet(0).data(), client.Packet(0).size(), rooms));
    REQUIRE(rooms.size() == 2);
}

TEST_CASE("EnetManager: JoinRoom is routed with the sender's id") {
    EnetManager server;
    TestClient client;
    RecordingCore core;
    server.Init(34607);
    server.SetInputHandler(&core);

    const port::ClientId id = ConnectClient(server, client, 34607);

    REQUIRE(client.Send(net::EncodeJoinRoom(AsRoom(5))));
    REQUIRE(Settle(server, client, [&] { return !core.joined.empty(); }));

    REQUIRE(core.joined.size() == 1);
    REQUIRE(static_cast<std::size_t>(core.joined[0].first) == 5);
    REQUIRE(core.joined[0].second == id);
}

TEST_CASE("EnetManager: ReadyToStart is routed with the sender's id") {
    EnetManager server;
    TestClient client;
    RecordingCore core;
    server.Init(34608);
    server.SetInputHandler(&core);

    const port::ClientId id = ConnectClient(server, client, 34608);

    REQUIRE(client.Send(net::EncodeReadyToStart(AsRoom(3))));
    REQUIRE(Settle(server, client, [&] { return !core.ready.empty(); }));

    REQUIRE(core.ready.size() == 1);
    REQUIRE(static_cast<std::size_t>(core.ready[0].first) == 3);
    REQUIRE(core.ready[0].second == id);
}

TEST_CASE("EnetManager: SubmitFrameData is routed with the sender's id") {
    EnetManager server;
    TestClient client;
    RecordingCore core;
    server.Init(34609);
    server.SetInputHandler(&core);

    const port::ClientId id = ConnectClient(server, client, 34609);

    REQUIRE(client.Send(net::EncodeSubmitFrameData(AsRoom(2), 33, {0xAB, 0xCD})));
    REQUIRE(Settle(server, client, [&] { return !core.frames.empty(); }));

    REQUIRE(core.frames.size() == 1);
    REQUIRE(static_cast<std::size_t>(core.frames[0].room) == 2);
    REQUIRE(core.frames[0].client == id);
    REQUIRE(core.frames[0].frame == 33);
    REQUIRE(core.frames[0].payload == std::vector<std::uint8_t>({0xAB, 0xCD}));
}

TEST_CASE("EnetManager: a disconnect is reported to the core") {
    EnetManager server;
    TestClient client;
    RecordingCore core;
    server.Init(34610);
    server.SetInputHandler(&core);

    const port::ClientId id = ConnectClient(server, client, 34610);
    client.Disconnect();

    REQUIRE(Settle(server, client, [&] { return !core.disconnected.empty(); }));

    REQUIRE(core.disconnected.size() == 1);
    REQUIRE(core.disconnected[0] == id);
    REQUIRE(server.GetClientCount() == 0);
}

TEST_CASE("EnetManager: a garbage packet is ignored and the link survives") {
    EnetManager server;
    TestClient client;
    RecordingCore core;
    server.Init(34611);
    server.SetInputHandler(&core);

    ConnectClient(server, client, 34611);
    REQUIRE(client.Send(std::vector<std::uint8_t>{0x7F, 0x01, 0x02}));

    Settle(server, client, [] { return false; }, 60); // let it arrive

    REQUIRE(core.created.empty());
    REQUIRE(core.joined.empty());
    REQUIRE(core.frames.empty());
    REQUIRE(core.disconnected.empty());
    REQUIRE(server.GetClientCount() == 1);
}

TEST_CASE("EnetManager: with no core attached incoming requests are dropped") {
    EnetManager server;
    TestClient client;
    server.Init(34612);

    ConnectClient(server, client, 34612);
    REQUIRE(client.Send(net::EncodeCreateRoom()));

    Settle(server, client, [] { return false; }, 60);

    REQUIRE(server.GetClientCount() == 1); // still connected, just ignored
}
