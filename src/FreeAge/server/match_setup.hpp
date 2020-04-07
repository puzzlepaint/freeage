// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <memory>

#include <QByteArray>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>

#include "FreeAge/common/free_age.hpp"
#include "FreeAge/server/settings.hpp"

/// Represents a player who joined a match that has not started yet.
struct PlayerInMatch {
  enum class State {
    /// Initial state. The connection was made, but the client needs to authorize itself.
    Connected,
    
    /// The client authorized itself. It is displayed as a player in the list.
    Joined
  };
  
  /// Socket that can be used to send and receive data to/from the player.
  QTcpSocket* socket;
  
  /// Buffer for bytes that have been received from the client, but could not
  /// be parsed yet (because only a partial message was received so far).
  QByteArray unparsedBuffer;
  
  /// Whether this client can administrate the match.
  bool isHost;
  
  /// The player name as provided by the client.
  QString name;
  
  /// The player color index.
  int playerColorIndex;
  
  /// Whether the player clicked the "ready" check box.
  bool isReady;
  
  /// The time at which the connection was made. This can be used to time the
  /// client out if it does not authorize itself within some time frame.
  TimePoint connectionTime;
  
  /// Current state of the connection.
  State state;
  
  /// The last point in time at which a ping was received from this player.
  TimePoint lastPingTime;
};

/// Returns true if the game has been started, false if the game has been aborted.
bool RunMatchSetupLoop(
    QTcpServer* server,
    std::vector<std::shared_ptr<PlayerInMatch>>* playersInMatch,
    ServerSettings* settings);
