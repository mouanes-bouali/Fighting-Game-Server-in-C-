#pragma once

#include "../port/ClientInput/IClientInput.hpp"
#include "../port/INetworkManager.hpp"

#include "GameLobby.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <utility>
#include <vector>

// The core: owns the lobbies and drives the tick loop.
// It only knows port::INetworkManager (send + pump), and it implements
// port::IClientInput for whatever the network delivers.
//
// Split of responsibility with the adapter:
//   * request/response (create, join, list) -> the adapter answers, because
//     only it knows which client asked.
//   * events (match started, confirmed frame, opponent left) -> raised here.
class ServerManager : public port::IClientInput {
public:
    explicit ServerManager(port::INetworkManager& network)
        : network_(network) {}

    ~ServerManager() = default;

    // ─── Game loop ────────────────────────────────────────────
    void Run() {
        while (!stopTicking) {
            Step();
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    }

    void Stop() { stopTicking = true; }

    // One iteration: read the wire, advance every running match, then tell the
    // players which frame was confirmed. Kept separate from Run() so it can be
    // driven by tests without a thread.
    void Step() {
        network_.PollEvents();

        for (auto& gameLobby : gameLobbies_) {
            if (gameLobby.GetState() != LobbyState::Playing) continue;

            const port::FrameNumber frame = gameLobby.GetCurrentTick();
            const Types::FrameCommand commands = gameLobby.GetFrameForTick(frame);

            gameLobby.Tick();

            BroadcastConfirmedFrame(gameLobby, frame, commands);
        }
    }

    // ─── port::IClientInput (things a client asks for) ────────
    port::RoomId CreateRoom(port::ClientId owner) override {
        const std::size_t id = nextRoomId_++;
        gameLobbies_.emplace_back(id, static_cast<std::size_t>(owner));
        return static_cast<port::RoomId>(id);
    }

    bool JoinRoom(port::RoomId room, port::ClientId client) override {
        GameLobby* lobby = FindLobby(room);
        if (!lobby) return false;

        const std::size_t before = lobby->GetPlayerIds().size();
        lobby->AddPlayer(static_cast<std::size_t>(client));
        return lobby->GetPlayerIds().size() > before;
    }

    std::vector<port::RoomId> ListRooms() override {
        std::vector<port::RoomId> rooms;
        rooms.reserve(gameLobbies_.size());
        for (const auto& gameLobby : gameLobbies_) {
            rooms.push_back(static_cast<port::RoomId>(gameLobby.GetLobbyId()));
        }
        return rooms;
    }

    void PlayerReadyToStart(port::RoomId room, port::ClientId client) override {
        GameLobby* lobby = FindLobby(room);
        if (!lobby) return;

        lobby->HandlePlayerReady(static_cast<std::size_t>(client));
        if (lobby->GetState() != LobbyState::Playing) return;

        const auto& players = lobby->GetPlayerIds();
        for (std::size_t slot = 0; slot < players.size(); ++slot) {
            network_.NotifyMatchStarted(static_cast<port::ClientId>(players[slot]),
                                        static_cast<std::uint8_t>(slot));
        }
    }

    void SubmitFrameData(port::RoomId room, port::ClientId client,
                         uint8_t frameNumber, std::vector<uint8_t> payload) override {
        GameLobby* lobby = FindLobby(room);
        if (!lobby) return;

        // The lobby peeks at the first byte as the action; the whole payload is
        // kept so it can be relayed to the other player untouched.
        const uint8_t action = payload.empty() ? 0 : payload.front();
        lobby->HandleInput(static_cast<std::size_t>(client), frameNumber, action, payload);
    }

    void ClientDisconnected(port::ClientId client) override {
        const std::size_t id = static_cast<std::size_t>(client);

        for (auto& gameLobby : gameLobbies_) {
            const auto& players = gameLobby.GetPlayerIds();
            if (std::find(players.begin(), players.end(), id) == players.end()) continue;

            gameLobby.HandlePlayerDisconnected(id);

            for (const auto player : players) {
                if (player == id) continue;
                network_.NotifyOpponentDisconnected(static_cast<port::ClientId>(player));
            }
            return; // a client only ever sits in one lobby
        }
    }

    // ─── Getter / Setter ──────────────────────────────────────
    port::INetworkManager& GetNetwork() { return network_; }
    const std::vector<GameLobby>& GetLobbies() const { return gameLobbies_; }

private:
    GameLobby* FindLobby(port::RoomId room) {
        for (auto& gameLobby : gameLobbies_) {
            if (gameLobby.GetLobbyId() == static_cast<std::size_t>(room)) return &gameLobby;
        }
        return nullptr;
    }

    void BroadcastConfirmedFrame(const GameLobby& lobby, port::FrameNumber frame,
                                 const Types::FrameCommand& commands) {
        std::vector<std::pair<port::ClientId, std::vector<std::uint8_t>>> inputs;
        for (const auto& command : commands.playersTick) {
            if (command.playerId == 0) continue; // that slot was silent this tick
            inputs.emplace_back(static_cast<port::ClientId>(command.playerId), command.payload);
        }

        for (const auto player : lobby.GetPlayerIds()) {
            network_.NotifyConfirmedFrame(static_cast<port::ClientId>(player), frame, inputs);
        }
    }

    port::INetworkManager& network_;
    std::vector<GameLobby> gameLobbies_;
    std::size_t nextRoomId_ = 1;
    bool stopTicking = false;
};
