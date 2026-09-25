#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include "Types.hpp"

enum class LobbyState {
    NotCreated,
    WaitingForPlayers,
    WaitingForReady,
    Playing,
    Finished
};

class GameLobby {
public:
    GameLobby() = default;

    GameLobby(size_t lobbyId, size_t roomOwner)
        : lobbyID_(lobbyId)
        , roomOwner_(roomOwner)
    {
        playersIds_.push_back(roomOwner);
        state_ = LobbyState::WaitingForPlayers;
    }

    ~GameLobby() = default;

    // ─── Getters ──────────────────────────────────────────
    size_t GetLobbyId() const { return lobbyID_; }
    size_t GetRoomOwner() const { return roomOwner_; }
    const std::vector<size_t>& GetPlayerIds() const { return playersIds_; }
    LobbyState GetState() const { return state_; }
    std::uint32_t GetCurrentTick() const { return tickNumber_; }
    bool IsFull() const { return playersIds_.size() >= 2; }

    // ─── Add a joining player ─────────────────────────────
    void AddPlayer(size_t id) {
        if (state_ != LobbyState::WaitingForPlayers) return;
        if (IsFull()) return;

        for (auto existing : playersIds_) {
            if (existing == id) return;
        }

        playersIds_.push_back(id);

        if (IsFull()) {
            state_ = LobbyState::WaitingForReady;
        }
    }

    // ─── Player clicks "ready" ────────────────────────────
    void HandlePlayerReady(size_t id) {
        if (state_ != LobbyState::WaitingForReady) return;

        bool isPlayer = false;
        for (auto existing : playersIds_) {
            if (existing == id) { isPlayer = true; break; }
        }
        if (!isPlayer) return;

        readyPlayers_.insert(id);

        if (readyPlayers_.size() == playersIds_.size()) {
            state_ = LobbyState::Playing;
        }
    }

    bool IsPlayerReady(size_t id) const {
        return readyPlayers_.find(id) != readyPlayers_.end();
    }
    // --- Player left ---------------------------------------
    void HandlePlayerDisconnected(size_t id) {
        for (auto existing : playersIds_) {
            if (existing == id) {
                state_ = LobbyState::Finished;
                return;
            }
        }
    }

    // ─── Receive input ────────────────────────────────────
    bool HandleInput(size_t playerId,
                     std::uint32_t tick,
                     std::uint8_t action,
                     const std::vector<std::uint8_t>& payload = {}) {
        if (state_ != LobbyState::Playing) return false;

        int slot = -1;
        for (size_t i = 0; i < playersIds_.size(); i++) {
            if (playersIds_[i] == playerId) {
                slot = static_cast<int>(i);
                break;
            }
        }
        if (slot == -1) return false;

        std::uint32_t targetTick = std::max(tick, tickNumber_);

        auto& frame = commandBuffer_[targetTick];

        frame.playersTick[slot].playerId = playerId;
        frame.playersTick[slot].tick = targetTick;
        frame.playersTick[slot].action = action;
        frame.playersTick[slot].payload = payload;

        return true;
    }

    // ─── Test helper ──────────────────────────────────────
    Types::FrameCommand GetFrameForTick(std::uint32_t tick) const {
        auto it = commandBuffer_.find(tick);
        if (it == commandBuffer_.end()) return {};
        return it->second;
    }

    // ─── The tick ─────────────────────────────────────────
    void Tick() {
        if (state_ != LobbyState::Playing) return;

        // 1. Grab the frame for the current tick
        auto it = commandBuffer_.find(tickNumber_);
        Types::FrameCommand frame;
        if (it != commandBuffer_.end()) {
            frame = std::move(it->second);
            commandBuffer_.erase(it);
        }

        // 2. Process (broadcast happens here, later)
        // For now, just advance.

        // 3. Advance
        tickNumber_++;
    }

private:
    size_t lobbyID_ = 0;
    size_t roomOwner_ = 0;
    std::map<std::uint32_t, Types::FrameCommand> commandBuffer_;
    std::uint32_t tickNumber_ = 0;
    std::vector<size_t> playersIds_;
    std::set<size_t> readyPlayers_;
    LobbyState state_ = LobbyState::NotCreated;
};