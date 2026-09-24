#include "GameLobby.hpp"
#include <chrono>
#include <thread>
#include <vector>
class ServerManager {
public:
    ServerManager();
    ~ServerManager();
    void Tick(){
        while (!stopTicking) {
        // do the logic 
           
            for (auto& gameLobby  : gameLobbies_) {
                gameLobby.Tick();
            
            }

        // sleep
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    }


private:
std::vector<GameLobby> gameLobbies_;
std::chrono::seconds time= std::chrono::seconds(0);
bool stopTicking=false;
};