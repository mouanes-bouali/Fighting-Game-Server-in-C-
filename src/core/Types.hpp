#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace Types {

struct Command {
    std::uint64_t playerId = 0;      // server fills this
    std::uint32_t tick = 0;          // which tick this is for
    std::uint8_t action = 0;         // bitmask of actions
    std::vector<std::uint8_t> payload; // extra data if needed
};
struct FrameCommand{
    std::array<Command, 2> playersTick;
};

} // namespace Types