#include "FreeAge/client/game_controller.hpp"

#include <mango/core/endian.hpp>

#include "FreeAge/common/logging.hpp"
#include "FreeAge/client/building.hpp"
#include "FreeAge/client/unit.hpp"

GameController::GameController(const std::shared_ptr<Match>& match, const std::shared_ptr<ServerConnection>& connection)
    : connection(connection),
      match(match) {
  connect(connection.get(), &ServerConnection::NewMessage, this, &GameController::ParseMessage);
  connection->SetParseMessages(true);
}

GameController::~GameController() {
  connection->SetParseMessages(false);
}

void GameController::ParseMessage(const QByteArray& buffer, ServerToClientMessage msgType, u16 msgLength) {
  switch (msgType) {
  case ServerToClientMessage::ChatBroadcast:
    // TODO
    break;
  case ServerToClientMessage::LoadingProgressBroadcast:
    HandleLoadingProgressBroadcast(buffer, msgLength);
    break;
  case ServerToClientMessage::GameBegin:
    HandleGameBeginMessage(buffer);
    break;
  case ServerToClientMessage::MapUncover:
    HandleMapUncoverMessage(buffer);
    break;
  case ServerToClientMessage::AddObject:
    HandleAddObjectMessage(buffer);
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
  
  u32 initialFood = mango::uload32(data + 19);
  u32 initialWood = mango::uload32(data + 23);
  u32 initialGold = mango::uload32(data + 27);
  u32 initialStone = mango::uload32(data + 31);
  // TODO: Store initial resource counts.
  (void) initialFood;
  (void) initialWood;
  (void) initialGold;
  (void) initialStone;
  
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
    
    map->AddObject(objectId, new ClientBuilding(playerIndex, buildingType, baseTile.x(), baseTile.y()));
  } else {
    UnitType unitType = static_cast<UnitType>(mango::uload16(data + 9));
    QPointF mapCoord(*reinterpret_cast<const float*>(data + 11),
                     *reinterpret_cast<const float*>(data + 15));
    
    map->AddObject(objectId, new ClientUnit(playerIndex, unitType, mapCoord));
  }
}
