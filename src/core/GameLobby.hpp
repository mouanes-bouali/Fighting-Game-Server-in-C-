#pragma once

#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

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
    bool IsFull() const { return playersIds_.size() >= 2; }

    // ─── Add a joining player ─────────────────────────────
    void AddPlayer(size_t id) {
        if (state_ != LobbyState::WaitingForPlayers) return;
        if (IsFull()) return;

        // Reject duplicates
        for (auto existing : playersIds_) {
            if (existing == id) return;
        }

        playersIds_.push_back(id);

        // When the lobby fills, move to the ready phase
        if (IsFull()) {
            state_ = LobbyState::WaitingForReady;
        }
    }
     void Tick(){

        // send to client tick number
        // wait 30ms 
        //treat data 

    }

    // ─── Player clicks "ready" ────────────────────────────
    void HandlePlayerReady(size_t id) {
        if (state_ != LobbyState::WaitingForReady) return;

        // Only accept known players
        bool isPlayer = false;
        for (auto existing : playersIds_) {
            if (existing == id) { isPlayer = true; break; }
        }
        if (!isPlayer) return;

        // Mark ready (set deduplicates automatically)
        readyPlayers_.insert(id);

        // Everyone ready → start
        if (readyPlayers_.size() == playersIds_.size()) {
            state_ = LobbyState::Playing;
        }
    }

    // ─── Query: has this player clicked ready? ────────────
    bool IsPlayerReady(size_t id) const {
        return readyPlayers_.find(id) != readyPlayers_.end();
    }

private:
    size_t lobbyID_ = 0;
    size_t roomOwner_ = 0;
    std::uint32_t tickNumber_= 0;
    std::vector<size_t> playersIds_;
    std::set<size_t> readyPlayers_;
    LobbyState state_ = LobbyState::NotCreated;
};