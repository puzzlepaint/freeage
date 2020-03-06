#pragma once

#include <memory>

#include <QObject>

#include "FreeAge/client/match.hpp"
#include "FreeAge/client/server_connection.hpp"

/// Handles the game on the client side:
/// * Handles incoming messages from the server
/// * Updates the game state such that it can be rendered
/// * Handles user input
class GameController : public QObject {
 Q_OBJECT
 public:
  GameController(const std::shared_ptr<Match>& match, const std::shared_ptr<ServerConnection>& connection);
  ~GameController();
  
 private slots:
  void ParseMessage(const QByteArray &buffer, ServerToClientMessage msgType, u16 msgLength);
  
 private:
  // Network message handlers
  void HandleLoadingProgressBroadcast(const QByteArray &buffer, u16 msgLength);
  
  std::shared_ptr<ServerConnection> connection;
  const std::shared_ptr<Match> match;
};
