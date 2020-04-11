// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/server/match_setup.hpp"

#include <QApplication>
#include <QThread>

#include "FreeAge/common/logging.hpp"
#include "FreeAge/common/messages.hpp"

// TODO (puzzlepaint): For some reason, this include needed to be after the Qt includes on my laptop
// in order for CIDE not to show some errors. Compiling always worked. Check the reason for the errors.
#include <mango/core/endian.hpp>

/// Creates a ServerToClientMessage::PlayerList message. Note that SetPlayerListMessagePlayerIndex()
/// has to be called on the created message in addition to finish its construction (and can be called
/// again to update the player index).
QByteArray CreatePlayerListMessage(const std::vector<std::shared_ptr<PlayerInMatch>>& playersInMatch, PlayerInMatch* playerToExclude, PlayerInMatch* playerToInclude) {
  // Create buffer
  QByteArray msg(4, Qt::Initialization::Uninitialized);
  char* data = msg.data();
  data[0] = static_cast<char>(ServerToClientMessage::PlayerList);
  
  for (const auto& player : playersInMatch) {
    if (player.get() == playerToExclude) {
      continue;
    }
    if (player->state == PlayerInMatch::State::Joined ||
        player.get() == playerToInclude) {
      // Append player name length (u16) + player name (in UTF-8)
      QByteArray playerNameUtf8 = player->name.toUtf8();
      msg.append(2, 0);
      mango::ustore16(msg.data() + msg.size() - 2, playerNameUtf8.size());
      msg.append(playerNameUtf8);
      
      // Append player color index (u16)
      msg.append(2, 0);
      mango::ustore16(msg.data() + msg.size() - 2, player->playerColorIndex);
      
      // Append whether the player is ready (u8)
      msg.append(player->isReady ? 1 : 0);
    }
  }
  
  mango::ustore16(msg.data() + 1, msg.size());
  return msg;
}

/// For a given player list message created by CreatePlayerListMessage(),
/// sets the index of the player that the message is sent to. This tells the
/// player about which of the players in the list is him/herself.
void SetPlayerListMessagePlayerIndex(QByteArray* msg, PlayerInMatch* player, const std::vector<std::shared_ptr<PlayerInMatch>>& playersInMatch, PlayerInMatch* playerToExclude, PlayerInMatch* playerToInclude) {
  int index = 0;
  for (const auto& listedPlayer : playersInMatch) {
    if (listedPlayer.get() == playerToExclude) {
      continue;
    }
    if (listedPlayer->state == PlayerInMatch::State::Joined ||
        listedPlayer.get() == playerToInclude) {
      if (listedPlayer.get() == player) {
        msg->data()[3] = index;
        return;
      }
      ++ index;
    }
  }
  
  LOG(ERROR) << "Server: SetPlayerListMessagePlayerIndex() could not determine the player's index.";
}

static void SendChatBroadcast(u16 sendingPlayerIndex, const QString& text, const std::vector<std::shared_ptr<PlayerInMatch>>& playersInMatch) {
  // Broadcast the chat message to all clients.
  // Note that we even send it back to the original sender. This is such that all
  // clients receive the chat in the same order.
  QByteArray chatBroadcastMsg = CreateChatBroadcastMessage(sendingPlayerIndex, text);
  for (const auto& player : playersInMatch) {
    if (player->state == PlayerInMatch::State::Joined) {
      player->socket->write(chatBroadcastMsg);
    }
  }
}

