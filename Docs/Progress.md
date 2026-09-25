# Project Progress — Fighting Game Server in C++

> Snapshot for an external tracker/reviewer. Self-contained: architecture, wire format,
> what is done, what is missing, known issues and the decision log.
> **Date:** 2026-09-25 · **Branch:** `main` · **Last commit:** `628d2ea` (docs: add architecture and tick-flow diagrams)
> **Everything described below is UNCOMMITTED working-tree state.** Verify with `git status --short`.

---

## 1. Snapshot

| Metric | Value |
|---|---|
| Source (excl. tests) | **1,065 lines** across 10 files |
| Tests | **1,080 lines** across 8 files |
| Test result | **70 test cases / 197 assertions — all passing** |
| Compiler warnings | none (`-Wall -Wextra -Wpedantic` clean) |
| Language / std | C++20 |
| Toolchain | clang 23.1.1 → `--target=x86_64-w64-mingw32`, Ninja, CMake 4.4.2, preset `clang-mingw` |
| Dependencies | **ENet 1.3.18** (built from source, installed at `C:/mingw64`), Catch2 v2 (vendored, header-only) |
| Link libs | `enet`, `winmm`, `ws2_32`, `pthread` |
| Runnable binaries | **none** — CMake builds only `tests` (blocker B1) |

### Progress estimate

| Scope | Done |
|---|---|
| Networking spine (ports, adapter, codec, input relay, tests) | **~90%** |
| Full README feature set (rules, countdown, reconnect, timeouts) | **~50-55%** |
| Unity / real-game client | **0%** (not started) |

### Status legend
`[x]` done & tested · `[~]` partial · `[ ]` not started · `[!]` blocked

---

## 2. What this project is

A **dedicated authoritative server** for a 2-player online fighting game (rollback-netcode style).
Clients run the full simulation locally; the server **collects each player's input per tick and
broadcasts the confirmed batch to both players** so both machines simulate the same fight.

Scope of this repo: the **server only**. Client-side prediction, rollback, rendering and UI belong to
the game client (target: Unity) and are not part of this codebase.

Requirements come from `README.md` (functional + non-functional) and `Docs/Diagrams.md` (context /
architecture / lobby state machine / tick sequence).

---

## 3. Architecture

Hexagonal (ports & adapters). The rule is *who may know what*:

- `src/port/` — interfaces only. The contract, no logic.
- `src/core/` — game logic. Never mentions ENet, sockets or bytes.
- `src/adapter/` — translators. The only place bytes exist.

```
        Unity / game client
              |  UDP
              v
+-----------------------------------------------------------------+
| src/adapter/ENet                                                |
|   EnetManager : public port::INetworkManager  (the adapter)      |
|     +-- Protocol.hpp/.cpp   bytes <-> C++ structs                |
|     +-- IClientInput* _inputHandler --------------------+        |
+---------------------------------------------------------|-------+
              ^                                           |
              | INetworkManager&        IClientInput&      |
              | (PollEvents / Notify*)  (CreateRoom / ...) |
+---------------------------------------------------------|-------+
| src/core/  ServerManager : public port::IClientInput <--+        |
|              +-- std::vector<GameLobby>  (tick + command buffer) |
+-----------------------------------------------------------------+
```

### The three ports

| Interface | Direction | Methods | Implemented by |
|---|---|---|---|
| `IClientInput` | client → server | `CreateRoom(owner)`, `JoinRoom`, `ListRooms`, `PlayerReadyToStart`, `SubmitFrameData`, `ClientDisconnected` | **ServerManager** |
| `IClientNotifier` | server → client | `NotifyLobbyUpdated`, `NotifyMatchStarted`, `NotifyConfirmedFrame`, `NotifyOpponentDisconnected` | **EnetManager** |
| `INetworkManager` | = `IClientNotifier` + `Init`/`Shutdown`/`PollEvents`/`SetInputHandler` | | **EnetManager** |

