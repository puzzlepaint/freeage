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
    return;
  }
  const char* data = msg.data();
  
  QPointF targetMapCoord(
      *reinterpret_cast<const float*>(data + 3),
      *reinterpret_cast<const float*>(data + 7));
  
  usize selectedUnitIdsSize = mango::uload16(data + 11);
  if (len != 13 + 4 * selectedUnitIdsSize) {
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
  QByteArray msg(object->isBuilding() ? 15 : 19, Qt::Initialization::Uninitialized);
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
        if (unit->GetUnitType() == UnitType::FemaleVillager ||
            unit->GetUnitType() == UnitType::MaleVillager) {
          initialViewCenter = unit->GetMapCoord();
        }
      }
    }
    
    // Send message.
    QByteArray gameBeginMsg = CreateGameBeginMessage(
        gameBeginServerTime,
        initialViewCenter,
        /*initialFood*/ 200,  // TODO: do not hardcode here
        /*initialWood*/ 200,  // TODO: do not hardcode here
        /*initialGold*/ 100,  // TODO: do not hardcode here
        /*initialStone*/ 200,  // TODO: do not hardcode here
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
        
        // Test whether the current goal was reached.
        QPointF toGoal = unit->GetPathTarget() - unit->GetMapCoord();
        float squaredDistanceToGoal = toGoal.x() * toGoal.x() + toGoal.y() * toGoal.y();
        float directionDotToGoal =
            unit->GetMovementDirection().x() * toGoal.x() +
            unit->GetMovementDirection().y() * toGoal.y();
        if (squaredDistanceToGoal <= moveDistance * moveDistance || directionDotToGoal <= 0) {
          // TODO: Only place the unit directly at the goal if this does not cause a collision.
          unit->SetMapCoord(unit->GetPathTarget());
          unit->StopMovement();
          unitMovementChanged = true;
        } else {
          // Move the unit if the path is free.
          // TODO: Only perform this mapCoord update if the target position is not occupied by a building or other unit.
          unit->SetMapCoord(unit->GetMapCoord() + moveDistance * unit->GetMovementDirection());
          
          // Check whether the unit moves over to the next segment in its planned path.
          // TODO
        }
      }
      
      if (unitMovementChanged) {
        // Notify all clients that see the unit about its new movement / about it having stopped moving.
        for (usize playerIndex = 0; playerIndex < playersInGame->size(); ++ playerIndex) {
          // TODO: Only do this if the player sees the unit.
          accumulatedMessages[playerIndex] +=
              CreateUnitMovementMessage(
                  objectId,
                  unit->GetMapCoord(),
                  unit->GetMoveSpeed() * unit->GetMovementDirection());
        }
      }
    } else if (object->isBuilding()) {
      // TODO: Anything to do here?
    }
  }
  
  // Send out the accumulated messages for each player. The advantage of the accumulation is that
  // the TCP header only has to be sent once for each player, rather than for each message.
  //
  // All messages of this game step are prefixed by a message indicating the current server time,
  // which avoids sending it with each single message.
  for (usize playerIndex = 0; playerIndex < playersInGame->size(); ++ playerIndex) {
    if (!accumulatedMessages[playerIndex].isEmpty()) {
      (*playersInGame)[playerIndex]->socket->write(
          CreateGameStepTimeMessage(gameStepServerTime) +
          accumulatedMessages[playerIndex]);
    }
  }
}
