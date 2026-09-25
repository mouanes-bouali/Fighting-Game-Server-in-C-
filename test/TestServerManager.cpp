#include "catch.hpp"

#include "../src/core/ServerManager.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

port::ClientId AsClient(std::size_t value) { return static_cast<port::ClientId>(value); }
port::RoomId AsRoom(std::size_t value) { return static_cast<port::RoomId>(value); }

// Records everything the core pushes out, so the ports can be asserted on
// without any sockets involved.
class FakeNetwork : public port::INetworkManager {
public:
    struct Frame {
        port::ClientId client;
        port::FrameNumber frame;
        std::vector<std::pair<port::ClientId, std::vector<std::uint8_t>>> inputs;
    };

    void Init(std::uint16_t) override {}
    void Shutdown() override {}
    void PollEvents() override { ++polls; }
    void SetInputHandler(port::IClientInput* handler) override { handler_ = handler; }

    void NotifyLobbyUpdated(port::ClientId client, const std::vector<port::RoomId>& rooms) override {
        lobbyUpdates.emplace_back(client, rooms);
    }

    void NotifyMatchStarted(port::ClientId client, uint8_t assignedSlot) override {
        matchStarted.emplace_back(client, assignedSlot);
    }

    void NotifyConfirmedFrame(
        port::ClientId client, port::FrameNumber frameNumber,
        const std::vector<std::pair<port::ClientId, std::vector<uint8_t>>>& inputs) override {
        frames.push_back(Frame{client, frameNumber, inputs});
    }

    void NotifyOpponentDisconnected(port::ClientId client) override {
        opponentDisconnected.push_back(client);
    }

    port::IClientInput* handler_ = nullptr;
    int polls = 0;
    std::vector<std::pair<port::ClientId, std::vector<port::RoomId>>> lobbyUpdates;
    std::vector<std::pair<port::ClientId, std::uint8_t>> matchStarted;
    std::vector<Frame> frames;
    std::vector<port::ClientId> opponentDisconnected;
};

// A two-player room, both ready: the match is playing.
struct PlayingMatch {
    FakeNetwork network;
    ServerManager server{network};
    port::RoomId room = AsRoom(0);

    PlayingMatch() {
        room = server.CreateRoom(AsClient(100));
        server.JoinRoom(room, AsClient(200));
        server.PlayerReadyToStart(room, AsClient(100));
        server.PlayerReadyToStart(room, AsClient(200));
    }

    const GameLobby& lobby() const { return server.GetLobbies().front(); }
};

} // namespace

TEST_CASE("ServerManager: CreateRoom gives the owner its own lobby") {
    FakeNetwork network;
    ServerManager server(network);

    const port::RoomId room = server.CreateRoom(AsClient(100));

    REQUIRE(static_cast<std::size_t>(room) == 1);
    REQUIRE(server.ListRooms().size() == 1);
    REQUIRE(server.GetLobbies().size() == 1);
    REQUIRE(server.GetLobbies()[0].GetPlayerIds().size() == 1);
    REQUIRE(server.GetLobbies()[0].GetPlayerIds()[0] == 100);
    REQUIRE(server.GetLobbies()[0].GetState() == LobbyState::WaitingForPlayers);
}

TEST_CASE("ServerManager: rooms get distinct ids") {
    FakeNetwork network;
    ServerManager server(network);

    const port::RoomId first = server.CreateRoom(AsClient(100));
    const port::RoomId second = server.CreateRoom(AsClient(300));

    REQUIRE(first != second);
    REQUIRE(server.ListRooms().size() == 2);
}

TEST_CASE("ServerManager: JoinRoom fills the lobby") {
    FakeNetwork network;
    ServerManager server(network);
    const port::RoomId room = server.CreateRoom(AsClient(100));

    REQUIRE(server.JoinRoom(room, AsClient(200)));

    REQUIRE(server.GetLobbies()[0].IsFull());
    REQUIRE(server.GetLobbies()[0].GetState() == LobbyState::WaitingForReady);
}