`EnetManager` **inherits** the notifier and **holds a raw pointer** to the input handler. The two
objects point at each other; neither derives from the other. No `EnetSender` class exists (deliberately
dropped as over-design — the manager *is* the notifier).

Strong types: `port::RoomId` / `port::ClientId` are `enum class : size_t`; `FrameNumber` is `uint32_t`.

### Composition root (intended `main`)

```cpp
EnetManager net;                  // declared first => outlives the core
ServerManager server(net);        // core holds INetworkManager&
net.SetInputHandler(&server);     // adapter holds IClientInput*
net.Init(8080);
server.Run();                     // loop: Step() then sleep
```

### Control flow

**Inbound (one client command):**
`PollEvents` → `ENET_EVENT_TYPE_RECEIVE` → `OnReceive` → `FindByPeer` (trusted `ClientId`) →
`DecodeClientRequest` → switch on message id → `_inputHandler->...` → `ServerManager` →
`GameLobby::HandleInput` stores it in `commandBuffer_[tick]`.

**Tick / outbound (`ServerManager::Step` = one server frame):**
1. `network_.PollEvents()`
2. for each lobby in state `Playing`: `frame = GetCurrentTick()` → `commands = GetFrameForTick(frame)`
   → `Tick()` (advances the tick, consumes that frame)
3. `BroadcastConfirmedFrame(lobby, frame, commands)` → **same input list to both players**
4. `NotifyConfirmedFrame` → `EncodeConfirmedFrame` → `SendTo` → `enet_peer_send` + `flush`

`Step()` is split from `Run()` specifically so tests can drive one frame directly, no thread needed.

---

## 4. Wire protocol v0 (binary, little-endian)

Implemented in `src/adapter/ENet/Protocol.hpp/.cpp`. Transport-agnostic — it would run unchanged over
raw UDP. One message per packet; every read is bounds-checked, so garbage/truncated input is dropped
rather than trusted.

**Client → Server**

| id | Message | Payload after the id byte |
|---|---|---|
| `0x01` | CreateRoom | *(none)* |
| `0x02` | JoinRoom | `u64 room` |
| `0x03` | ListRooms | *(none)* |
| `0x04` | ReadyToStart | `u64 room` |
| `0x05` | SubmitFrameData | `u64 room`, `u8 frame`, `u16 len` + `len` bytes |

**Server → Client**

| id | Message | Payload |
|---|---|---|
| `0x81` | LobbyUpdated | `u32 count`, then `count` × `u64 room` |
| `0x82` | MatchStarted | `u8 slot` (0 or 1) |
| `0x83` | ConfirmedFrame | `u32 frame`, `u32 count`, then `count` × (`u64 clientId`, `u16 len` + bytes) |
| `0x84` | OpponentDisconnected | *(none)* |

**Rules / limits**

- ENet channels: **0** = control, **1** = confirmed frames → a client must connect with **≥ 2 channels**.
- `ClientId` values start at **1** (0 means "nobody"), assigned on connect; a client can never claim one.
- `slot` = index in join order (0 = room creator).
- Blobs capped at **1024 bytes**, list counts at **1024** (decoder rejects bigger; encoder truncates).
- No protocol version byte, no handshake, no auth/token, no reconnect token.

**Missing for a real client (protocol gaps):** there is no message that tells a client *its own*
`ClientId`, so it cannot tell which entry in `ConfirmedFrame` is its own input; the room creator is
never told its new room id (it only receives a room *list*); there is no `MatchEnded`; `MatchStarted`
carries no RNG seed.

---

## 5. Build & run

```powershell
cmake --preset clang-mingw                 # configure (finds ENet, reports its paths)
cmake --build --preset clang-mingw         # build
ctest --test-dir build --output-on-failure # or: .\build\tests.exe
```

Configure output proves the dependency is wired:

