// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <fstream>
#include <memory>

#include <QObject>

#include "FreeAge/client/map.hpp"
#include "FreeAge/client/match.hpp"
#include "FreeAge/client/render_window.hpp"
#include "FreeAge/client/server_connection.hpp"
#include "FreeAge/common/player.hpp"

/// Handles the game on the client side:
/// * Handles incoming messages from the server
/// * Updates the game state such that it can be rendered
/// * Handles user input
class GameController : public QObject {
 Q_OBJECT
 public:
  GameController(const std::shared_ptr<Match>& match, const std::shared_ptr<ServerConnection>& connection, bool debugNetworking);
  
  inline void SetRenderWindow(const std::shared_ptr<RenderWindow> renderWindow) { this->renderWindow = renderWindow; }
  
  void ParseMessagesUntil(double displayedServerTime);
  
  void ProduceUnit(const std::vector<u32>& selection, UnitType type, int count);
  
  /// Returns the current resource amount of the player.
  inline const ResourceAmount& GetCurrentResourceAmount() { return player->resources; }
  
  /// Returns the latest known resource amount, even if this is for a server time that
  /// should not be displayed yet. This value is used to determine whether to make
  /// "produce unit" or "research technology" buttons active or inactive.
  // TODO: Remove? Does not seem really useful and we cannot get this unless we peek into messages just for it.
  inline const ResourceAmount& GetLatestKnownResourceAmount() { return /*latestKnownPlayerResources*/ player->resources; }
  
  inline Player* GetPlayer() { return player; }
  inline Player* GetPlayer(int index) { return index == kGaiaPlayerIndex ? gaiaPlayer : &players.at(index); }

  inline int GetBuildingTypeCount(BuildingType buildingType) const { return player->GetPlayerStats().GetBuildingTypeCount(buildingType); }
  inline int GetUnitTypeCount(UnitType unitType) const { return player->GetPlayerStats().GetUnitTypeCount(unitType); }

  inline bool IsPlayerHoused() const { return isHoused; }

  inline double GetGameStartServerTimeSeconds() const { return gameStartServerTimeSeconds; }
  
  inline void SetLastDisplayedServerTime(double serverTime) { lastDisplayedServerTime = serverTime; }
  
 private:
  void ParseMessage(const QByteArray& data, ServerToClientMessage msgType);
  
  // Network message handlers
  void HandleLoadingProgressBroadcast(const QByteArray& data);
  void HandleGameBeginMessage(const QByteArray& data);
  void HandleMapUncoverMessage(const QByteArray& data);
  void HandleAddObjectMessage(const QByteArray& data);
  void HandleObjectDeathMessage(const QByteArray& data);
  void HandleUnitMovementMessage(const QByteArray& data);
  void HandleUnitGarrisonMessage(const QByteArray& data);
  void HandleGameStepTimeMessage(const QByteArray& data);
  void HandleResourcesUpdateMessage(const QByteArray& data, ResourceAmount* resources);
  void HandleBuildPercentageUpdate(const QByteArray& data);
  void HandleChangeUnitTypeMessage(const QByteArray& data);
  void HandleSetCarriedResourcesMessage(const QByteArray& data);
  void HandleHPUpdateMessage(const QByteArray& data);
  void HandlePlayerLeaveBroadcast(const QByteArray& data);
  void HandleQueueUnitMessage(const QByteArray& data);
  void HandleUpdateProductionMessage(const QByteArray& data);
  void HandleRemoveFromProductionQueueMessage(const QByteArray& data);
  void HandleSetHousedMessage(const QByteArray& data);
  
  /// Change the garrison status of a unit and the targets object garrisoned units list. 
  /// The enter parameter is true for garrison and false for ungarrison.
  void ChangeUnitGarrisonStatus(ClientUnit* unit, ClientObject* targetObject, bool enter);
  
  std::shared_ptr<ServerConnection> connection;
  std::shared_ptr<Match> match;
  std::shared_ptr<Map> map;
  std::shared_ptr<RenderWindow> renderWindow;
  
  double gameStartServerTimeSeconds = std::numeric_limits<double>::max();
  
  /// The server time of the last received GameStepTime message. This applies to all
  /// following messages until the next GameStepTime message is received. Initially
  /// (as long as no GameStepTime message was received yet), currentGameStepServerTime
  /// is set to a negative value.
  double currentGameStepServerTime = -1;
  
  // TODO: use a client specific Player struct, possibly containing a pointer to the Match::Player
  // NOTE: after the in game players the player of Gaia is stored
  std::vector<Player> players;
  
  // Pointer to current Player inside the players vector.
  Player* player;

  // Pointer to Gaia Player inside the players vector.
  Player* gaiaPlayer;

  /// Whether the player is currently housed.
  bool isHoused = false;
  
  /// The last server time that has been used to display the game state in the render window.
  /// - All network packets for server times *before* this should be applied immediately.
  ///   However, this case should be avoided if possible (by displaying a server time that
  ///   is sufficiently behind the actual current time on the server) since it leads to
  ///   visual jumps.
  /// - All network packets for server times *after* this should be cached. They will be
  ///   applied before the first rendering iteration for a server time after the packet time.
  double lastDisplayedServerTime = -1;
  
  
  // -- Statistics to debug the server time handling. --
  
  /// For all game step time messages that arrive late, measures the
  /// average time in the past in seconds. This shows by how far they are late.
  double averageMsgTimeInPast = 0;
  
  /// The number of game step time messages that arrived after when they should have arrived
  /// to be rendered without a jump. Should ideally remain zero.
  int numMsgsArrivedTooLate = 0;
  
  /// For all game step time messages that arrive for the future (as intended), measures the
  /// average time in the future in seconds. This should be as low as possible to minimize the
  /// lag.
  double averageMsgTimeInFuture = 0;
  
  /// Counter for averageMsgTimeInFuture.
  u32 numMsgsArrivedForFuture = 0;
  
  u32 statisticsDebugOutputCounter = 0;
  
  // For network debugging.
  double lastMessageServerTime = -1;
  double lastMessageClientTime = -1;
  bool debugNetworking = false;
  int networkLogCounter = 0;
  std::ofstream networkingDebugFile;
};
