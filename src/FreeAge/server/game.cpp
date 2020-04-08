// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/server/game.hpp"

#include <iostream>
#include <queue>

#include <QApplication>
#include <QImage>
#include <QThread>

#include "FreeAge/common/logging.hpp"
#include "FreeAge/common/messages.hpp"
#include "FreeAge/common/timing.hpp"
#include "FreeAge/common/util.hpp"
#include "FreeAge/server/building.hpp"
#include "FreeAge/server/unit.hpp"

// TODO (puzzlepaint): For some reason, this include needed to be after the Qt includes on my laptop
// in order for CIDE not to show some errors. Compiling always worked. Check the reason for the errors.
#include <mango/core/endian.hpp>

void PlayerInGame::RemoveFromGame() {
  unparsedBuffer.clear();
  isConnected = false;
}


Game::Game(ServerSettings* settings)
    : settings(settings) {}

void Game::RunGameLoop(std::vector<std::shared_ptr<PlayerInGame>>* playersInGame) {
  constexpr float kTargetFPS = 30;
  constexpr float kSimulationTimeInterval = 1 / kTargetFPS;
  
  accumulatedMessages.resize(playersInGame->size());
  for (usize playerIndex = 0; playerIndex < playersInGame->size(); ++ playerIndex) {
    accumulatedMessages[playerIndex].reserve(1024);
  }
  
  this->playersInGame = playersInGame;
  bool firstLoopIteration = true;
  
  while (!shouldExit) {
    // Read data from player connections and handle broken connections.
    for (usize playerIndex = 0; playerIndex < playersInGame->size(); ++ playerIndex) {
      auto& player = playersInGame->at(playerIndex);
      
      if (!player->isConnected) {
        // TODO: Allow players to reconnect to the game
        continue;
      }
      
      // Read new data from the connection.
      int prevSize = player->unparsedBuffer.size();
      player->unparsedBuffer += player->socket->readAll();
      
      bool removePlayer = false;
      if (player->unparsedBuffer.size() > prevSize ||
          (firstLoopIteration && !player->unparsedBuffer.isEmpty())) {
        ParseMessagesResult parseResult = TryParseClientMessages(player.get(), *playersInGame);
        removePlayer = parseResult == ParseMessagesResult::PlayerLeftOrShouldBeDisconnected;
      }
      
      // Remove connections which got ParseMessagesResult::PlayerLeftOrShouldBeDisconnected,
      // which did not send pings in time, or if the connection was lost.
      constexpr int kNoPingTimeout = 5000;
      bool socketDisconnected = player->socket->state() != QAbstractSocket::ConnectedState;
      bool pingTimeout = MillisecondsDuration(Clock::now() - player->lastPingTime).count() > kNoPingTimeout;
      if (removePlayer || socketDisconnected || pingTimeout) {
        RemovePlayer(playerIndex, (socketDisconnected || pingTimeout) ? PlayerExitReason::Drop : PlayerExitReason::Resign);
        continue;
      }
    }
    
    // Process Qt events.
    qApp->processEvents(QEventLoop::AllEvents);
    
    // Simulate a game step if it is due.
    // TODO: Do we need to consider the possibility of falling behind more and more with the simulation?
    //       I guess that the game would break anyway then.
    if (map) {
      double serverTime = GetCurrentServerTime();
      while (serverTime >= lastSimulationTime + kSimulationTimeInterval) {
        /// Simulate one game step.
        SimulateGameStep(lastSimulationTime + kSimulationTimeInterval, kSimulationTimeInterval);
        
        lastSimulationTime += kSimulationTimeInterval;
      }
      
      // If the next game step is due far enough in the future, sleep a bit to avoid "busy waiting" and reduce the CPU load.
      // However, we must not sleep for the whole time, since we should keep handling client messages
      // and processing Qt events in the meantime.
      constexpr double kMaxSleepTimeSeconds = 0.0005;  // 0.5 milliseconds
      double nextIterationTime = lastSimulationTime + kSimulationTimeInterval;
      serverTime = GetCurrentServerTime();
      double sleepTimeSeconds = std::min(kMaxSleepTimeSeconds, nextIterationTime - serverTime);
      if (sleepTimeSeconds > 0) {
        constexpr double kSecondsToMicroseconds = 1000 * 1000;
        QThread::usleep(kSecondsToMicroseconds * sleepTimeSeconds + 0.5);
      }
    } else {
      QThread::msleep(1);
    }
    
    firstLoopIteration = false;
  }
}

void Game::HandleLoadingProgress(const QByteArray& msg, PlayerInGame* player, const std::vector<std::shared_ptr<PlayerInGame>>& players) {
  if (msg.size() < 4) {
    LOG(ERROR) << "Received a too short LoadingProgress message";
    return;
  }
  
  u8 percentage = msg.data()[3];
  
  // Broadcast the loading progress to all other clients.
  QByteArray broadcastMsg = CreateLoadingProgressBroadcastMessage(player->index, percentage);
  for (const auto& otherPlayer : players) {
    if (otherPlayer.get() != player) {
      otherPlayer->socket->write(broadcastMsg);
    }
  }
}

void Game::HandleLoadingFinished(PlayerInGame* player, const std::vector<std::shared_ptr<PlayerInGame> >& players) {
  if (player->finishedLoading) {
    return;
  }
  player->finishedLoading = true;
  
  bool allPlayersFinishedLoading = true;
  for (const auto& otherPlayer : players) {
    if (!otherPlayer->finishedLoading) {
      allPlayersFinishedLoading = false;
      break;
    }
  }
  
  if (allPlayersFinishedLoading) {
    // Start the game.
    StartGame();
  }
}

void Game::SendChatBroadcast(u16 sendingPlayerIndex, const QString& text, const std::vector<std::shared_ptr<PlayerInGame>>& players) {
  // Broadcast the chat message to all clients.
  // Note that we even send it back to the original sender. This is such that all
  // clients receive the chat in the same order.
  QByteArray chatBroadcastMsg = CreateChatBroadcastMessage(sendingPlayerIndex, text);
  for (const auto& player : players) {
    player->socket->write(chatBroadcastMsg);
    player->socket->flush();
  }
}

// TODO: This is duplicated from match_setup.cpp, de-duplicate this
void Game::HandleChat(const QByteArray& msg, PlayerInGame* player, u32 len, const std::vector<std::shared_ptr<PlayerInGame>>& players) {
  QString text = QString::fromUtf8(msg.mid(3, len - 3));
  
  // Determine the index of the sending player.
  int sendingPlayerIndex = 0;
  for (usize i = 0; i < players.size(); ++ i) {
    if (players[i].get() == player) {
      break;
    } else {
      ++ sendingPlayerIndex;
    }
  }
  
  SendChatBroadcast(sendingPlayerIndex, text, players);
}

// TODO: This is duplicated from match_setup.cpp, de-duplicate this
void Game::HandlePing(const QByteArray& msg, PlayerInGame* player) {
  if (msg.size() < 3 + 8) {
    LOG(ERROR) << "Received a too short Ping message";
    return;
  }
  
  u64 number = mango::uload64(msg.data() + 3);
  
  TimePoint pingHandleTime = Clock::now();
  player->lastPingTime = pingHandleTime;
  
  double serverTimeSeconds = SecondsDuration(pingHandleTime - settings->serverStartTime).count();
  QByteArray response = CreatePingResponseMessage(number, serverTimeSeconds);
  qint64 result = player->socket->write(response);
  if (result != response.size()) {
    LOG(ERROR) << "Error sending PingResponse message: write() returned " << result << ", but the message size is " << response.size();
  }
  player->socket->flush();
}

void Game::HandleMoveToMapCoordMessage(const QByteArray& msg, PlayerInGame* player, u32 len) {
  // Parse message
  if (len < 13 + 4) {
    LOG(ERROR) << "Server: Erroneous MoveToMapCoord message (1)";
    return;
  }
  const char* data = msg.data();
  
  QPointF targetMapCoord(
      *reinterpret_cast<const float*>(data + 3),
      *reinterpret_cast<const float*>(data + 7));
  
  usize selectedUnitIdsSize = mango::uload16(data + 11);
  if (len != 13 + 4 * selectedUnitIdsSize) {
    LOG(ERROR) << "Server: Erroneous MoveToMapCoord message (2)";
    return;
  }
  std::vector<u32> selectedUnitIds(selectedUnitIdsSize);
  int offset = 13;
  for (usize i = 0; i < selectedUnitIdsSize; ++ i) {
    selectedUnitIds[i] = mango::uload32(data + offset);
    offset += 4;
  }
  
  // Handle move command (for all IDs which are actually units of the sending client)
  for (u32 id : selectedUnitIds) {
    auto it = map->GetObjects().find(id);
    if (it == map->GetObjects().end() ||
        !it->second->isUnit() ||
        it->second->GetPlayerIndex() != player->index) {
      continue;
    }
    
    ServerUnit* unit = AsUnit(it->second);
    unit->SetMoveToTarget(targetMapCoord);
  }
}

void Game::HandleSetTargetMessage(const QByteArray& msg, PlayerInGame* player, u32 len) {
  // Parse message
  if (len < 9 + 4) {
    LOG(ERROR) << "Server: Erroneous SetTarget message (1)";
    return;
  }
  const char* data = msg.data();
  
  u32 targetId = mango::uload32(data + 3);
  auto targetIt = map->GetObjects().find(targetId);
  if (targetIt == map->GetObjects().end()) {
    LOG(WARNING) << "Server: Received a SetTarget message for a target ID that does not exist (anymore?)";
    return;
  }
  
  usize selectedUnitIdsSize = mango::uload16(data + 7);
  if (len != 9 + 4 * selectedUnitIdsSize) {
    LOG(ERROR) << "Server: Erroneous SetTarget message (2)";
    return;
  }
  std::vector<u32> unitIds(selectedUnitIdsSize);
  int offset = 9;
  for (usize i = 0; i < selectedUnitIdsSize; ++ i) {
    unitIds[i] = mango::uload32(data + offset);
    offset += 4;
  }
  
  // Handle command (for all suitable IDs which are actually units of the sending client)
  SetUnitTargets(unitIds, player->index, targetId, targetIt->second);
}