void SendWelcomeAndJoinMessage(PlayerInMatch* player, const std::vector<std::shared_ptr<PlayerInMatch>>& playersInMatch, const ServerSettings& settings) {
  // Send the new player the welcome message.
  player->socket->write(CreateWelcomeMessage());
  
  // Send the current lobby settings to the new player.
  player->socket->write(CreateSettingsUpdateMessage(settings.allowNewConnections, settings.mapSize, true));
  
  // Notify all players about the new player list.
  QByteArray playerListMsg = CreatePlayerListMessage(playersInMatch, nullptr, player);
  for (const auto& otherPlayer : playersInMatch) {
    if (otherPlayer->state == PlayerInMatch::State::Joined ||
        otherPlayer.get() == player) {
      SetPlayerListMessagePlayerIndex(&playerListMsg, otherPlayer.get(), playersInMatch, nullptr, player);
      otherPlayer->socket->write(playerListMsg);
    }
  }
  
  // If a player joins, send a random join message.
  if (!player->isHost) {
    constexpr int kJoinMessagesCount = 8;
    QString joinMessages[kJoinMessagesCount] = {
        QObject::tr("[%1 joined the game room. Wololo!]"),
        QObject::tr("[%1 joined the game room, exclaims \"Nice town!\", and takes it.]"),
        QObject::tr("[%1 joined the game room. 105]"),
        QObject::tr("[%1 joined the game room, let the siege begin!]"),
        QObject::tr("[%1 joined the game room and fast-castles into knights.]"),
        QObject::tr("[%1 joined the game room and goes for monks & siege.]"),
        QObject::tr("[%1 joined the game room, time to hide your villagers in the corners!]"),
        QObject::tr("[%1 joined the game room and insta-converts the enemy's army.]")};
    
    // Prevent using the same message two times in a row.
    // TODO: Avoid static?
    static int lastJoinMessage = -1;
    int messageIndex = (rand() % kJoinMessagesCount);
    if (messageIndex == lastJoinMessage) {
      messageIndex = (messageIndex + 1) % kJoinMessagesCount;
    }
    lastJoinMessage = messageIndex;
    
    SendChatBroadcast(std::numeric_limits<u16>::max(), joinMessages[messageIndex].arg(player->name), playersInMatch);
  }
}

bool HandleHostConnect(const QByteArray& msg, int len, PlayerInMatch* player, const std::vector<std::shared_ptr<PlayerInMatch>>& playersInMatch, const ServerSettings& settings) {
  LOG(INFO) << "Server: Received HostConnect";
  
  if (msg.length() < 3 + hostTokenLength || len < 3 + hostTokenLength) {
    LOG(ERROR) << "Received a too short HostConnect message";
    return false;
  }
  
  QByteArray providedToken = msg.mid(3, hostTokenLength);
  if (providedToken != settings.hostToken) {
    LOG(WARNING) << "Received a HostConnect message with an invalid host token: " << providedToken.toStdString();
    return false;
  }
  
  for (const auto& otherPlayer : playersInMatch) {
    if (otherPlayer->isHost) {
      LOG(WARNING) << "Received a HostConnect message with correct token, but there is already a host";
      return false;
    }
  }
  player->isHost = true;
  
  player->name = QString::fromUtf8(msg.mid(3 + hostTokenLength, len - (3 + hostTokenLength)));
  player->playerColorIndex = 0;
  player->state = PlayerInMatch::State::Joined;
  
  SendWelcomeAndJoinMessage(player, playersInMatch, settings);
  return true;
}

bool HandleConnect(const QByteArray& msg, int len, PlayerInMatch* player, const std::vector<std::shared_ptr<PlayerInMatch>>& playersInMatch, const ServerSettings& settings) {
  LOG(INFO) << "Server: Received Connect";
  
  bool thereIsAHost = false;
  for (const auto& otherPlayer : playersInMatch) {
    if (otherPlayer->isHost) {
      thereIsAHost = true;
      break;
    }
  }
  if (!thereIsAHost) {
    LOG(ERROR) << "Received Connect message, but there is no host. Rejecting the connection.";
    return false;
  }
  
  player->name = QString::fromUtf8(msg.mid(3, len - 3));
  // Find the lowest free player color index
  int playerColorToTest = 0;
  for (; playerColorToTest < 999; ++ playerColorToTest) {
    bool isFree = true;
    for (const auto& otherPlayer : playersInMatch) {
      if (otherPlayer->state == PlayerInMatch::State::Joined &&
          otherPlayer->playerColorIndex == playerColorToTest) {
        isFree = false;
        break;
      }
    }
    if (isFree) {
      break;
    }
  }
  player->playerColorIndex = playerColorToTest;
  player->state = PlayerInMatch::State::Joined;
  
  SendWelcomeAndJoinMessage(player, playersInMatch, settings);
  return true;
}

