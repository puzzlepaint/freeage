#include "FreeAge/server/game.hpp"

#include <QApplication>
#include <QThread>

#include "FreeAge/common/logging.hpp"
#include "FreeAge/common/messages.hpp"

// TODO (puzzlepaint): For some reason, this include needed to be after the Qt includes on my laptop
// in order for CIDE not to show some errors. Compiling always worked. Check the reason for the errors.
#include <mango/core/endian.hpp>

void HandleLoadingProgress(const QByteArray& msg, PlayerInGame* player, const std::vector<std::shared_ptr<PlayerInGame>>& players) {
  if (msg.size() < 4) {
    LOG(ERROR) << "Server: Received LoadingProgress message which is too short";
    return;
  }
  
  // Broadcast the player's loading progress to all other players.
  u8 percentage = msg.data()[3];
  
  QByteArray broadcastMsg = CreateLoadingProgressBroadcastMessage(player->index, percentage);
  for (const auto& otherPlayer : players) {
    if (otherPlayer.get() != player) {
      otherPlayer->socket->write(broadcastMsg);
    }
  }
}

static void SendChatBroadcast(u16 sendingPlayerIndex, const QString& text, const std::vector<std::shared_ptr<PlayerInGame>>& players) {
  // Broadcast the chat message to all clients.
  // Note that we even send it back to the original sender. This is such that all
  // clients receive the chat in the same order.
  QByteArray chatBroadcastMsg = CreateChatBroadcastMessage(sendingPlayerIndex, text);
  for (const auto& player : players) {
    player->socket->write(chatBroadcastMsg);
  }
}

// TODO: This is duplicated from match_setup.cpp, de-duplicate this
static void HandleChat(const QByteArray& msg, PlayerInGame* player, int len, const std::vector<std::shared_ptr<PlayerInGame>>& players) {
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
static void HandlePing(const QByteArray& msg, PlayerInGame* player, const ServerSettings& settings) {
  u64 number = mango::uload64(msg.data() + 3);
  
  TimePoint pingHandleTime = Clock::now();
  player->lastPingTime = pingHandleTime;
  
  double serverTimeSeconds = SecondsDuration(pingHandleTime - settings.serverStartTime).count();
  player->socket->write(CreatePingResponseMessage(number, serverTimeSeconds));
}

enum class ParseMessagesResult {
  NoAction = 0,
  PlayerLeftOrShouldBeDisconnected
};

static ParseMessagesResult TryParseClientMessages(PlayerInGame* player, const std::vector<std::shared_ptr<PlayerInGame>>& players, ServerSettings* settings) {
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
      HandlePing(player->unparsedBuffer, player, *settings);
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

void RunGameLoop(std::vector<std::shared_ptr<PlayerInGame>>* playersInGame, ServerSettings* settings) {
  while (true) {
    // Communicate with player connections.
    for (auto it = playersInGame->begin(); it != playersInGame->end(); ) {
      PlayerInGame& player = **it;
      
      // Read new data from the connection.
      int prevSize = player.unparsedBuffer.size();
      player.unparsedBuffer += player.socket->readAll();
      
      bool removePlayer = false;
      if (player.unparsedBuffer.size() > prevSize) {
        ParseMessagesResult parseResult = TryParseClientMessages(&player, *playersInGame, settings);
        removePlayer = parseResult == ParseMessagesResult::PlayerLeftOrShouldBeDisconnected;
      }
      
      // Remove connections which got ParseMessagesResult::PlayerLeftOrShouldBeDisconnected,
      // which did not send pings in time, or if the connection was lost.
      constexpr int kNoPingTimeout = 5000;
      bool socketDisconnected = player.socket->state() != QAbstractSocket::ConnectedState;
      bool pingTimeout = MillisecondsDuration(Clock::now() - player.lastPingTime).count() > kNoPingTimeout;
      if (removePlayer || socketDisconnected || pingTimeout) {
        LOG(WARNING) << "Removing player " << player.name.toStdString() << " (index " << player.index << "). Reason: "
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
    QThread::msleep(1);
  }
}
