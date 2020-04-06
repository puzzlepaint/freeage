#include "FreeAge/client/game_controller.hpp"

#include <iomanip>

#include <mango/core/endian.hpp>

#include "FreeAge/common/logging.hpp"
#include "FreeAge/client/building.hpp"
#include "FreeAge/client/decal.hpp"
#include "FreeAge/client/unit.hpp"

GameController::GameController(const std::shared_ptr<Match>& match, const std::shared_ptr<ServerConnection>& connection, bool debugNetworking)
    : connection(connection),
      match(match),
      debugNetworking(debugNetworking) {
  if (debugNetworking) {
    networkingDebugFile.open("network_debug_log_messages.txt", std::ios::out);
    networkingDebugFile << std::setprecision(14);
  }
}

void GameController::ParseMessagesUntil(double displayedServerTime) {
  // - Messages that relate to a time before lastDisplayedServerTime are received late.
  // - Messages that relate to a time between lastDisplayedServerTime and displayedServerTime are processed now.
  // - Messages that relate to a time after displayedServerTime will be processed later.
  
  connection->Lock();
  
  auto* messages = connection->GetReceivedMessages();
  u32 numParsedMessages = 0;
  
  for (const ReceivedMessage& msg : *messages) {
    if (msg.type == ServerToClientMessage::GameStepTime ||
        currentGameStepServerTime <= displayedServerTime) {
      ParseMessage(msg.data, msg.type);
      ++ numParsedMessages;
      
      if (debugNetworking) {
        // For every 10 time that we receive a new game step time,
        // save the game step (server) time and the client time at message receival of the last message before.
        // I.e., save some of the last received messages for some server times. This later enables
        // to test for different server time offset schemes whether we could process these messages in time (before
        // the displayed server time reaches their server time) or not.
        if (currentGameStepServerTime > lastMessageServerTime && lastMessageServerTime > 0) {
          ++ networkLogCounter;
          if (networkLogCounter % 10 == 0) {
            networkingDebugFile << "messageServerTime " << lastMessageServerTime << " clientTime " << lastMessageClientTime << std::endl;
          }
        }
        lastMessageServerTime = currentGameStepServerTime;
        lastMessageClientTime = connection->GetClientTimeNow();
      }
      
      // Keep statistics about whether messages arrive in time or late to help debug the server time handling.
      if (currentGameStepServerTime >= 0) {  // if the game has started
        if (currentGameStepServerTime <= lastDisplayedServerTime) {
          double timeInPast = lastDisplayedServerTime - currentGameStepServerTime;
          LOG(WARNING) << "Received message " << timeInPast << " seconds late. Message type: " << static_cast<int>(msg.type);
          
          averageMsgTimeInPast = (numMsgsArrivedTooLate * averageMsgTimeInPast + timeInPast) / (numMsgsArrivedTooLate + 1);
          ++ numMsgsArrivedTooLate;
        } else {
          double timeInFuture = currentGameStepServerTime - lastDisplayedServerTime;
          averageMsgTimeInFuture = (numMsgsArrivedForFuture * averageMsgTimeInFuture + timeInFuture) / (numMsgsArrivedForFuture + 1);
          ++ numMsgsArrivedForFuture;
        }
        
        ++ statisticsDebugOutputCounter;
        if (statisticsDebugOutputCounter % 20 == 0) {
          LOG(INFO) << "--- Networking debug statistics ---";
          
          double filteredPing;
          double filteredOffset;
          connection->EstimateCurrentPingAndOffset(&filteredPing, &filteredOffset);
          LOG(INFO) << "- cur ping: " << static_cast<int>(1000 * filteredPing + 0.5) << " ms";
          
          if (numMsgsArrivedTooLate > 0) {
            LOG(WARNING) << "- # late msgs: " << numMsgsArrivedTooLate;
            LOG(WARNING) << "  avg time in past: " << averageMsgTimeInPast << " s";
          } else {
            LOG(INFO) << "- # late msgs: " << numMsgsArrivedTooLate;
            LOG(INFO) << "  avg time in past: --";
          }
          LOG(INFO) << "- # good msgs: " << numMsgsArrivedForFuture;
          LOG(INFO) << "  avg time in future: " << averageMsgTimeInFuture << " s";
          
          LOG(INFO) << "-----------------------------------";
        }
      }
    } else {
      break;
    }
  }
  
  messages->erase(messages->begin(), messages->begin() + numParsedMessages);
  
  connection->Unlock();
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

void GameController::ParseMessage(const QByteArray& data, ServerToClientMessage msgType) {
  // The messages are sorted by the frequency in which we expect to get them.
  switch (msgType) {
  case ServerToClientMessage::SetCarriedResources:
    HandleSetCarriedResourcesMessage(data);
    break;
  case ServerToClientMessage::UnitMovement:
    HandleUnitMovementMessage(data);
    break;
  case ServerToClientMessage::HPUpdate:
    HandleHPUpdateMessage(data);
    break;
  case ServerToClientMessage::AddObject:
    HandleAddObjectMessage(data);
    break;
  case ServerToClientMessage::ObjectDeath:
    HandleObjectDeathMessage(data);
    break;
  case ServerToClientMessage::BuildPercentageUpdate:
    HandleBuildPercentageUpdate(data);
    break;
  case ServerToClientMessage::QueueUnit:
    HandleQueueUnitMessage(data);
    break;
  case ServerToClientMessage::MapUncover:
    HandleMapUncoverMessage(data);
    break;
  case ServerToClientMessage::ChangeUnitType:
    HandleChangeUnitTypeMessage(data);
    break;
  case ServerToClientMessage::GameStepTime:
    HandleGameStepTimeMessage(data);
    break;
  case ServerToClientMessage::ResourcesUpdate:
    HandleResourcesUpdateMessage(data, &playerResources);
    break;
  case ServerToClientMessage::UpdateProduction:
    HandleUpdateProductionMessage(data);
    break;
  case ServerToClientMessage::RemoveFromProductionQueue:
    HandleRemoveFromProductionQueueMessage(data);
    break;
  case ServerToClientMessage::SetHoused:
    HandleSetHousedMessage(data);
    break;
  case ServerToClientMessage::ChatBroadcast:
    // TODO
    break;
  case ServerToClientMessage::LoadingProgressBroadcast:
    HandleLoadingProgressBroadcast(data);
    break;
  case ServerToClientMessage::PlayerLeaveBroadcast:
    HandlePlayerLeaveBroadcast(data);
    break;
  case ServerToClientMessage::GameBegin:
    HandleGameBeginMessage(data);
    break;
  default:
    LOG(WARNING) << "GameController received a message that it cannot handle: " << static_cast<int>(msgType);
    break;
  }
}

void GameController::HandleLoadingProgressBroadcast(const QByteArray& data) {
  int playerIndex = std::min<int>(data[0], match->GetPlayers().size() - 1);
  int percentage = std::min<int>(100, data[1]);
  match->SetPlayerLoadingPercentage(playerIndex, percentage);
}

void GameController::HandleGameBeginMessage(const QByteArray& data) {
  const char* buffer = data.data();
  
  memcpy(&gameStartServerTimeSeconds, buffer + 0, 8);
  
  QPointF initialViewCenterMapCoord(
      *reinterpret_cast<const float*>(buffer + 8),
      *reinterpret_cast<const float*>(buffer + 12));
  
  u32 initialWood = mango::uload32(buffer + 16);
  u32 initialFood = mango::uload32(buffer + 20);
  u32 initialGold = mango::uload32(buffer + 24);
  u32 initialStone = mango::uload32(buffer + 28);
  playerResources = ResourceAmount(initialWood, initialFood, initialGold, initialStone);
  // latestKnownPlayerResources = playerResources;  // TODO: Remove?
  
  u16 mapWidth = mango::uload16(buffer + 32);
  u16 mapHeight = mango::uload16(buffer + 34);
  map.reset(new Map(mapWidth, mapHeight));
  renderWindow->SetMap(map);
  renderWindow->SetScroll(initialViewCenterMapCoord);
}

void GameController::HandleMapUncoverMessage(const QByteArray& data) {
  for (int y = 0; y <= map->GetHeight(); ++ y) {
    for (int x = 0; x <= map->GetWidth(); ++ x) {
      map->elevationAt(x, y) = data[x + y * (map->GetWidth() + 1)];
    }
  }
  
  map->SetNeedsRenderResourcesUpdate(true);
}

void GameController::HandleAddObjectMessage(const QByteArray& data) {
  const char* buffer = data.data();
  
  ObjectType objectType = static_cast<ObjectType>(buffer[0]);
  u32 objectId = mango::uload32(buffer + 1);
  int playerIndex = (buffer[5] == 127) ? -1 : buffer[5];
  u32 initialHP = mango::uload32(buffer + 6);
  
  if (objectType == ObjectType::Building) {
    BuildingType buildingType = static_cast<BuildingType>(mango::uload16(buffer + 10));
    QPoint baseTile(mango::uload16(buffer + 12),
                    mango::uload16(buffer + 14));
    float buildPercentage = *reinterpret_cast<const float*>(buffer + 16);
    
    map->AddObject(objectId, new ClientBuilding(playerIndex, buildingType, baseTile.x(), baseTile.y(), buildPercentage, initialHP));
    
    if (buildPercentage == 100) {
      availablePopulationSpace += GetBuildingProvidedPopulationSpace(buildingType);
    }
  } else {
    UnitType unitType = static_cast<UnitType>(mango::uload16(buffer + 10));
    QPointF mapCoord(*reinterpret_cast<const float*>(buffer + 12),
                     *reinterpret_cast<const float*>(buffer + 16));
    
    map->AddObject(objectId, new ClientUnit(playerIndex, unitType, mapCoord, initialHP));
    
    populationCount += 1;
  }
}

void GameController::HandleObjectDeathMessage(const QByteArray& data) {
  const char* buffer = data.data();
  
  u32 objectId = mango::uload32(buffer + 0);
  auto it = map->GetObjects().find(objectId);
  if (it == map->GetObjects().end()) {
    LOG(ERROR) << "Received an ObjectDeath message for an object ID that is not in the map.";
    return;
  }
  ClientObject* object = it->second;
  
  // Convert the object into a decal that:
  // - First plays the destruction / death animation (if any)
  // - Then displays a rubble pile / decay sprite (if any)
  //
  // In addition, handle population count / space changes.
  if (object->isBuilding()) {
    ClientBuilding* building = static_cast<ClientBuilding*>(object);
    if (building->GetBuildPercentage() == 100) {
      Decal* newDecal = new Decal(building, map.get(), currentGameStepServerTime);
      renderWindow->AddDecal(newDecal);
      
      // Subtract the population space that this building gave.
      availablePopulationSpace -= GetBuildingProvidedPopulationSpace(building->GetType());
    } else {
      // TODO: Destruction animations for foundations
    }
  } else if (object->isUnit()) {
    Decal* newDecal = new Decal(static_cast<ClientUnit*>(object), map.get(), currentGameStepServerTime);
    renderWindow->AddDecal(newDecal);
    
    populationCount -= 1;
  }
  
  delete object;
  map->GetObjects().erase(it);
}

void GameController::HandleUnitMovementMessage(const QByteArray& data) {
  const char* buffer = data.data();
  
  u32 unitId = mango::uload32(buffer + 0);
  QPointF startPoint(
      *reinterpret_cast<const float*>(buffer + 4),
      *reinterpret_cast<const float*>(buffer + 8));
  QPointF speed(
      *reinterpret_cast<const float*>(buffer + 12),
      *reinterpret_cast<const float*>(buffer + 16));
  UnitAction action = static_cast<UnitAction>(buffer[20]);
  
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

void GameController::HandleGameStepTimeMessage(const QByteArray& data) {
  memcpy(&currentGameStepServerTime, data.data() + 0, 8);
}

void GameController::HandleResourcesUpdateMessage(const QByteArray& data, ResourceAmount* resources) {
  const char* buffer = data.data();
  
  u32 wood = mango::uload32(buffer + 0);
  u32 food = mango::uload32(buffer + 4);
  u32 gold = mango::uload32(buffer + 8);
  u32 stone = mango::uload32(buffer + 12);
  
  *resources = ResourceAmount(wood, food, gold, stone);
}

void GameController::HandleBuildPercentageUpdate(const QByteArray& data) {
  const char* buffer = data.data();
  
  u32 buildingId = mango::uload32(buffer + 0);
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
  memcpy(&percentage, buffer + 4, 4);
  
  ClientBuilding* building = static_cast<ClientBuilding*>(it->second);
  if (building->GetBuildPercentage() != 100 && percentage == 100) {
    // The building has been completed.
    availablePopulationSpace += GetBuildingProvidedPopulationSpace(building->GetType());
  }
  building->SetBuildPercentage(percentage);
}

void GameController::HandleChangeUnitTypeMessage(const QByteArray& data) {
  const char* buffer = data.data();
  
  u32 buildingId = mango::uload32(buffer + 0);
  auto it = map->GetObjects().find(buildingId);
  if (it == map->GetObjects().end()) {
    LOG(ERROR) << "Received a ChangeUnitType message for an object ID that is not in the map.";
    return;
  }
  if (!it->second->isUnit()) {
    LOG(ERROR) << "Received a ChangeUnitType message for an object ID that is a different type than a unit.";
    return;
  }
  
  UnitType newType = static_cast<UnitType>(mango::uload16(buffer + 4));
  
  ClientUnit* unit = static_cast<ClientUnit*>(it->second);
  unit->SetType(newType);
}

void GameController::HandleSetCarriedResourcesMessage(const QByteArray& data) {
  const char* buffer = data.data();
  
  u32 unitId = mango::uload32(buffer + 0);
  auto it = map->GetObjects().find(unitId);
  if (it == map->GetObjects().end()) {
    LOG(ERROR) << "Received a SetCarriedResources message for an object ID that is not in the map.";
    return;
  }
  if (!it->second->isUnit()) {
    LOG(ERROR) << "Received a SetCarriedResources message for an object ID that is a different type than a unit.";
    return;
  }
  ClientUnit* villager = static_cast<ClientUnit*>(it->second);
  if (!IsVillager(villager->GetType())) {
    LOG(ERROR) << "Received a SetCarriedResources message for a unit that is not a villager.";
    return;
  }
  
  ResourceType type = static_cast<ResourceType>(buffer[4]);
  u8 amount = buffer[5];
  
  villager->SetCarriedResources(type, amount);
}

void GameController::HandleHPUpdateMessage(const QByteArray& data) {
  const char* buffer = data.data();
  
  u32 objectId = mango::uload32(buffer + 0);
  auto it = map->GetObjects().find(objectId);
  if (it == map->GetObjects().end()) {
    LOG(ERROR) << "Received a HPUpdate message for an object ID that is not in the map.";
    return;
  }
  ClientObject* object = it->second;
  
  u32 newHP = mango::uload32(buffer + 4);
  
  object->SetHP(newHP);
}

void GameController::HandlePlayerLeaveBroadcast(const QByteArray& data) {
  u8 playerIndex = data[0];
  PlayerExitReason reason = static_cast<PlayerExitReason>(data[1]);
  
  Match::PlayerState newState;
  if (reason == PlayerExitReason::Resign) {
    newState = Match::PlayerState::Resigned;
  } else if (reason == PlayerExitReason::Drop) {
    newState = Match::PlayerState::Dropped;
  } else if (reason == PlayerExitReason::Defeat) {
    newState = Match::PlayerState::Defeated;
  } else {
    LOG(ERROR) << "Invalid PlayerExitReason received with PlayerLeaveBroadcast message: " << static_cast<int>(reason);
    return;
  }
  
  match->SetPlayerState(playerIndex, newState);
  
  // If we are the last remaining player, we win.
  if (match->GetThisPlayer().state == Match::PlayerState::Playing) {
    bool haveOtherPlayingPlayer = false;
    for (usize playerIdx = 0; playerIdx < match->GetPlayers().size(); ++ playerIdx) {
      if (playerIdx == static_cast<usize>(match->GetPlayerIndex())) {
        continue;
      }
      
      const auto& player = match->GetPlayers()[playerIdx];
      if (player.state == Match::PlayerState::Playing) {
        haveOtherPlayingPlayer = true;
        break;
      }
    }
    
    if (!haveOtherPlayingPlayer) {
      match->SetPlayerState(match->GetPlayerIndex(), Match::PlayerState::Won);
    }
  }
}

void GameController::HandleQueueUnitMessage(const QByteArray& data) {
  const char* buffer = data.data();
  
  u32 buildingId = mango::uload32(buffer + 0);
  auto it = map->GetObjects().find(buildingId);
  if (it == map->GetObjects().end()) {
    LOG(ERROR) << "Received a QueueUnit message for an object ID that is not in the map.";
    return;
  }
  if (!it->second->isBuilding()) {
    LOG(ERROR) << "Received a QueueUnit message for an object ID that is a different type than a building.";
    return;
  }
  
  UnitType unitType = static_cast<UnitType>(mango::uload16(buffer + 4));
  
  ClientBuilding* building = static_cast<ClientBuilding*>(it->second);
  building->QueueUnit(unitType);
}

void GameController::HandleUpdateProductionMessage(const QByteArray& data) {
  const char* buffer = data.data();
  
  u32 buildingId = mango::uload32(buffer + 0);
  auto it = map->GetObjects().find(buildingId);
  if (it == map->GetObjects().end()) {
    LOG(ERROR) << "Received a UpdateProduction message for an object ID that is not in the map.";
    return;
  }
  if (!it->second->isBuilding()) {
    LOG(ERROR) << "Received a UpdateProduction message for an object ID that is a different type than a building.";
    return;
  }
  
  float percentage = *reinterpret_cast<const float*>(buffer + 4);
  float progressPerSecond = *reinterpret_cast<const float*>(buffer + 8);
  
  ClientBuilding* building = static_cast<ClientBuilding*>(it->second);
  building->SetProductionState(currentGameStepServerTime, percentage, progressPerSecond);
}

void GameController::HandleRemoveFromProductionQueueMessage(const QByteArray& data) {
  const char* buffer = data.data();
  
  u32 buildingId = mango::uload32(buffer + 0);
  auto it = map->GetObjects().find(buildingId);
  if (it == map->GetObjects().end()) {
    LOG(ERROR) << "Received a RemoveFromProductionQueue message for an object ID that is not in the map.";
    return;
  }
  if (!it->second->isBuilding()) {
    LOG(ERROR) << "Received a RemoveFromProductionQueue message for an object ID that is a different type than a building.";
    return;
  }
  
  ClientBuilding* building = static_cast<ClientBuilding*>(it->second);
  building->DequeueFirstUnit();
}

void GameController::HandleSetHousedMessage(const QByteArray& data) {
  isHoused = data.data()[0] != 0;
}