void Game::HandleProduceUnitMessage(const QByteArray& msg, PlayerInGame* player) {
  if (msg.size() < 3 + 6) {
    LOG(ERROR) << "Received a too short ProduceUnit message";
    return;
  }
  const char* data = msg.data();
  
  u32 buildingId = mango::uload32(data + 3);
  UnitType unitType = static_cast<UnitType>(mango::uload16(data + 7));
  
  // Safely get the production building.
  auto buildingIt = map->GetObjects().find(buildingId);
  if (buildingIt == map->GetObjects().end()) {
    LOG(WARNING) << "Received a ProduceUnit message for a building with a non-existant object ID";
    return;
  }
  ServerObject* buildingObject = buildingIt->second;
  if (!buildingObject->isBuilding()) {
    LOG(WARNING) << "Received a ProduceUnit message for a production building object ID that is not a building";
    return;
  }
  ServerBuilding* productionBuilding = AsBuilding(buildingObject);
  
  if (productionBuilding->GetPlayerIndex() != player->index) {
    LOG(ERROR) << "Received a ProduceUnit message for a building that is not owned by the player";
    return;
  }
  if (productionBuilding->GetBuildPercentage() != 100) {
    LOG(ERROR) << "Received a ProduceUnit message for a building that is not fully constructed";
    return;
  }
  if (!productionBuilding->CanProduce(unitType, player)) {
    LOG(ERROR) << "Received a ProduceUnit message for a unit that either cannot be produced from the given building or for which the player does not have the right civilization/technologies";
    return;
  }
  
  // Is there space in the production queue?
  if (productionBuilding->GetProductionQueue().size() >= kMaxProductionQueueSize) {
    return;
  }
  
  // Does the player have sufficient resources to produce this unit?
  ResourceAmount unitCost = GetUnitCost(unitType);
  if (!player->resources.CanAfford(unitCost)) {
    LOG(WARNING) << "Received a ProduceUnit message for a unit for which the player has not enough resources";
    return;
  }
  // Subtract the unit cost from the player's resources.
  player->resources.Subtract(unitCost);
  
  // Add the unit to the production queue.
  productionBuilding->QueueUnit(unitType);
  accumulatedMessages[player->index] += CreateQueueUnitMessage(buildingId, static_cast<u16>(unitType));
}

void Game::HandlePlaceBuildingFoundationMessage(const QByteArray& msg, PlayerInGame* player) {
  if (msg.size() < 3 + 8) {
    LOG(ERROR) << "Received a too short PlaceBuildingFoundation message (1)";
    return;
  }
  const char* data = msg.data();
  
  BuildingType type = static_cast<BuildingType>(mango::uload16(data + 3));
  // TODO: Check whether the player is allowed to build this type of building
  
  QSize foundationSize = GetBuildingSize(type);
  QPoint baseTile(
      mango::uload16(data + 5),
      mango::uload16(data + 7));
  if (baseTile.x() + foundationSize.width() > map->GetWidth() ||
      baseTile.y() + foundationSize.height() > map->GetHeight()) {
    LOG(ERROR) << "Received a PlaceBuildingFoundation message with out-of-bounds building coordinates";
    return;
  }
  
  u16 villagerIdsSize = mango::uload16(data + 9);
  if (msg.size() < 3 + 8 + 4 * villagerIdsSize) {
    LOG(ERROR) << "Received a too short PlaceBuildingFoundation message (2)";
    return;
  }
  std::vector<u32> villagerIds(villagerIdsSize);
  int offset = 11;
  for (usize i = 0; i < villagerIdsSize; ++ i) {
    villagerIds[i] = mango::uload32(data + offset);
    offset += 4;
  }
  
  // Can the foundation be placed at the given location?
  // TODO: The same logic is implemented on the client, can that be unified?
  // TODO: Docks need a special case
  
  // 1) Check whether any map tile at this location is occupied.
  // TODO: We should also check against foundations set by the same player.
  for (int y = baseTile.y(); y < baseTile.y() + foundationSize.height(); ++ y) {
    for (int x = baseTile.x(); x < baseTile.x() + foundationSize.width(); ++ x) {
      if (map->occupiedForBuildingsAt(x, y)) {
        // TODO: Once map visibility is implemented, players must be allowed to place foundations over other players'
        //       buildings that they don't see. Otherwise, "foundation scanning" will be possible (as in the original game).
        LOG(WARNING) << "Received a PlaceBuildingFoundation message for an occupied space";
        return;
      }
    }
  }
  
  // 2) Check whether the maximum elevation difference within the building space does not exceed 2.
  //    TODO: I made this criterion up without testing it; is that actually how the original game works?
  // TODO: This criterion must not apply to farms.
  int minElevation = std::numeric_limits<int>::max();
  int maxElevation = std::numeric_limits<int>::min();
  for (int y = baseTile.y(); y <= baseTile.y() + foundationSize.height(); ++ y) {
    for (int x = baseTile.x(); x <= baseTile.x() + foundationSize.width(); ++ x) {
      int elevation = map->elevationAt(x, y);
      minElevation = std::min(minElevation, elevation);
      maxElevation = std::max(maxElevation, elevation);
    }
  }
  
  if (maxElevation - minElevation > 2) {
    LOG(WARNING) << "Received a PlaceBuildingFoundation message for a space that is too hilly";
    return;
  }
  
  // Does the player have sufficient resources to place this foundation?
  ResourceAmount cost = GetBuildingCost(type);
  if (!player->resources.CanAfford(cost)) {
    LOG(WARNING) << "Received a PlaceBuildingFoundation message for a building for which the player has not enough resources";
    return;
  }
  // Subtract the unit cost from the player's resources.
  player->resources.Subtract(cost);
  
  // Add the foundation and tell the sending player that it has been added.
  u32 newBuildingId;
  ServerBuilding* newBuildingFoundation = map->AddBuilding(player->index, type, baseTile, /*buildPercentage*/ 0, &newBuildingId, /*addOccupancy*/ false);
  
  QByteArray addObjectMsg = CreateAddObjectMessage(newBuildingId, newBuildingFoundation);
  accumulatedMessages[player->index] += addObjectMsg;
  
  // For all given villagers, set the target to the new foundation.
  SetUnitTargets(villagerIds, player->index, newBuildingId, newBuildingFoundation);
}

void Game::HandleDeleteObjectMessage(const QByteArray& msg, PlayerInGame* player) {
  if (msg.size() < 3 + 4) {
    LOG(ERROR) << "Received a too short DeleteObject message";
    return;
  }
  const char* data = msg.data();
  
  u32 objectId = mango::uload32(data + 3);
  
  // Safely get the object.
  auto buildingIt = map->GetObjects().find(objectId);
  if (buildingIt == map->GetObjects().end()) {
    LOG(WARNING) << "Received a DeleteObject message for an ID that does not exist";
    return;
  }
  ServerObject* object = buildingIt->second;
  if (object->GetPlayerIndex() != player->index) {
    LOG(ERROR) << "Received a DeleteObject message for an object that the player does not own";
    return;
  }
  
  DeleteObject(objectId, true);
}

void Game::HandleDequeueProductionQueueItemMessage(const QByteArray& msg, PlayerInGame* player) {
  if (msg.size() < 3 + 4 + 1) {
    LOG(ERROR) << "Received a too short DequeueProductionQueueItem message";
    return;
  }
  const char* data = msg.data();
  
  u32 objectId = mango::uload32(data + 3);
  
  // Safely get the production building.
  auto buildingIt = map->GetObjects().find(objectId);
  if (buildingIt == map->GetObjects().end()) {
    LOG(WARNING) << "Received a DequeueProductionQueueItem message for an ID that does not exist";
    return;
  }
  ServerObject* object = buildingIt->second;
  if (object->GetPlayerIndex() != player->index) {
    LOG(ERROR) << "Received a DequeueProductionQueueItem message for an object that the player does not own";
    return;
  }
  if (!object->isBuilding()) {
    LOG(ERROR) << "Received a DequeueProductionQueueItem message for an object that is not a building";
    return;
  }
  ServerBuilding* building = AsBuilding(object);
  
  // Safely get the queue index.
  u8 queueIndexFromBack = data[7];
  if (queueIndexFromBack >= building->GetProductionQueue().size()) {
    LOG(WARNING) << "queueIndexFromBack (" << queueIndexFromBack << ") >= building->GetProductionQueue().size() (" << building->GetProductionQueue().size() << ")";
    return;
  }
  u8 queueIndex = building->GetProductionQueue().size() - 1 - queueIndexFromBack;
  
  // Adjust population count (if relevant).
  if (queueIndex == 0 && building->GetProductionPercentage() > 0) {
    playersInGame->at(object->GetPlayerIndex())->populationIncludingInProduction -= 1;
  }
  
  // Remove the item from the queue.
  UnitType removedType = building->RemoveItemFromQueue(queueIndex);
  
  // Refund the resources for the item.
  player->resources.Add(GetUnitCost(removedType));
  
  // Tell the client about the successful removal.
  accumulatedMessages[player->index] += CreateRemoveFromProductionQueueMessage(objectId, queueIndex);
}

Game::ParseMessagesResult Game::TryParseClientMessages(PlayerInGame* player, const std::vector<std::shared_ptr<PlayerInGame>>& players) {
  while (true) {
    if (player->unparsedBuffer.size() < 3) {
      return ParseMessagesResult::NoAction;
    }
    
    char* data = player->unparsedBuffer.data();
    u16 msgLength = mango::uload16(data + 1);
    
    if (player->unparsedBuffer.size() < msgLength) {
      return ParseMessagesResult::NoAction;
    }
    
    if (msgLength < 3) {
      LOG(ERROR) << "Received a too short message. The received message length is (should be at least 3): " << msgLength;
    } else {
      ClientToServerMessage msgType = static_cast<ClientToServerMessage>(data[0]);
      
      switch (msgType) {
      case ClientToServerMessage::MoveToMapCoord:
        HandleMoveToMapCoordMessage(player->unparsedBuffer, player, msgLength);
        break;
      case ClientToServerMessage::SetTarget:
        HandleSetTargetMessage(player->unparsedBuffer, player, msgLength);
        break;
      case ClientToServerMessage::ProduceUnit:
        HandleProduceUnitMessage(player->unparsedBuffer, player);
        break;
      case ClientToServerMessage::PlaceBuildingFoundation:
        HandlePlaceBuildingFoundationMessage(player->unparsedBuffer, player);
        break;
      case ClientToServerMessage::DequeueProductionQueueItem:
        HandleDequeueProductionQueueItemMessage(player->unparsedBuffer, player);
        break;
      case ClientToServerMessage::DeleteObject:
        HandleDeleteObjectMessage(player->unparsedBuffer, player);
        break;
      case ClientToServerMessage::Chat:
        HandleChat(player->unparsedBuffer, player, msgLength, players);
        break;
      case ClientToServerMessage::Ping:
        HandlePing(player->unparsedBuffer, player);
        break;
      case ClientToServerMessage::Leave:
        LOG(INFO) << "Server: Got leave message from player " << player->name.toStdString() << " (index " << player->index << ")";
        return ParseMessagesResult::PlayerLeftOrShouldBeDisconnected;
      case ClientToServerMessage::LoadingProgress:
        HandleLoadingProgress(player->unparsedBuffer, player, players);
        break;
      case ClientToServerMessage::LoadingFinished:
        HandleLoadingFinished(player, players);
        break;
      default:
        LOG(ERROR) << "Server: Received a message in the game phase that cannot be parsed in this phase: " << static_cast<int>(msgType);
        break;
      }
    }
    
    player->unparsedBuffer.remove(0, msgLength);
  }
}