void HandleSettingsUpdate(const QByteArray& msg, const std::vector<std::shared_ptr<PlayerInMatch>>& playersInMatch, QTcpServer* server, ServerSettings* settings) {
  if (msg.size() < 3 + 3) {
    LOG(ERROR) << "Received a too short SettingsUpdate message";
    return;
  }
  
  settings->allowNewConnections = msg.data()[3] > 0;
  
  settings->mapSize = mango::uload16(msg.data() + 4);
  
  // Check whether accepting new connections needs to be paused/resumed
  bool isHostReady = false;
  for (const auto& player : playersInMatch) {
    if (player->isHost) {
      isHostReady = player->isReady;
      break;
    }
  }
  
  bool shouldAcceptingBePaused = !settings->allowNewConnections || isHostReady;
  if (shouldAcceptingBePaused && !settings->acceptingConnectionsPaused) {
    server->pauseAccepting();
  } else if (!shouldAcceptingBePaused && settings->acceptingConnectionsPaused) {
    server->resumeAccepting();
  }
  settings->acceptingConnectionsPaused = shouldAcceptingBePaused;
  
  // NOTE: Since the messages are identical apart from the message type, we could actually directly take
  // the received message data and just exchange the message type.
  QByteArray broadcastMsg = CreateSettingsUpdateMessage(settings->allowNewConnections, settings->mapSize, true);
  for (const auto& player : playersInMatch) {
    if (!player->isHost && player->state == PlayerInMatch::State::Joined) {
      player->socket->write(broadcastMsg);
    }
  }
}

void HandleReadyUp(const QByteArray& msg, PlayerInMatch* player, const std::vector<std::shared_ptr<PlayerInMatch>>& playersInMatch, QTcpServer* server, ServerSettings* settings) {
  if (msg.size() < 3 + 1) {
    LOG(ERROR) << "Received a too short ReadyUp message";
    return;
  }
  
  bool isReady = msg.data()[3] > 0;
  
  // If the ready state of the host changes, check whether accepting new connections needs to be paused/resumed
  if (player->isHost) {
    bool shouldAcceptingBePaused = !settings->allowNewConnections || isReady;
    if (shouldAcceptingBePaused && !settings->acceptingConnectionsPaused) {
      server->pauseAccepting();
    } else if (!shouldAcceptingBePaused && settings->acceptingConnectionsPaused) {
      server->resumeAccepting();
    }
    settings->acceptingConnectionsPaused = shouldAcceptingBePaused;
  }
  player->isReady = isReady;
  
  // Notify all players about the change in ready state
  QByteArray playerListMsg = CreatePlayerListMessage(playersInMatch, nullptr, nullptr);
  for (const auto& otherPlayer : playersInMatch) {
    if (otherPlayer->state == PlayerInMatch::State::Joined) {
      SetPlayerListMessagePlayerIndex(&playerListMsg, otherPlayer.get(), playersInMatch, nullptr, nullptr);
      otherPlayer->socket->write(playerListMsg);
    }
  }
}

static void HandleChat(const QByteArray& msg, PlayerInMatch* player, int len, const std::vector<std::shared_ptr<PlayerInMatch>>& playersInMatch) {
  QString text = QString::fromUtf8(msg.mid(3, len - 3));
  
  // Determine the index of the sending player.
  int sendingPlayerIndex = 0;
  for (usize i = 0; i < playersInMatch.size(); ++ i) {
    if (playersInMatch[i].get() == player) {
      break;
    } else if (playersInMatch[i]->state == PlayerInMatch::State::Joined) {
      ++ sendingPlayerIndex;
    }
  }
  
  SendChatBroadcast(sendingPlayerIndex, text, playersInMatch);
}

static void HandlePing(const QByteArray& msg, PlayerInMatch* player, const ServerSettings& settings) {
  if (msg.size() < 3 + 8) {
    LOG(ERROR) << "Received a too short Ping message";
    return;
  }
  
  u64 number = mango::uload64(msg.data() + 3);
  
  TimePoint pingHandleTime = Clock::now();
  player->lastPingTime = pingHandleTime;
  
  double serverTimeSeconds = SecondsDuration(pingHandleTime - settings.serverStartTime).count();
  player->socket->write(CreatePingResponseMessage(number, serverTimeSeconds));
  player->socket->flush();
}

