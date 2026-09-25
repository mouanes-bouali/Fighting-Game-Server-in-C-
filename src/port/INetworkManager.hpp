#pragma once

#include "ClientInput/IClientInput.hpp"
#include "ServerOutput/IClientNotifier.hpp"

#include <cstdint>

namespace port {

// The network adapter, as the core sees it.
//   * it sends    -> IClientNotifier
//   * it receives -> SetInputHandler(IClientInput*)
class INetworkManager : public IClientNotifier {
public:
    virtual ~INetworkManager() = default;

    // ─── Lifecycle ────────────────────────────────────────────
    virtual void Init(std::uint16_t port) = 0;
    virtual void Shutdown() = 0;

    // ─── Pump ─────────────────────────────────────────────────
    virtual void PollEvents() = 0;

    // ─── Getter / Setter ──────────────────────────────────────
    virtual void SetInputHandler(IClientInput* handler) = 0;
};

} // namespace port