QByteArray Game::CreateMapUncoverMessage() {
  int width = map->GetWidth();
  int height = map->GetHeight();
  
  // TODO: Support large maps. If the message is too large, split it up into two or more messages.
  CHECK_LE(1 + 2 + (width + 1) * (height + 1), std::numeric_limits<u16>::max());
  
  // Create buffer
  QByteArray msg(1 + 2 + (width + 1) * (height + 1), Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ServerToClientMessage::MapUncover);
  mango::ustore16(data + 1, msg.size());
  
  // Fill buffer
  for (int y = 0; y <= height; ++ y) {
    for (int x = 0; x <= width; ++ x) {
      data[3 + x + y * (width + 1)] = map->elevationAt(x, y);
    }
  }
  
  return msg;
}

QByteArray Game::CreateAddObjectMessage(u32 objectId, ServerObject* object) {
  // Create buffer
  QByteArray msg(object->isBuilding() ? 23 : 23, Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ServerToClientMessage::AddObject);
  mango::ustore16(data + 1, msg.size());
  
  // Fill buffer
  data[3] = static_cast<u8>(object->GetObjectType());  // TODO: Currently unneeded since this could be derived from the message length
  mango::ustore32(data + 4, objectId);  // TODO: Maybe save bytes here as long as e.g. less than 16 bits are non-zero?
  *reinterpret_cast<u8*>(data + 8) = object->GetPlayerIndex();
  mango::ustore32(data + 9, object->GetHP());
  
  if (object->isBuilding()) {
    ServerBuilding* building = AsBuilding(object);
    mango::ustore16(data + 13, static_cast<u16>(building->GetBuildingType()));  // TODO: Would 8 bits be sufficient here?
    mango::ustore16(data + 15, building->GetBaseTile().x());
    mango::ustore16(data + 17, building->GetBaseTile().y());
    *reinterpret_cast<float*>(data + 19) = building->GetBuildPercentage();
  } else {
    ServerUnit* unit = AsUnit(object);
    mango::ustore16(data + 13, static_cast<u16>(unit->GetUnitType()));  // TODO: Would 8 bits be sufficient here?
    *reinterpret_cast<float*>(data + 15) = unit->GetMapCoord().x();
    *reinterpret_cast<float*>(data + 19) = unit->GetMapCoord().y();
  }
  
  return msg;
}

void Game::StartGame() {
  LOG(INFO) << "Server: Generating map ...";
  
  // Generate the map.
  map.reset(new ServerMap(settings->mapSize, settings->mapSize));
  map->GenerateRandomMap(playersInGame->size(), /*seed*/ 0);  // TODO: Choose seed
  
  LOG(INFO) << "Server: Preparing game start ...";
  
  // Send a start message with the server time at which the game starts,
  // including the initial view center for each player (on its initial TC),
  // the player's initial resources, and the map size.
  constexpr double kGameBeginOffsetSeconds = 0.2;  // give some time for the initial messages to arrive and be processed
  double serverTime = GetCurrentServerTime();
  gameBeginServerTime = serverTime + kGameBeginOffsetSeconds;
  lastSimulationTime = gameBeginServerTime;
  
  for (auto& player : *playersInGame) {
    // Find the player's town center and start with it in the center of the view.
    // If the player does not have a town center, find any villager and center on it instead.
    // If there is neither a town center nor a villager, center on any object of the player.
    QPointF initialViewCenter(0.5f * map->GetWidth(), 0.5f * map->GetHeight());
    for (const auto& item : map->GetObjects()) {
      if (item.second->GetPlayerIndex() != player->index) {
        continue;
      }
      
      if (item.second->isBuilding()) {
        ServerBuilding* building = AsBuilding(item.second);
        if (building->GetBuildingType() == BuildingType::TownCenter) {
          QSize buildingSize = GetBuildingSize(building->GetBuildingType());
          initialViewCenter =
              QPointF(building->GetBaseTile().x() + 0.5f * buildingSize.width(),
                      building->GetBaseTile().y() + 0.5f * buildingSize.height());
          break;
        }
      } else {  // if (item.second->isUnit()) {
        ServerUnit* unit = AsUnit(item.second);
        if (IsVillager(unit->GetUnitType())) {
          initialViewCenter = unit->GetMapCoord();
        }
      }
    }
    
    // Send message.
    QByteArray gameBeginMsg = CreateGameBeginMessage(
        gameBeginServerTime,
        initialViewCenter,
        player->resources.wood(),
        player->resources.food(),
        player->resources.gold(),
        player->resources.stone(),
        map->GetWidth(),
        map->GetHeight());
    player->socket->write(gameBeginMsg);
  }
  
  // Send a message with the initial visible map content
  QByteArray mapUncoverMsg = CreateMapUncoverMessage();
  for (auto& player : *playersInGame) {
    player->socket->write(mapUncoverMsg);
  }
  
  // Send creation messages for the initial map objects and count initial population
  const auto& objects = map->GetObjects();
  for (const auto& item : objects) {
    ServerObject* object = item.second;
    
    QByteArray addObjectMsg = CreateAddObjectMessage(item.first, object);
    for (auto& player : *playersInGame) {
      player->socket->write(addObjectMsg);
    }
    
    if (object->isBuilding()) {
      ServerBuilding* building = AsBuilding(object);
      if (building->GetPlayerIndex() != kGaiaPlayerIndex) {
        playersInGame->at(building->GetPlayerIndex())->availablePopulationSpace += GetBuildingProvidedPopulationSpace(building->GetBuildingType());
      }
    } else if (object->isUnit()) {
      playersInGame->at(object->GetPlayerIndex())->populationIncludingInProduction += 1;
    }
  }
  for (auto& player : *playersInGame) {
    player->socket->flush();
  }
  
  LOG(INFO) << "Server: Game start prepared";
}

void Game::SimulateGameStep(double gameStepServerTime, float stepLengthInSeconds) {
  // Reset all players to "not housed".
  for (auto& player : *playersInGame) {
    player->isHoused = false;
  }
  
  // Iterate over all game objects to update their state.
  auto end = map->GetObjects().end();
  for (auto it = map->GetObjects().begin(); it != end; ++ it) {
    const u32& objectId = it->first;
    ServerObject* object = it->second;
    
    if (object->isUnit()) {
      ServerUnit* unit = AsUnit(object);
      SimulateGameStepForUnit(objectId, unit, gameStepServerTime, stepLengthInSeconds);
    } else if (object->isBuilding()) {
      ServerBuilding* building = AsBuilding(object);
      SimulateGameStepForBuilding(objectId, building, stepLengthInSeconds);
    }
  }
  
  // Handle delayed object deletion.
  for (u32 id : objectDeleteList) {
    auto it = map->GetObjects().find(id);
    if (it != map->GetObjects().end()) {
      delete it->second;
      map->GetObjects().erase(it);
    }
  }
  objectDeleteList.clear();
  
  // Check whether we need to send "housed" messages to clients.
  for (usize playerIndex = 0; playerIndex < playersInGame->size(); ++ playerIndex) {
    auto& player = (*playersInGame)[playerIndex];
    if (!player->isConnected) {
      continue;
    }
    
    if (player->isHoused != player->wasHousedBefore) {
      accumulatedMessages[playerIndex] += CreateSetHousedMessage(player->isHoused);
      
      player->wasHousedBefore = player->isHoused;
    }
  }
  
  // Send out the accumulated messages for each player. The advantage of the accumulation is that
  // the TCP header only has to be sent once for each player, rather than for each message.
  //
  // All messages of this game step are prefixed by a message indicating the current server time,
  // which avoids sending it with each single message.
  for (usize playerIndex = 0; playerIndex < playersInGame->size(); ++ playerIndex) {
    auto& player = (*playersInGame)[playerIndex];
    if (!player->isConnected) {
      accumulatedMessages[playerIndex].clear();
      continue;
    }
    
    // Does the player need to be notified about a changed amount of resources?
    if (player->resources != player->lastResources) {
      accumulatedMessages[playerIndex] += CreateResourcesUpdateMessage(player->resources);
      player->lastResources = player->resources;
    }
    
    if (!accumulatedMessages[playerIndex].isEmpty()) {
      player->socket->write(
          CreateGameStepTimeMessage(gameStepServerTime) +
          accumulatedMessages[playerIndex]);
      accumulatedMessages[playerIndex].clear();
      player->socket->flush();
    }
  }
}

static bool DoesUnitTouchBuildingArea(ServerUnit* unit, const QPointF& unitMapCoord, ServerBuilding* building, float errorMargin) {
  // Get the point withing the building's area which is closest to the unit
  QSize buildingSize = GetBuildingSize(building->GetBuildingType());
  const QPoint& baseTile = building->GetBaseTile();
  QPointF closestPointInBuilding(
      std::max<float>(baseTile.x(), std::min<float>(baseTile.x() + buildingSize.width(), unitMapCoord.x())),
      std::max<float>(baseTile.y(), std::min<float>(baseTile.y() + buildingSize.height(), unitMapCoord.y())));
  
  // Check whether this point is closer to the unit than the unit's radius.
  float unitRadius = GetUnitRadius(unit->GetUnitType());
  float threshold = unitRadius - errorMargin;
  return SquaredDistance(unitMapCoord, closestPointInBuilding) < threshold * threshold;
}

static bool DoUnitsTouch(ServerUnit* unit, const QPointF& unitMapCoord, ServerUnit* otherUnit, float errorMargin) {
  float unitRadius = GetUnitRadius(unit->GetUnitType());
  float otherUnitRadius = GetUnitRadius(otherUnit->GetUnitType());
  
  float threshold = unitRadius + otherUnitRadius - errorMargin;
  return SquaredDistance(unitMapCoord, otherUnit->GetMapCoord()) < threshold * threshold;
}

static bool IsFoundationFree(ServerBuilding* foundation, ServerMap* map) {
  // Check whether map tiles are occupied
  const QPoint& baseTile = foundation->GetBaseTile();
  QSize foundationSize = GetBuildingSize(foundation->GetBuildingType());
  for (int y = baseTile.y(); y < baseTile.y() + foundationSize.height(); ++ y) {
    for (int x = baseTile.x(); x < baseTile.x() + foundationSize.width(); ++ x) {
      if (map->occupiedForBuildingsAt(x, y)) {
        return false;
      }
    }
  }
  
  // Check whether units are on top of the foundation
  for (const auto& item : map->GetObjects()) {
    if (item.second->isUnit()) {
      ServerUnit* unit = AsUnit(item.second);
      if (DoesUnitTouchBuildingArea(unit, unit->GetMapCoord(), foundation, 0.01f)) {
        return false;
      }
    }
  }
  
  return true;
}

