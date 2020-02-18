#include <chrono>
#include <memory>
#include <vector>

#include <mango/core/endian.hpp>
#include <QApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include "FreeAge/free_age.h"
#include "FreeAge/logging.h"
#include "FreeAge/messages.h"

typedef std::chrono::steady_clock Clock;
typedef Clock::time_point TimePoint;

QByteArray hostToken;

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
  
  /// The time at which the connection was made. This can be used to time the
  /// client out if it does not authorize itself within some time frame.
  TimePoint connectionTime;
  
  /// Current state of the connection.
  State state;
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
    }
  }
  
  mango::ustore16(msg.data() + 1, msg.size());
  return msg;
}

bool HandleHostConnect(const QByteArray& msg, int len, PlayerInMatch* player, const std::vector<std::shared_ptr<PlayerInMatch>>& playersInMatch) {
  LOG(INFO) << "Server: Received HostConnect";
  
  if (msg.length() < 3 + hostTokenLength) {
    return false;
  }
  
  QByteArray providedToken = msg.mid(3, hostTokenLength);
  if (providedToken != hostToken) {
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
  
  player->socket->write(CreateWelcomeMessage());
  QByteArray playerListMsg = CreatePlayerListMessage(playersInMatch, nullptr, player);
  for (const auto& otherPlayer : playersInMatch) {
    if (otherPlayer->state == PlayerInMatch::State::Joined ||
        otherPlayer.get() == player) {
      otherPlayer->socket->write(playerListMsg);
    }
  }
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
  
  player->socket->write(CreateWelcomeMessage());
  QByteArray playerListMsg = CreatePlayerListMessage(playersInMatch, nullptr, player);
  for (const auto& otherPlayer : playersInMatch) {
    if (otherPlayer->state == PlayerInMatch::State::Joined ||
        otherPlayer.get() == player) {
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

void HandlePing(const QByteArray& /*msg*/, PlayerInMatch* player) {
  LOG(INFO) << "Server: Received Ping";
  
  player->socket->write(CreatePingResponseMessage());
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
bool TryParseClientMessages(PlayerInMatch* player, const std::vector<std::shared_ptr<PlayerInMatch>>& playersInMatch) {
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
      if (!HandleHostConnect(player->unparsedBuffer, msgLength, player, playersInMatch)) {
        return false;
      }
      break;
    case ClientToServerMessage::Connect:
      HandleConnect(player->unparsedBuffer, msgLength, player, playersInMatch);
      break;
    case ClientToServerMessage::Chat:
      HandleChat(player->unparsedBuffer, player, msgLength, playersInMatch);
      break;
    case ClientToServerMessage::Ping:
      HandlePing(player->unparsedBuffer, player);
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
  if (argc != 2) {
    LOG(INFO) << "Usage: FreeAgeServer <host_token>";
    return 1;
  }
  hostToken = argv[1];
  if (hostToken.size() != hostTokenLength) {
    LOG(ERROR) << "The provided host token has an incorrect length. Required length: " << hostTokenLength << ", actual length: " << hostToken.size();
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
  
  while (true) {
    // Check for new connections
    bool timedOut = false;
    if (server.waitForNewConnection(/*msec*/ 0, &timedOut) && !timedOut) {
      // A new connection is available, get a pointer to it (does not need to be freed).
      QTcpSocket* socket = server.nextPendingConnection();
      if (socket) {
        LOG(INFO) << "Server: Got new connection";
        
        std::shared_ptr<PlayerInMatch> newPlayer(new PlayerInMatch());
        newPlayer->socket = socket;
        newPlayer->isHost = false;
        newPlayer->playerColorIndex = -1;
        newPlayer->connectionTime = Clock::now();
        newPlayer->state = PlayerInMatch::State::Connected;
        playersInMatch.push_back(newPlayer);
      }
    }
    
    // Handle existing connections.
    for (auto it = playersInMatch.begin(); it != playersInMatch.end(); ) {
      PlayerInMatch& player = **it;
      
      // Read new data from the connection.
      int prevSize = player.unparsedBuffer.size();
      player.unparsedBuffer += player.socket->readAll();
      if (player.unparsedBuffer.size() > prevSize) {
        if (!TryParseClientMessages(&player, playersInMatch)) {
          if (player.isHost) {
            // The host left and the game has been aborted as a result. Exit the server.
            return 0;
          }
          delete player.socket;
          it = playersInMatch.erase(it);
          continue;
        }
      }
      
      // Time out connections which did not authorize themselves in time.
      constexpr int kAuthorizeTimeout = 2000;
      if (player.state == PlayerInMatch::State::Connected &&
          std::chrono::duration<double, std::milli>(Clock::now() - player.connectionTime).count() > kAuthorizeTimeout) {
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
  
  return 0;
}
