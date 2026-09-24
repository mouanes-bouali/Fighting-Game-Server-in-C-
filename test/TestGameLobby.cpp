#include "catch.hpp"
#include "../src/core/GameLobby.hpp"

TEST_CASE("GameLobby: starts with just the owner") {
    GameLobby lobby(1, 100);
    REQUIRE(lobby.GetLobbyId() == 1);
    REQUIRE(lobby.GetRoomOwner() == 100);
    REQUIRE(lobby.GetPlayerIds().size() == 1);
    REQUIRE(lobby.GetPlayerIds()[0] == 100);
    REQUIRE(lobby.GetState() == LobbyState::WaitingForPlayers);
}

TEST_CASE("GameLobby: AddPlayer adds the second player") {
    GameLobby lobby(1, 100);
    lobby.AddPlayer(200);
    REQUIRE(lobby.GetPlayerIds().size() == 2);
    REQUIRE(lobby.IsFull() == true);
}

TEST_CASE("GameLobby: lobby moves to WaitingForReady when full") {
    GameLobby lobby(1, 100);
    REQUIRE(lobby.GetState() == LobbyState::WaitingForPlayers);
    lobby.AddPlayer(200);
    REQUIRE(lobby.GetState() == LobbyState::WaitingForReady);
}

TEST_CASE("GameLobby: AddPlayer ignores duplicates") {
    GameLobby lobby(1, 100);
    lobby.AddPlayer(100);
    REQUIRE(lobby.GetPlayerIds().size() == 1);
}

TEST_CASE("GameLobby: AddPlayer ignores when full") {
    GameLobby lobby(1, 100);
    lobby.AddPlayer(200);
    lobby.AddPlayer(300);
    REQUIRE(lobby.GetPlayerIds().size() == 2);
}

TEST_CASE("GameLobby: no one is ready at the start") {
    GameLobby lobby(1, 100);
    lobby.AddPlayer(200);
    REQUIRE(lobby.IsPlayerReady(100) == false);
    REQUIRE(lobby.IsPlayerReady(200) == false);
}

TEST_CASE("GameLobby: IsPlayerReady returns true after ready") {
    GameLobby lobby(1, 100);
    lobby.AddPlayer(200);
    lobby.HandlePlayerReady(100);
    REQUIRE(lobby.IsPlayerReady(100) == true);
    REQUIRE(lobby.IsPlayerReady(200) == false);
}

TEST_CASE("GameLobby: one ready is not enough to start") {
    GameLobby lobby(1, 100);
    lobby.AddPlayer(200);
    lobby.HandlePlayerReady(100);
    REQUIRE(lobby.GetState() == LobbyState::WaitingForReady);
}

TEST_CASE("GameLobby: both ready starts playing") {
    GameLobby lobby(1, 100);
    lobby.AddPlayer(200);
    lobby.HandlePlayerReady(100);
    lobby.HandlePlayerReady(200);
    REQUIRE(lobby.GetState() == LobbyState::Playing);
}

TEST_CASE("GameLobby: ready from unknown player is ignored") {
    GameLobby lobby(1, 100);
    lobby.AddPlayer(200);
    lobby.HandlePlayerReady(999);
    REQUIRE(lobby.IsPlayerReady(999) == false);
    REQUIRE(lobby.GetState() == LobbyState::WaitingForReady);
}

TEST_CASE("GameLobby: ready twice is fine") {
    GameLobby lobby(1, 100);
    lobby.AddPlayer(200);
    lobby.HandlePlayerReady(100);
    lobby.HandlePlayerReady(100);
    lobby.HandlePlayerReady(200);
    REQUIRE(lobby.GetState() == LobbyState::Playing);
}