static bool TryEvadeUnit(ServerUnit* unit, float moveDistance, const QPointF& newMapCoord, ServerUnit* collidingUnit, QPointF* evadeMapCoord) {
  // Intersect a circle of radius "moveDistance", centered at unit->GetMapCoord(),
  // with a circle of radius GetUnitRadius(unit->GetUnitType()) + GetUnitRadius(collidingUnit->GetUnitType()), centered at collidingUnit->GetMapCoord().
  constexpr float kErrorTolerance = 1e-3f;
  
  const QPointF unitCenter = unit->GetMapCoord();
  float unitMoveRadius = moveDistance;
  
  const QPointF obstacleCenter = collidingUnit->GetMapCoord();
  float obstacleRadius = GetUnitRadius(unit->GetUnitType()) + GetUnitRadius(collidingUnit->GetUnitType()) + kErrorTolerance;
  
  QPointF unitToObstacle = obstacleCenter - unitCenter;
  float centerDistance = Length(unitToObstacle);
  QPointF unitToObstacleDir = unitToObstacle / std::max(1e-5f, centerDistance);
  
  float a = (unitMoveRadius * unitMoveRadius - obstacleRadius * obstacleRadius + centerDistance * centerDistance) / (2 * centerDistance);
  float termInSqrt = unitMoveRadius * unitMoveRadius - a * a;
  if (termInSqrt <= 0) {
    return false;
  }
  float h = sqrtf(termInSqrt);
  
  QPointF basePoint = unitCenter + a * unitToObstacleDir;
  
  QPointF intersection1 = basePoint + h * QPointF(unitToObstacleDir.y(), -unitToObstacleDir.x());
  QPointF intersection2 = basePoint - h * QPointF(unitToObstacleDir.y(), -unitToObstacleDir.x());
  
  float squaredDistance1 = SquaredDistance(newMapCoord, intersection1);
  float squaredDistance2 = SquaredDistance(newMapCoord, intersection2);
  
  *evadeMapCoord = (squaredDistance1 < squaredDistance2) ? intersection1 : intersection2;
  return true;
}

void Game::SimulateGameStepForUnit(u32 unitId, ServerUnit* unit, double gameStepServerTime, float stepLengthInSeconds) {
  bool unitMovementChanged = false;
  
  // If the unit is currently attacking, continue this, since it cannot be interrupted.
  if (unit->GetCurrentAction() == UnitAction::Attack) {
    bool stayInPlace = false;
    
    auto targetIt = map->GetObjects().find(unit->GetTargetObjectId());
    u32 targetId;
    ServerObject* target;
    if (targetIt == map->GetObjects().end()) {
      targetId = kInvalidObjectId;
      target = nullptr;
    } else {
      targetId = targetIt->first;
      target = targetIt->second;
    }
    
    if (SimulateMeleeAttack(unitId, unit, targetId, target, gameStepServerTime, stepLengthInSeconds, &unitMovementChanged, &stayInPlace)) {
      // The attack is still in progress.
      return;
    }
    
    // The attack finished.
    // If any other command has been given to the unit in the meantime, follow the other command.
    auto manualTargetIt = map->GetObjects().find(unit->GetManuallyTargetedObjectId());
    if (manualTargetIt != map->GetObjects().end()) {
      unit->SetTarget(manualTargetIt->first, manualTargetIt->second, false);
    }
  }
  
  // If the unit's goal has been updated, plan a path towards the goal.
  if (unit->HasMoveToTarget() && !unit->HasPath()) {
    PlanUnitPath(unit);
    unitMovementChanged = true;
  } else if (unit->HasMoveToTarget() && unit->GetTargetObjectId() != kInvalidObjectId) {
    // Check whether we target a moving object. If yes and the target has moved too much,
    // re-plan our path to the target.
    auto targetIt = map->GetObjects().find(unit->GetTargetObjectId());
    if (targetIt == map->GetObjects().end()) {
      unit->RemoveTarget();
    } else if (targetIt->second->isUnit()) {
      ServerUnit* targetUnit = AsUnit(targetIt->second);
      
      constexpr float kReplanThresholdDistance = 0.1f * 0.1f;
      if (SquaredDistance(targetUnit->GetMapCoord(), unit->GetMoveToTargetMapCoord()) > kReplanThresholdDistance) {
        unit->SetTarget(unit->GetTargetObjectId(), targetUnit, false);
        PlanUnitPath(unit);
        unitMovementChanged = true;
      }
    }
  }
  
  if (unit->GetMovementDirection() != QPointF(0, 0)) {
    float moveDistance = unit->GetMoveSpeed() * stepLengthInSeconds;
    
    QPointF newMapCoord = unit->GetMapCoord() + moveDistance * unit->GetMovementDirection();
    bool stayInPlace = false;
    
    // If the unit has a target object, test whether it touches this target.
    u32 targetObjectId = unit->GetTargetObjectId();
    if (targetObjectId != kInvalidObjectId) {
      auto targetIt = map->GetObjects().find(targetObjectId);
      if (targetIt == map->GetObjects().end()) {
        unit->RemoveTarget();
      } else {
        ServerObject* targetObject = targetIt->second;
        if (targetObject->isBuilding()) {
          ServerBuilding* targetBuilding = AsBuilding(targetObject);
          if (DoesUnitTouchBuildingArea(unit, newMapCoord, targetBuilding, 0)) {
            InteractionType interaction = GetInteractionType(unit, targetBuilding);
            
            if (interaction == InteractionType::Construct) {
              SimulateBuildingConstruction(stepLengthInSeconds, unit, targetObjectId, targetBuilding, &unitMovementChanged, &stayInPlace);
            } else if (interaction == InteractionType::CollectBerries ||
                       interaction == InteractionType::CollectWood ||
                       interaction == InteractionType::CollectGold ||
                       interaction == InteractionType::CollectStone) {
              SimulateResourceGathering(stepLengthInSeconds, unitId, unit, targetBuilding, &unitMovementChanged, &stayInPlace);
            } else if (interaction == InteractionType::DropOffResource) {
              SimulateResourceDropOff(unitId, unit, &unitMovementChanged);
            } else if (interaction == InteractionType::Attack) {
              SimulateMeleeAttack(unitId, unit, targetIt->first, targetBuilding, gameStepServerTime, stepLengthInSeconds, &unitMovementChanged, &stayInPlace);
            }
          }
        } else if (targetObject->isUnit()) {
          ServerUnit* targetUnit = AsUnit(targetObject);
          if (DoUnitsTouch(unit, newMapCoord, targetUnit, 0)) {
            InteractionType interaction = GetInteractionType(unit, targetUnit);
            
            if (interaction == InteractionType::Attack) {
              SimulateMeleeAttack(unitId, unit, targetIt->first, targetUnit, gameStepServerTime, stepLengthInSeconds, &unitMovementChanged, &stayInPlace);
            }
          }
        }
      }
    }
    
    if (!stayInPlace && unit->HasPath()) {
      // Test whether the current goal was reached.
      QPointF toGoal = unit->GetNextPathTarget() - unit->GetMapCoord();
      float squaredDistanceToGoal = SquaredLength(toGoal);
      float directionDotToGoal =
          unit->GetMovementDirection().x() * toGoal.x() +
          unit->GetMovementDirection().y() * toGoal.y();
      
      if (squaredDistanceToGoal <= moveDistance * moveDistance || directionDotToGoal <= 0) {
        // The goal was reached.
        if (!map->DoesUnitCollide(unit, unit->GetNextPathTarget())) {
          unit->SetMapCoord(unit->GetNextPathTarget());
        }
        
        // Continue with the next part of the path if any, or stop if the path was completed.
        unit->PathSegmentCompleted();
        if (unit->HasPath()) {
          // Continue with the next path segment.
          // TODO: This is a duplicate of the code at the end of PlanUnitPath()
          QPointF direction = unit->GetNextPathTarget() - unit->GetMapCoord();
          direction = direction / std::max(1e-4f, Length(direction));
          unit->SetMovementDirection(direction);
        } else {
          // Completed the path.
          unit->StopMovement();
        }
        
        unitMovementChanged = true;
      } else {
        // Move the unit if the path is free.
        ServerUnit* collidingUnit;
        if (map->DoesUnitCollide(unit, newMapCoord, &collidingUnit)) {
          bool evaded = false;
          if (collidingUnit != nullptr) {
            // Try to evade the unit by moving alongside it.
            QPointF evadeMapCoord;
            if (TryEvadeUnit(unit, moveDistance, newMapCoord, collidingUnit, &evadeMapCoord) &&
                !map->DoesUnitCollide(unit, evadeMapCoord, &collidingUnit)) {
              // Successfully found a side step to avoid bumping into the other unit.
              // Test whether this would still bring us closer to our goal.
              if (SquaredDistance(unit->GetNextPathTarget(), evadeMapCoord) <
                  SquaredDistance(unit->GetNextPathTarget(), unit->GetMapCoord())) {
                // Use the evade step.
                // Change our movement direction in order to still face the next path goal.
                unit->SetMapCoord(evadeMapCoord);
                
                QPointF direction = unit->GetNextPathTarget() - unit->GetMapCoord();
                direction = direction / std::max(1e-4f, Length(direction));
                unit->SetMovementDirection(direction);
                
                if (unit->GetCurrentAction() != UnitAction::Moving) {
                  unit->SetCurrentAction(UnitAction::Moving);
                }
                
                unitMovementChanged = true;
                evaded = true;
              }
            }
          }
          
          if (!evaded && unit->GetCurrentAction() != UnitAction::Idle) {
            unit->PauseMovement();
            unitMovementChanged = true;
          }
        } else {
          unit->SetMapCoord(newMapCoord);
          
          if (unit->GetCurrentAction() != UnitAction::Moving) {
            unitMovementChanged = true;
            unit->SetCurrentAction(UnitAction::Moving);
          }
        }
      }
    }
  }
  
  if (unitMovementChanged) {
    // Notify all clients that see the unit about its new movement / animation.
    for (usize playerIndex = 0; playerIndex < playersInGame->size(); ++ playerIndex) {
      // TODO: Only do this if the player sees the unit.
      
      accumulatedMessages[playerIndex] +=
          CreateUnitMovementMessage(
              unitId,
              unit->GetMapCoord(),
              unit->GetMoveSpeed() * unit->GetMovementDirection(),
              unit->GetCurrentAction());
    }
  }
}

