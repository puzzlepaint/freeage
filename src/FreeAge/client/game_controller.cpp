#include "FreeAge/client/game_controller.hpp"

#include <mango/core/endian.hpp>

#include "FreeAge/common/logging.hpp"
#include "FreeAge/client/building.hpp"
#include "FreeAge/client/unit.hpp"

GameController::GameController(const std::shared_ptr<Match>& match, const std::shared_ptr<ServerConnection>& connection)
    : connection(connection),
      match(match) {
  currentGameStepServerTime = -1;
  
  connect(connection.get(), &ServerConnection::NewMessage, this, &GameController::HandleMessage);
  connection->SetParseMessages(true);
}

GameController::~GameController() {
  connection->SetParseMessages(false);
}

void GameController::ParseMessagesUntil(double displayedServerTime) {
  while (!futureMessages.empty() && futureMessages.front().first <= displayedServerTime) {
    QByteArray& buffer = futureMessages.front().second;
    
    u32 parsedUntil = 0;
    while (true) {
      if (buffer.size() - parsedUntil < 3) {
        break;
      }
      
      char* data = buffer.data() + parsedUntil;
      u16 msgLength = mango::uload16(data + 1);
      
      if (buffer.size() - parsedUntil < msgLength) {
        break;
      }
      
      ServerToClientMessage msgType = static_cast<ServerToClientMessage>(data[0]);
      ParseMessage(data, msgType, msgLength);
      
      parsedUntil += msgLength;
    }
    
    if (parsedUntil == static_cast<u32>(buffer.size())) {
      futureMessages.erase(futureMessages.begin());
    } else {
      buffer.remove(0, parsedUntil);
      break;
    }
  }
}

void GameController::ProduceUnit(const std::vector<u32>& selection, UnitType type) {
  if (selection.empty()) {
    LOG(ERROR) << "Attempted to produce a unit without a selected building.";
    return;
  }
  
  // TODO: Implement proper multi-queue.
  // For now, we always queue the unit in the first selected building.
  connection->Write(CreateProduceUnitMessage(selection.front(), static_cast<u16>(type)));
}

void GameController::HandleMessage(const QByteArray& buffer, ServerToClientMessage msgType, u16 msgLength) {
  // GameStepTime messages are always parsed immediately such that we know the server time
  // for the following packets.
  if (msgType == ServerToClientMessage::GameStepTime) {
    HandleGameStepTimeMessage(buffer);
    
    // Keep statistics about when game steps arrive to help debug the server time handling.
    if (currentGameStepServerTime <= lastDisplayedServerTime) {
      double timeInPast = lastDisplayedServerTime - currentGameStepServerTime;
      averageGameStepTimeInPast = (numGameStepsArrivedTooLate * averageGameStepTimeInPast + timeInPast) / (numGameStepsArrivedTooLate + 1);
      ++ numGameStepsArrivedTooLate;
    } else {
      double timeInFuture = currentGameStepServerTime - lastDisplayedServerTime;
      averageGameStepTimeInFuture = (numGameStepsArrivedForFuture * averageGameStepTimeInFuture + timeInFuture) / (numGameStepsArrivedForFuture + 1);
      ++ numGameStepsArrivedForFuture;
    }
    
    ++ statisticsDebugOutputCounter;
    if (statisticsDebugOutputCounter % 5 * 30 == 0) {
      LOG(INFO) << "--- Networking debug statistics ---";
      
      double filteredPing;
      double filteredOffset;
      connection->EstimateCurrentPingAndOffset(&filteredPing, &filteredOffset);
      LOG(INFO) << "- cur ping: " << filteredPing;
      
      if (numGameStepsArrivedTooLate > 0) {
        LOG(WARNING) << "- # late steps: " << numGameStepsArrivedTooLate;
        LOG(WARNING) << "  avg time in past: " << averageGameStepTimeInPast << " s";
      } else {
        LOG(INFO) << "- # late steps: " << numGameStepsArrivedTooLate;
        LOG(INFO) << "  avg time in past: --";
      }
      LOG(INFO) << "- # good steps: " << numGameStepsArrivedForFuture;
      LOG(INFO) << "  avg time in future: " << averageGameStepTimeInFuture << " s";
      
      LOG(INFO) << "-----------------------------------";
    }
    
    return;
  }
  
  // We always peek into resource update messages immediately in order to know the most up-to-date resource amount of the player.
  // Note that these messages are parsed again later, then into &playerResources instead of &latestKnownPlayerResources.
  if (msgType == ServerToClientMessage::ResourcesUpdate) {
    HandleResourcesUpdateMessage(buffer, &latestKnownPlayerResources);
  }
  
  if (currentGameStepServerTime <= lastDisplayedServerTime) {
    // The packet is for a server time that should have already been considered in the
    // last rendering iteration. This is a bad case that leads to visual jumps.
    // Parse the packet immediately.
    // TODO: We could avoid jumps by correcting the game state smoothly instead of immediately.
    //       However, then it takes longer for it to get corrected.
    ParseMessage(buffer.data(), msgType, msgLength);
  } else {
    // The message is for a server time that should be considered in a future rendering
    // iteration. This is the desirable case.
    // Cache the packet for later processing.
    // TODO: Can we re-design the package handling to avoid copying the message buffer around here?
    if (futureMessages.empty() || futureMessages.back().first != currentGameStepServerTime) {
      futureMessages.push_back(std::make_pair(currentGameStepServerTime, buffer.mid(0, msgLength)));
    } else {
      futureMessages.back().second += buffer.mid(0, msgLength);
    }
  }
}

