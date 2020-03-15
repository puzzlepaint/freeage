#include "FreeAge/common/messages.hpp"

#include <mango/core/endian.hpp>
#include <QString>

#include "FreeAge/common/logging.hpp"
#include "FreeAge/common/resources.hpp"

// TODO: Create a helper function to create the base of all of these similar messages

QByteArray CreateHostConnectMessage(const QByteArray& hostToken, const QString& playerName) {
  // Prepare
  QByteArray playerNameUtf8 = playerName.toUtf8();
  
  // Create buffer
  QByteArray msg(1 + 2 + hostToken.size() + playerNameUtf8.size(), Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ClientToServerMessage::HostConnect);
  mango::ustore16(data + 1, msg.size());
  
  // Fill buffer
  memcpy(data + 3, hostToken.data(), hostToken.size());
  memcpy(data + 3 + hostToken.size(), playerNameUtf8.data(), playerNameUtf8.size());
  
  return msg;
}

QByteArray CreateConnectMessage(const QString& playerName) {
  // Prepare
  QByteArray playerNameUtf8 = playerName.toUtf8();
  
  // Create buffer
  QByteArray msg(1 + 2 + playerNameUtf8.size(), Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ClientToServerMessage::Connect);
  mango::ustore16(data + 1, msg.size());
  
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
  // Create buffer
  QByteArray msg(1 + 2 + 1, Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ClientToServerMessage::ReadyUp);
  mango::ustore16(data + 1, msg.size());
  
  // Fill buffer
  data[3] = clientIsReady ? 1 : 0;
  
  return msg;
}

QByteArray CreateChatMessage(const QString& text) {
  // Prepare
  QByteArray textUtf8 = text.toUtf8();
  
  // Create buffer
  QByteArray msg(1 + 2 + textUtf8.size(), Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ClientToServerMessage::Chat);
  mango::ustore16(data + 1, msg.size());
  
  // Fill buffer
  memcpy(data + 3, textUtf8.data(), textUtf8.size());
  
  return msg;
}

QByteArray CreatePingMessage(u64 number) {
  // Create buffer
  QByteArray msg(1 + 2 + 8, Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ClientToServerMessage::Ping);
  mango::ustore16(data + 1, msg.size());
  
  // Fill buffer
  mango::ustore64(data + 3, number);
  
  return msg;
}

QByteArray CreateLeaveMessage() {
  // Create buffer
  QByteArray msg(1 + 2, Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ClientToServerMessage::Leave);
  mango::ustore16(data + 1, msg.size());
  
  return msg;
}

QByteArray CreateStartGameMessage() {
  // Create buffer
  QByteArray msg(1 + 2, Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ClientToServerMessage::StartGame);
  mango::ustore16(data + 1, msg.size());
  
  return msg;
}

QByteArray CreateLoadingProgressMessage(u8 percentage) {
  // Create buffer
  QByteArray msg(1 + 2 + 1, Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ClientToServerMessage::LoadingProgress);
  mango::ustore16(data + 1, msg.size());
  
  // Fill buffer
  data[3] = percentage;
  
  return msg;
}

