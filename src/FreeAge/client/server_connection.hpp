#pragma once

#include <mutex>

#include <QObject>
#include <QTcpSocket>
#include <QTimer>

#include "FreeAge/common/free_age.hpp"
#include "FreeAge/common/messages.hpp"

/// Handles the basics of the connection to the server:
/// * Ping handling
/// * Synchronization with the server time
class ServerConnection : public QObject {
 Q_OBJECT
 public:
  ServerConnection();
  
  bool ConnectToServer(const QString& serverAddress, int timeout, bool retryUntilTimeout);
  
  void Shutdown();
  
  bool WaitForWelcomeMessage(int timeout);
  
  /// Sets whether to automatically parse new messages (a Qt event loop must be running for this to work).
  /// Make sure that a suitable message receiver exists to prevent the messages from getting lost.
  void SetParseMessages(bool enable);
  
  /// Returns the server time at which the game state should be displayed by the client right now.
  inline double GetDisplayedServerTime() {
    double clientTimeSeconds = SecondsDuration(Clock::now() - connectionStartTime).count();
    
    // TODO: Drop ping outliers
    // TODO: Change the offset on the client time *smoothly* once new measurements come in and
    //       old ones are dropped to prevent visual jumps.
    double offsetSum = 0;
    for (double offset : lastTimeOffsets) {
      offsetSum += offset;
    }
    double filteredOffset = offsetSum / lastTimeOffsets.size();
    
    return clientTimeSeconds + filteredOffset;
  }
  
  /// Thread-safe writing to the connection's socket.
  /// TODO: We can probably drop this, since a QTcpSocket can only be accessed from one thread anyway, otherwise it breaks.
  inline void Write(const QByteArray& message) {
    socketMutex.lock();
    socket.write(message);
    socketMutex.unlock();
  }
  
  inline bool ConnectionToServerLost() const { return connectionToServerLost; }
  
 public slots:
  /// Manually triggers parsing new messages.
  /// Make sure that a suitable message receiver exists to prevent the messages from getting lost.
  void TryParseMessages();
  
 signals:
  void NewMessage(const QByteArray& buffer, ServerToClientMessage msgType, u16 msgLength);
  void NewPingMeasurement(int milliseconds);
  void ConnectionLost();
  
 private slots:
  void PingAndCheckConnection();
  
  void HandlePingResponseMessage(const QByteArray& msg, const TimePoint& receiveTime);
  
 private:
  // -- Connection --
  
  /// Socket which is connected to the server.
  QTcpSocket socket;
  
  /// Mutex for thread-safe access to the socket.
  std::mutex socketMutex;
  
  /// Contains data which has been received from the server but was not parsed yet.
  QByteArray unparsedReceivedBuffer;
  
  /// Whether the connection to the server has been lost (either due to a straight
  /// disconnect, or because there was no reply to a ping in some time).
  bool connectionToServerLost = false;
  
  
  // -- Time synchronization --
  
  /// Start time of the connection, defines the client time as the seconds that
  /// passed from this time point on.
  TimePoint connectionStartTime;
  
  /// Last obtained time offsets, i.e., the offset that has to be added to the
  /// client time to obtain the server time. A single offset may be computed by
  /// filtering the entries in this vector somehow, e.g., drop outliers and average the rest.
  std::vector<double> lastTimeOffsets;
  
  
  // -- Ping --
  
  /// The last time point at which a ping response was received.
  TimePoint lastPingResponseTime;
  
  /// Numbers and times of previously sent ping messages.
  std::vector<std::pair<u64, TimePoint>> sentPings;
  
  /// Number of the next ping message to send.
  u64 nextPingNumber;
  
  /// Timer which repeatedly calls PingAndCheckConnection().
  QTimer pingAndConnectionCheckTimer;
};
