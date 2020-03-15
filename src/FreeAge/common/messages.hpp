#pragma once

#include <vector>

#include <QByteArray>
#include <QPointF>

#include "FreeAge/common/building_types.hpp"
#include "FreeAge/common/free_age.hpp"


struct ResourceAmount;

static constexpr int hostTokenLength = 6;


/// Types of messages sent by clients to the server.
enum class ClientToServerMessage {
  /// Initial message sent by the host to the server.
  HostConnect = 0,
  
  /// Initial message sent by a non-host to the server.
  Connect,
  
  /// A message that contains the latest game settings set by the host.
  SettingsUpdate,
  
  /// Sent when the ready state of the client changes.
  ReadyUp,
  
  /// A chat message.
  Chat,
  
  /// A regularly sent message to check whether the client is still
  /// connected and to measure the current ping.
  Ping,
  
  /// Sent by the player upon leaving the match.
  Leave,
  
  /// Sent by the host to the server to start the game.
  StartGame,
  
  /// Sent by a client to update its game loading progress.
  /// As a special case, loading progress messages with a progress state
  /// starting from 101% are interpreted by the server as the loading
  /// process being finished. (100% is *not* counted as finished to
  /// prevent this from happening early when the number is rounded up
  /// from 99.5% on).
  LoadingProgress,
  
  // --- In-game messages ---
  
  /// A move command for some selected units to a given position.
  /// The units will try to move as close as possible to this position.
  MoveToMapCoord,
  
  /// A command for some selected units pointing to a given target object (building or unit).
  /// This can actually invoke a variety of actions:
  /// * Moving a villager or fishing ship to a resource / farm / fish trap lets it collect the resource.
  /// * Moving a military unit to an enemy unit or building attacks it.
  /// * Pointing a building that can shoot projectiles to an enemy unit or building sets the
  ///   attack focus on that object if it is within range.
  /// * Moving a unit to a transport ship / siege tower garrisons it into the ship / tower.
  /// * Moving a unit to a building in which it can be garrisoned performs the garrisoning.
  /// * Moving a monk with a relic to a monastery drops off the relic in the monastery.
  /// * Moving a trade cart / cog to an allied market / dock starts trading with it.
  SetTarget,
  
  /// Sent upon pressing the button / hotkey to produce a unit.
  ProduceUnit,
  
  /// Sent to place a building foundation.
  PlaceBuildingFoundation,
};

QByteArray CreateHostConnectMessage(const QByteArray& hostToken, const QString& playerName);

QByteArray CreateConnectMessage(const QString& playerName);

QByteArray CreateSettingsUpdateMessage(bool allowMorePlayersToJoin, u16 mapSize, bool isBroadcast);

QByteArray CreateReadyUpMessage(bool clientIsReady);

QByteArray CreateChatMessage(const QString& text);

QByteArray CreatePingMessage(u64 number);

QByteArray CreateLeaveMessage();

QByteArray CreateStartGameMessage();

QByteArray CreateLoadingProgressMessage(u8 percentage);

QByteArray CreateMoveToMapCoordMessage(
    const std::vector<u32>& selectedUnitIds,
    const QPointF& targetMapCoord);

QByteArray CreateSetTargetMessage(
    const std::vector<u32>& unitIds,
    u32 targetObjectId);

QByteArray CreateProduceUnitMessage(
    u32 buildingId,
    u16 unitType);

QByteArray CreatePlaceBuildingFoundationMessage(
    BuildingType type,
    const QPoint& baseTile);


/// Types of messages sent by the server to clients.
enum class ServerToClientMessage {
  /// A response to the ClientToServerMessage::HostConnect and
  /// ClientToServerMessage::Connect messages.
  Welcome = 0,
  
  /// A message that the server sends to all non-host clients after the host changed a setting.
  SettingsUpdateBroadcast,
  
  /// The game has been aborted because the host left.
  GameAborted,
  
  /// An update to the player list.
  PlayerList,
  
  /// A chat message.
  ChatBroadcast,
  
  /// A response to a ping message, containing the value of the server time at the time
  /// the response message was sent.
  PingResponse,
  
  /// A message that gets broadcast to all clients to notify them that the host has
  /// started the game.
  StartGameBroadcast,
  
  /// The game loading progress state of a player getting broadcast to all players.
  LoadingProgressBroadcast,
  
  /// Contains:
  /// * The server time at which the game starts,
  /// * The initial center of the view for the client,
  /// * The initial resources of the client,
  /// * The map size.
  GameBegin,
  
  // --- In-game messages ---
  
  /// A portion of the map is uncovered. The server tells the client about the map content there.
  MapUncover,
  
  /// A new map object (building or unit) is created respectively enters the client's view.
  /// TODO: TCP messages have quite some overhead. Add an option to batch such messages together with other in-game messages?
  ///       Especially trees could be added quite frequently.
  AddObject,
  
  /// Tells the client that all messages following this one belong to the given game
  /// step time, until the next GameStepTime message is received.
  GameStepTime,
  
  /// Tells the client about the start point and speed of a unit's movement.
  /// The speed may be zero, which indicates that the unit has stopped moving.
  UnitMovement,
  
  /// Tells the client about updates to its game resource amounts (wood, food, etc.)
  ResourcesUpdate,
  
  /// Tells the client about a new build percentage of a building being constructed.
  BuildPercentageUpdate,
};

QByteArray CreateWelcomeMessage();

QByteArray CreateGameAbortedMessage();

QByteArray CreateChatBroadcastMessage(u16 sendingPlayerIndex, const QString& text);

QByteArray CreatePingResponseMessage(u64 number, double serverTimeSeconds);

QByteArray CreateStartGameBroadcastMessage();

QByteArray CreateLoadingProgressBroadcastMessage(u8 playerIndex, u8 percentage);

QByteArray CreateGameBeginMessage(
    double gameStartServerTimeSeconds,
    const QPointF& initialViewCenterMapCoord,
    u32 initialWood,
    u32 initialFood,
    u32 initialGold,
    u32 initialStone,
    u16 mapWidth,
    u16 mapHeight);

QByteArray CreateGameStepTimeMessage(double gameStepServerTime);

QByteArray CreateUnitMovementMessage(
    u32 unitId,
    const QPointF& startPoint,
    const QPointF& speed);

QByteArray CreateResourcesUpdateMessage(const ResourceAmount& amount);

QByteArray CreateBuildPercentageUpdateMessage(u32 buildingId, float progress);