void GameController::ParseMessage(const char* data, ServerToClientMessage msgType, u16 msgLength) {
  // The messages are sorted by the frequency in which we expect to get them.
  switch (msgType) {
  case ServerToClientMessage::UnitMovement:
    HandleUnitMovementMessage(data);
    break;
  case ServerToClientMessage::AddObject:
    HandleAddObjectMessage(data);
    break;
  case ServerToClientMessage::BuildPercentageUpdate:
    HandleBuildPercentageUpdate(data);
    break;
  case ServerToClientMessage::MapUncover:
    HandleMapUncoverMessage(data);
    break;
  case ServerToClientMessage::ChangeUnitType:
    HandleChangeUnitTypeMessage(data);
    break;
  case ServerToClientMessage::ResourcesUpdate:
    HandleResourcesUpdateMessage(data, &playerResources);
    break;
  case ServerToClientMessage::ChatBroadcast:
    // TODO
    break;
  case ServerToClientMessage::LoadingProgressBroadcast:
    HandleLoadingProgressBroadcast(data, msgLength);
    break;
  case ServerToClientMessage::GameBegin:
    HandleGameBeginMessage(data);
    break;
  default:
    LOG(WARNING) << "GameController received a message that it cannot handle: " << static_cast<int>(msgType);
    break;
  }
}

void GameController::HandleLoadingProgressBroadcast(const char* data, u16 /*msgLength*/) {
  int playerIndex = std::min<int>(data[3], match->GetPlayers().size() - 1);
  int percentage = std::min<int>(100, data[4]);
  match->SetPlayerLoadingPercentage(playerIndex, percentage);
}

void GameController::HandleGameBeginMessage(const char* data) {
  memcpy(&gameStartServerTimeSeconds, data + 3, 8);
  
  QPointF initialViewCenterMapCoord(
      *reinterpret_cast<const float*>(data + 11),
      *reinterpret_cast<const float*>(data + 15));
  
  u32 initialWood = mango::uload32(data + 19);
  u32 initialFood = mango::uload32(data + 23);
  u32 initialGold = mango::uload32(data + 27);
  u32 initialStone = mango::uload32(data + 31);
  playerResources = ResourceAmount(initialWood, initialFood, initialGold, initialStone);
  latestKnownPlayerResources = playerResources;
  
  u16 mapWidth = mango::uload16(data + 35);
  u16 mapHeight = mango::uload16(data + 37);
  map.reset(new Map(mapWidth, mapHeight));
  renderWindow->SetMap(map);
  renderWindow->SetScroll(initialViewCenterMapCoord);
}

void GameController::HandleMapUncoverMessage(const char* data) {
  for (int y = 0; y <= map->GetHeight(); ++ y) {
    for (int x = 0; x <= map->GetWidth(); ++ x) {
      map->elevationAt(x, y) = data[3 + x + y * (map->GetWidth() + 1)];
    }
  }
  
  map->SetNeedsRenderResourcesUpdate(true);
}

