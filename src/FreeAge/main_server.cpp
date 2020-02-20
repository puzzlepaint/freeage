#include <chrono>
#include <limits>
#include <memory>
#include <vector>

#include <mango/core/endian.hpp>
#include <QApplication>
#include <QByteArray>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include "FreeAge/free_age.h"
#include "FreeAge/logging.h"
#include "FreeAge/messages.h"

typedef std::chrono::steady_clock Clock;
typedef Clock::time_point TimePoint;

struct ServerSettings {
  /// Token with which the server was started. This is used by the host
  /// to authorize itself when making the TCP connection to the server.
  QByteArray hostToken;
  
  /// State of the setting whether additional players may connect to the server.
  bool allowNewConnections = true;
  
  /// Whether accepting new connections is paused on the QTcpServer. This may be true
  /// even if allowNewConnections is true, which happens when the host readied up while
  /// not unchecking the allowNewConnections setting.
  bool acceptingConnectionsPaused = false;
};

/// Represents a player who joined a match that has not started yet.
struct PlayerInMatch {
  enum class State {
    /// Initial state. The connection was made, but the client needs to authorize itself.
    Connected,
    
    /// The client authorized itself. It is displayed as a player in the list.
    Joined
  };
  
  /// Socket that can be used to send and receive data to/from the player.
  QTcpSocket* socket;
  
  /// Buffer for bytes that have been received from the client, but could not
  /// be parsed yet (because only a partial message was received so far).
  QByteArray unparsedBuffer;
  
  /// Whether this client can administrate the match.
  bool isHost;
  
  /// The player name as provided by the client.
  QString name;
  
  /// The player color index.
  int playerColorIndex;
  
  /// Whether the player clicked the "ready" check box.
  bool isReady;
  
  /// The time at which the connection was made. This can be used to time the
  /// client out if it does not authorize itself within some time frame.
  TimePoint connectionTime;
  
  /// Current state of the connection.
  State state;
  
  /// Numbers and times of previously sent ping messages.
  std::vector<std::pair<u64, TimePoint>> sentPings;
  
  /// Number of the next ping message to send.
  u64 nextPingNumber;
  
  /// The last point in time at which a ping response was received from this player.
  TimePoint lastPingResponseTime;
};

