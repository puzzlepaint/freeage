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
  
  this->playersInGame = playersInGame;
  bool firstLoopIteration = true;
  
  while (true) {
    // Communicate with player connections.
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
    
    // Handle Qt events.
    qApp->processEvents(QEventLoop::AllEvents);
    
    // Simulate the game.
    // TODO: Do we need to consider the possibility of falling behind more and more with the simulation?
    //       I guess that the game would break anyway then.
    if (map) {
      double serverTime = GetCurrentServerTime();
      while (serverTime >= lastSimulationTime + kSimulationTimeInterval) {
        /// Simulate one game step.
        SimulateGameStep(lastSimulationTime + kSimulationTimeInterval, kSimulationTimeInterval);
        
        lastSimulationTime += kSimulationTimeInterval;
      }
      
      // Sleep until the next simulation iteration is due (in case it is due in the future).
      double nextIterationTime = lastSimulationTime + kSimulationTimeInterval;
      serverTime = GetCurrentServerTime();
      double sleepTimeSeconds = nextIterationTime - serverTime;
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
  player->socket->write(CreatePingResponseMessage(number, serverTimeSeconds));
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
  
  // NOTE: This addObject message will be interpreted on the client together with the last
  //       sent game step server time. This should be fine, but should be kept in mind in case
  //       one wants to make changes here.
  QByteArray addObjectMsg = CreateAddObjectMessage(newBuildingId, newBuildingFoundation);
  player->socket->write(addObjectMsg);
  
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
  QByteArray msg(object->isBuilding() ? 19 : 19, Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ServerToClientMessage::AddObject);
  mango::ustore16(data + 1, msg.size());
  
  // Fill buffer
  data[3] = static_cast<u8>(object->GetObjectType());  // TODO: Currently unneeded since this could be derived from the message length
  mango::ustore32(data + 4, objectId);  // TODO: Maybe save bytes here as long as e.g. less than 16 bits are non-zero?
  data[8] = object->GetPlayerIndex();
  
  if (object->isBuilding()) {
    ServerBuilding* building = static_cast<ServerBuilding*>(object);
    mango::ustore16(data + 9, static_cast<u16>(building->GetBuildingType()));  // TODO: Would 8 bits be sufficient here?
    mango::ustore16(data + 11, building->GetBaseTile().x());
    mango::ustore16(data + 13, building->GetBaseTile().y());
    *reinterpret_cast<float*>(data + 15) = building->GetBuildPercentage();
  } else {
    ServerUnit* unit = static_cast<ServerUnit*>(object);
    mango::ustore16(data + 9, static_cast<u16>(unit->GetUnitType()));  // TODO: Would 8 bits be sufficient here?
    *reinterpret_cast<float*>(data + 11) = unit->GetMapCoord().x();
    *reinterpret_cast<float*>(data + 15) = unit->GetMapCoord().y();
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
  }
  
  LOG(INFO) << "Server: Game start prepared";
}

void Game::SimulateGameStep(double gameStepServerTime, float stepLengthInSeconds) {
  std::vector<QByteArray> accumulatedMessages(playersInGame->size());
  for (usize playerIndex = 0; playerIndex < playersInGame->size(); ++ playerIndex) {
    accumulatedMessages[playerIndex].reserve(1024);
  }
  
  // Iterate over all game objects to update their state.
  auto end = map->GetObjects().end();
  for (auto it = map->GetObjects().begin(); it != end; ++ it) {
    const u32& objectId = it->first;
    ServerObject* object = it->second;
    
    if (object->isUnit()) {
      ServerUnit* unit = static_cast<ServerUnit*>(object);
      SimulateGameStepForUnit(objectId, unit, stepLengthInSeconds, &accumulatedMessages);
    } else if (object->isBuilding()) {
      ServerBuilding* building = static_cast<ServerBuilding*>(object);
      SimulateGameStepForBuilding(objectId, building, stepLengthInSeconds, &accumulatedMessages);
    }
  }
  
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
  float offsetLength = sqrt(offset.x() * offset.x() + offset.y() * offset.y());
  float unitRadius = GetUnitRadius(unit->GetUnitType());
  return offsetLength + errorMargin < unitRadius;
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

void Game::SimulateGameStepForUnit(u32 unitId, ServerUnit* unit, float stepLengthInSeconds, std::vector<QByteArray>* accumulatedMessages) {
  bool unitMovementChanged = false;
  
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
            // TODO: This duplicates code in SetTarget() in server/unit.cpp. Make a function GetTargetInteractionType().
            // If the unit is a villager and the target building is a foundation of the same player,
            // let the villager construct the building.
            if (IsVillager(unit->GetUnitType()) &&
                targetBuilding->GetPlayerIndex() == unit->GetPlayerIndex() &&
                targetBuilding->GetBuildPercentage() < 100) {
              // A villager constructs a building.
              
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
                      (*accumulatedMessages)[player->index] += addObjectMsg;
                    }
                  }
                } else {
                  // Construction blocked.
                  // TODO: Rather than stopping, try to move to the nearest point next to the building (if necessary),
                  //       and try to command all allied units on the foundation to do the same.
                  unit->StopMovement();
                  
                  canConstruct = false;
                }
              }
              
              // Add progress to the construction.
              // TODO: In the original game, two villagers building does not result in twice the speed. Account for this.
              if (canConstruct) {
                double constructionTime = GetBuildingConstructionTime(targetBuilding->GetBuildingType());
                double constructionStepInPercent = 100 * stepLengthInSeconds / constructionTime;
                double newPercentage = std::min<double>(100, targetBuilding->GetBuildPercentage() + constructionStepInPercent);
                targetBuilding->SetBuildPercentage(newPercentage);
                
                // Tell all clients that see the building about the new build percentage.
                // TODO: Group those updates together for each frame (together with the build speed handling in case multiple villagers are building at the same time)
                QByteArray msg = CreateBuildPercentageUpdateMessage(targetObjectId, targetBuilding->GetBuildPercentage());
                for (auto& player : *playersInGame) {
                  (*accumulatedMessages)[player->index] += msg;
                }
                
                if (unit->GetCurrentAction() != UnitAction::Building) {
                  unitMovementChanged = true;
                  unit->SetCurrentAction(UnitAction::Building);
                }
              } else {
                if (unit->GetCurrentAction() != UnitAction::Idle) {
                  unitMovementChanged = true;
                  unit->SetCurrentAction(UnitAction::Idle);
                }
              }
              
              stayInPlace = true;
            }
          }
        } else if (targetObject->isUnit()) {
          // ServerUnit* targetUnit = static_cast<ServerUnit*>(targetUnit);
          // TODO: unit interactions with units
        }
      }
    }
    
    if (!stayInPlace) {
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
      
      (*accumulatedMessages)[playerIndex] +=
          CreateUnitMovementMessage(
              unitId,
              unit->GetMapCoord(),
              unit->GetMoveSpeed() * unit->GetMovementDirection(),
              unit->GetCurrentAction());
    }
  }
}

