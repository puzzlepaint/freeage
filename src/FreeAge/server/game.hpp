// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <memory>
#include <vector>

#include <QByteArray>
#include <QString>
#include <QTcpSocket>

#include "FreeAge/common/free_age.hpp"
#include "FreeAge/common/messages.hpp"
#include "FreeAge/common/resources.hpp"
#include "FreeAge/server/map.hpp"
#include "FreeAge/server/settings.hpp"

class ServerBuilding;
class ServerUnit;

/// Represents a player in a game.
struct PlayerInGame {
  void RemoveFromGame();
  
  
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
  
  /// Whether there (still) is an active connection to this player.
  bool isConnected = true;
  
  /// Whether the player finished loading the game resources.
  bool finishedLoading = false;
  
  /// The current game resources of the player (wood, food, gold, stone).
  ResourceAmount resources;
  
  /// The resources of the player at the end of the last game simulation step.
  /// Only if this differs from the current resource amount, then an update message
  /// needs to be sent to the client.
  ResourceAmount lastResources;
  
  /// The current available population space of this player.
  int availablePopulationSpace = 0;
  
  /// The current population count of this player,
  /// *including units being produced*. This is required for "housed" checking,
  /// and it is different from the population count shown to the client.
  int populationIncludingInProduction = 0;
  
  /// Whether the player is currently housed.
  /// This is reset to false at the beginning of each game step and set to true
  /// if any of the player's buildings cannot produce something due to lack of population space.
  bool isHoused = false;
  
  /// Whether the player was housed in the last game step.
  /// Used to determine whether an update of the housed state needs to be sent to the client.
  bool wasHousedBefore = false;
};

class Game {
 public:
  Game(ServerSettings* settings);
  
  void RunGameLoop(std::vector<std::shared_ptr<PlayerInGame>>* playersInGame);
  
 private:
  enum class ParseMessagesResult {
    NoAction = 0,
    PlayerLeftOrShouldBeDisconnected
  };
  
  void HandleLoadingProgress(const QByteArray& msg, PlayerInGame* player, const std::vector<std::shared_ptr<PlayerInGame>>& players);
  void HandleLoadingFinished(PlayerInGame* player, const std::vector<std::shared_ptr<PlayerInGame>>& players);
  void SendChatBroadcast(u16 sendingPlayerIndex, const QString& text, const std::vector<std::shared_ptr<PlayerInGame>>& players);
  void HandleChat(const QByteArray& msg, PlayerInGame* player, u32 len, const std::vector<std::shared_ptr<PlayerInGame>>& players);
  void HandlePing(const QByteArray& msg, PlayerInGame* player);
  void HandleMoveToMapCoordMessage(const QByteArray& msg, PlayerInGame* player, u32 len);
  void HandleSetTargetMessage(const QByteArray& msg, PlayerInGame* player, u32 len);
  void HandleProduceUnitMessage(const QByteArray& msg, PlayerInGame* player);
  void HandlePlaceBuildingFoundationMessage(const QByteArray& msg, PlayerInGame* player);
  void HandleDeleteObjectMessage(const QByteArray& msg, PlayerInGame* player);
  ParseMessagesResult TryParseClientMessages(PlayerInGame* player, const std::vector<std::shared_ptr<PlayerInGame>>& players);
  
  // TODO: Right now, this creates a message containing the whole map content.
  //       Later, only the areas seen by a client (and a small border around them)
  //       should be sent to the client.
  QByteArray CreateMapUncoverMessage();
  QByteArray CreateAddObjectMessage(u32 objectId, ServerObject* object);
  
  inline double GetCurrentServerTime() { return SecondsDuration(Clock::now() - settings->serverStartTime).count(); }
  
  void StartGame();
  void SimulateGameStep(double gameStepServerTime, float stepLengthInSeconds);
  void SimulateGameStepForUnit(u32 unitId, ServerUnit* unit, double gameStepServerTime, float stepLengthInSeconds);
  bool IsPathFree(float unitRadius, const QPointF& p0, const QPointF& p1, const QRect& openRect);
  void PlanUnitPath(ServerUnit* unit);
  void SimulateBuildingConstruction(float stepLengthInSeconds, ServerUnit* villager, u32 targetObjectId, ServerBuilding* targetBuilding, bool* unitMovementChanged, bool* stayInPlace);
  void SimulateResourceGathering(float stepLengthInSeconds, u32 villagerId, ServerUnit* villager, ServerBuilding* targetBuilding, bool* unitMovementChanged, bool* stayInPlace);
  void SimulateResourceDropOff(u32 villagerId, ServerUnit* villager, bool* unitMovementChanged);
  void SimulateGameStepForBuilding(u32 buildingId, ServerBuilding* building, float stepLengthInSeconds);
  /// Returns true if the attack is still in progress, false if it finished.
  bool SimulateMeleeAttack(u32 unitId, ServerUnit* unit, u32 targetId, ServerObject* target, double gameStepServerTime, float stepLengthInSeconds, bool* unitMovementChanged, bool* stayInPlace);
  
  void ProduceUnit(ServerBuilding* building, UnitType unitInProduction);
  
  void SetUnitTargets(const std::vector<u32>& unitIds, int playerIndex, u32 targetId, ServerObject* targetObject);
  
  void DeleteObject(u32 objectId, bool deletedManually);
  
  void RemovePlayer(int playerIndex, PlayerExitReason reason);
  
  
  /// Stores the game map and the objects on it.
  std::shared_ptr<ServerMap> map;
  
  /// The server time in seconds at which the actual game begins (after all clients
  /// finished loading).
  double gameBeginServerTime;
  
  /// The server time at which the last game iteration was simulated.
  /// The next iteration is due 1 / FPS seconds after this time.
  double lastSimulationTime;
  
  /// List of players in the game.
  std::vector<std::shared_ptr<PlayerInGame>>* playersInGame;
  
  /// Stores the object IDs that should be deleted at the end of the current
  /// game step. This list is required since we generally cannot directly delete
  //  arbitrary objects during the game step simulation. This is because
  /// this consists of iterating over the objects list, and direct deletion of arbitrary
  /// elements could invalidate the iterator.
  std::vector<u32> objectDeleteList;
  
  /// For each player, stores accumulated messages that will be sent out
  /// upon the next conclusion of a game simulation step. Accumulating
  /// messages helps to reduce the overhead that many individual messages
  /// would have. In addition, as a convenience feature, each batch of
  /// accumulated messages gets prefixed by a GameStepTime message, such
  /// that other individual messages do not need to repeat the current server
  /// time in each message.
  std::vector<QByteArray> accumulatedMessages;
  
  bool shouldExit = false;
  
  ServerSettings* settings;  // not owned
};