/// Tests whether the unit could walk from p0 to p1 (or vice versa) without colliding
/// with a building. Notice that this function does not check whether the start and
/// end points themselves are (fully) free, it only checks the space between them.
bool Game::IsPathFree(float unitRadius, const QPointF& p0, const QPointF& p1, const QRect& openRect) {
  // Obtain the points to the right and left of p0 and p1.
  constexpr float kErrorEpsilon = 1e-3f;
  
  QPointF p0ToP1 = p1 - p0;
  QPointF right(
      -p0ToP1.y(),
      p0ToP1.x());
  right *= (unitRadius + kErrorEpsilon) / std::max(1e-4f, Length(right));
  
  QPointF p0Right = p0 + right;
  QPointF p0Left = p0 - right;
  QPointF p1Right = p1 + right;
  QPointF p1Left = p1 - right;
  
  // Rasterize the polygon defined by all the points into the map grid.
  int minRow = std::numeric_limits<int>::max();
  int maxRow = 0;
  std::vector<std::pair<int, int>> rowRanges(map->GetHeight(), std::make_pair(std::numeric_limits<int>::max(), 0));
  
  auto rasterize = [&](int x, int y) {
    // For safety, clamp the coordinate to the map area.
    x = std::max(0, std::min(map->GetWidth() - 1, x));
    y = std::max(0, std::min(map->GetHeight() - 1, y));
    
    minRow = std::min(minRow, y);
    maxRow = std::max(maxRow, y);
    
    auto& rowRange = rowRanges[y];
    rowRange.first = std::min(rowRange.first, x);
    rowRange.second = std::max(rowRange.second, x);
  };
  
  auto rasterizeLine = [&](const QPointF& start, const QPointF& end) {
    QPointF cur = start;
    QPointF remaining = end - start;
    
    int x = static_cast<int>(cur.x());
    int y = static_cast<int>(cur.y());
    
    int endX = static_cast<int>(end.x());
    int endY = static_cast<int>(end.y());
    
    int sx = (remaining.x() > 0) ? 1 : -1;
    int sy = (remaining.y() > 0) ? 1 : -1;
    
    while (true) {
      rasterize(x, y);
      
      float fx = cur.x() - x;
      float fy = cur.y() - y;
      
      float xToBorder = (sx > 0) ? (1 - fx) : fx;
      float yToBorder = (sy > 0) ? (1 - fy) : fy;
      
      if (fabs(remaining.x()) <= xToBorder &&
          fabs(remaining.y()) <= yToBorder) {
        // The goal is in the current square.
        break;
      }
      if ((endX - x) * sx <= 0 &&
          (endY - y) * sy <= 0) {
        // We somehow surpassed the end without noticing.
        LOG(WARNING) << "Emergency exit.";
        break;
      }
      
      float diffX, diffY;
      if (fabs(remaining.x()) / std::max<float>(1e-5f, fabs(remaining.y())) >
          xToBorder / std::max(1e-5f, yToBorder)) {
        // Go in x direction
        if (sx > 0) {
          // Go to the right.
          diffX = - (1 - fx);
        } else {
          // Go to the left.
          diffX = fx;
        }
        diffY = remaining.y() * ((remaining.x() + diffX) / remaining.x() - 1);
        x += sx;
      } else {
        // Go in y direction
        if (sy > 0) {
          // Go to the bottom.
          diffY = - (1 - fy);
        } else {
          // Go to the top.
          diffY = fy;
        }
        diffX = remaining.x() * ((remaining.y() + diffY) / remaining.y() - 1);
        y += sy;
      }
      
      remaining.setX(remaining.x() + diffX);
      remaining.setY(remaining.y() + diffY);
      cur.setX(cur.x() - diffX);
      cur.setY(cur.y() - diffY);
    }
  };
  
  rasterizeLine(p0Right, p1Right);
  rasterizeLine(p0Left, p1Left);
  rasterizeLine(p0Right, p0Left);
  rasterizeLine(p1Right, p1Left);
  
  // Test whether any tile within the boundaries of the rasterized area is occupied.
  // If yes, the path is blocked.
  constexpr bool kDebugRasterization = false;
  constexpr const char* kDebugImagePath = "/tmp/FreeAge_pathFree_debug.png";
  if (kDebugRasterization) {
    QImage debugImage(map->GetWidth(), map->GetHeight(), QImage::Format_RGB32);
    debugImage.fill(qRgb(255, 255, 255));
    
    for (int row = minRow; row <= maxRow; ++ row) {
      auto& rowRange = rowRanges[row];
      for (int col = rowRange.first; col <= rowRange.second; ++ col) {
        if (map->occupiedForUnitsAt(col, row) && !openRect.contains(col, row, false)) {
          debugImage.setPixelColor(col, row, qRgb(255, 0, 0));
        } else {
          debugImage.setPixelColor(col, row, qRgb(0, 255, 0));
        }
      }
    }
    
    LOG(WARNING) << "Saving IsPathFree() debug image to " << kDebugImagePath;
    debugImage.save(kDebugImagePath);
    
    char dummy;
    std::cin >> dummy;
  }
  
  for (int row = minRow; row <= maxRow; ++ row) {
    auto& rowRange = rowRanges[row];
    for (int col = rowRange.first; col <= rowRange.second; ++ col) {
      if (map->occupiedForUnitsAt(col, row) && !openRect.contains(col, row, false)) {
        return false;
      }
    }
  }
  
  return true;
}

