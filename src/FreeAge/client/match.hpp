#pragma once

#include <vector>

#include <QString>

/// High-level information about the current match: list of players, game mode, etc.
class Match {
 public:
  struct Player {
    QString name;
    int playerColorIndex;
    
    /// Loading percentage of this player. Only relevant before the game start.
    int loadingPercentage = 0;
  };
  
  inline void SetPlayerInfo(const std::vector<Player>& players, int playerIndexInList) {
    this->players = players;
    this->playerIndexInList = playerIndexInList;
  }
  
  inline const std::vector<Player>& GetPlayers() const { return players; }
  inline void SetPlayerLoadingPercentage(int playerIndex, int percentage) { players[playerIndex].loadingPercentage = percentage; }
  inline int GetPlayerIndex() const { return playerIndexInList; }
  
 private:
  std::vector<Player> players;
  
  /// The index of this client's player in the players vector.
  int playerIndexInList;
};