```
-- ENet include : C:/mingw64/include
-- ENet library : C:/mingw64/lib/libenet.a
```

**ENet install (one-time, already done on this machine):**

```powershell
git clone --depth 1 https://github.com/lsalzman/enet.git
cmake -S enet -B enet-build -G Ninja -DCMAKE_INSTALL_PREFIX=C:/mingw64 `
      -DCMAKE_C_COMPILER=clang -DCMAKE_C_COMPILER_TARGET=x86_64-w64-mingw32
cmake --build enet-build --target install
```

`CMakeLists.txt` fails loudly with these instructions if ENet is missing, and accepts
`-DENET_INCLUDE_DIR=... -DENET_LIBRARY=...` overrides.

### Toolchain gotchas discovered (all solved)

- **ENet's own `CMakeLists.txt` targets CMake < 3.5**, which CMake 4 refuses → its
  `cmake_minimum_required` had to be raised before building ENet.
- **`undefined symbol: nanosleep64`** when linking anything using `std::this_thread::sleep_for`:
  this MinGW is the **posix-seh (winpthreads)** variant and clang does not add `-lpthread`
  the way g++ does → `pthread` added to `target_link_libraries`.
- **`enum class : size_t` cannot be initialised from `int`** — `port::ClientId id = 0;` and
  `_nextClientId++` do not compile; the counter is a `std::size_t` with an explicit cast.

### Test inventory

| File | Lines | Covers |
|---|---|---|
| `TestProtocol.cpp` | 188 | round-trip of all 9 messages, empty payloads, wrong id, truncated packets, oversized blob/count |
| `TestGameLobby.cpp` | 85 | pre-existing lobby state tests (unchanged, still green) |
| `TestGameLobbyMatch.cpp` | 91 | payload preserved for relay, action-only input, ignored input, late input re-stamp, tick consumes frame, disconnect |
| `TestServerManager.cpp` | 212 | 12 cases against a `FakeNetwork`: create/join/ready/slots, broadcast to both players, silent player, tick advance, disconnect, finished match |
| `TestEnetManager.cpp` | 216 | **real loopback sockets**: bind, double init/shutdown, connect → id, two clients, all 5 requests routed with sender id, disconnect, garbage packet, no-core case |
| `TestEnetNotifier.cpp` | 97 | all four `Notify*` arrive and decode on a real client; notifying a non-connected client is a no-op |
| `EnetTestHelpers.hpp` | 187 | shared `RecordingCore`, `TestClient` (raw ENet client), `Settle()` pump helper |

`FakeNetwork : INetworkManager` and `RecordingCore : IClientInput` are the payoff of the ports: the core
is tested with no sockets at all, and the adapter is tested with no game logic at all.

---

## 6. Implemented (done & tested)

**Build / infra**
- [x] CMake finds, verifies and links ENet; reports its paths at configure time; clangd gets the include dir via `compile_commands.json`.
- [x] Catch2 v2 suite auto-globs `test/*.cpp`; `ctest` green.
- [x] Codebase compiles warning-free under `-Wall -Wextra -Wpedantic`.

**Ports**
- [x] `IClientNotifier` with strong ids (`RoomId`, `ClientId`, `FrameNumber`); `#pragma once` + correct includes.
- [x] `IClientInput` (renamed from the typo'd `IClinetInput`) — reuses the same strong types, so both
      interfaces cannot silently disagree on method signatures.
- [x] `INetworkManager : IClientNotifier` + `Init`/`Shutdown`/`PollEvents`/`SetInputHandler`.

**Adapter (`src/adapter/ENet`)**
- [x] Binary codec: 5 client + 4 server messages, writer/reader, bounds-checked, malformed input dropped.
- [x] `EnetManager` implements `INetworkManager`; peer ↔ `ClientId` map (ids from 1); connect / receive / disconnect handling.
- [x] Inbound routing that always stamps the **sender's own** `ClientId` (no spoofing).
- [x] All four `Notify*` actually build and send packets (channels 0/1, reliable, `flush`); sending to an unknown client is a safe no-op.
- [x] `IsRunning()`, `GetClientCount()`, `GetClientIds()`, `GetInputHandler()`.
- [x] Double `Init` / double `Shutdown` are safe; `PollEvents` before `Init` is a no-op.

**Core (`src/core`)**
- [x] `GameLobby` state machine: `NotCreated → WaitingForPlayers → WaitingForReady → Playing → Finished`.
- [x] `GameLobby::HandleInput` stores the input **and now keeps the raw payload** (it used to discard it, which would have made relaying send empty bytes); late inputs re-stamped to the current tick.
- [x] `GameLobby::HandlePlayerDisconnected` → `Finished`.
- [x] `ServerManager` implements `IClientInput`; rooms get unique ids; the creator becomes the sole first player.
- [x] Both-ready → `NotifyMatchStarted` to both players with slots 0 and 1.
- [x] `Step()` advances every playing match one tick and broadcasts `NotifyConfirmedFrame` to **both** players with the same list of both inputs.
- [x] Disconnect → lobby `Finished` + `NotifyOpponentDisconnected` to the surviving player.
- [x] Lobbies, ticks and inputs are integer-only (no floating point anywhere).

**Removed / cleaned**
- [x] Deleted three empty duplicate stubs (`adapter/ServerOutput.hpp`, `adapter/ClientInput.hpp`, `core/ClientNotifier.hpp`) — `EnetManager` is the single adapter.
- [x] Deleted the standalone `EnetSender` idea and the superseded `core/EnetManager.hpp` stub.

---

## 7. Not done / roadmap

### Hard blockers for anything end-to-end

**[!] B1 — no runnable server.** There is no `main.cpp` and no server executable target; CMake builds
only `tests`. There is nothing to launch, so nothing can connect to it.

**[!] B2 — a client cannot interpret the traffic.** The server never tells a client its own `ClientId`,
so `ConfirmedFrame`'s `(clientId, input)` pairs are ambiguous — a client cannot tell its own input from
the opponent's. Also missing: the creator's room id, `MatchEnded`, a protocol version byte.

### Server milestones

| | Milestone | Contents | Status |
|---|---|---|---|
| M1 | **Make it run** | `main.cpp`, `server` target, real 20 Hz loop, port from argv, Ctrl-C → `Stop()`, basic logging | `[ ]` |
| M2 | **Match lifecycle** | 3-second countdown state, separate "press Start" vs "assets Ready", RNG seed inside `MatchStarted`, per-tick heartbeat | `[ ]` |
| M3 | **Rules** | action/type model, HP + cooldowns, deterministic sort (by playerId, then type), input validation, `NotifyMatchEnded` | `[ ]` |
| M4 | **Resilience** | 5-second silence timeout, reconnect window with resume, lobby cleanup / leave-room, ±3 tick input window | `[ ]` |
| M5 | **Hardening** | full-stack socket test (2 real clients ↔ adapter ↔ core), protocol version byte, malformed-traffic fuzzing | `[ ]` |

### Client track (Unity)

| | Item | Status |
|---|---|---|
| C1 | Decide the transport for the Unity client (§9 decision D8 — **open**) | `[ ]` |
| C2 | `UnityClient.cpp` native plugin (C ABI over the same stock ENet + shared `Protocol.cpp`) | `[ ]` |
| C3 | `NetClient.cs` (`[DllImport]` wrapper + per-frame `NetPoll` + `BinaryReader` decode) | `[ ]` |
| C4 | Drop DLL into `Assets/Plugins/x86_64/`, smoke test one client → CreateRoom → Ready → read frames | `[ ]` |
| C5 | Bake the client-side simulation/rollback (outside this repo) | `[ ]` |

### Requirement checklist vs README

| README requirement | Status |
|---|---|
| Lobby creation with unique ID + share | `[x]` |
| Lobby state, both players visible, can't start alone | `[x]` |
| Match start + 3-second countdown | `[ ]` (starts instantly) |
| Ready handshake, tick loop only after both ready | `[x]` |
| Input relay (each gets own + opponent inputs) | `[x]` |
| Deterministic sort by playerId then type | `[~]` (slot order, no sort/type) |
| Server-side validation (HP, cooldowns) | `[ ]` |
| Win condition (HP 0) + winner announced | `[ ]` |
| Disconnect: wait N seconds, resume or end | `[~]` (ends immediately) |
| Tick rate 20 Hz (50 ms) | `[ ]` (see bug I1) |
| Tick broadcast every tick | `[~]` (only inside `ConfirmedFrame`) |
| ±3 tick input window | `[ ]` |
| Late input re-stamping | `[x]` |
| Determinism, no floating point | `[x]` |
| Random seed shared at match creation | `[ ]` |
| 5-second silence timeout | `[ ]` |
| Ping check mentioned in the README flow | `[ ]` |

---

## 8. Known issues

**I1 — tick rate is wrong (real bug).** `ServerManager::Run()` sleeps **50 microseconds** and `Step()`
advances every playing lobby by one tick per iteration, so the tick rate equals the loop rate (CPU
speed), not the required **50 ms / 20 Hz**. Everything time-based (countdown, silence timeout, input
window) depends on this being fixed first.

**I2 — no shutdown path.** `Stop()` exists but nothing calls it; there is no Ctrl-C / signal handling,
so a running server could only be killed.

**I3 — attacker-controlled memory growth.** `GameLobby::HandleInput` does
`targetTick = max(tick, tickNumber_)` and inserts into `commandBuffer_[targetTick]`. A client sending a
huge tick number creates an entry far in the future; there is no window check or cap, so the map can
grow without bound.

**I4 — nothing tells a client its own `ClientId`** (see B2), which makes `ConfirmedFrame` unusable for
its main purpose (distinguishing own input from opponent's for rollback).

**I5 — no protocol version byte.** A client built against a different message layout is not rejected; its
packets just silently fail to decode.

**I6 — no diagnostics.** There is no logging anywhere; a dropped/malformed packet is invisible.

**I7 — hard-coded limits.** `EnetManager` fixes 32 max clients and 2 channels; `Types::FrameCommand` is
`std::array<Command, 2>` (exactly 2 players), and `playerId == 0` is overloaded as the "silent slot"
sentinel (safe only because real ids start at 1).

**I8 — lobbies are never reclaimed.** `std::vector<GameLobby>` only grows; rooms never expire, ids are
never reused, and there is no "leave room" request.

**I9 — empty frames are still sent**, so a quiet tick does produce a packet (this effectively acts as the
per-tick heartbeat while a match is `Playing`). Worth making explicit in the protocol docs.

---

## 9. Decision log

| # | Decision | Rationale |
|---|---|---|
| D1 | Hexagonal ports (`port/` + `core/` + `adapter/`) | Lets the transport be swapped without touching game logic — already paying off (ENet today, raw UDP or a Unity plugin are candidates). |
| D2 | No standalone `EnetSender` class | The manager *is* the notifier (it inherits `IClientNotifier`); a second class was over-design. |
| D3 | `EnetManager` holds `IClientInput*` (raw); `ServerManager` holds `INetworkManager&` | Neither owns the other. `EnetManager` must be declared first in `main` so the core dies first and the pointer stays valid. |
| D4 | Deleted `adapter/ServerOutput.hpp`, `adapter/ClientInput.hpp`, `core/ClientNotifier.hpp` | Empty duplicates of what `EnetManager` already does. |
| D5 | Renamed `IClinetInput` → `IClientInput`; single set of strong types | Typos aside, the two port headers originally declared *different* `RoomId`/`ClientId` types, which could silently disagree on method signatures. |
| D6 | `CreateRoom()` → `CreateRoom(ClientId owner)` | Without the owner the server fabricated owner 0; the creator must be recorded as the first player. |
| D7 | Added `IClientInput::ClientDisconnected(ClientId)` | The core had no way to be told a peer dropped, yet the README requires disconnect handling. |
| D8 | **OPEN — Unity client transport** | (a) C# ENet binding: the two popular ones state they use a *modified* ENet protocol, so interop with stock ENet is unverified. (b) C++ native plugin DLL over the same stock ENet + a shared `Protocol.cpp`: **verified buildable on this machine** (`-shared`, unmangled exports, `-static` → self-contained 43,520-byte DLL). (c) Raw UDP adapter: no dependencies, Unity's built-in `UdpClient`, but reliability/ordering/handshake must be written by hand. Leaning (b), not decided. |
| D9 | Request/response ops answered by the adapter; events raised by the core | The adapter is the only layer that knows *who* asked, so one-way state changes are acknowledged there; the core raises match events. Prevents double notifications. |
| D10 | `GameLobby::HandleInput` keeps the raw payload | It previously discarded it, which would have relayed empty inputs — the relay feature would have been silently broken. |
| D11 | `EnetManager` kept even though raw UDP is a candidate | No decision to delete yet; both can implement the same port. |
| D12 | Tests use fakes at the port boundary (`FakeNetwork`, `RecordingCore`) | Core tested without sockets; adapter tested with real loopback sockets but no game logic. |
| D13 | ENet installed into the MinGW prefix (`C:/mingw64`), not vendored | Keeps the repo small; CMake finds it and fails loudly with install instructions if absent. |

---

## 10. Next actions (recommended order)

1. **[M1] Make the server runnable + [B2] `Welcome` message.** `main.cpp` + a `server` CMake target, the
   20 Hz loop fix (I1), port from argv, Ctrl-C → `Stop()`, and a `Welcome` message carrying the assigned
   `ClientId` + a protocol version byte. Nothing on the client side can work before this. ~1 session.
2. **[D8] Decide the Unity transport** and freeze protocol **v1** (version byte, room-created reply,
   `MatchEnded`, seed in `MatchStarted`).
3. **[C2/C3] Native plugin + C# client**, then one end-to-end smoke test: Unity connects → `CreateRoom`
   → `Ready` → both clients receive the same `ConfirmedFrame`.

After that, the remaining README scope is M2–M5 (countdown, rules/HP/validation, reconnect, hardening).

### How to verify this report

```powershell
git --no-pager status --short          # everything here is uncommitted
git --no-pager log --oneline -1        # HEAD = 628d2ea
cmake --preset clang-mingw             # should print "ENet include/library"
cmake --build --preset clang-mingw
ctest --test-dir build                 # expect: 100% tests passed, 70 cases / 197 assertions
```

### Current working-tree state (uncommitted)

```
 M CMakeLists.txt              (ENet discovery + link, server sources, pthread)
 M src/core/GameLobby.hpp      (payload kept, HandlePlayerDisconnected)
 M src/core/ServerManager.hpp  (INetworkManager& + Step() + full IClientInput)
 M src/port/ServerOutput/IClientNotifier.hpp   (pragma once, includes)
 D src/core/ClientNotifier.hpp                 (deleted duplicate stub)
 M src/core/Types.hpp          (pre-existing edit, not part of this work)
 M README.md                   (pre-existing edit, not part of this work)
?? src/adapter/ENet/{Protocol.hpp,Protocol.cpp,EnetManager.hpp,EnetManager.cpp}   (new)
?? src/port/INetworkManager.hpp / src/port/ClientInput/IClientInput.hpp           (new)
?? test/{TestProtocol,TestServerManager,TestEnetManager,TestEnetNotifier,TestGameLobbyMatch}.cpp + EnetTestHelpers.hpp
```

