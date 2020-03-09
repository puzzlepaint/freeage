#pragma once

#include <memory>

#include <QObject>

#include "FreeAge/client/map.hpp"
#include "FreeAge/client/match.hpp"
#include "FreeAge/client/render_window.hpp"
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
  
  inline void SetRenderWindow(const std::shared_ptr<RenderWindow> renderWindow) { this->renderWindow = renderWindow; }
  
  inline double GetGameStartServerTimeSeconds() const { return gameStartServerTimeSeconds; }
  
 private slots:
  void ParseMessage(const QByteArray& buffer, ServerToClientMessage msgType, u16 msgLength);
  
 private:
  // Network message handlers
  void HandleLoadingProgressBroadcast(const QByteArray& buffer, u16 msgLength);
  void HandleGameBeginMessage(const QByteArray& buffer);
  void HandleMapUncoverMessage(const QByteArray& buffer);
  void HandleAddObjectMessage(const QByteArray& buffer);
  
  std::shared_ptr<ServerConnection> connection;
  std::shared_ptr<Match> match;
  std::shared_ptr<Map> map;
  std::shared_ptr<RenderWindow> renderWindow;
  
  double gameStartServerTimeSeconds = std::numeric_limits<double>::max();
};