void Game::PlanUnitPath(ServerUnit* unit) {
  Timer pathPlanningTimer;
  
  typedef float CostT;
  
  int mapWidth = map->GetWidth();
  int mapHeight = map->GetHeight();
  
  // Determine the tile that the unit stands on. This will be the start tile.
  QPoint start(
      std::max(0, std::min(mapWidth - 1, static_cast<int>(unit->GetMapCoord().x()))),
      std::max(0, std::min(mapHeight - 1, static_cast<int>(unit->GetMapCoord().y()))));
  
  // Determine the goal tiles and treat them as open even if they are occupied.
  // This is done for the tiles taken up by the unit's target.
  // This allows us to plan a path "into" the target.
  QRect goalRect;
  if (unit->GetTargetObjectId() != kInvalidObjectId) {
    auto targetIt = map->GetObjects().find(unit->GetTargetObjectId());
    if (targetIt != map->GetObjects().end()) {
      ServerObject* targetObject = targetIt->second;
      if (targetObject->isBuilding()) {
        ServerBuilding* targetBuilding = AsBuilding(targetObject);
        
        const QPoint& baseTile = targetBuilding->GetBaseTile();
        QSize buildingSize = GetBuildingSize(targetBuilding->GetBuildingType());
        goalRect = QRect(baseTile, buildingSize);
      }
    }
  }
  if (goalRect.isNull()) {
    goalRect = QRect(
        std::max(0, std::min(mapWidth - 1, static_cast<int>(unit->GetMoveToTargetMapCoord().x()))),
        std::max(0, std::min(mapHeight - 1, static_cast<int>(unit->GetMoveToTargetMapCoord().y()))),
        1,
        1);
  }
  
  // Use A* to plan a path from the start to the goal tile.
  // * Treat unit-occupied tiles as obstacles.
  // * Treat tiles that are occupied by the unit's target building (if any) as free,
  //   such that the algorithm can plan a path "into" the goal.
  // * If the goal is not reachable, return the path that leads to the reachable
  //   position that is closest to the goal.
  struct Location {
    QPoint loc;
    float priority;
    
    inline Location(const QPoint& loc, float priority)
        : loc(loc),
          priority(priority) {}
    
    inline bool operator> (const Location& other) const {
      return priority > other.priority;
    }
  };
  
  std::priority_queue<Location, std::vector<Location>, std::greater<Location>> priorityQueue;
  
  priorityQueue.emplace(start, 0.f);
  
  std::vector<CostT> costSoFar(mapWidth * mapHeight);
  // TODO: Perhaps some potential overhead of this may be reduced by doing a memset() to zero instead,
  //       using "negative zero" for the start node, and later comparing to "positive zero" as a special case using std::signbit()?
  std::fill(std::begin(costSoFar), std::end(costSoFar), std::numeric_limits<float>::infinity());
  costSoFar[start.x() + mapWidth * start.y()] = 0;
  
  // Directions are encoded as row-major indices of grid cells in a 4x4 grid,
  // with (1, 1) being the origin of movement. A 3x3 grid would suffice,
  // however, with a 4x4 grid, computations are faster. So, for example,
  // the value 0 corresponds to cell (0, 0) in the grid, which has an offset of (-1, -1)
  // from the movement origin. So, the movement came from (-1, -1).
  // Second example: The value 4 corresponds to cell (0, 1) with movement (-1, 0).
  // The value 5 corresponds to zero movement, this is used for the start and for initialization.
  constexpr u8 cameFromUninitializedValue = 5;
  std::vector<u8> cameFrom(mapWidth * mapHeight);
  memset(cameFrom.data(), cameFromUninitializedValue, mapWidth * mapHeight);
  
  CostT smallestReachedHeuristicValue = std::numeric_limits<CostT>::max();
  QPoint smallestReachedHeuristicTile(-1, -1);
  
  static int numNeighborsToCheckArray[11] = {
    3,
    7,
    3,
    0,  // no valid direction
    7,
    8,
    7,
    0,  // no valid direction
    3,
    7,
    3,
  };
  static QPoint neighborsToCheckArray[11][8] = {
    {QPoint(1, 0), QPoint(0, 1), /*dependent on previous being free*/ QPoint(1, 1)},
    {QPoint(0, 1), /*occ. test*/ QPoint(1, -1), QPoint(1, 0), QPoint(1, 1), /*occ. test*/ QPoint(-1, -1), QPoint(-1, 0), QPoint(-1, 1)},
    {QPoint(-1, 0), QPoint(0, 1), /*dependent on previous being free*/ QPoint(-1, 1)},
    {},
    {QPoint(1, 0), /*occ. test*/ QPoint(-1, -1), QPoint(0, -1), QPoint(1, -1), /*occ. test*/ QPoint(-1, 1), QPoint(0, 1), QPoint(1, 1)},
    {QPoint(0, -1), QPoint(-1, 0), QPoint(1, 0), QPoint(0, 1), QPoint(1, 1), QPoint(-1, -1), QPoint(1, -1), QPoint(-1, 1)},
    {QPoint(-1, 0), /*occ. test*/ QPoint(1, -1), QPoint(0, -1), QPoint(-1, -1), /*occ. test*/ QPoint(1, 1), QPoint(0, 1), QPoint(-1, 1)},
    {},
    {QPoint(0, -1), QPoint(1, 0), /*dependent on previous being free*/ QPoint(1, -1)},
    {QPoint(0, -1), /*occ. test*/ QPoint(1, 1), QPoint(1, 0), QPoint(1, -1), /*occ. test*/ QPoint(-1, 1), QPoint(-1, 0), QPoint(-1, -1)},
    {QPoint(-1, 0), QPoint(0, -1), /*dependent on previous being free*/ QPoint(-1, -1)},
  };
  // Here we encode that diagonal movements require the two adjacent tiles to be free.
  static u8 neighborsRequireFreeTiles[11][8] = {
    {0, 0, 0b11},
    {0, /*occ. test*/ 0, 0, 0b101, /*occ. test*/ 0, 0, 0b100001},
    {0, 0, 0b11},
    {},
    {0, /*occ. test*/ 0, 0, 0b101, /*occ. test*/ 0, 0, 0b100001},
    {0, 0, 0, 0, 0b1100, 0b0011, 0b0101, 0b1010},
    {0, /*occ. test*/ 0, 0, 0b101, /*occ. test*/ 0, 0, 0b100001},
    {},
    {0, 0, 0b11},
    {0, /*occ. test*/ 0, 0, 0b101, /*occ. test*/ 0, 0, 0b100001},
    {0, 0, 0b11},
  };
  
  int debugConsideredNodesCount = 0;
  
  // Set this to true to have a debug image written to /tmp/FreeAge_pathfinding_debug.png.
  // Legend:
  // * Black: Occupied tiles.
  // * Dark green: open rect.
  // * White: Free tiles, never considered by pathfinding.
  // * Light red: Free tiles, considered by pathfinding.
  // * Light yellow: Free tiles, added to the priority queue as a neighbor but not directly considered.
  // * Green: Free tiles that are part of the final path.
  constexpr bool kOutputDebugImage = false;
  constexpr const char* kDebugImagePath = "/tmp/FreeAge_pathfinding_debug.png";
  QImage debugImage;
  if (kOutputDebugImage) {
    debugImage = QImage(mapWidth, mapHeight, QImage::Format_RGB32);
    for (int y = 0; y < mapHeight; ++ y) {
      for (int x = 0; x < mapWidth; ++ x) {
        if (map->occupiedForUnitsAt(x, y)) {
          debugImage.setPixel(x, y, qRgb(0, 0, 0));
        } else {
          debugImage.setPixel(x, y, qRgb(255, 255, 255));
        }
      }
    }
    for (int y = goalRect.y(); y < goalRect.bottom(); ++ y) {
      for (int x = goalRect.x(); x < goalRect.right(); ++ x) {
        debugImage.setPixel(x, y, qRgb(0, 100, 0));
      }
    }
  }
  
  QPoint reachedGoalTile(-1, -1);
  while (!priorityQueue.empty()) {
    Location current = priorityQueue.top();
    priorityQueue.pop();
    
    ++ debugConsideredNodesCount;
    if (kOutputDebugImage) {
      debugImage.setPixel(current.loc.x(), current.loc.y(), qRgb(255, 127, 127));
    }
    
    if (goalRect.contains(current.loc, false)) {
      reachedGoalTile = current.loc;
      break;
    }
    
    int currentGridIndex = current.loc.x() + mapWidth * current.loc.y();
    CostT currentCost = costSoFar[currentGridIndex];
    int currentCameFrom = cameFrom[currentGridIndex];
    
    int numNeighborsToCheck = numNeighborsToCheckArray[currentCameFrom];
    QPoint* neighbors = neighborsToCheckArray[currentCameFrom];
    u8 freeNeighbors = 0;  // bitmask with a 1 for each free neighbor
    
    for (int neighborIdx = 0; neighborIdx < numNeighborsToCheck; ++ neighborIdx) {
      QPoint neighborDir = neighbors[neighborIdx];
      QPoint nextTile = current.loc + neighborDir;
      int nextGridIndex = nextTile.x() + mapWidth * nextTile.y();
      
      // Special case:
      // For straight movements, numNeighborsToCheck is 7. It contains two sets of directions preceded by an occupancy check.
      // This means that we only consider those directions if the occupancy-checked tile is occupied.
      // The occupancy check is at neighbor indices 1 and 4 and each applies to the two following neighbors.
      int skipLenght = 0;
      if (numNeighborsToCheck == 7 &&
          (neighborIdx == 1 || neighborIdx == 4)) {
        // This neighbor is an occupancy check. If it is occupied, we need to consider the following
        // two neighbors, otherwise we can skip them.
        skipLenght = 2;
      } else {
        u8 requiredFreeNeighbors = neighborsRequireFreeTiles[currentCameFrom][neighborIdx];
        if ((freeNeighbors & requiredFreeNeighbors) != requiredFreeNeighbors) {
          // The required previous free neighbors are not free.
          continue;
        }
      }
      
      // Skip neighbor if it is outside of the map.
      if (nextTile.x() < 0 || nextTile.y() < 0 ||
          nextTile.x() >= mapWidth || nextTile.y() >= mapHeight) {
        neighborIdx += skipLenght;
        continue;
      }
      // Skip neighbor if it is occupied.
      if (map->occupiedForUnitsAt(nextTile.x(), nextTile.y()) && !goalRect.contains(nextTile, false)) {
        // Continue while not skipping over possible neighbors depending on this as an occupancy check (since the check returned true).
        continue;
      }
      
      freeNeighbors |= 1 << neighborIdx;
      
      // Skip this if it is a failed occupancy check (instead of an actual neighbor).
      if (skipLenght > 0) {
        // If this was an occupancy check, but the tile is free, continue while skipping over the dependent neighbors.
        neighborIdx += skipLenght;
        continue;
      }
      
      // Compute the cost to reach this neighbor from the start.
      constexpr float sqrt2 = 1.41421356237310f;
      CostT newCost = currentCost + ((neighborDir.manhattanLength() == 2) ? sqrt2 : 1);
      
      // If the cost is better than the best cost known so far, expand the path to this neighbor.
      CostT* nextCostSoFar = &costSoFar[nextGridIndex];
      if (newCost < *nextCostSoFar) {
        *nextCostSoFar = newCost;
        
        // Compute the "diagonal distance" as a heuristic for the remaining path length to the goal.
        // This is a distance metric on the grid while allowing diagonal movements.
        int goalX = std::max(goalRect.x(), std::min(goalRect.x() + goalRect.width() - 1, nextTile.x()));
        int goalY = std::max(goalRect.y(), std::min(goalRect.y() + goalRect.height() - 1, nextTile.y()));
        int xDiff = std::abs(nextTile.x() - goalX);
        int yDiff = std::abs(nextTile.y() - goalY);
        int minDiff = std::min(xDiff, yDiff);
        int maxDiff = std::max(xDiff, yDiff);
        CostT heuristic = minDiff * sqrt2 + (maxDiff - minDiff) * 1;
        
        // Remember the closest tile to the goal that we found. This becomes important
        // in case we cannot reach the goal at all.
        if (heuristic < smallestReachedHeuristicValue) {
          smallestReachedHeuristicValue = heuristic;
          smallestReachedHeuristicTile = nextTile;
        }
        
        priorityQueue.emplace(nextTile, newCost + heuristic);
        cameFrom[nextGridIndex] = ((-neighborDir.x()) + 1) + 4 * ((-neighborDir.y()) + 1);
        
        if (kOutputDebugImage) {
          debugImage.setPixel(nextTile.x(), nextTile.y(), qRgb(255, 255, 127));
        }
      }
    }
  }
  
  LOG(1) << "Pathfinding: considered " << debugConsideredNodesCount << " nodes (max possible: " << (mapWidth * mapHeight) << ")";
  
  // Did we find a path to the goal or only to some other tile that is close to the goal?
  QPoint targetTile;
  if (reachedGoalTile.x() < 0) {
    // No path to the goal was found. Go to the reachable node that is closest to the goal.
    if (smallestReachedHeuristicTile.x() >= 0) {
      LOG(1) << "Pathfinding: Goal not reached; going as close as possible";
      targetTile = smallestReachedHeuristicTile;
    } else {
      LOG(1) << "Pathfinding: Goal not reached and there is no better tile than the initial one. Stopping.";
      unit->StopMovement();
    }
  } else {
    LOG(1) << "Pathfinding: Goal reached";
    targetTile = reachedGoalTile;
  }
  
  // Reconstruct the path, tracking back from "targetTile" using "cameFrom".
  // We leave out the start tile since the unit is already within that tile.
  std::vector<QPointF> reversePath;
  QPoint currentTile = targetTile;
  while (currentTile != start) {
    reversePath.push_back(QPointF(currentTile.x() + 0.5f, currentTile.y() + 0.5f));
    
    if (kOutputDebugImage) {
      debugImage.setPixel(currentTile.x(), currentTile.y(), qRgb(0, 255, 0));
    }
    
    int cameFromDirection = cameFrom[currentTile.x() + mapWidth * currentTile.y()];
    if (cameFromDirection >= 11 || numNeighborsToCheckArray[cameFromDirection] == 0 || cameFromDirection == 5) {
      LOG(ERROR) << "Erroneous value in cameFrom[] while reconstructing path: " << cameFromDirection;
      break;
    }
    
    int cameFromX = (cameFromDirection % 4) - 1;
    currentTile.setX(currentTile.x() + cameFromX);
    
    int cameFromY = (cameFromDirection / 4) - 1;
    currentTile.setY(currentTile.y() + cameFromY);
  }
  if (kOutputDebugImage) {
    debugImage.setPixel(start.x(), start.y(), qRgb(0, 255, 0));
    
    LOG(WARNING) << "Writing pathfinding debug image to: " << kDebugImagePath;
    debugImage.save(kDebugImagePath);
  }
  
  // Replace the last point with the exact goal location (if we can reach the goal)
  // TODO: If we can't reach the goal, maybe append a point here that makes the unit walk into the obstacle?
  if (reachedGoalTile.x() >= 0) {
    if (reversePath.empty()) {
      reversePath.push_back(unit->GetMoveToTargetMapCoord());
    } else if (goalRect.width() == 1 && goalRect.height() == 1) {
      reversePath[0] = unit->GetMoveToTargetMapCoord();
    }
  }
  
  LOG(1) << "Pathfinding: Non-smoothed path length is " << reversePath.size();
  
  // Smooth the planned path by attempting to drop corners.
  float unitRadius = GetUnitRadius(unit->GetUnitType());
  for (usize i = 1; i < reversePath.size(); ++ i) {
    const QPointF& p0 = (i == reversePath.size() - 1) ? unit->GetMapCoord() : reversePath[i + 1];
    const QPointF& p1 = reversePath[i - 1];
    
    if (IsPathFree(unitRadius, p0, p1, goalRect)) {
      reversePath.erase(reversePath.begin() + i);
      -- i;
    }
  }
  
  LOG(1) << "Pathfinding: Smoothed path length is " << reversePath.size();
  LOG(1) << "Pathfinding: Took " << pathPlanningTimer.Stop(false) << " s" << (kOutputDebugImage ? " (not accurate since kOutputDebugImage is true!)" : "");
  
  // Assign the path to the unit.
  unit->SetPath(reversePath);
  
  // Start traversing the path:
  // Set the unit's movement direction to the first segment of the path.
  QPointF direction = unit->GetNextPathTarget() - unit->GetMapCoord();
  direction = direction / std::max(1e-4f, Length(direction));
  unit->SetMovementDirection(direction);
}

