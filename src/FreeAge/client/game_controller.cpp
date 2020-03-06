#include "FreeAge/client/game_controller.hpp"

#include "FreeAge/common/logging.hpp"

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
