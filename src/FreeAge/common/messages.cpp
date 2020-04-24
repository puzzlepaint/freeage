// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/common/messages.hpp"

#include <mango/core/endian.hpp>
#include <QString>

#include "FreeAge/common/logging.hpp"
#include "FreeAge/common/resources.hpp"

static inline QByteArray CreateClientToServerMessageHeader(int dataSize, ClientToServerMessage type) {
  // Create buffer
  QByteArray msg(3 + dataSize, Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(type);
  mango::ustore16(data + 1, msg.size());
  
  return msg;
}


QByteArray CreateHostConnectMessage(const QByteArray& hostToken, const QString& playerName) {
  // Prepare
  QByteArray playerNameUtf8 = playerName.toUtf8();
  
  // Create buffer
  QByteArray msg = CreateClientToServerMessageHeader(hostToken.size() + playerNameUtf8.size(), ClientToServerMessage::HostConnect);
  char* data = msg.data();
  
  // Fill buffer
  memcpy(data + 3, hostToken.data(), hostToken.size());
  memcpy(data + 3 + hostToken.size(), playerNameUtf8.data(), playerNameUtf8.size());
  
  return msg;
}

QByteArray CreateConnectMessage(const QString& playerName) {
  // Prepare
  QByteArray playerNameUtf8 = playerName.toUtf8();
  
  // Create buffer
  QByteArray msg = CreateClientToServerMessageHeader(playerNameUtf8.size(), ClientToServerMessage::Connect);
  char* data = msg.data();
  
  // Fill buffer
  memcpy(data + 3, playerNameUtf8.data(), playerNameUtf8.size());
  
  return msg;
}

QByteArray CreateSettingsUpdateMessage(bool allowMorePlayersToJoin, u16 mapSize, bool isBroadcast) {
  // Create buffer
  QByteArray msg(1 + 2 + 1 + 2, Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = isBroadcast ? static_cast<char>(ServerToClientMessage::SettingsUpdateBroadcast) : static_cast<char>(ClientToServerMessage::SettingsUpdate);
  mango::ustore16(data + 1, msg.size());
  
  // Fill buffer
  data[3] = allowMorePlayersToJoin ? 1 : 0;
  mango::ustore16(data + 4, mapSize);
  
  return msg;
}

QByteArray CreateReadyUpMessage(bool clientIsReady) {
  QByteArray msg = CreateClientToServerMessageHeader(1, ClientToServerMessage::ReadyUp);
  char* data = msg.data();
  data[3] = clientIsReady ? 1 : 0;
  return msg;
}

QByteArray CreateChatMessage(const QString& text) {
  // Prepare
  QByteArray textUtf8 = text.toUtf8();
  
  // Create buffer
  QByteArray msg = CreateClientToServerMessageHeader(textUtf8.size(), ClientToServerMessage::Chat);
  char* data = msg.data();
  
  // Fill buffer
  memcpy(data + 3, textUtf8.data(), textUtf8.size());
  
  return msg;
}

QByteArray CreatePingMessage(u64 number) {
  QByteArray msg = CreateClientToServerMessageHeader(8, ClientToServerMessage::Ping);
  char* data = msg.data();
  mango::ustore64(data + 3, number);
  return msg;
}

QByteArray CreateLeaveMessage() {
  return CreateClientToServerMessageHeader(0, ClientToServerMessage::Leave);
}

QByteArray CreateStartGameMessage() {
  return CreateClientToServerMessageHeader(0, ClientToServerMessage::StartGame);
}

QByteArray CreateLoadingProgressMessage(u8 percentage) {
  QByteArray msg = CreateClientToServerMessageHeader(1, ClientToServerMessage::LoadingProgress);
  msg[3] = percentage;
  return msg;
}

QByteArray CreateLoadingFinishedMessage() {
  return CreateClientToServerMessageHeader(0, ClientToServerMessage::LoadingFinished);
}

QByteArray CreateMoveToMapCoordMessage(const std::vector<u32>& selectedUnitIds, const QPointF& targetMapCoord) {
  if (selectedUnitIds.empty()) {
    return QByteArray();
  }
  
  // Create buffer
  QByteArray msg = CreateClientToServerMessageHeader(10 + 4 * selectedUnitIds.size(), ClientToServerMessage::MoveToMapCoord);
  char* data = msg.data();
  
  // Fill buffer
  *reinterpret_cast<float*>(data + 3) = targetMapCoord.x();
  *reinterpret_cast<float*>(data + 7) = targetMapCoord.y();
  
  mango::ustore16(data + 11, selectedUnitIds.size());  // TODO: This could also be derived from the message length
  int offset = 13;
  for (u32 unitId : selectedUnitIds) {
    mango::ustore32(data + offset, unitId);
    offset += 4;
  }
  
  return msg;
}

QByteArray CreateSetTargetMessage(const std::vector<u32>& unitIds, u32 targetObjectId) {
  if (unitIds.empty()) {
    return QByteArray();
  }
  
  // Create buffer
  QByteArray msg = CreateClientToServerMessageHeader(6 + 4 * unitIds.size(), ClientToServerMessage::SetTarget);
  char* data = msg.data();
  
  // Fill buffer
  mango::ustore32(data + 3, targetObjectId);
  
  mango::ustore16(data + 7, unitIds.size());  // TODO: This could also be derived from the message length
  int offset = 9;
  for (u32 unitId : unitIds) {
    mango::ustore32(data + offset, unitId);
    offset += 4;
  }
  
  return msg;
}

QByteArray CreateSetTargetWithInteractionMessage(const std::vector<u32>& unitIds, u32 targetObjectId, InteractionType interaction) {
  if (unitIds.empty()) {
    return QByteArray();
  }

  // Create buffer
  QByteArray msg = CreateClientToServerMessageHeader(10 + 4 * unitIds.size(), ClientToServerMessage::SetTargetWithInteraction);
  char* data = msg.data();
  
  // Fill buffer
  mango::ustore32(data + 3, targetObjectId);

  mango::ustore16(data + 7, unitIds.size());  // TODO: This could also be derived from the message length
  int offset = 9;
  for (u32 unitId : unitIds) {
    mango::ustore32(data + offset, unitId);
    offset += 4;
  }

  mango::ustore32(data + offset, static_cast<u32>(interaction)); // TODO: reduce size ?
  
  return msg;
}

QByteArray CreateUngarrisonUnitsMessage(const std::vector<u32>& unitIds) {
  if (unitIds.empty()) {
    return QByteArray();
  }

  // Create buffer
  QByteArray msg = CreateClientToServerMessageHeader(2 + 4 * unitIds.size(), ClientToServerMessage::UngarrisonUnits);
  char* data = msg.data();
  
  // Fill buffer
  mango::ustore16(data + 3, unitIds.size());  // TODO: This could also be derived from the message length
  int offset = 5;
  for (u32 unitId : unitIds) {
    mango::ustore32(data + offset, unitId);
    offset += 4;
  }

  return msg;
}

QByteArray CreateProduceUnitMessage(u32 buildingId, u16 unitType) {
  QByteArray msg = CreateClientToServerMessageHeader(6, ClientToServerMessage::ProduceUnit);
  char* data = msg.data();
  mango::ustore32(data + 3, buildingId);
  mango::ustore16(data + 7, unitType);
  return msg;
}

QByteArray CreatePlaceBuildingFoundationMessage(BuildingType type, const QPoint& baseTile, const std::vector<u32>& selectedVillagerIds) {
  QByteArray msg = CreateClientToServerMessageHeader(8 + 4 * selectedVillagerIds.size(), ClientToServerMessage::PlaceBuildingFoundation);
  char* data = msg.data();
  
  mango::ustore16(data + 3, static_cast<u16>(type));
  mango::ustore16(data + 5, baseTile.x());
  mango::ustore16(data + 7, baseTile.y());
  
  mango::ustore16(data + 9, selectedVillagerIds.size());  // TODO: This could also be derived from the message length
  int offset = 11;
  for (u32 unitId : selectedVillagerIds) {
    mango::ustore32(data + offset, unitId);
    offset += 4;
  }
  
  return msg;
}

QByteArray CreateDeleteObjectMessage(u32 objectId) {
  QByteArray msg = CreateClientToServerMessageHeader(4, ClientToServerMessage::DeleteObject);
  char* data = msg.data();
  mango::ustore32(data + 3, objectId);
  return msg;
}

QByteArray CreateDequeueProductionQueueItemMessage(u32 productionBuildingId, u8 queueIndexFromBack) {
  QByteArray msg = CreateClientToServerMessageHeader(5, ClientToServerMessage::DequeueProductionQueueItem);
  char* data = msg.data();
  mango::ustore32(data + 3, productionBuildingId);
  data[7] = queueIndexFromBack;
  return msg;
}


static inline QByteArray CreateServerToClientMessageHeader(int dataSize, ServerToClientMessage type) {
  // Create buffer
  QByteArray msg(3 + dataSize, Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(type);
  mango::ustore16(data + 1, msg.size());
  
  return msg;
}

QByteArray CreateWelcomeMessage() {
  QByteArray msg = CreateServerToClientMessageHeader(4, ServerToClientMessage::Welcome);
  char* data = msg.data();
  mango::ustore32(data + 3, networkProtocolVersion);
  return msg;
}

QByteArray CreateGameAbortedMessage() {
  return CreateServerToClientMessageHeader(0, ServerToClientMessage::GameAborted);
}

QByteArray CreateChatBroadcastMessage(u16 sendingPlayerIndex, const QString& text) {
  // Prepare
  QByteArray textUtf8 = text.toUtf8();
  
  // Create buffer
  QByteArray msg = CreateServerToClientMessageHeader(2 + textUtf8.size(), ServerToClientMessage::ChatBroadcast);
  char* data = msg.data();
  
  // Fill buffer
  mango::ustore16(data + 3, sendingPlayerIndex);
  
  memcpy(data + 3 + 2, textUtf8.data(), textUtf8.size());
  
  return msg;
}

QByteArray CreatePingResponseMessage(u64 number, double serverTimeSeconds) {
  QByteArray msg = CreateServerToClientMessageHeader(8 + 8, ServerToClientMessage::PingResponse);
  char* data = msg.data();
  mango::ustore64(data + 3, number);
  memcpy(data + 3 + 8, &serverTimeSeconds, 8);
  return msg;
}

QByteArray CreateStartGameBroadcastMessage() {
  return CreateServerToClientMessageHeader(0, ServerToClientMessage::StartGameBroadcast);
}

QByteArray CreateLoadingProgressBroadcastMessage(u8 playerIndex, u8 percentage) {
  QByteArray msg = CreateServerToClientMessageHeader(1 + 1, ServerToClientMessage::LoadingProgressBroadcast);
  char* data = msg.data();
  data[3] = playerIndex;
  data[4] = percentage;
  return msg;
}

QByteArray CreateGameBeginMessage(
    double gameStartServerTimeSeconds,
    const QPointF& initialViewCenterMapCoord,
    u32 initialWood,
    u32 initialFood,
    u32 initialGold,
    u32 initialStone,
    u16 mapWidth,
    u16 mapHeight) {
  // Create buffer
  QByteArray msg = CreateServerToClientMessageHeader(36, ServerToClientMessage::GameBegin);
  char* data = msg.data();
  
  // Fill buffer
  memcpy(data + 3, &gameStartServerTimeSeconds, 8);
  *reinterpret_cast<float*>(data + 11) = initialViewCenterMapCoord.x();
  *reinterpret_cast<float*>(data + 15) = initialViewCenterMapCoord.y();
  mango::ustore32(data + 19, initialWood);
  mango::ustore32(data + 23, initialFood);
  mango::ustore32(data + 27, initialGold);
  mango::ustore32(data + 31, initialStone);
  mango::ustore16(data + 35, mapWidth);
  mango::ustore16(data + 37, mapHeight);
  
  return msg;
}

QByteArray CreateGameStepTimeMessage(double gameStepServerTime) {
  QByteArray msg = CreateServerToClientMessageHeader(8, ServerToClientMessage::GameStepTime);
  char* data = msg.data();
  memcpy(data + 3, &gameStepServerTime, 8);
  return msg;
}

QByteArray CreateUnitMovementMessage(u32 unitId, const QPointF& startPoint, const QPointF& speed, UnitAction action) {
  QByteArray msg = CreateServerToClientMessageHeader(21, ServerToClientMessage::UnitMovement);
  char* data = msg.data();
  mango::ustore32(data + 3, unitId);
  *reinterpret_cast<float*>(data + 7) = startPoint.x();
  *reinterpret_cast<float*>(data + 11) = startPoint.y();
  *reinterpret_cast<float*>(data + 15) = speed.x();
  *reinterpret_cast<float*>(data + 19) = speed.y();
  data[23] = static_cast<u8>(action);
  return msg;
}

QByteArray CreateUnitGarrisonMessage(u32 unitId, u32 targetObjectId) {
  QByteArray msg = CreateServerToClientMessageHeader(8, ServerToClientMessage::UnitGarrison);
  char* data = msg.data();
  mango::ustore32(data + 3, unitId);
  mango::ustore32(data + 7, targetObjectId);
  return msg;
}

QByteArray CreateResourcesUpdateMessage(const ResourceAmount& amount) {
  QByteArray msg = CreateServerToClientMessageHeader(16, ServerToClientMessage::ResourcesUpdate);
  char* data = msg.data();
  mango::ustore32(data + 3, amount.wood());
  mango::ustore32(data + 7, amount.food());
  mango::ustore32(data + 11, amount.gold());
  mango::ustore32(data + 15, amount.stone());
  return msg;
}

QByteArray CreateBuildPercentageUpdateMessage(u32 buildingId, float progress) {
  QByteArray msg = CreateServerToClientMessageHeader(4 + 4, ServerToClientMessage::BuildPercentageUpdate);
  char* data = msg.data();
  mango::ustore32(data + 3, buildingId);
  memcpy(data + 3 + 4, &progress, 4);
  return msg;
}

QByteArray CreateChangeUnitTypeMessage(u32 unitId, UnitType type) {
  QByteArray msg = CreateServerToClientMessageHeader(4 + 2, ServerToClientMessage::ChangeUnitType);
  char* data = msg.data();
  mango::ustore32(data + 3, unitId);
  mango::ustore16(data + 7, static_cast<u16>(type));
  return msg;
}

QByteArray CreateSetCarriedResourcesMessage(u32 unitId, ResourceType type, u8 amount) {
  QByteArray msg = CreateServerToClientMessageHeader(6, ServerToClientMessage::SetCarriedResources);
  char* data = msg.data();
  mango::ustore32(data + 3, unitId);
  data[7] = static_cast<u8>(type);
  data[8] = amount;
  return msg;
}

QByteArray CreateHPUpdateMessage(u32 objectId, u32 newHP) {
  QByteArray msg = CreateServerToClientMessageHeader(8, ServerToClientMessage::HPUpdate);
  char* data = msg.data();
  mango::ustore32(data + 3, objectId);
  mango::ustore32(data + 7, newHP);
  return msg;
}

QByteArray CreateObjectDeathMessage(u32 objectId) {
  QByteArray msg = CreateServerToClientMessageHeader(4, ServerToClientMessage::ObjectDeath);
  char* data = msg.data();
  mango::ustore32(data + 3, objectId);
  return msg;
}

QByteArray CreatePlayerLeaveBroadcastMessage(u8 playerIndex, PlayerExitReason reason) {
  QByteArray msg = CreateServerToClientMessageHeader(2, ServerToClientMessage::PlayerLeaveBroadcast);
  char* data = msg.data();
  data[3] = playerIndex;
  data[4] = static_cast<u8>(reason);
  return msg;
}

QByteArray CreateQueueUnitMessage(u32 buildingId, u16 unitType) {
  QByteArray msg = CreateServerToClientMessageHeader(6, ServerToClientMessage::QueueUnit);
  char* data = msg.data();
  mango::ustore32(data + 3, buildingId);
  mango::ustore16(data + 7, unitType);
  return msg;
}

QByteArray CreateUpdateProductionMessage(u32 buildingId, float progressValue, float progressPerSecond) {
  QByteArray msg = CreateServerToClientMessageHeader(4 + 4 + 4, ServerToClientMessage::UpdateProduction);
  char* data = msg.data();
  mango::ustore32(data + 3, buildingId);
  *reinterpret_cast<float*>(data + 7) = progressValue;
  *reinterpret_cast<float*>(data + 11) = progressPerSecond;
  return msg;
}

QByteArray CreateRemoveFromProductionQueueMessage(u32 buildingId, u8 queueIndex) {
  QByteArray msg = CreateServerToClientMessageHeader(5, ServerToClientMessage::RemoveFromProductionQueue);
  char* data = msg.data();
  mango::ustore32(data + 3, buildingId);
  data[7] = queueIndex;
  return msg;
}

QByteArray CreateSetHousedMessage(bool housed) {
  QByteArray msg = CreateServerToClientMessageHeader(1, ServerToClientMessage::SetHoused);
  msg.data()[3] = housed ? 1 : 0;
  return msg;
}