QByteArray CreatePlayerListMessage(const std::vector<std::shared_ptr<PlayerInMatch>>& playersInMatch, PlayerInMatch* playerToExclude, PlayerInMatch* playerToInclude) {
  // Create buffer
  QByteArray msg(3, Qt::Initialization::Uninitialized);
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

void SendChatBroadcast(u16 sendingPlayerIndex, const QString& text, const std::vector<std::shared_ptr<PlayerInMatch>>& playersInMatch) {
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

void SendWelcomeAndJoinMessage(PlayerInMatch* player, const std::vector<std::shared_ptr<PlayerInMatch>>& playersInMatch) {
  // Send the new player the welcome message.
  player->socket->write(CreateWelcomeMessage());
  
  // Notify all players about the new player list.
  QByteArray playerListMsg = CreatePlayerListMessage(playersInMatch, nullptr, player);
  for (const auto& otherPlayer : playersInMatch) {
    if (otherPlayer->state == PlayerInMatch::State::Joined ||
        otherPlayer.get() == player) {
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
  
  if (msg.length() < 3 + hostTokenLength) {
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
  
  SendWelcomeAndJoinMessage(player, playersInMatch);
  return true;
}

void HandleConnect(const QByteArray& msg, int len, PlayerInMatch* player, const std::vector<std::shared_ptr<PlayerInMatch>>& playersInMatch) {
  LOG(INFO) << "Server: Received Connect";
  
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
  
  SendWelcomeAndJoinMessage(player, playersInMatch);
}

void HandleSettingsUpdate(const QByteArray& msg, const std::vector<std::shared_ptr<PlayerInMatch>>& playersInMatch, QTcpServer* server, ServerSettings* settings) {
  LOG(INFO) << "Server: Received SettingsUpdate";
  
  settings->allowNewConnections = msg.data()[3] > 0;
  
  u16 mapSize = mango::uload16(msg.data() + 4);
  
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
  QByteArray broadcastMsg = CreateSettingsUpdateMessage(settings->allowNewConnections, mapSize, true);
  for (const auto& player : playersInMatch) {
    if (!player->isHost && player->state == PlayerInMatch::State::Joined) {
      player->socket->write(broadcastMsg);
    }
  }
}

void HandleReadyUp(const QByteArray& msg, PlayerInMatch* player, const std::vector<std::shared_ptr<PlayerInMatch>>& playersInMatch, QTcpServer* server, ServerSettings* settings) {
  LOG(INFO) << "Server: Received ReadyUp";
  
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
      otherPlayer->socket->write(playerListMsg);
    }
  }
}

void HandleChat(const QByteArray& msg, PlayerInMatch* player, int len, const std::vector<std::shared_ptr<PlayerInMatch>>& playersInMatch) {
  LOG(INFO) << "Server: Received Chat";
  
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

void HandlePingResponse(const QByteArray& msg, PlayerInMatch* player) {
  u64 number = mango::uload64(msg.data() + 3);
  
  for (usize i = 0; i < player->sentPings.size(); ++ i) {
    const auto& item = player->sentPings[i];
    if (item.first == number) {
      float elapsedMilliseconds = std::chrono::duration<float, std::milli>(Clock::now() - item.second).count();
      player->sentPings.erase(player->sentPings.begin() + i);
      
      player->lastPingResponseTime = Clock::now();
      
      // From time to time, notify the client about its ping
      // NOTE: We could average the pings over the timespan since the last notification here
      if (number % 2 == 0) {
        player->socket->write(CreatePingNotifyMessage(static_cast<int>(elapsedMilliseconds + 0.5f)));
      }
      
      return;
    }
  }
  
  LOG(ERROR) << "Received a ping response for a ping number that is not in sentPings";
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
      otherPlayer->socket->write(msg);
      if (player->isHost) {
        // Here, we have to ensure that everything gets sent before the server exits.
        otherPlayer->socket->waitForBytesWritten(200);
      }
    }
  }
}

/// Returns false if the player left the match or should be disconnected.
bool TryParseClientMessages(PlayerInMatch* player, const std::vector<std::shared_ptr<PlayerInMatch>>& playersInMatch, QTcpServer* server, ServerSettings* settings) {
  while (true) {
    if (player->unparsedBuffer.size() < 3) {
      return true;
    }
    
    char* data = player->unparsedBuffer.data();
    u16 msgLength = mango::uload16(data + 1);
    
    if (player->unparsedBuffer.size() < msgLength) {
      return true;
    }
    
    ClientToServerMessage msgType = static_cast<ClientToServerMessage>(data[0]);
    
    switch (msgType) {
    case ClientToServerMessage::HostConnect:
      if (!HandleHostConnect(player->unparsedBuffer, msgLength, player, playersInMatch, *settings)) {
        return false;
      }
      break;
    case ClientToServerMessage::Connect:
      HandleConnect(player->unparsedBuffer, msgLength, player, playersInMatch);
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
    case ClientToServerMessage::PingResponse:
      HandlePingResponse(player->unparsedBuffer, player);
      break;
    case ClientToServerMessage::Leave:
      HandleLeave(player->unparsedBuffer, player, playersInMatch);
      return false;
    }
    
    player->unparsedBuffer.remove(0, msgLength);
  }
}

int main(int argc, char** argv) {
  // Seed the random number generator.
  srand(time(nullptr));
  
  // Initialize loguru.
  loguru::g_preamble_date = false;
  loguru::g_preamble_thread = false;
  loguru::g_preamble_uptime = false;
  loguru::g_stderr_verbosity = 2;
  if (argc > 0) {
    loguru::init(argc, argv, /*verbosity_flag*/ nullptr);
  }
  
  // Create and initialize a QApplication.
  QApplication qapp(argc, argv);
  QCoreApplication::setOrganizationName("FreeAge");
  QCoreApplication::setOrganizationDomain("free-age.org");
  QCoreApplication::setApplicationName("FreeAge");
  
  LOG(INFO) << "Server: Start";
  
  // Parse command line arguments.
  ServerSettings settings;
  
  if (argc != 2) {
    LOG(INFO) << "Usage: FreeAgeServer <host_token>";
    return 1;
  }
  settings.hostToken = argv[1];
  if (settings.hostToken.size() != hostTokenLength) {
    LOG(ERROR) << "The provided host token has an incorrect length. Required length: " << hostTokenLength << ", actual length: " << settings.hostToken.size();
    return 1;
  }
  
  // Start listening for incoming connections.
  QTcpServer server;
  if (!server.listen(QHostAddress::Any, serverPort)) {
    LOG(ERROR) << "Failed to start listening for connections.";
    return 1;
  }
  
  // Match setup phase
  std::vector<std::shared_ptr<PlayerInMatch>> playersInMatch;
  
  LOG(INFO) << "Server: Entering match setup phase";
  TimePoint lastPingTime = Clock::now();
  constexpr int kPingInterval = 500;
  while (true) {
    // Regularly send out ping messages to all joined players to check that they are
    // still connected and to measure the current ping.
    TimePoint now = Clock::now();
    if (std::chrono::duration<double, std::milli>(now - lastPingTime).count() > kPingInterval) {
      lastPingTime = now;
      for (const auto& player : playersInMatch) {
        if (player->state == PlayerInMatch::State::Joined) {
          player->sentPings.emplace_back(player->nextPingNumber, Clock::now());
          player->socket->write(CreatePingMessage(player->nextPingNumber));
          ++ player->nextPingNumber;
        }
      }
    }
    
    // Check for new connections
    bool timedOut = false;
    if (server.waitForNewConnection(/*msec*/ 0, &timedOut) && !timedOut) {
      // A new connection is available, get a pointer to it (does not need to be freed).
      while (QTcpSocket* socket = server.nextPendingConnection()) {
        LOG(INFO) << "Server: Got new connection";
        
        socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        
        std::shared_ptr<PlayerInMatch> newPlayer(new PlayerInMatch());
        newPlayer->socket = socket;
        newPlayer->isHost = false;
        newPlayer->playerColorIndex = -1;
        newPlayer->isReady = false;
        newPlayer->connectionTime = Clock::now();
        newPlayer->state = PlayerInMatch::State::Connected;
        newPlayer->nextPingNumber = 0;
        newPlayer->lastPingResponseTime = Clock::now();
        playersInMatch.push_back(newPlayer);
      }
    }
    
    // Communicate with existing connections.
    for (auto it = playersInMatch.begin(); it != playersInMatch.end(); ) {
      PlayerInMatch& player = **it;
      
      // Read new data from the connection.
      int prevSize = player.unparsedBuffer.size();
      player.unparsedBuffer += player.socket->readAll();
      if (player.unparsedBuffer.size() > prevSize) {
        if (!TryParseClientMessages(&player, playersInMatch, &server, &settings)) {
          if (player.isHost) {
            // The host left and the game has been aborted as a result. Exit the server.
            return 0;
          }
          delete player.socket;
          it = playersInMatch.erase(it);
          continue;
        }
      }
      
      // Time out connections which did not send ping responses in time, or if the connection was lost.
      constexpr int kNoPingTimeout = 5000;
      if (player.state == PlayerInMatch::State::Joined &&
          (player.socket->state() != QAbstractSocket::ConnectedState ||
           std::chrono::duration<double, std::milli>(Clock::now() - player.lastPingResponseTime).count() > kNoPingTimeout)) {
        delete player.socket;
        it = playersInMatch.erase(it);
        
        QByteArray playerListMsg =
            CreatePlayerListMessage(playersInMatch, nullptr, nullptr) +
            CreateChatBroadcastMessage(std::numeric_limits<u16>::max(), QObject::tr("[The connection to %1 was lost.]"));
        for (const auto& otherPlayer : playersInMatch) {
          if (otherPlayer->state == PlayerInMatch::State::Joined) {
            otherPlayer->socket->write(playerListMsg);
          }
        }
        
        continue;
      }
      
      // Time out connections which did not authorize themselves in time, or if the connection was lost.
      constexpr int kAuthorizeTimeout = 2000;
      if (player.state == PlayerInMatch::State::Connected &&
          (player.socket->state() != QAbstractSocket::ConnectedState ||
           std::chrono::duration<double, std::milli>(Clock::now() - player.connectionTime).count() > kAuthorizeTimeout)) {
        delete player.socket;
        it = playersInMatch.erase(it);
        continue;
      }
      
      ++ it;
    }
    
    qapp.processEvents(QEventLoop::AllEvents);
    QThread::msleep(1);
  }
  
  // The match has been started. Reparent all client connections and delete the QTcpServer to stop accepting new connections.
  // TODO
  
  LOG(INFO) << "Server: Exit";
  return 0;
}
