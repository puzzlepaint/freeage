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
    
    qApp->processEvents(QEventLoop::AllEvents);
    QThread::msleep(1);  // TODO: Change this once we also compute the game loop
    
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
void Game::HandleChat(const QByteArray& msg, PlayerInGame* player, int len, const std::vector<std::shared_ptr<PlayerInGame>>& players) {
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
  double serverTimeSeconds = SecondsDuration(Clock::now() - settings->serverStartTime).count();
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
        serverTimeSeconds + kGameBeginOffsetSeconds,
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
