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
  
  void ParseMessagesUntil(double displayedServerTime);
  
  void ProduceUnit(const std::vector<u32>& selection, UnitType type);
  
  /// Returns the current resource amount of the player.
  inline const ResourceAmount& GetCurrentResourceAmount() { return playerResources; }
  
  /// Returns the latest known resource amount, even if this is for a server time that
  /// should not be displayed yet. This value is used to determine whether to make
  /// "produce unit" or "research technology" buttons active or inactive.
  inline const ResourceAmount& GetLatestKnownResourceAmount() { return latestKnownPlayerResources; }
  
  inline double GetGameStartServerTimeSeconds() const { return gameStartServerTimeSeconds; }
  
  inline void SetLastDisplayedServerTime(double serverTime) { lastDisplayedServerTime = serverTime; }
  
 private slots:
  void HandleMessage(const QByteArray& buffer, ServerToClientMessage msgType, u16 msgLength);
  
 private:
  void ParseMessage(const char* data, ServerToClientMessage msgType, u16 msgLength);
  
  // Network message handlers
  void HandleLoadingProgressBroadcast(const char* data, u16 msgLength);
  void HandleGameBeginMessage(const char* data);
  void HandleMapUncoverMessage(const char* data);
  void HandleAddObjectMessage(const char* data);
  void HandleUnitMovementMessage(const char* data);
  void HandleGameStepTimeMessage(const char* data);
  void HandleResourcesUpdateMessage(const char* data, ResourceAmount* resources);
  void HandleBuildPercentageUpdate(const char* data);
  void HandleChangeUnitTypeMessage(const char* data);
  
  std::shared_ptr<ServerConnection> connection;
  std::shared_ptr<Match> match;
  std::shared_ptr<Map> map;
  std::shared_ptr<RenderWindow> renderWindow;
  
  double gameStartServerTimeSeconds = std::numeric_limits<double>::max();
  
  /// The server time of the last received GameStepTime message. This applies to all
  /// following messages until the next GameStepTime message is received. Initially
  /// (as long as no GameStepTime message was received yet), currentGameStepServerTime
  /// is set to a negative value.
  double currentGameStepServerTime;
  
  /// The resources of the player at the last rendered server time.
  ResourceAmount playerResources;
  
  /// The most up-to-date known resources of the player (possibly for a server time in the future).
  ResourceAmount latestKnownPlayerResources;
  
  /// The last server time that has been used to display the game state in the render window.
  /// - All network packets for server times *before* this should be applied immediately.
  ///   However, this case should be avoided if possible (by displaying a server time that
  ///   is sufficiently behind the actual current time on the server) since it leads to
  ///   visual jumps.
  /// - All network packets for server times *after* this should be cached. They will be
  ///   applied before the first rendering iteration for a server time after the packet time.
  double lastDisplayedServerTime = -1;
  
  /// Cache for messages for a server time that should be displayed in the future.
  /// Pairs of (server time, message(s)). Ordered by increasing server time.
  std::vector<std::pair<double, QByteArray>> futureMessages;
  
  // -- Statistics to debug the server time handling. --
  
  /// For all game step time messages that arrive late, measures the
  /// average time in the past in seconds. This shows by how far they are late.
  double averageGameStepTimeInPast = 0;
  
  /// The number of game step time messages that arrived after when they should have arrived
  /// to be rendered without a jump. Should ideally remain zero.
  int numGameStepsArrivedTooLate = 0;
  
  /// For all game step time messages that arrive for the future (as intended), measures the
  /// average time in the future in seconds. This should be as low as possible to minimize the
  /// lag.
  double averageGameStepTimeInFuture = 0;
  
  /// Counter for averageGameStepTimeInFuture.
  u32 numGameStepsArrivedForFuture = 0;
  
  u32 statisticsDebugOutputCounter = 0;
};
