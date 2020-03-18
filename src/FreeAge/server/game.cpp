#include "FreeAge/server/game.hpp"

#include <QApplication>
#include <QThread>

#include "FreeAge/common/logging.hpp"
#include "FreeAge/common/messages.hpp"
#include "FreeAge/server/building.hpp"
#include "FreeAge/server/unit.hpp"

// TODO (puzzlepaint): For some reason, this include needed to be after the Qt includes on my laptop
// in order for CIDE not to show some errors. Compiling always worked. Check the reason for the errors.
#include <mango/core/endian.hpp>

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
  
  while (true) {
    // Read data from player connections and handle broken connections.
    for (auto it = playersInGame->begin(); it != playersInGame->end(); ) {
      PlayerInGame& player = **it;
      
      // Read new data from the connection.
      int prevSize = player.unparsedBuffer.size();
      player.unparsedBuffer += player.socket->readAll();
      
      bool removePlayer = false;
      if (player.unparsedBuffer.size() > prevSize ||
          (firstLoopIteration && !player.unparsedBuffer.isEmpty())) {
        ParseMessagesResult parseResult = TryParseClientMessages(&player, *playersInGame);
        removePlayer = parseResult == ParseMessagesResult::PlayerLeftOrShouldBeDisconnected;
      }
      
      // Remove connections which got ParseMessagesResult::PlayerLeftOrShouldBeDisconnected,
      // which did not send pings in time, or if the connection was lost.
      constexpr int kNoPingTimeout = 5000;
      bool socketDisconnected = player.socket->state() != QAbstractSocket::ConnectedState;
      bool pingTimeout = MillisecondsDuration(Clock::now() - player.lastPingTime).count() > kNoPingTimeout;
      if (removePlayer || socketDisconnected || pingTimeout) {
        LOG(WARNING) << "Removing player: " << player.name.toStdString() << " (index " << player.index << "). Reason: "
                     << (removePlayer ? "handled message" : (socketDisconnected ? "socket disconnected" : "ping timeout"));
        
        delete player.socket;
        it = playersInGame->erase(it);
        if (playersInGame->empty()) {
          // All players left the game.
          return;
        }
        
        // TODO: Notify the remaining players about the player drop / leave
        continue;
      }
      
      ++ it;
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
    LOG(ERROR) << "Server: Received LoadingProgress message which is too short";
    return;
  }
  
  // Broadcast the player's loading progress to all other players.
  u8 percentage = msg.data()[3];
  
  if (percentage > 100) {
    // The client finished loading.
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
  } else {
    // Broadcast the loading progress to all other clients.
    QByteArray broadcastMsg = CreateLoadingProgressBroadcastMessage(player->index, percentage);
    for (const auto& otherPlayer : players) {
      if (otherPlayer.get() != player) {
        otherPlayer->socket->write(broadcastMsg);
      }
    }
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
  LOG(INFO) << "Server: Received Chat";
  
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
    
    ServerUnit* unit = static_cast<ServerUnit*>(it->second);
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
  ServerBuilding* productionBuilding = static_cast<ServerBuilding*>(buildingObject);
  
  if (productionBuilding->GetBuildPercentage() != 100) {
    LOG(ERROR) << "Received a ProduceUnit message for a building that is not fully constructed";
    return;
  }
  if (!productionBuilding->CanProduce(unitType, player)) {
    LOG(ERROR) << "Received a ProduceUnit message for a unit that either cannot be produced from the given building or for which the player does not have the right civilization/technologies";
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
}

void Game::HandlePlaceBuildingFoundationMessage(const QByteArray& msg, PlayerInGame* player) {
  const char* data = msg.data();
  
  BuildingType type = static_cast<BuildingType>(mango::uload16(data + 3));
  QSize foundationSize = GetBuildingSize(type);
  QPoint baseTile(
      mango::uload16(data + 5),
      mango::uload16(data + 7));
  
  u16 villagerIdsSize = mango::uload16(data + 9);
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
      if (map->occupiedAt(x, y)) {
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
    default:
      LOG(ERROR) << "Server: Received a message in the game phase that cannot be parsed in this phase: " << static_cast<int>(msgType);
      break;
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
  data[8] = (object->GetPlayerIndex() == -1) ? 127 : object->GetPlayerIndex();
  mango::ustore32(data + 9, object->GetHP());
  
  if (object->isBuilding()) {
    ServerBuilding* building = static_cast<ServerBuilding*>(object);
    mango::ustore16(data + 13, static_cast<u16>(building->GetBuildingType()));  // TODO: Would 8 bits be sufficient here?
    mango::ustore16(data + 15, building->GetBaseTile().x());
    mango::ustore16(data + 17, building->GetBaseTile().y());
    *reinterpret_cast<float*>(data + 19) = building->GetBuildPercentage();
  } else {
    ServerUnit* unit = static_cast<ServerUnit*>(object);
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
  constexpr double kGameBeginOffsetSeconds = 1.0;  // give some time for the initial messages to arrive and be processed
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
        ServerBuilding* building = static_cast<ServerBuilding*>(item.second);
        if (building->GetBuildingType() == BuildingType::TownCenter) {
          initialViewCenter =
              QPointF(building->GetBaseTile().x() + 0.5f * GetBuildingSize(building->GetBuildingType()).width(),
                      building->GetBaseTile().y() + 0.5f * GetBuildingSize(building->GetBuildingType()).height());
          break;
        }
      } else {  // if (item.second->isUnit()) {
        ServerUnit* unit = static_cast<ServerUnit*>(item.second);
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
  
  // Send creation messages for the initial map objects
  const std::unordered_map<u32, ServerObject*>& objects = map->GetObjects();
  for (auto& player : *playersInGame) {
    for (const auto& item : objects) {
      ServerObject* object = item.second;
      QByteArray addObjectMsg = CreateAddObjectMessage(item.first, object);
      player->socket->write(addObjectMsg);
    }
    
    player->socket->flush();
  }
  
  LOG(INFO) << "Server: Game start prepared";
}

void Game::SimulateGameStep(double gameStepServerTime, float stepLengthInSeconds) {
  // Iterate over all game objects to update their state.
  auto end = map->GetObjects().end();
  for (auto it = map->GetObjects().begin(); it != end; ++ it) {
    const u32& objectId = it->first;
    ServerObject* object = it->second;
    
    if (object->isUnit()) {
      ServerUnit* unit = static_cast<ServerUnit*>(object);
      SimulateGameStepForUnit(objectId, unit, gameStepServerTime, stepLengthInSeconds);
    } else if (object->isBuilding()) {
      ServerBuilding* building = static_cast<ServerBuilding*>(object);
      SimulateGameStepForBuilding(objectId, building, stepLengthInSeconds);
    }
  }
  
  // Handle delayed object deletion.
  for (u32 id : objectDeleteList) {
    map->GetObjects().erase(id);
  }
  objectDeleteList.clear();
  
  // Send out the accumulated messages for each player. The advantage of the accumulation is that
  // the TCP header only has to be sent once for each player, rather than for each message.
  //
  // All messages of this game step are prefixed by a message indicating the current server time,
  // which avoids sending it with each single message.
  for (usize playerIndex = 0; playerIndex < playersInGame->size(); ++ playerIndex) {
    auto& player = (*playersInGame)[playerIndex];
    
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
  QPointF offset = unitMapCoord - closestPointInBuilding;
  float offsetLengthSquared = offset.x() * offset.x() + offset.y() * offset.y();
  float unitRadius = GetUnitRadius(unit->GetUnitType());
  float threshold = unitRadius - errorMargin;
  return offsetLengthSquared < threshold * threshold;
}

static bool DoUnitsTouch(ServerUnit* unit, const QPointF& unitMapCoord, ServerUnit* otherUnit, float errorMargin) {
  float unitRadius = GetUnitRadius(unit->GetUnitType());
  float otherUnitRadius = GetUnitRadius(otherUnit->GetUnitType());
  
  QPointF offset = unitMapCoord - otherUnit->GetMapCoord();
  float offsetLengthSquared = offset.x() * offset.x() + offset.y() * offset.y();
  float threshold = unitRadius + otherUnitRadius - errorMargin;
  return offsetLengthSquared < threshold * threshold;
}

static bool IsFoundationFree(ServerBuilding* foundation, ServerMap* map) {
  // Check whether map tiles are occupied
  const QPoint& baseTile = foundation->GetBaseTile();
  QSize foundationSize = GetBuildingSize(foundation->GetBuildingType());
  for (int y = baseTile.y(); y < baseTile.y() + foundationSize.height(); ++ y) {
    for (int x = baseTile.x(); x < baseTile.x() + foundationSize.width(); ++ x) {
      if (map->occupiedAt(x, y)) {
        return false;
      }
    }
  }
  
  // Check whether units are on top of the foundation
  for (const auto& item : map->GetObjects()) {
    if (item.second->isUnit()) {
      ServerUnit* unit = static_cast<ServerUnit*>(item.second);
      if (DoesUnitTouchBuildingArea(unit, unit->GetMapCoord(), foundation, 0.01f)) {
        return false;
      }
    }
  }
  
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
    // TODO: Actually plan a path. For now, we just set the move-to target as a single path target point.
    unit->SetPath(unit->GetMoveToTargetMapCoord());
    
    // Start traversing the path:
    // Set the unit's movement direction to the first segment of the path.
    QPointF direction = unit->GetMoveToTargetMapCoord() - unit->GetMapCoord();
    direction = direction / std::max(1e-4f, sqrtf(direction.x() * direction.x() + direction.y() * direction.y()));
    unit->SetMovementDirection(direction);
    unitMovementChanged = true;
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
        ServerObject* targetObject = static_cast<ServerObject*>(targetIt->second);
        if (targetObject->isBuilding()) {
          ServerBuilding* targetBuilding = static_cast<ServerBuilding*>(targetObject);
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
          ServerUnit* targetUnit = static_cast<ServerUnit*>(targetObject);
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
      QPointF toGoal = unit->GetPathTarget() - unit->GetMapCoord();
      float squaredDistanceToGoal = toGoal.x() * toGoal.x() + toGoal.y() * toGoal.y();
      float directionDotToGoal =
          unit->GetMovementDirection().x() * toGoal.x() +
          unit->GetMovementDirection().y() * toGoal.y();
      
      if (squaredDistanceToGoal <= moveDistance * moveDistance || directionDotToGoal <= 0) {
        // The goal was reached.
        if (!map->DoesUnitCollide(unit, unit->GetPathTarget())) {
          unit->SetMapCoord(unit->GetPathTarget());
        }
        unit->StopMovement();
        unitMovementChanged = true;
      } else {
        // Move the unit if the path is free.
        if (map->DoesUnitCollide(unit, newMapCoord)) {
          unit->StopMovement();
          unitMovementChanged = true;
        } else {
          unit->SetMapCoord(newMapCoord);
          
          if (unit->GetCurrentAction() != UnitAction::Moving) {
            unitMovementChanged = true;
            unit->SetCurrentAction(UnitAction::Moving);
          }
        }
        
        // Check whether the unit moves over to the next segment in its planned path.
        // TODO
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
    float bestDistance = std::numeric_limits<float>::infinity();
    u32 bestDropOffPointId;
    ServerBuilding* bestDropOffPoint = nullptr;
    
    for (auto& item : map->GetObjects()) {
      if (item.second->GetPlayerIndex() == villager->GetPlayerIndex() &&
          item.second->isBuilding()) {
        ServerBuilding* candidateDropOffPoint = static_cast<ServerBuilding*>(item.second);
        if (IsDropOffPointForResource(candidateDropOffPoint->GetBuildingType(), villager->GetCarriedResourceType())) {
          // TODO: Improve the distance computation. Ideally we would use the distance that the
          //       villager has to walk to the edge of the building, not the straight-line distance to its center.
          QSize candidateSize = GetBuildingSize(candidateDropOffPoint->GetBuildingType());
          QPointF candidateCenter = candidateDropOffPoint->GetBaseTile() + 0.5f * QPointF(candidateSize.width(), candidateSize.height());
          QPointF offset = candidateCenter - villager->GetMapCoord();
          float distance = offset.x() * offset.x() + offset.y() * offset.y();
          
          if (distance < bestDistance) {
            bestDistance = distance;
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

void Game::SimulateGameStepForBuilding(u32 /*buildingId*/, ServerBuilding* building, float stepLengthInSeconds) {
  // If the building's production queue is non-empty, add progress on the item that is currently being produced / researched.
  UnitType unitInProduction;
  if (building->IsUnitQueued(&unitInProduction)) {
    float productionTime = GetUnitProductionTime(unitInProduction);
    float timeStepPercentage = 100 * stepLengthInSeconds / productionTime;
    float newPercentage = building->GetProductionPercentage() + timeStepPercentage;
    if (newPercentage >= 100) {
      // Special case for UnitType::MaleVillager: Randomly decide whether to produce a male or female.
      if (unitInProduction == UnitType::MaleVillager) {
        unitInProduction = (rand() % 2 == 0) ? UnitType::MaleVillager : UnitType::FemaleVillager;
      }
      
      // Create the unit.
      ProduceUnit(building, unitInProduction);
      building->RemoveCurrentItemFromQueue();
      
      newPercentage = 0;
    }
    building->SetProductionPercentage(newPercentage);
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
      meleeArmor = GetUnitMeleeArmor(static_cast<ServerUnit*>(target)->GetUnitType());
    } else {
      CHECK(target->isBuilding());
      meleeArmor = GetBuildingMeleeArmor(static_cast<ServerBuilding*>(target)->GetBuildingType());
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
      // TODO: Convert the object into some other form to remember
      //       the potential destroy / death animation and rubble / decay sprite.
      //       We need to store this so we can tell other clients about its existence
      //       which currently do not see the object but may explore its location later.
      QByteArray msg = CreateObjectDeathMessage(targetId);
      for (auto& player : *playersInGame) {
        accumulatedMessages[player->index] += msg;
      }
      
      objectDeleteList.push_back(targetId);
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
    
    ServerUnit* unit = static_cast<ServerUnit*>(it->second);
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
