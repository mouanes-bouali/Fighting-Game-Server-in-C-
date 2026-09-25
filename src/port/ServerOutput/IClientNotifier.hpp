#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace port {
// ─── Strong types ─────────────────────────────────────────────
enum class RoomId : size_t {};
enum class ClientId : size_t {};
using FrameNumber = uint32_t;

// ─── Server → Client (outbound) ──────────────────────────────
class IClientNotifier {
public:
    virtual ~IClientNotifier() = default;

    virtual void NotifyLobbyUpdated(ClientId client,
                                     const std::vector<RoomId>& rooms) = 0;

    virtual void NotifyMatchStarted(ClientId client,
                                     uint8_t assignedSlot) = 0;

    virtual void NotifyConfirmedFrame(
        ClientId client,
        FrameNumber frameNumber,
        const std::vector<std::pair<ClientId, std::vector<uint8_t>>>& inputs) = 0;

    virtual void NotifyOpponentDisconnected(ClientId client) = 0;
};
}