void HandleLeave(const QByteArray& /*msg*/, PlayerInMatch* player, const std::vector<std::shared_ptr<PlayerInMatch>>& playersInMatch) {
  if (player->isHost) {
    LOG(INFO) << "Server: Received Leave by host";
  } else {
    LOG(INFO) << "Server: Received Leave by client";
  }
  
  // If the host left, abort the game and exit.
  // Else, notify the remaining players about the new player list.
  QByteArray msg = player->isHost ? CreateGameAbortedMessage() : CreatePlayerListMessage(playersInMatch, player, nullptr);
  if (!player->isHost) {
    msg += CreateChatBroadcastMessage(std::numeric_limits<u16>::max(), QObject::tr("[%1 left the game room.]").arg(player->name));
  }
  
  for (const auto& otherPlayer : playersInMatch) {
    if (otherPlayer.get() == player) {
      continue;
    }
    if (otherPlayer->state == PlayerInMatch::State::Joined) {
      if (!player->isHost) {
        SetPlayerListMessagePlayerIndex(&msg, otherPlayer.get(), playersInMatch, player, nullptr);
      }
      otherPlayer->socket->write(msg);
      if (player->isHost) {
        // Here, we have to ensure that everything gets sent before the server exits.
        otherPlayer->socket->waitForBytesWritten(200);
      }
    }
  }
}

bool HandleStartGame(const QByteArray& /*msg*/, PlayerInMatch* player, const std::vector<std::shared_ptr<PlayerInMatch>>& playersInMatch) {
  if (player->isHost) {
    LOG(INFO) << "Server: Received StartGame by host";
  } else {
    LOG(ERROR) << "Server: Received StartGame by a client! Clients are not permitted to send this message.";
    return false;
  }
  
  for (const auto& otherPlayer : playersInMatch) {
    if (otherPlayer->state == PlayerInMatch::State::Joined &&
        !otherPlayer->isReady) {
      LOG(WARNING) << "Server: Received StartGame by host, but not all players are ready. This can happen in case of delays.";
      return false;
    }
  }
  
  // Stop accepting new connections and drop all players in non-joined state.
  return true;
}

enum class ParseMessagesResult {
  NoAction = 0,
  PlayerLeftOrShouldBeDisconnected,
  GameStarted
};

static ParseMessagesResult TryParseClientMessages(PlayerInMatch* player, const std::vector<std::shared_ptr<PlayerInMatch>>& playersInMatch, QTcpServer* server, ServerSettings* settings) {
  while (true) {
    if (player->unparsedBuffer.size() < 3) {
      return ParseMessagesResult::NoAction;
    }
    
    char* data = player->unparsedBuffer.data();
    u16 msgLength = mango::uload16(data + 1);
    
    if (player->unparsedBuffer.size() < msgLength) {
      return ParseMessagesResult::NoAction;
    }
    
    if (msgLength < 3) {
      LOG(ERROR) << "Received a too short message. The given message length is (should be at least 3): " << msgLength;
    } else {
      ClientToServerMessage msgType = static_cast<ClientToServerMessage>(data[0]);
      
      switch (msgType) {
      case ClientToServerMessage::HostConnect:
        if (!HandleHostConnect(player->unparsedBuffer, msgLength, player, playersInMatch, *settings)) {
          return ParseMessagesResult::PlayerLeftOrShouldBeDisconnected;
        }
        break;
      case ClientToServerMessage::Connect:
        if (!HandleConnect(player->unparsedBuffer, msgLength, player, playersInMatch, *settings)) {
          return ParseMessagesResult::PlayerLeftOrShouldBeDisconnected;
        }
        break;
      case ClientToServerMessage::SettingsUpdate:
        HandleSettingsUpdate(player->unparsedBuffer, playersInMatch, server, settings);
        break;
      case ClientToServerMessage::ReadyUp:
        HandleReadyUp(player->unparsedBuffer, player, playersInMatch, server, settings);
        break;
      case ClientToServerMessage::Chat:
        HandleChat(player->unparsedBuffer, player, msgLength, playersInMatch);
        break;
      case ClientToServerMessage::Ping:
        HandlePing(player->unparsedBuffer, player, *settings);
        break;
      case ClientToServerMessage::Leave:
        HandleLeave(player->unparsedBuffer, player, playersInMatch);
        return ParseMessagesResult::PlayerLeftOrShouldBeDisconnected;
      case ClientToServerMessage::StartGame:
        HandleStartGame(player->unparsedBuffer, player, playersInMatch);
        player->unparsedBuffer.remove(0, msgLength);
        return ParseMessagesResult::GameStarted;
      default:
        LOG(ERROR) << "Received a message in the match setup phase that cannot be parsed in this phase: " << static_cast<int>(msgType);
        break;
      }
    }
    
    player->unparsedBuffer.remove(0, msgLength);
  }
}