void Game::SimulateBuildingConstruction(float stepLengthInSeconds, ServerUnit* villager, u32 targetObjectId, ServerBuilding* targetBuilding, bool* unitMovementChanged, bool* stayInPlace) {
  // Special case for the start: If the foundation has 0 percent build progress,
  // we must first verify that the foundation space is free. If yes:
  // * Add the foundation's occupancy to the map.
  // * Tell all clients that observe the foundation about it (except the client which
  //   is constructing it, which already knows it).
  // If the space is occupied, notify the constructing player that construction has halted.
  // TODO: In the latter case, if only allied units obstruct the foundation, we should first
  //       try to make these units move off of the foudation. Only if this fails then the construction should halt.
  bool canConstruct = true;
  if (targetBuilding->GetBuildPercentage() == 0) {
    if (IsFoundationFree(targetBuilding, map.get())) {
      // Add the foundation's occupancy to the map.
      // TODO: The foundation's occupancy may differ from the final building's occupancy, e.g., for town centers. Handle this case properly.
      map->AddBuildingOccupancy(targetBuilding);
      
      // Tell all clients that observe the foundation about it (except the client which
      // is constructing it, which already knows it).
      QByteArray addObjectMsg = CreateAddObjectMessage(targetObjectId, targetBuilding);
      for (auto& player : *playersInGame) {
        if (player->index != targetBuilding->GetPlayerIndex()) {
          accumulatedMessages[player->index] += addObjectMsg;
        }
      }
    } else {
      // Construction blocked.
      // TODO: Rather than stopping, try to move to the nearest point next to the building (if necessary),
      //       and try to command all allied units on the foundation to do the same.
      villager->StopMovement();
      *unitMovementChanged = true;
      
      canConstruct = false;
    }
  }
  
  // Add progress to the construction.
  // TODO: In the original game, two villagers building does not result in twice the speed. Account for this.
  if (canConstruct) {
    double constructionTime = GetBuildingConstructionTime(targetBuilding->GetBuildingType());
    double constructionStepAmount = stepLengthInSeconds / constructionTime;
    
    double newPercentage = std::min<double>(100, targetBuilding->GetBuildPercentage() + 100 * constructionStepAmount);
    if (newPercentage == 100 && targetBuilding->GetBuildPercentage() != 100) {
      // Building completed.
      if (targetBuilding->GetPlayerIndex() != kGaiaPlayerIndex) {
        playersInGame->at(targetBuilding->GetPlayerIndex())->availablePopulationSpace += GetBuildingProvidedPopulationSpace(targetBuilding->GetBuildingType());
      }
    }
    targetBuilding->SetBuildPercentage(newPercentage);
    
    // Tell all clients that see the building about the new build percentage.
    // TODO: Group those updates together for each frame (together with the build speed handling in case multiple villagers are building at the same time)
    QByteArray msg = CreateBuildPercentageUpdateMessage(targetObjectId, targetBuilding->GetBuildPercentage());
    for (auto& player : *playersInGame) {
      accumulatedMessages[player->index] += msg;
    }
    
    u32 maxHP = GetBuildingMaxHP(targetBuilding->GetBuildingType());
    double addedHP = constructionStepAmount * maxHP;
    targetBuilding->SetHP(std::min<float>(targetBuilding->GetHPInternalFloat() + addedHP, maxHP));
    
    // TODO: Would it make sense to batch these together in case there are multiple updates to an object's HP in the same time step?
    QByteArray msg2 = CreateHPUpdateMessage(targetObjectId, targetBuilding->GetHP());
    for (auto& player : *playersInGame) {
      accumulatedMessages[player->index] += msg2;
    }
    
    if (villager->GetCurrentAction() != UnitAction::Task) {
      *unitMovementChanged = true;
      villager->SetCurrentAction(UnitAction::Task);
    }
  } else {
    if (villager->GetCurrentAction() != UnitAction::Idle) {
      *unitMovementChanged = true;
      villager->SetCurrentAction(UnitAction::Idle);
    }
  }
  
  *stayInPlace = true;
}

void Game::SimulateResourceGathering(float stepLengthInSeconds, u32 villagerId, ServerUnit* villager, ServerBuilding* targetBuilding, bool* unitMovementChanged, bool* stayInPlace) {
  // Determine the resource type to gather.
  ResourceType gatheredType;
  if (IsTree(targetBuilding->GetBuildingType())) {
    gatheredType = ResourceType::Wood;
  } else if (targetBuilding->GetBuildingType() == BuildingType::ForageBush) {
    gatheredType = ResourceType::Food;
  } else if (targetBuilding->GetBuildingType() == BuildingType::GoldMine) {
    gatheredType = ResourceType::Gold;
  } else if (targetBuilding->GetBuildingType() == BuildingType::StoneMine) {
    gatheredType = ResourceType::Stone;
  } else {
    LOG(ERROR) << "Server: Failed to determine the resource type to gather.";
    return;
  }
  
  // If the villager carried any other resource type than the type gathered, drop it.
  bool resourcesDropped = villager->GetCarriedResourceType() != gatheredType;
  if (resourcesDropped) {
    villager->SetCarriedResourceAmount(0);
    villager->SetCarriedResourceType(gatheredType);
  }
  
  // Determine the number of resource units collected.
  // TODO: The gather rate should vary per resource type and depend on the player's civilization and technologies
  constexpr double gatherRate = 2.0;
  double resourcesGathered = gatherRate * stepLengthInSeconds;
  
  constexpr int carryCapacity = 10;  // TODO: Should depend on technologies etc.
  
  int previousIntegerAmount = villager->GetCarriedResourceAmount();
  villager->SetCarriedResourceAmount(
      std::min<float>(carryCapacity, villager->GetCarriedResourceAmountInternalFloat() + resourcesGathered));
  int currentIntegerAmount = villager->GetCarriedResourceAmount();
  
  if (resourcesDropped || currentIntegerAmount != previousIntegerAmount) {
    // Notify the client that owns the villager about its new carry amount
    accumulatedMessages[villager->GetPlayerIndex()] += CreateSetCarriedResourcesMessage(villagerId, gatheredType, currentIntegerAmount);
  }
  
  // Make the villager target a resource drop-off point if its carrying capacity is reached.
  if (villager->GetCarriedResourceAmount() == carryCapacity) {
    // TODO: Speed up this search?
    float bestSquaredDistance = std::numeric_limits<float>::infinity();
    u32 bestDropOffPointId;
    ServerBuilding* bestDropOffPoint = nullptr;
    
    for (auto& item : map->GetObjects()) {
      if (item.second->GetPlayerIndex() == villager->GetPlayerIndex() &&
          item.second->isBuilding()) {
        ServerBuilding* candidateDropOffPoint = AsBuilding(item.second);
        if (IsDropOffPointForResource(candidateDropOffPoint->GetBuildingType(), villager->GetCarriedResourceType())) {
          // TODO: Improve the distance computation. Ideally we would use the distance that the
          //       villager has to walk to the edge of the building, not the straight-line distance to its center.
          QSize candidateSize = GetBuildingSize(candidateDropOffPoint->GetBuildingType());
          QPointF candidateCenter = candidateDropOffPoint->GetBaseTile() + 0.5f * QPointF(candidateSize.width(), candidateSize.height());
          
          float squaredDistance = SquaredDistance(candidateCenter, villager->GetMapCoord());
          
          if (squaredDistance < bestSquaredDistance) {
            bestSquaredDistance = squaredDistance;
            bestDropOffPoint = candidateDropOffPoint;
            bestDropOffPointId = item.first;
          }
        }
      }
    }
    
    if (bestDropOffPoint) {
      villager->SetTarget(bestDropOffPointId, bestDropOffPoint, /*isManualTargeting*/ false);
    } else {
      // TODO: Should we explicitly stop the gathering action here?
    }
  } else {
    if (villager->GetCurrentAction() != UnitAction::Task) {
      *unitMovementChanged = true;
      villager->SetCurrentAction(UnitAction::Task);
    }
    *stayInPlace = true;
  }
}

void Game::SimulateResourceDropOff(u32 villagerId, ServerUnit* villager, bool* unitMovementChanged) {
  ResourceAmount amount(0, 0, 0, 0);
  amount.resources[static_cast<int>(villager->GetCarriedResourceType())] = villager->GetCarriedResourceAmount();
  (*playersInGame)[villager->GetPlayerIndex()]->resources.Add(amount);
  
  villager->SetCarriedResourceAmount(0);
  accumulatedMessages[villager->GetPlayerIndex()] += CreateSetCarriedResourcesMessage(villagerId, villager->GetCarriedResourceType(), 0);
  
  // If the villager was originally tasked onto a resource, make it return to this resource.
  if (villager->GetManuallyTargetedObjectId() != villager->GetTargetObjectId()) {
    auto it = map->GetObjects().find(villager->GetManuallyTargetedObjectId());
    if (it != map->GetObjects().end()) {
      villager->SetTarget(villager->GetManuallyTargetedObjectId(), it->second, /*isManualTargeting*/ false);
    } else {
      // The manually targeted object does not exist anymore, stop.
      // TODO: This happens when a resource is depleted. In this case, make the villager move on to a nearby resource of the same type.
      villager->StopMovement();
      *unitMovementChanged = true;
    }
  } else {
    villager->StopMovement();
    *unitMovementChanged = true;
  }
}

void Game::SimulateGameStepForBuilding(u32 buildingId, ServerBuilding* building, float stepLengthInSeconds) {
  // If the building's production queue is non-empty, add progress on the item that is currently being produced / researched.
  UnitType unitInProduction;
  if (building->IsUnitQueued(&unitInProduction)) {
    auto& player = *playersInGame->at(building->GetPlayerIndex());
    
    bool canProduce = true;
    float previousPercentage = building->GetProductionPercentage();
    if (previousPercentage == 0) {
      // Only start producing the unit if population space is available.
      canProduce = player.populationIncludingInProduction < player.availablePopulationSpace;
      if (!canProduce) {
        player.isHoused = true;
      }
    }
    
    if (canProduce) {
      float productionTime = GetUnitProductionTime(unitInProduction);
      float timeStepPercentage = 100 * stepLengthInSeconds / productionTime;
      float newPercentage = building->GetProductionPercentage() + timeStepPercentage;
      
      bool completed = false;
      if (newPercentage >= 100) {
        // Remove the population count for the unit in production.
        player.populationIncludingInProduction -= 1;
        
        // Special case for UnitType::MaleVillager: Randomly decide whether to produce a male or female.
        if (unitInProduction == UnitType::MaleVillager) {
          unitInProduction = (rand() % 2 == 0) ? UnitType::MaleVillager : UnitType::FemaleVillager;
        }
        
        // Create the unit.
        ProduceUnit(building, unitInProduction);
        building->RemoveCurrentItemFromQueue();
        
        newPercentage = 0;
        completed = true;
        accumulatedMessages[building->GetPlayerIndex()] += CreateRemoveFromProductionQueueMessage(buildingId, 0);
      }
      
      building->SetProductionPercentage(newPercentage);
      if (!completed && previousPercentage == 0) {
        // If the production just starts, notify the client about it.
        accumulatedMessages[building->GetPlayerIndex()] += CreateUpdateProductionMessage(buildingId, building->GetProductionPercentage(), 100.f / productionTime);
        
        // Add the population count for the unit in production.
        player.populationIncludingInProduction += 1;
      }
    }
  }
}