QByteArray CreateMoveToMapCoordMessage(const std::vector<u32>& selectedUnitIds, const QPointF& targetMapCoord) {
  if (selectedUnitIds.empty()) {
    return QByteArray();
  }
  
  // Create buffer
  QByteArray msg(13 + 4 * selectedUnitIds.size(), Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ClientToServerMessage::MoveToMapCoord);
  mango::ustore16(data + 1, msg.size());
  
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
  QByteArray msg(9 + 4 * unitIds.size(), Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ClientToServerMessage::SetTarget);
  mango::ustore16(data + 1, msg.size());
  
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

QByteArray CreateProduceUnitMessage(u32 buildingId, u16 unitType) {
  // Create buffer
  QByteArray msg(9, Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ClientToServerMessage::ProduceUnit);
  mango::ustore16(data + 1, msg.size());
  
  // Fill buffer
  mango::ustore32(data + 3, buildingId);
  mango::ustore16(data + 7, unitType);
  
  return msg;
}

QByteArray CreatePlaceBuildingFoundationMessage(BuildingType type, const QPoint& baseTile) {
  // Create buffer
  QByteArray msg(9, Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ClientToServerMessage::PlaceBuildingFoundation);
  mango::ustore16(data + 1, msg.size());
  
  // Fill buffer
  mango::ustore16(data + 3, static_cast<u16>(type));
  mango::ustore16(data + 5, baseTile.x());
  mango::ustore16(data + 7, baseTile.y());
  
  return msg;
}


QByteArray CreateWelcomeMessage() {
  // Create buffer
  QByteArray msg(1 + 2, Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ServerToClientMessage::Welcome);
  mango::ustore16(data + 1, msg.size());
  
  return msg;
}

QByteArray CreateGameAbortedMessage() {
  // Create buffer
  QByteArray msg(1 + 2, Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ServerToClientMessage::GameAborted);
  mango::ustore16(data + 1, msg.size());
  
  return msg;
}

QByteArray CreateChatBroadcastMessage(u16 sendingPlayerIndex, const QString& text) {
  // Prepare
  QByteArray textUtf8 = text.toUtf8();
  
  // Create buffer
  QByteArray msg(1 + 2 + 2 + textUtf8.size(), Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ServerToClientMessage::ChatBroadcast);
  mango::ustore16(data + 1, msg.size());
  
  // Fill buffer
  mango::ustore16(data + 3, sendingPlayerIndex);
  
  memcpy(data + 3 + 2, textUtf8.data(), textUtf8.size());
  
  return msg;
}

QByteArray CreatePingResponseMessage(u64 number, double serverTimeSeconds) {
  // Create buffer
  QByteArray msg(1 + 2 + 8 + 8, Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ServerToClientMessage::PingResponse);
  mango::ustore16(data + 1, msg.size());
  
  // Fill buffer
  mango::ustore64(data + 3, number);
  memcpy(data + 3 + 8, &serverTimeSeconds, 8);
  
  return msg;
}

QByteArray CreateStartGameBroadcastMessage() {
  // Create buffer
  QByteArray msg(1 + 2, Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ServerToClientMessage::StartGameBroadcast);
  mango::ustore16(data + 1, msg.size());
  
  return msg;
}

QByteArray CreateLoadingProgressBroadcastMessage(u8 playerIndex, u8 percentage) {
  // Create buffer
  QByteArray msg(1 + 2 + 1 + 1, Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ServerToClientMessage::LoadingProgressBroadcast);
  mango::ustore16(data + 1, msg.size());
  
  // Fill buffer
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
  QByteArray msg(39, Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ServerToClientMessage::GameBegin);
  mango::ustore16(data + 1, msg.size());
  
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
  // Create buffer
  QByteArray msg(1 + 2 + 8, Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ServerToClientMessage::GameStepTime);
  mango::ustore16(data + 1, msg.size());
  
  // Fill buffer
  memcpy(data + 3, &gameStepServerTime, 8);
  
  return msg;
}

QByteArray CreateUnitMovementMessage(u32 unitId, const QPointF& startPoint, const QPointF& speed, UnitAction action) {
  // Create buffer
  QByteArray msg(24, Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ServerToClientMessage::UnitMovement);
  mango::ustore16(data + 1, msg.size());
  
  // Fill buffer
  mango::ustore32(data + 3, unitId);
  *reinterpret_cast<float*>(data + 7) = startPoint.x();
  *reinterpret_cast<float*>(data + 11) = startPoint.y();
  *reinterpret_cast<float*>(data + 15) = speed.x();
  *reinterpret_cast<float*>(data + 19) = speed.y();
  data[23] = static_cast<u8>(action);
  
  return msg;
}

QByteArray CreateResourcesUpdateMessage(const ResourceAmount& amount) {
  // Create buffer
  QByteArray msg(19, Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ServerToClientMessage::ResourcesUpdate);
  mango::ustore16(data + 1, msg.size());
  
  // Fill buffer
  mango::ustore32(data + 3, amount.wood());
  mango::ustore32(data + 7, amount.food());
  mango::ustore32(data + 11, amount.gold());
  mango::ustore32(data + 15, amount.stone());
  
  return msg;
}

QByteArray CreateBuildPercentageUpdateMessage(u32 buildingId, float progress) {
  // Create buffer
  QByteArray msg(1 + 2 + 4 + 4, Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ServerToClientMessage::BuildPercentageUpdate);
  mango::ustore16(data + 1, msg.size());
  
  // Fill buffer
  mango::ustore32(data + 3, buildingId);
  memcpy(data + 3 + 4, &progress, 4);
  
  return msg;
}

QByteArray CreateChangeUnitTypeMessage(u32 unitId, UnitType type) {
  // Create buffer
  QByteArray msg(1 + 2 + 4 + 2, Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ServerToClientMessage::ChangeUnitType);
  mango::ustore16(data + 1, msg.size());
  
  // Fill buffer
  mango::ustore32(data + 3, unitId);
  mango::ustore16(data + 7, static_cast<u16>(type));
  
  return msg;
}