bool RunMatchSetupLoop(QTcpServer* server, std::vector<std::shared_ptr<PlayerInMatch>>* playersInMatch, ServerSettings* settings) {
  while (true) {
    // Check for new connections
    while (QTcpSocket* socket = server->nextPendingConnection()) {
      // A new connection is available. The pointer to it does not need to be freed.
      LOG(INFO) << "Server: Got new connection";
      
      socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
      
      std::shared_ptr<PlayerInMatch> newPlayer(new PlayerInMatch());
      newPlayer->socket = socket;
      newPlayer->isHost = false;
      newPlayer->playerColorIndex = -1;
      newPlayer->isReady = false;
      newPlayer->connectionTime = Clock::now();
      newPlayer->state = PlayerInMatch::State::Connected;
      newPlayer->lastPingTime = Clock::now();
      playersInMatch->push_back(newPlayer);
    }
    
    // Communicate with existing connections.
    for (auto it = playersInMatch->begin(); it != playersInMatch->end(); ) {
      PlayerInMatch& player = **it;
      
      // Read new data from the connection.
      int prevSize = player.unparsedBuffer.size();
      player.unparsedBuffer += player.socket->readAll();
      if (player.unparsedBuffer.size() > prevSize) {
        ParseMessagesResult parseResult = TryParseClientMessages(&player, *playersInMatch, server, settings);
        
        if (parseResult == ParseMessagesResult::GameStarted) {
          return true;
        } else if (parseResult == ParseMessagesResult::PlayerLeftOrShouldBeDisconnected) {
          if (player.isHost) {
            // The host left and the game has been aborted as a result. Exit the server.
            return false;
          }
          delete player.socket;
          it = playersInMatch->erase(it);
          continue;
        }
      }
      
      // Time out connections which did not send pings in time, or if the connection was lost.
      constexpr int kNoPingTimeout = 5000;
      if (player.state == PlayerInMatch::State::Joined &&
          (player.socket->state() != QAbstractSocket::ConnectedState ||
           MillisecondsDuration(Clock::now() - player.lastPingTime).count() > kNoPingTimeout)) {
        delete player.socket;
        it = playersInMatch->erase(it);
        
        QByteArray playerListMsg =
            CreatePlayerListMessage(*playersInMatch, nullptr, nullptr) +
            CreateChatBroadcastMessage(std::numeric_limits<u16>::max(), QObject::tr("[The connection to %1 was lost.]"));
        for (const auto& otherPlayer : *playersInMatch) {
          if (otherPlayer->state == PlayerInMatch::State::Joined) {
            SetPlayerListMessagePlayerIndex(&playerListMsg, otherPlayer.get(), *playersInMatch, nullptr, nullptr);
            otherPlayer->socket->write(playerListMsg);
          }
        }
        
        continue;
      }
      
      // Time out connections which did not authorize themselves in time, or if the connection was lost.
      constexpr int kAuthorizeTimeout = 2000;
      if (player.state == PlayerInMatch::State::Connected &&
          (player.socket->state() != QAbstractSocket::ConnectedState ||
           MillisecondsDuration(Clock::now() - player.connectionTime).count() > kAuthorizeTimeout)) {
        delete player.socket;
        it = playersInMatch->erase(it);
        continue;
      }
      
      ++ it;
    }
    
    qApp->processEvents(QEventLoop::AllEvents);
    QThread::msleep(1);
  }
}
