#include "FreeAge/client/game_controller.hpp"

#include <mango/core/endian.hpp>

#include "FreeAge/common/logging.hpp"
#include "FreeAge/client/building.hpp"
#include "FreeAge/client/unit.hpp"

GameController::GameController(const std::shared_ptr<Match>& match, const std::shared_ptr<ServerConnection>& connection)
    : connection(connection),
      match(match) {
  currentGameStepServerTime = -1;
  
  connect(connection.get(), &ServerConnection::NewMessage, this, &GameController::ParseMessage);
  connection->SetParseMessages(true);
}

GameController::~GameController() {
  connection->SetParseMessages(false);
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

const ResourceAmount& GameController::GetCurrentResourceAmount(double serverTime) {
  if (playerResources.empty()) {
    LOG(FATAL) << "GetCurrentResourceAmount() called while the playerResources vector is empty.";
  }
  
  for (int i = static_cast<int>(playerResources.size()) - 1; i >= 0; -- i) {
    if (serverTime >= playerResources[i].first) {
      if (i > 0) {
        playerResources.erase(playerResources.begin(), playerResources.begin() + i);
      }
      
      return playerResources.front().second;
    }
  }
  
  LOG(ERROR) << "GetCurrentResourceAmount() did not find a resource amount which is valid for the given server time.";
  return playerResources.front().second;
}

void GameController::ParseMessage(const QByteArray& buffer, ServerToClientMessage msgType, u16 msgLength) {
  // The messages are sorted by the frequency in which we expect to get them.
  switch (msgType) {
  case ServerToClientMessage::UnitMovement:
    HandleUnitMovementMessage(buffer);
    break;
  case ServerToClientMessage::AddObject:
    HandleAddObjectMessage(buffer);
    break;
  case ServerToClientMessage::BuildPercentageUpdate:
    HandleBuildPercentageUpdate(buffer);
    break;
  case ServerToClientMessage::MapUncover:
    HandleMapUncoverMessage(buffer);
    break;
  case ServerToClientMessage::ChangeUnitType:
    HandleChangeUnitTypeMessage(buffer);
    break;
  case ServerToClientMessage::GameStepTime:
    HandleGameStepTimeMessage(buffer);
    break;
  case ServerToClientMessage::ResourcesUpdate:
    HandleResourcesUpdateMessage(buffer);
    break;
  case ServerToClientMessage::ChatBroadcast:
    // TODO
    break;
  case ServerToClientMessage::LoadingProgressBroadcast:
    HandleLoadingProgressBroadcast(buffer, msgLength);
    break;
  case ServerToClientMessage::GameBegin:
    HandleGameBeginMessage(buffer);
    break;
  default:
    LOG(WARNING) << "GameController received a message that it cannot handle: " << static_cast<int>(msgType);
    break;
  }
}

void GameController::HandleLoadingProgressBroadcast(const QByteArray& buffer, u16 /*msgLength*/) {
  int playerIndex = std::min<int>(buffer.data()[3], match->GetPlayers().size() - 1);
  int percentage = std::min<int>(100, buffer.data()[4]);
  match->SetPlayerLoadingPercentage(playerIndex, percentage);
}

void GameController::HandleGameBeginMessage(const QByteArray& buffer) {
  const char* data = buffer.data();
  
  memcpy(&gameStartServerTimeSeconds, data + 3, 8);
  
  QPointF initialViewCenterMapCoord(
      *reinterpret_cast<const float*>(data + 11),
      *reinterpret_cast<const float*>(data + 15));
  
  u32 initialWood = mango::uload32(data + 19);
  u32 initialFood = mango::uload32(data + 23);
  u32 initialGold = mango::uload32(data + 27);
  u32 initialStone = mango::uload32(data + 31);
  playerResources.push_back(std::make_pair(-1, ResourceAmount(initialWood, initialFood, initialGold, initialStone)));
  
  u16 mapWidth = mango::uload16(data + 35);
  u16 mapHeight = mango::uload16(data + 37);
  map.reset(new Map(mapWidth, mapHeight));
  renderWindow->SetMap(map);
  renderWindow->SetScroll(initialViewCenterMapCoord);
}

void GameController::HandleMapUncoverMessage(const QByteArray& buffer) {
  const char* data = buffer.data();
  for (int y = 0; y <= map->GetHeight(); ++ y) {
    for (int x = 0; x <= map->GetWidth(); ++ x) {
      map->elevationAt(x, y) = data[3 + x + y * (map->GetWidth() + 1)];
    }
  }
  
  map->SetNeedsRenderResourcesUpdate(true);
}

void GameController::HandleAddObjectMessage(const QByteArray& buffer) {
  const char* data = buffer.data();
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

void GameController::HandleUnitMovementMessage(const QByteArray& buffer) {
  const char* data = buffer.data();
  
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
  unit->AddMovementSegment(currentGameStepServerTime, startPoint, speed, action);
}

void GameController::HandleGameStepTimeMessage(const QByteArray& buffer) {
  memcpy(&currentGameStepServerTime, buffer.data() + 3, 8);
}

void GameController::HandleResourcesUpdateMessage(const QByteArray& buffer) {
  const char* data = buffer.data();
  
  u32 wood = mango::uload32(data + 3);
  u32 food = mango::uload32(data + 7);
  u32 gold = mango::uload32(data + 11);
  u32 stone = mango::uload32(data + 15);
  
  playerResources.push_back(std::make_pair(currentGameStepServerTime, ResourceAmount(wood, food, gold, stone)));
}

void GameController::HandleBuildPercentageUpdate(const QByteArray& buffer) {
  const char* data = buffer.data();
  
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
  building->AddBuildPercentage(currentGameStepServerTime, percentage);
}

void GameController::HandleChangeUnitTypeMessage(const QByteArray& buffer) {
  const char* data = buffer.data();
  
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