void Game::SimulateGameStepForBuilding(u32 /*buildingId*/, ServerBuilding* building, float stepLengthInSeconds, std::vector<QByteArray>* accumulatedMessages) {
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
      ProduceUnit(building, unitInProduction, accumulatedMessages);
      building->RemoveCurrentItemFromQueue();
      
      newPercentage = 0;
    }
    building->SetProductionPercentage(newPercentage);
  }
}

void Game::ProduceUnit(ServerBuilding* building, UnitType unitInProduction, std::vector<QByteArray>* accumulatedMessages) {
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
    (*accumulatedMessages)[player->index] += addObjectMsg;
  }
}

void Game::SetUnitTargets(const std::vector<u32>& unitIds, int playerIndex, u32 targetId, ServerObject* targetObject) {
  for (u32 id : unitIds) {
    auto it = map->GetObjects().find(id);
    if (it == map->GetObjects().end() ||
        !it->second->isUnit() ||
        it->second->GetPlayerIndex() != playerIndex) {
      continue;
    }
    
    ServerUnit* unit = static_cast<ServerUnit*>(it->second);
    UnitType oldUnitType = unit->GetUnitType();
    
    unit->SetTarget(targetId, targetObject);
    
    if (oldUnitType != unit->GetUnitType()) {
      // Notify all clients that see the unit about its change of type.
      // TODO: Batch this message into the message batches for the game time steps instead of sending it directly.
      //       Right now, the message uses the time step preceding this message handling ...
      QByteArray msg = CreateChangeUnitTypeMessage(id, unit->GetUnitType());
      for (auto& player : *playersInGame) {
        player->socket->write(msg);
      }
    }
  }
}