void GameController::HandleAddObjectMessage(const char* data) {
  ObjectType objectType = static_cast<ObjectType>(data[3]);
  u32 objectId = mango::uload32(data + 4);
  u8 playerIndex = data[8];
  
  if (objectType == ObjectType::Building) {
    BuildingType buildingType = static_cast<BuildingType>(mango::uload16(data + 9));
    QPoint baseTile(mango::uload16(data + 11),
                    mango::uload16(data + 13));
    float buildPercentage = *reinterpret_cast<const float*>(data + 15);
    
    map->AddObject(objectId, new ClientBuilding(playerIndex, buildingType, baseTile.x(), baseTile.y(), buildPercentage, currentGameStepServerTime));
  } else {
    UnitType unitType = static_cast<UnitType>(mango::uload16(data + 9));
    QPointF mapCoord(*reinterpret_cast<const float*>(data + 11),
                     *reinterpret_cast<const float*>(data + 15));
    
    map->AddObject(objectId, new ClientUnit(playerIndex, unitType, mapCoord, currentGameStepServerTime));
  }
}

void GameController::HandleUnitMovementMessage(const char* data) {
  u32 unitId = mango::uload32(data + 3);
  QPointF startPoint(
      *reinterpret_cast<const float*>(data + 7),
      *reinterpret_cast<const float*>(data + 11));
  QPointF speed(
      *reinterpret_cast<const float*>(data + 15),
      *reinterpret_cast<const float*>(data + 19));
  UnitAction action = static_cast<UnitAction>(data[23]);
  
  auto it = map->GetObjects().find(unitId);
  if (it == map->GetObjects().end()) {
    LOG(ERROR) << "Received a UnitMovement message for an object ID that is not in the map.";
    return;
  }
  if (!it->second->isUnit()) {
    LOG(ERROR) << "Received a UnitMovement message for an object ID that is a different type than a unit.";
    return;
  }
  
  ClientUnit* unit = static_cast<ClientUnit*>(it->second);
  unit->SetMovementSegment(currentGameStepServerTime, startPoint, speed, action);
}

void GameController::HandleGameStepTimeMessage(const char* data) {
  memcpy(&currentGameStepServerTime, data + 3, 8);
}

void GameController::HandleResourcesUpdateMessage(const char* data, ResourceAmount* resources) {
  u32 wood = mango::uload32(data + 3);
  u32 food = mango::uload32(data + 7);
  u32 gold = mango::uload32(data + 11);
  u32 stone = mango::uload32(data + 15);
  
  *resources = ResourceAmount(wood, food, gold, stone);
}

void GameController::HandleBuildPercentageUpdate(const char* data) {
  u32 buildingId = mango::uload32(data + 3);
  auto it = map->GetObjects().find(buildingId);
  if (it == map->GetObjects().end()) {
    LOG(ERROR) << "Received a BuildPercentageUpdate message for an object ID that is not in the map.";
    return;
  }
  if (!it->second->isBuilding()) {
    LOG(ERROR) << "Received a BuildPercentageUpdate message for an object ID that is a different type than a building.";
    return;
  }
  
  float percentage;
  memcpy(&percentage, data + 3 + 4, 4);
  
  ClientBuilding* building = static_cast<ClientBuilding*>(it->second);
  building->SetBuildPercentage(percentage);
}

void GameController::HandleChangeUnitTypeMessage(const char* data) {
  u32 buildingId = mango::uload32(data + 3);
  auto it = map->GetObjects().find(buildingId);
  if (it == map->GetObjects().end()) {
    LOG(ERROR) << "Received a ChangeUnitType message for an object ID that is not in the map.";
    return;
  }
  if (!it->second->isUnit()) {
    LOG(ERROR) << "Received a ChangeUnitType message for an object ID that is a different type than a unit.";
    return;
  }
  
  UnitType newType = static_cast<UnitType>(mango::uload16(data + 7));
  
  ClientUnit* unit = static_cast<ClientUnit*>(it->second);
  unit->SetType(newType);
}
