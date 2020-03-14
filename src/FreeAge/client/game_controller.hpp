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
  
  void ProduceUnit(const std::vector<u32>& selection, UnitType type);
  
  /// Returns the resource amount of the player at the queried server time.
  /// The returned reference is only valid until the next call to this function.
  /// Server times must be queried in increasing order only (since old values are dropped).
  const ResourceAmount& GetCurrentResourceAmount(double serverTime);
  
 private slots:
  void ParseMessage(const QByteArray& buffer, ServerToClientMessage msgType, u16 msgLength);
  
 private:
  // Network message handlers
  void HandleLoadingProgressBroadcast(const QByteArray& buffer, u16 msgLength);
  void HandleGameBeginMessage(const QByteArray& buffer);
  void HandleMapUncoverMessage(const QByteArray& buffer);
  void HandleAddObjectMessage(const QByteArray& buffer);
  void HandleUnitMovementMessage(const QByteArray& buffer);
  void HandleGameStepTimeMessage(const QByteArray& buffer);
  void HandleResourcesUpdateMessage(const QByteArray& buffer);
  
  std::shared_ptr<ServerConnection> connection;
  std::shared_ptr<Match> match;
  std::shared_ptr<Map> map;
  std::shared_ptr<RenderWindow> renderWindow;
  
  double gameStartServerTimeSeconds = std::numeric_limits<double>::max();
  double currentGameStepServerTime;
  
  /// Player resources over time. The double in the pair gives the server time
  /// from which on the resource amount is valid. Entries in the list are ordered
  /// by increasing server time.
  ///
  /// Outdated entries are deleted. Thus:
  /// * The first entry is the one which should currently be displayed.
  /// * The last entry is the most up-to-date one, which should be used for deciding
  ///   whether unit/technology buttons are active.
  std::vector<std::pair<double, ResourceAmount>> playerResources;
};
