// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <vector>

#include <QString>

/// High-level information about the current match: list of players, game mode, etc.
class Match {
 public:
  enum class PlayerState {
    Playing,
    Resigned,
    Defeated,
    Won,
    Dropped
  };
  
  struct Player {
    QString name;
    int playerColorIndex;
    
    /// Loading percentage of this player. Only relevant before the game start.
    int loadingPercentage = 0;
    
    PlayerState state = PlayerState::Playing;
  };
  
  inline void SetPlayerInfo(const std::vector<Player>& players, int playerIndexInList) {
    this->players = players;
    this->playerIndexInList = playerIndexInList;
  }
  
  inline const std::vector<Player>& GetPlayers() const { return players; }
  inline void SetPlayerLoadingPercentage(int playerIndex, int percentage) { players[playerIndex].loadingPercentage = percentage; }
  inline void SetPlayerState(int playerIndex, PlayerState state) { players[playerIndex].state = state; }
  
  /// Returns the index of this client's player in the player list.
  inline int GetPlayerIndex() const { return playerIndexInList; }
  
  /// Returns a reference to this client's player object.
  inline Player& GetThisPlayer() { return players[playerIndexInList]; }
  
  /// Returns false if this client's player resigned, or the game ended.
  inline bool IsPlayerStillInGame() const { return players[playerIndexInList].state == PlayerState::Playing; }
  
 private:
  std::vector<Player> players;
  
  /// The index of this client's player in the players vector.
  int playerIndexInList;
};