bool Game::SimulateMeleeAttack(u32 /*unitId*/, ServerUnit* unit, u32 targetId, ServerObject* target, double gameStepServerTime, float stepLengthInSeconds, bool* unitMovementChanged, bool* stayInPlace) {
  if (unit->GetCurrentAction() != UnitAction::Attack) {
    *unitMovementChanged = true;
    unit->SetCurrentAction(UnitAction::Attack);
    unit->SetCurrentActionStartTime(gameStepServerTime);
  }
  *stayInPlace = true;
  
  int numAttackFrames = GetUnitAttackFrames(unit->GetUnitType());
  constexpr int kFramesPerSecond = 30;
  double fullAttackTime = numAttackFrames / (1.f * kFramesPerSecond);
  double attackDamageTime = 0.5 * fullAttackTime;  // TODO: Does this differ among units? Is this available in some data file?
  
  double timeSinceActionStart = gameStepServerTime - unit->GetCurrentActionStartTime();
  
  if (timeSinceActionStart >= attackDamageTime &&
      timeSinceActionStart - stepLengthInSeconds < attackDamageTime &&
      target != nullptr) {
    // Compute the attack damage.
    int meleeArmor;
    if (target->isUnit()) {
      meleeArmor = GetUnitMeleeArmor(AsUnit(target)->GetUnitType());
    } else {
      CHECK(target->isBuilding());
      meleeArmor = GetBuildingMeleeArmor(AsBuilding(target)->GetBuildingType());
    }
    int meleeDamage = std::max(0, static_cast<int>(GetUnitMeleeAttack(unit->GetUnitType())) - meleeArmor);
    
    // TODO: Pierce damage
    // TODO: Damage bonuses
    // TODO: Elevation multiplier 5/4 or 3/4
    
    int totalDamage = std::max(1, meleeDamage);
    
    // Do the attack damage.
    float hp = target->GetHPInternalFloat() - totalDamage;
    if (hp > 0.5f) {
      target->SetHP(hp);
      
      // Notify all clients that see the target about its HP change
      // TODO: Would it make sense to batch these together in case there are multiple updates to an object's HP in the same time step?
      QByteArray msg = CreateHPUpdateMessage(targetId, target->GetHP());
      for (auto& player : *playersInGame) {
        accumulatedMessages[player->index] += msg;
      }
    } else {
      // Remove the target.
      DeleteObject(targetId, false);
    }
  }
  
  if (timeSinceActionStart >= fullAttackTime) {
    // The attack animation finished.
    unit->SetCurrentActionStartTime(gameStepServerTime);
    return false;
  }
  
  return true;
}

void Game::ProduceUnit(ServerBuilding* building, UnitType unitInProduction) {
  // Create the unit object.
  u32 newUnitId;
  ServerUnit* newUnit = map->AddUnit(building->GetPlayerIndex(), unitInProduction, QPointF(-999, -999), &newUnitId);
  
  // Look for a free space to place the unit next to the building.
  float unitRadius = GetUnitRadius(unitInProduction);
  
  QSize buildingSize = GetBuildingSize(building->GetBuildingType());
  QPointF buildingBaseCoord(building->GetBaseTile() + QPointF(buildingSize.width() + unitRadius, -unitRadius));
  float extendedWidth = buildingSize.width() + 2 * unitRadius;
  float extendedHeight = buildingSize.height() + 2 * unitRadius;
  
  bool foundFreeSpace = false;
  QPointF freeSpace(-999, -999);
  
  // Try to place the unit on the bottom-left or bottom-right side of the building.
  // This equals +x or -y in map coordinates.
  float offset = 0;
  while (offset <= std::max(extendedWidth, extendedHeight)) {
    // Test bottom-right side
    if (offset < extendedHeight) {
      QPointF testPoint = buildingBaseCoord + QPointF(0, offset);
      if (!map->DoesUnitCollide(newUnit, testPoint)) {
        foundFreeSpace = true;
        freeSpace = testPoint;
        break;
      }
    }
    
    // Test bottom-left side
    if (offset < extendedWidth) {
      QPointF testPoint = buildingBaseCoord + QPointF(-offset, 0);
      if (!map->DoesUnitCollide(newUnit, testPoint)) {
        foundFreeSpace = true;
        freeSpace = testPoint;
        break;
      }
    }
    
    // Incease the offset. Make sure that we reach the end in a reasonable number
    // of steps, even in case the unit radius is very small (or even zero).
    offset += std::min(0.02f * extendedWidth, 2 * unitRadius);
  }
  
  // Try to place the unit on the top-left or top-right side of the building.
  // This equals -x or +y in map coordinates.
  if (!foundFreeSpace) {
    offset = 2 * unitRadius;
    while (offset <= std::max(extendedWidth, extendedHeight)) {
      // Test top-left side
      if (offset < extendedHeight) {
        QPointF testPoint = buildingBaseCoord + QPointF(-extendedWidth, offset);
        if (!map->DoesUnitCollide(newUnit, testPoint)) {
          foundFreeSpace = true;
          freeSpace = testPoint;
          break;
        }
      }
      
      // Test top-right side
      if (offset < extendedWidth) {
        QPointF testPoint = buildingBaseCoord + QPointF(-offset, extendedHeight);
        if (!map->DoesUnitCollide(newUnit, testPoint)) {
          foundFreeSpace = true;
          freeSpace = testPoint;
          break;
        }
      }
      
      // Incease the offset. Make sure that we reach the end in a reasonable number
      // of steps, even in case the unit radius is very small (or even zero).
      offset += std::min(0.02f * extendedWidth, 2 * unitRadius);
    }
  }
  
  if (foundFreeSpace) {
    newUnit->SetMapCoord(freeSpace);
  } else {
    // TODO: Garrison the unit in the building
  }
  
  // Send messages to clients that see the new unit
  QByteArray addObjectMsg = CreateAddObjectMessage(newUnitId, newUnit);
  for (auto& player : *playersInGame) {
    accumulatedMessages[player->index] += addObjectMsg;
  }
  
  // Handle population counting
  playersInGame->at(newUnit->GetPlayerIndex())->populationIncludingInProduction += 1;
}

void Game::SetUnitTargets(const std::vector<u32>& unitIds, int playerIndex, u32 targetId, ServerObject* targetObject) {
  for (u32 id : unitIds) {
    auto it = map->GetObjects().find(id);
    if (it == map->GetObjects().end() ||
        !it->second->isUnit() ||
        it->second->GetPlayerIndex() != playerIndex) {
      LOG(WARNING) << "Got SetTarget message for invalid unit ID: " << id;
      continue;
    }
    
    ServerUnit* unit = AsUnit(it->second);
    UnitType oldUnitType = unit->GetUnitType();
    
    unit->SetTarget(targetId, targetObject, /*isManualTargeting*/ true);
    
    if (oldUnitType != unit->GetUnitType()) {
      // Notify all clients that see the unit about its change of type.
      QByteArray msg = CreateChangeUnitTypeMessage(id, unit->GetUnitType());
      for (auto& player : *playersInGame) {
        accumulatedMessages[player->index] += msg;
      }
    }
  }
}

void Game::DeleteObject(u32 objectId, bool deletedManually) {
  // TODO: Convert the object into some other form to remember
  //       the potential destroy / death animation and rubble / decay sprite.
  //       We need to store this so we can tell other clients about its existence
  //       which currently do not see the object but may explore its location later.
  QByteArray msg = CreateObjectDeathMessage(objectId);
  for (auto& player : *playersInGame) {
    accumulatedMessages[player->index] += msg;
  }
  
  auto it = map->GetObjects().find(objectId);
  if (it == map->GetObjects().end()) {
    LOG(ERROR) << "Did not find the object to delete in the object map.";
    return;
  }
  ServerObject* object = it->second;
  
  objectDeleteList.push_back(objectId);
  
  // For buildings:
  // * Remove their map occupancy
  // * If not completed and deleted manually, refund some resources
  if (object->isBuilding()) {
    ServerBuilding* building = AsBuilding(object);
    if (building->GetBuildPercentage() > 0) {
      map->RemoveBuildingOccupancy(building);
    }
    if (deletedManually && building->GetBuildPercentage() < 100) {
      float remainingResourceAmount = 1 - building->GetBuildPercentage() / 100.f;
      
      ResourceAmount cost = GetBuildingCost(building->GetBuildingType());
      playersInGame->at(building->GetPlayerIndex())->resources.Add(remainingResourceAmount * cost);
    }
  }
  
  // Handle population counting.
  if (object->isBuilding()) {
    ServerBuilding* building = AsBuilding(object);
    if (building->GetPlayerIndex() != kGaiaPlayerIndex) {
      playersInGame->at(building->GetPlayerIndex())->availablePopulationSpace -= GetBuildingProvidedPopulationSpace(building->GetBuildingType());
    }
  } else if (object->isUnit()) {
    playersInGame->at(object->GetPlayerIndex())->populationIncludingInProduction -= 1;
  }
  
  // If all objects of a player are gone, the player gets defeated.
  if (object->GetPlayerIndex() != kGaiaPlayerIndex) {
    bool playerHasRemainingObject = false;
    for (const auto& item : map->GetObjects()) {
      if (item.second->GetPlayerIndex() == object->GetPlayerIndex() &&
          item.first != it->first) {
        playerHasRemainingObject = true;
        break;
      }
    }
    
    if (!playerHasRemainingObject) {
      RemovePlayer(object->GetPlayerIndex(), PlayerExitReason::Defeat);
    }
  }
}

void Game::RemovePlayer(int playerIndex, PlayerExitReason reason) {
  QString reasonString;
  if (reason == PlayerExitReason::Resign) {
    reasonString = "player resigned";
  } else if (reason == PlayerExitReason::Drop) {
    reasonString = "connection dropped";
  } else if (reason == PlayerExitReason::Defeat) {
    reasonString = "player got defeated";
  } else {
    LOG(ERROR) << "Unknown reason passed to RemovePlayer(): " << static_cast<int>(reason);
  }
  
  auto& player = playersInGame->at(playerIndex);
  LOG(WARNING) << "Removing player: " << player->name.toStdString() << " (index " << player->index << "). Reason: " << reasonString.toStdString();
  player->RemoveFromGame();
  
  // Notify the remaining players about the player's exit
  // TODO: For these messages and the one sent below, clients may think
  //       that they receive them late since they are not preceded by a game time message.
  //       Maybe create a special case for this message type on the client side?
  QByteArray leaveBroadcastMsg = CreatePlayerLeaveBroadcastMessage(player->index, reason);
  for (auto& otherPlayer : *playersInGame) {
    if (otherPlayer->isConnected) {
      otherPlayer->socket->write(leaveBroadcastMsg);
      otherPlayer->socket->flush();
    }
  }
  
  // In case of a defeat, notify the defeated player.
  if (reason == PlayerExitReason::Defeat) {
    player->socket->write(CreatePlayerLeaveBroadcastMessage(player->index, reason));
    player->socket->flush();
  }
  
  // TODO: If all other players finished loading and the last player who did not drops,
  //       then start the game for the remaining players (or cancel it altogether)
  
  int numConnectedPlayers = 0;
  for (auto& otherPlayer : *playersInGame) {
    if (otherPlayer->isConnected) {
      ++ numConnectedPlayers;
    }
  }
  if (numConnectedPlayers <= 1) {
    LOG(INFO) << "Server: All, or all but one player disconnected. Exiting.";
    shouldExit = true;
  }
}
