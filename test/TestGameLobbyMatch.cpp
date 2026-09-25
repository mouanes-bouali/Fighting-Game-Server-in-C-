#include "catch.hpp"

#include "../src/core/GameLobby.hpp"

#include <cstdint>
#include <vector>

namespace {

// Moves a lobby into Playing: two players, both ready.
GameLobby PlayingLobby() {
    GameLobby lobby(1, 100);
    lobby.AddPlayer(200);
    lobby.HandlePlayerReady(100);
    lobby.HandlePlayerReady(200);
    return lobby;
}

} // namespace

TEST_CASE("GameLobby: HandleInput keeps the raw payload for the relay") {
    GameLobby lobby = PlayingLobby();
    const std::vector<std::uint8_t> payload{0x05, 0xAA, 0xBB};

    REQUIRE(lobby.HandleInput(100, 0, payload.front(), payload));

    const Types::FrameCommand frame = lobby.GetFrameForTick(0);
    REQUIRE(frame.playersTick[0].playerId == 100);
    REQUIRE(frame.playersTick[0].tick == 0);
    REQUIRE(frame.playersTick[0].action == 0x05);
    REQUIRE(frame.playersTick[0].payload == payload);
}

TEST_CASE("GameLobby: HandleInput without a payload still records the action") {
    GameLobby lobby = PlayingLobby();

    REQUIRE(lobby.HandleInput(200, 0, 0x0F));

    const Types::FrameCommand frame = lobby.GetFrameForTick(0);
    REQUIRE(frame.playersTick[1].playerId == 200);
    REQUIRE(frame.playersTick[1].action == 0x0F);
    REQUIRE(frame.playersTick[1].payload.empty());
}

TEST_CASE("GameLobby: HandleInput is ignored outside a running match") {
    GameLobby lobby(1, 100);

    REQUIRE_FALSE(lobby.HandleInput(100, 0, 0x01));
}

TEST_CASE("GameLobby: HandleInput from a stranger is ignored") {
    GameLobby lobby = PlayingLobby();

    REQUIRE_FALSE(lobby.HandleInput(999, 0, 0x01));
}

TEST_CASE("GameLobby: a late input is re-stamped to the current tick") {
    GameLobby lobby = PlayingLobby();
    lobby.Tick(); // now ticking at 1

    REQUIRE(lobby.HandleInput(100, 0, 0x01));

    REQUIRE(lobby.GetFrameForTick(1).playersTick[0].playerId == 100);
}

TEST_CASE("GameLobby: Tick consumes the frame it just played") {
    GameLobby lobby = PlayingLobby();
    lobby.HandleInput(100, 0, 0x01, {0x01});

    lobby.Tick();

    REQUIRE(lobby.GetCurrentTick() == 1);
    REQUIRE(lobby.GetFrameForTick(0).playersTick[0].playerId == 0);
}

TEST_CASE("GameLobby: a player leaving ends the match") {
    GameLobby lobby = PlayingLobby();
    REQUIRE(lobby.GetState() == LobbyState::Playing);

    lobby.HandlePlayerDisconnected(100);

    REQUIRE(lobby.GetState() == LobbyState::Finished);
}

TEST_CASE("GameLobby: a stranger leaving changes nothing") {
    GameLobby lobby = PlayingLobby();

    lobby.HandlePlayerDisconnected(999);

    REQUIRE(lobby.GetState() == LobbyState::Playing);
}
