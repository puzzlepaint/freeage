// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/client/game_controller.hpp"

#include <iomanip>

#include <mango/core/endian.hpp>

#include "FreeAge/common/game_data.hpp"
#include "FreeAge/common/logging.hpp"
#include "FreeAge/common/type_stats_data.hpp"
#include "FreeAge/client/building.hpp"
#include "FreeAge/client/decal.hpp"
#include "FreeAge/client/unit.hpp"

GameController::GameController(const std::shared_ptr<Match>& match, const std::shared_ptr<ServerConnection>& connection, bool debugNetworking)
    : connection(connection),
      match(match),
      players(),
      debugNetworking(debugNetworking) {
  if (debugNetworking) {
    networkingDebugFile.open("network_debug_log_messages.txt", std::ios::out);
    networkingDebugFile << std::setprecision(14);
  }
  // NOTE: The game data are loaded temporarily here. However the loading is instant and for
  //       now there is no need for something like a loading screen. Also it is unclear if
  //       the data will be needed in later stage, so they are just deleted after the creation of
  //       the players. (currently the Gaia plater is used to access the base stats)
  GameData gameData;
  LoadGameData(gameData);
  // Create the player list based on the match information
  players.reserve(match->GetPlayers().size() + 1);
  int playerIndex = 0;
  for (const Match::Player& matchPlayer : match->GetPlayers()) {
    players.emplace_back(playerIndex, matchPlayer.playerColorIndex, gameData, kDefaultCivilization);
    ++ playerIndex;
  }
  // Add the Gaia player to the last position of the vector
  players.emplace_back(kGaiaPlayerIndex, 0, gameData, Civilization::Gaia);
  // Store pointer to the current and Gaia player
  player = &players.at(match->GetPlayerIndex());
  gaiaPlayer = &players.back();
  // Apply the civ bonuses to each player
  for (auto& player : players) {
    if (player.civilization == Civilization::Gaia) {
      continue;
    }
    // Uses the gaia player as the base player for now
    player.ApplyTechnologyModifications(Technology::DarkAge, *gaiaPlayer);
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
        if (statisticsDebugOutputCounter % 100 == 0) {
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

void GameController::ProduceUnit(const std::vector<u32>& selection, UnitType type, int count) {
  if (selection.empty()) {
    LOG(ERROR) << "Attempted to produce a unit without a selected building.";
    return;
  }

  int affordable = player->resources.CanAffordTimes(player->GetUnitStats(type).cost);
  count = std::min<int>(count, affordable);

  if (count == 0) {
    return;
  }

  // TODO: Implement proper multi-queue.
  // For now, we always queue the unit in the first selected building.
  for (int i = 0; i < count; ++ i) {
    // TODO: group messages
    connection->Write(CreateProduceUnitMessage(selection.front(), static_cast<u16>(type)));
  }
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
  case ServerToClientMessage::UnitGarrison:
    HandleUnitGarrisonMessage(data);
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
    HandleResourcesUpdateMessage(data, &player->resources);
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
  if (data.size() < 2) {
    LOG(ERROR) << "Received a too short LoadingProgressBroadcast message";
    return;
  }
  int playerIndex = data[0];
  if (playerIndex < 0 || playerIndex >= static_cast<int>(match->GetPlayers().size())) {
    LOG(ERROR) << "Received a LoadingProgressBroadcast message containing an invalid player index";
    return;
  }
  int percentage = std::min<int>(100, data[1]);
  match->SetPlayerLoadingPercentage(playerIndex, percentage);
}

void GameController::HandleGameBeginMessage(const QByteArray& data) {
  if (data.size() < 36) {
    LOG(ERROR) << "Received a too short GameBegin message";
    return;
  }
  const char* buffer = data.data();
  
  memcpy(&gameStartServerTimeSeconds, buffer + 0, 8);
  
  u32 initialWood = mango::uload32(buffer + 16);
  u32 initialFood = mango::uload32(buffer + 20);
  u32 initialGold = mango::uload32(buffer + 24);
  u32 initialStone = mango::uload32(buffer + 28);
  player->resources = ResourceAmount(initialWood, initialFood, initialGold, initialStone);
  
  u16 mapWidth = mango::uload16(buffer + 32);
  u16 mapHeight = mango::uload16(buffer + 34);
  map.reset(new Map(mapWidth, mapHeight));
  renderWindow->SetMap(map);
  QPointF initialViewCenterMapCoord(
      std::max<float>(0, std::min<float>(map->GetWidth(), *reinterpret_cast<const float*>(buffer + 8))),
      std::max<float>(0, std::min<float>(map->GetHeight(), *reinterpret_cast<const float*>(buffer + 12))));
  renderWindow->SetScroll(initialViewCenterMapCoord);
}

void GameController::HandleMapUncoverMessage(const QByteArray& data) {
  if (data.size() < (map->GetWidth() + 1) * (map->GetHeight() + 1)) {
    LOG(ERROR) << "Received a too short MapUncover message";
    return;
  }
  
  for (int y = 0; y <= map->GetHeight(); ++ y) {
    for (int x = 0; x <= map->GetWidth(); ++ x) {
      int elevation = data[x + y * (map->GetWidth() + 1)];
      if (elevation < 0 || elevation > map->GetMaxElevation()) {
        LOG(WARNING) << "Received invalid map elevation: " << elevation << " (should be from 0 to " << map->GetMaxElevation() << ")";
      }
      map->elevationAt(x, y) = elevation;
    }
  }
  
  map->SetNeedsRenderResourcesUpdate(true);
}

void GameController::HandleAddObjectMessage(const QByteArray& data) {
  if (data.size() < 20) {
    LOG(ERROR) << "Received a too short AddObject message";
    return;
  }
  const char* buffer = data.data();
  
  ObjectType objectType = static_cast<ObjectType>(buffer[0]);
  u32 objectId = mango::uload32(buffer + 1);
  int playerIndex = *reinterpret_cast<const u8*>(buffer + 5);
  if (playerIndex != kGaiaPlayerIndex && playerIndex >= static_cast<int>(match->GetPlayers().size())) {
    LOG(ERROR) << "Received an AddObject message containing an invalid player index";
    return;
  }
  u32 initialHP = mango::uload32(buffer + 6);
  
  if (objectType == ObjectType::Building) {
    BuildingType buildingType = static_cast<BuildingType>(mango::uload16(buffer + 10));
    if (buildingType >= BuildingType::NumBuildings) {
      LOG(ERROR) << "Received an AddObject message containing an invalid BuildingType";
      return;
    }
    
    QPoint baseTile(mango::uload16(buffer + 12),
                    mango::uload16(buffer + 14));
    QSize buildingSize = GetPlayer(playerIndex)->GetBuildingStats(buildingType).size;
    if (baseTile.x() + buildingSize.width() > map->GetWidth() ||
        baseTile.y() + buildingSize.height() > map->GetHeight()) {
      LOG(ERROR) << "Received an AddObject message containing a building with out-of-bounds coordinates";
      return;
    }
    float buildPercentage = *reinterpret_cast<const float*>(buffer + 16);
    
    ClientBuilding* newBuilding = new ClientBuilding(GetPlayer(playerIndex), buildingType, baseTile.x(), baseTile.y(), buildPercentage, initialHP);
    map->AddObject(objectId, newBuilding);
    if (playerIndex == match->GetPlayerIndex()) {
      player->GetPlayerStats().BuildingAdded(buildingType, buildPercentage == 100);

      if (buildPercentage == 100) {
        newBuilding->UpdateFieldOfView(map.get(), 1);
      }
    }
  } else if (objectType == ObjectType::Unit) {
    UnitType unitType = static_cast<UnitType>(mango::uload16(buffer + 10));
    if (unitType >= UnitType::NumUnits) {
      LOG(ERROR) << "Received an AddObject message containing an invalid UnitType";
      return;
    }
    
    QPointF mapCoord(*reinterpret_cast<const float*>(buffer + 12),
                     *reinterpret_cast<const float*>(buffer + 16));
    
    ClientUnit* newUnit = new ClientUnit(GetPlayer(playerIndex), unitType, mapCoord, initialHP);
    map->AddObject(objectId, newUnit);
    if (playerIndex == match->GetPlayerIndex()) {
      player->GetPlayerStats().UnitAdded(newUnit->GetType());
      
      newUnit->UpdateFieldOfView(map.get(), 1);
    }
  } else {
    LOG(ERROR) << "Received AddObject message with invalid ObjectType";
  }
}

void GameController::HandleObjectDeathMessage(const QByteArray& data) {
  if (data.size() < 4) {
    LOG(ERROR) << "Received a too short ObjectDeath message";
    return;
  }
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
    ClientBuilding* building = AsBuilding(object);
    if (building->IsCompleted()) {
      Decal* newDecal = new Decal(building, map.get(), currentGameStepServerTime);
      renderWindow->AddDecal(newDecal);
      
      if (building->GetPlayerIndex() == match->GetPlayerIndex()) {
        player->GetPlayerStats().BuildingRemoved(building->GetType(), true);

        object->UpdateFieldOfView(map.get(), -1);
      }
    } else {
      // TODO: Destruction animations for foundations

      if (building->GetPlayerIndex() == match->GetPlayerIndex()) {
        player->GetPlayerStats().BuildingRemoved(building->GetType(), false);
      }
    }
  } else if (object->isUnit()) {
    Decal* newDecal = new Decal(AsUnit(object), map.get(), currentGameStepServerTime);
    renderWindow->AddDecal(newDecal);
    
    if (object->GetPlayerIndex() == match->GetPlayerIndex()) {
      player->GetPlayerStats().UnitRemoved(AsUnit(object)->GetType());
      
      object->UpdateFieldOfView(map.get(), -1);
    }
  }
  
  delete object;
  map->GetObjects().erase(it);
}

void GameController::HandleUnitMovementMessage(const QByteArray& data) {
  if (data.size() < 21) {
    LOG(ERROR) << "Received a too short UnitMovement message";
    return;
  }
  const char* buffer = data.data();
  
  u32 unitId = mango::uload32(buffer + 0);
  QPointF startPoint(
      *reinterpret_cast<const float*>(buffer + 4),
      *reinterpret_cast<const float*>(buffer + 8));
  QPointF speed(
      *reinterpret_cast<const float*>(buffer + 12),
      *reinterpret_cast<const float*>(buffer + 16));
  if (buffer[20] >= static_cast<int>(UnitAction::NumActions)) {
    LOG(ERROR) << "Received UnitMovement message with invalid UnitAction";
    return;
  }
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
  
  ClientUnit* unit = AsUnit(it->second);
  unit->SetMovementSegment(currentGameStepServerTime, startPoint, speed, action, map.get(), match.get());
}

void GameController::HandleUnitGarrisonMessage(const QByteArray& data) {
  if (data.size() < 8) {
    LOG(ERROR) << "Received a too short UnitGarrison message";
    return;
  }
  const char* buffer = data.data();
  
  u32 unitId = mango::uload32(buffer + 0);
  u32 targetObjectId = mango::uload32(buffer + 4);

  auto unitIt = map->GetObjects().find(unitId);
  if (unitIt == map->GetObjects().end()) {
    LOG(ERROR) << "Received a UnitGarrison message for an object ID that is not in the map.";
    return;
  }
  if (!unitIt->second->isUnit()) {
    LOG(ERROR) << "Received a UnitGarrison message for an object ID that is a different type than a unit.";
    return;
  }
  auto targetObjectIt = map->GetObjects().find(targetObjectId);
  if (targetObjectIt == map->GetObjects().end()) {
    LOG(ERROR) << "Received a UnitGarrison message with target object ID that is not in the map.";
    return;
  }

  ClientUnit* unit = AsUnit(unitIt->second);
  ChangeUnitGarrisonStatus(unit, targetObjectIt->second, !unit->IsGarrisoned());
}

void GameController::ChangeUnitGarrisonStatus(ClientUnit* unit, ClientObject* targetObject, bool enter) {
  // NOTE: the unit is ca be the current player's or not
  if (enter) {
    unit->SetMovementSegment(currentGameStepServerTime, unit->GetMapCoord(), QPointF(0, 0), UnitAction::Idle, map.get(), match.get());
    if (match->GetPlayerIndex() == unit->GetPlayerIndex()) {
      unit->UpdateFieldOfView(map.get(), -1);
    }
    targetObject->GarrisonUnit(unit);
    unit->SetGarrisonedInsideObject(targetObject);
  } else {
    targetObject->UngarrisonUnit(unit);
    unit->SetGarrisonedInsideObject(nullptr);
    unit->ClearOverrideDirection();
    if (match->GetPlayerIndex() == unit->GetPlayerIndex()) {
      unit->UpdateFieldOfView(map.get(), 1);
    }
  }
  
  // Garrison related missing features from client and server:
  // TODO: If relic, keep track of number of relics in player stats #relics
  // TODO: Bonus attack to some buildings (possibly not here)
  // TODO: Bonus speed to some units (possibly not here)
  // TODO: Heal garrisoned units (not here, in the simulation)
}

void GameController::HandleGameStepTimeMessage(const QByteArray& data) {
  if (data.size() < 8) {
    LOG(ERROR) << "Received a too short GameStepTime message";
    return;
  }
  memcpy(&currentGameStepServerTime, data.data() + 0, 8);
}

void GameController::HandleResourcesUpdateMessage(const QByteArray& data, ResourceAmount* resources) {
  if (data.size() < 16) {
    LOG(ERROR) << "Received a too short ResourcesUpdate message";
    return;
  }
  const char* buffer = data.data();
  
  u32 wood = mango::uload32(buffer + 0);
  u32 food = mango::uload32(buffer + 4);
  u32 gold = mango::uload32(buffer + 8);
  u32 stone = mango::uload32(buffer + 12);
  
  *resources = ResourceAmount(wood, food, gold, stone);
}

void GameController::HandleBuildPercentageUpdate(const QByteArray& data) {
  if (data.size() < 4 + 4) {
    LOG(ERROR) << "Received a too short BuildPercentageUpdate message";
    return;
  }
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
  
  ClientBuilding* building = AsBuilding(it->second);
  if (building->GetPlayerIndex() == match->GetPlayerIndex()) {
    if (!building->IsCompleted() && percentage == 100) {
      // The building has been completed.
      player->GetPlayerStats().BuildingFinished(building->GetType());

      building->UpdateFieldOfView(map.get(), 1);
    }
  }
  building->SetBuildPercentage(percentage);
}

void GameController::HandleChangeUnitTypeMessage(const QByteArray& data) {
  if (data.size() < 4 + 2) {
    LOG(ERROR) << "Received a too short ChangeUnitType message";
    return;
  }
  const char* buffer = data.data();
  
  u32 unitId = mango::uload32(buffer + 0);
  auto it = map->GetObjects().find(unitId);
  if (it == map->GetObjects().end()) {
    LOG(ERROR) << "Received a ChangeUnitType message for an object ID that is not in the map.";
    return;
  }
  if (!it->second->isUnit()) {
    LOG(ERROR) << "Received a ChangeUnitType message for an object ID that is a different type than a unit.";
    return;
  }
  
  UnitType newType = static_cast<UnitType>(mango::uload16(buffer + 4));
  if (newType >= UnitType::NumUnits) {
    LOG(ERROR) << "Received a ChangeUnitType message with an invalid UnitType";
    return;
  }
  
  ClientUnit* unit = AsUnit(it->second);
  UnitType oldType = unit->GetType();
  unit->SetType(newType);

  player->GetPlayerStats().UnitTransformed(oldType, newType);
}

void GameController::HandleSetCarriedResourcesMessage(const QByteArray& data) {
  if (data.size() < 6) {
    LOG(ERROR) << "Received too short SetCarriedResources message";
    return;
  }
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
  ClientUnit* villager = AsUnit(it->second);
  if (!IsVillager(villager->GetType())) {
    LOG(ERROR) << "Received a SetCarriedResources message for a unit that is not a villager.";
    return;
  }
  
  if (buffer[4] >= static_cast<int>(ResourceType::NumTypes)) {
    LOG(ERROR) << "Received SetCarriedResources message with invalid resource type";
    return;
  }
  ResourceType type = static_cast<ResourceType>(buffer[4]);
  u8 amount = buffer[5];
  
  villager->SetCarriedResources(type, amount);
}

void GameController::HandleHPUpdateMessage(const QByteArray& data) {
  if (data.size() < 8) {
    LOG(ERROR) << "Received a too short HPUpdate message";
    return;
  }
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
  if (data.size() < 2) {
    LOG(ERROR) << "Received a too short PlayerLeaveBroadcast message";
    return;
  }
  u8 playerIndex = data[0];
  if (playerIndex >= match->GetPlayers().size()) {
    LOG(ERROR) << "Received a PlayerLeaveBroadcast message with an invalid player index";
    return;
  }
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
  if (data.size() < 6) {
    LOG(ERROR) << "Received a too short QueueUnit message";
    return;
  }
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
  if (unitType >= UnitType::NumUnits) {
    LOG(ERROR) << "Received a QueueUnit message with an invalid UnitType";
    return;
  }
  
  ClientBuilding* building = AsBuilding(it->second);
  building->QueueUnit(unitType);
}

void GameController::HandleUpdateProductionMessage(const QByteArray& data) {
  if (data.size() < 4 + 4 + 4) {
    LOG(ERROR) << "Received a too short UpdateProduction message";
    return;
  }
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
  
  ClientBuilding* building = AsBuilding(it->second);
  building->SetProductionState(currentGameStepServerTime, percentage, progressPerSecond);
}

void GameController::HandleRemoveFromProductionQueueMessage(const QByteArray& data) {
  if (data.size() < 5) {
    LOG(ERROR) << "Received a too short RemoveFromProductionQueue message";
    return;
  }
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
  
  u8 queueIndex = buffer[4];
  
  ClientBuilding* building = AsBuilding(it->second);
  // This handles the case of the queue being empty.
  building->DequeueUnit(queueIndex);
}

void GameController::HandleSetHousedMessage(const QByteArray& data) {
  if (data.size() < 1) {
    LOG(ERROR) << "Received a too short SetHoused message";
    return;
  }
  isHoused = data.data()[0] != 0;
}