TEST_CASE("ServerManager: JoinRoom on an unknown room fails") {
    FakeNetwork network;
    ServerManager server(network);

    REQUIRE_FALSE(server.JoinRoom(AsRoom(99), AsClient(200)));
}

TEST_CASE("ServerManager: JoinRoom twice from the same client is refused") {
    FakeNetwork network;
    ServerManager server(network);
    const port::RoomId room = server.CreateRoom(AsClient(100));

    REQUIRE(server.JoinRoom(room, AsClient(200)));
    REQUIRE_FALSE(server.JoinRoom(room, AsClient(200)));
}

TEST_CASE("ServerManager: one ready is not enough, both start the match") {
    FakeNetwork network;
    ServerManager server(network);
    const port::RoomId room = server.CreateRoom(AsClient(100));
    server.JoinRoom(room, AsClient(200));

    server.PlayerReadyToStart(room, AsClient(100));
    REQUIRE(network.matchStarted.empty());

    server.PlayerReadyToStart(room, AsClient(200));
    REQUIRE(network.matchStarted.size() == 2);
    REQUIRE(network.matchStarted[0].first == AsClient(100));
    REQUIRE(network.matchStarted[0].second == 0);
    REQUIRE(network.matchStarted[1].first == AsClient(200));
    REQUIRE(network.matchStarted[1].second == 1);
}

TEST_CASE("ServerManager: Step broadcasts the confirmed frame to both players") {
    PlayingMatch match;

    match.server.SubmitFrameData(match.room, AsClient(100), 0, {0x11});
    match.server.SubmitFrameData(match.room, AsClient(200), 0, {0x22});

    match.server.Step();

    REQUIRE(match.network.polls == 1);
    REQUIRE(match.network.frames.size() == 2);
    REQUIRE(match.network.frames[0].client == AsClient(100));
    REQUIRE(match.network.frames[1].client == AsClient(200));

    for (const auto& sent : match.network.frames) {
        REQUIRE(sent.frame == 0);
        REQUIRE(sent.inputs.size() == 2);
    }

    // Both players receive the same batch: their own input and the opponent's.
    REQUIRE(match.network.frames[0].inputs == match.network.frames[1].inputs);
    REQUIRE(match.network.frames[0].inputs[0].second == std::vector<std::uint8_t>({0x11}));
    REQUIRE(match.network.frames[0].inputs[1].second == std::vector<std::uint8_t>({0x22}));
}

TEST_CASE("ServerManager: a silent player still receives the frame") {
    PlayingMatch match;

    match.server.SubmitFrameData(match.room, AsClient(100), 0, {0x11});
    match.server.Step();

    REQUIRE(match.network.frames.size() == 2);
    REQUIRE(match.network.frames[0].inputs.size() == 1); // only the one who spoke
}

TEST_CASE("ServerManager: the tick advances after a step") {
    PlayingMatch match;

    REQUIRE(match.lobby().GetCurrentTick() == 0);
    match.server.Step();
    REQUIRE(match.lobby().GetCurrentTick() == 1);
}

TEST_CASE("ServerManager: a disconnect ends the match and warns the opponent") {
    PlayingMatch match;

    match.server.ClientDisconnected(AsClient(100));

    REQUIRE(match.lobby().GetState() == LobbyState::Finished);
    REQUIRE(match.network.opponentDisconnected.size() == 1);
    REQUIRE(match.network.opponentDisconnected[0] == AsClient(200));
}

TEST_CASE("ServerManager: a finished match stops broadcasting frames") {
    PlayingMatch match;
    match.server.ClientDisconnected(AsClient(200));
    match.network.frames.clear();

    match.server.Step();

    REQUIRE(match.network.frames.empty());
}

TEST_CASE("ServerManager: disconnecting an unknown client is harmless") {
    FakeNetwork network;
    ServerManager server(network);

    server.ClientDisconnected(AsClient(999));

    REQUIRE(network.opponentDisconnected.empty());
}
