# Fighting Game Server in C++

the player enters the game, selects to play online, create or join a server.
another play selects online and than create a room and when shared , other player can be joined and shown in the lobby, when both press start, the game scene starts, each player is assigned in its postion and the other player postion we put a bot, when enemy press move , it sends a packet to the server and and also the player movment , and then check the ping , if everything is right it sends them both the inputs they gave , making sure each one getting his inputs and the enemy inputs, and the game runs the input logics, and so on until someone dies, if someone lost connection the game stops, 

# Functional Requirements
-Lobby creation, Player A creates a lobby with a unique ID and shares the ID with Player B. Player B joins by entering the ID.

-Lobby state, Both players appear in the lobby UI. Neither can start the match until both have joined.

Match start — Both players press "Start". Server runs a 3-second countdown.

-Ready handshake — After the countdown, both clients load assets, then send "Ready". Server does not start the tick loop until both are ready.

-Simulation — Each client runs the full game simulation locally. Both fighters are simulated on both machines. The opponent's fighter is driven by the opponent's real inputs, not a bot.

Input relay — When a player presses a button, the client:

Applies it locally (prediction)

Sends it to the server

-Server broadcast — The server collects commands per tick, sorts them deterministically (by playerId, then type), and broadcasts the batch to both clients.

-Rollback — If the server's confirmed inputs contradict the client's prediction, the client rolls back to the affected tick, re-applies the correct inputs, and re-simulates forward.

-Win condition — When a player's HP reaches 0, the match ends. The winner is announced.

-Disconnect — If a player disconnects, the server waits N seconds for reconnection. If they return, the match resumes. If not, the match ends.

# Non-Functional Requirements
Tick rate — The server ticks at 20Hz (50ms per tick).

Tick broadcast — Every tick, the server broadcasts the tick number to both players.

Input window — Commands are accepted if their tick is within ±3 ticks of the current tick.

Late input handling — If a command arrives late (tick already passed), it's re-stamped to the current tick and used going forward.

Determinism — All simulation code uses integer/fixed-point math only. No floating-point.

Random seed — A single random seed is generated at match creation and shared with both clients.

Timeout — If a player is silent for 5 seconds, they're marked as disconnected.