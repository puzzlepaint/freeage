// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <vector>

#include <QByteArray>
#include <QPointF>

#include "FreeAge/common/building_types.hpp"
#include "FreeAge/common/free_age.hpp"
#include "FreeAge/common/object_types.hpp"
#include "FreeAge/common/unit_types.hpp"


struct ResourceAmount;

// #############################################
// # Please update this every time you make a  #
// # change to the network messages!           #
// #                                           #
// # This will make the client issue a warning #
// # when connecting to a server with a        #
// # different version.                        #
// #############################################
static constexpr u32 networkProtocolVersion = 1;

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
  LoadingProgress,
  
  /// Sent by the client to indicate that it finished loading the game.
  LoadingFinished,
  
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

  /// Same as SetTarget but with the interaction specified.
  SetTargetWithInteraction,
  
  /// Sent upon pressing the button / hotkey to produce a unit.
  ProduceUnit,
  
  /// Sent to place a building foundation.
  PlaceBuildingFoundation,
  
  /// Sent to delete an own object.
  DeleteObject,
  
  /// De-queues a production queue item, refunding the resources for that item.
  /// Note that the item is specified by giving its index *from the back*, so
  /// the last item would have index 0, for example. This is because the server
  /// might finish producing the first item (or theoretically even more than one)
  /// in the queue after this message is sent and before it is received. If we
  /// used the index from the front, this would change the item that this message
  /// refers to. Using the index from the back does not change it.
  DequeueProductionQueueItem,
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

QByteArray CreateLoadingFinishedMessage();

QByteArray CreateMoveToMapCoordMessage(
    const std::vector<u32>& selectedUnitIds,
    const QPointF& targetMapCoord);

QByteArray CreateSetTargetMessage(
    const std::vector<u32>& unitIds,
    u32 targetObjectId);

QByteArray CreateSetTargetWithInteractionMessage(
    const std::vector<u32>& unitIds,
    u32 targetObjectId,
    InteractionType interaction);

QByteArray CreateProduceUnitMessage(
    u32 buildingId,
    u16 unitType);

QByteArray CreatePlaceBuildingFoundationMessage(
    BuildingType type,
    const QPoint& baseTile,
    const std::vector<u32>& selectedVillagerIds);

QByteArray CreateDeleteObjectMessage(
    u32 objectId);

QByteArray CreateDequeueProductionQueueItemMessage(
    u32 productionBuildingId,
    u8 queueIndexFromBack);


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
  AddObject,
  
  /// Tells the client that all messages following this one belong to the given game
  /// step time, until the next GameStepTime message is received.
  GameStepTime,
  
  /// Tells the client about the start point and speed of a unit's movement.
  /// The speed may be zero, which indicates that the unit has stopped moving.
  UnitMovement,
  
  /// Tells the client that a unit is garrisoned or ungarrisoned from an object (building or unit).
  UnitGarrison,

  /// Tells the client about updates to its game resource amounts (wood, food, etc.)
  ResourcesUpdate,
  
  /// Tells the client about a new build percentage of a building being constructed.
  BuildPercentageUpdate,
  
  /// Tells the client that an existing unit seen by the client changed its UnitType.
  ChangeUnitType,
  
  /// Sets the type and amount of carried resources for a villager.
  SetCarriedResources,
  
  /// Tells the client about a new HP value for a unit or building.
  HPUpdate,
  
  /// An object was destroyed / killed / deleted and should be removed from the object list.
  /// - If there is a destruction / death animation for the object,
  ///   it starts playing on the client after receiving this message.
  /// - Afterwards, the object potentially turns into a rubble pile or leaves a decay sprite.
  /// These two items are handled separately (without an object instance) since in
  /// both cases this entity will not affect the remaining objects anymore, and units / buildings
  /// in this state should not be selectable anymore.
  ObjectDeath,
  
  /// Sent to notify a client about another client leaving the game (either by resigning or due to dropping).
  PlayerLeaveBroadcast,
  
  /// Sent when a unit is successfully queued in a production building.
  QueueUnit,
  
  /// Tells the client about the current state of the production of the first queued unit/technology in a building.
  UpdateProduction,
  
  /// Tells the client to remove the first item of a building's production queue,
  /// and reset the current production progress of that building to zero.
  RemoveFromProductionQueue,
  
  /// Tells the client whether it is housed or not.
  /// NOTE: The client could in principle figure this out itself, but it seemed easier and
  ///       less error-prone to introduce this message instead. At least this ensures that the
  ///       client behaves the same way as the server in this regard. And the amount of additional
  ///       transmitted data should be completely irrelevant.
  SetHoused,
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
    const QPointF& speed,
    UnitAction action);

QByteArray CreateUnitGarrisonMessage(u32 unitId, u32 targetObjectId);

QByteArray CreateResourcesUpdateMessage(const ResourceAmount& amount);

QByteArray CreateBuildPercentageUpdateMessage(u32 buildingId, float progress);

QByteArray CreateChangeUnitTypeMessage(u32 unitId, UnitType type);

QByteArray CreateSetCarriedResourcesMessage(u32 unitId, ResourceType type, u8 amount);

QByteArray CreateHPUpdateMessage(u32 objectId, u32 newHP);

QByteArray CreateObjectDeathMessage(u32 objectId);

enum class PlayerExitReason {
  Resign = 0,
  Drop = 1,
  Defeat = 2
};

QByteArray CreatePlayerLeaveBroadcastMessage(u8 playerIndex, PlayerExitReason reason);

QByteArray CreateQueueUnitMessage(u32 buildingId, u16 unitType);

QByteArray CreateUpdateProductionMessage(u32 buildingId, float progressValue, float progressPerSecond);

QByteArray CreateRemoveFromProductionQueueMessage(u32 buildingId, u8 queueIndex);

QByteArray CreateSetHousedMessage(bool housed);
