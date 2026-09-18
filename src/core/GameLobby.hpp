class GameLobby {
public:
    GameLobby();
    GameLobby(const GameLobby& other);
    GameLobby& operator=(const GameLobby& other);
    GameLobby(GameLobby&& other) noexcept;
    GameLobby& operator=(GameLobby&& other) noexcept;
    ~GameLobby();

private:
    // members
};