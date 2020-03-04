#pragma once

#include <memory>
#include <vector>

#include <QByteArray>
#include <QString>
#include <QTcpSocket>

#include "FreeAge/common/free_age.hpp"
#include "FreeAge/server/settings.hpp"

/// Represents a player in a game.
struct PlayerInGame {
  /// The player's index in the playersInGame vector.
  int index;
  
  /// Socket that can be used to send and receive data to/from the player.
  QTcpSocket* socket;
  
  /// Buffer for bytes that have been received from the client, but could not
  /// be parsed yet (because only a partial message was received so far).
  QByteArray unparsedBuffer;
  
  /// The player name as provided by the client.
  QString name;
  
  /// The player color index.
  int playerColorIndex;
  
  /// The last point in time at which a ping was received from this player.
  TimePoint lastPingTime;
};

void RunGameLoop(std::vector<std::shared_ptr<PlayerInGame>>* playersInGame, ServerSettings* settings);
