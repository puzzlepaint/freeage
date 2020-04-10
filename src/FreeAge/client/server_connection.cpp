// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/client/server_connection.hpp"

#include <iomanip>
#include <fstream>

#include <QApplication>
#include <QThread>

#include "FreeAge/common/logging.hpp"

// TODO (puzzlepaint): For some reason, this include needed to be at the end using clang-10-rc2 on my laptop to not cause weird errors in CIDE. Why?
#include <mango/core/endian.hpp>

/// Manages the connection thread (back-end for the ServerConnection class).
///
/// The thread main function is run(). It creates the QTcpSocket and starts
/// a Qt event loop. The next step is ConnectToServer() being called by ServerConnection,
/// then WaitForWelcomeMessage() being called by ServerConnection.
///
/// PingResponse type messages are handled directly by the ServerConnectionThread to
/// minimize the delay in handling them. Other message types are added to the
/// receivedMessages vector, where they can be accessed by the main thread (after
/// locking).
class ServerConnectionThread : public QThread {
 Q_OBJECT
 public:
  inline std::mutex& GetReceivedMessagesMutex() { return receivedMessagesMutex; }
  inline std::vector<ReceivedMessage>& GetReceivedMessages() { return receivedMessages; }
  
  inline std::mutex& GetPingAndOffsetsMutex() { return pingAndOffsetsMutex; }
  inline std::vector<double>& GetLastTimeOffsets() { return lastTimeOffsets; }
  inline std::vector<double>& GetLastPings() { return lastPings; }
  
  inline const TimePoint& GetConnectionStartTime() const { return connectionStartTime; }
  
  void SetDebugNetworking(bool enable) {
    debugNetworking = enable;
    if (debugNetworking) {
      networkingDebugFile.open("network_debug_log_offsets.txt", std::ios::out);
      networkingDebugFile << std::setprecision(14);
    } else {
      networkingDebugFile.close();
    }
  }
  
  void GetCurrentSmoothedPingAndOffset(const TimePoint& timePoint, double* offset, double* ping) {
    double elapsedSeconds = SecondsDuration(timePoint - lastPingResponseTime).count();
    
    int timeOffsetChangeSign = (robustOffsetAverage > lastSmoothedTimeOffset) ? 1 : -1;
    *offset = lastSmoothedTimeOffset + timeOffsetChangeSign * elapsedSeconds * smoothedValueChangeSpeed;
    if (timeOffsetChangeSign == 1) {
      *offset = std::min(robustOffsetAverage, *offset);
    } else {
      *offset = std::max(robustOffsetAverage, *offset);
    }
    
    int pingChangeSign = (robustPingAverage > lastSmoothedPing) ? 1 : -1;
    *ping = lastSmoothedPing + pingChangeSign * elapsedSeconds * smoothedValueChangeSpeed;
    if (pingChangeSign == 1) {
      *ping = std::min(robustPingAverage, *ping);
    } else {
      *ping = std::max(robustPingAverage, *ping);
    }
  }
  
 signals:
  void NewMessage();
  void ConnectionLost();
  void NewPingMeasurement(int milliseconds);
  
 public slots:
  bool ConnectToServer(const QString& serverAddress, int timeout, bool retryUntilTimeout) {
    // Issue the connection request
    socket->connectToHost(serverAddress, serverPort, QIODevice::ReadWrite);
    // socket.setLocalPort(TODO?);
    
    // Wait for the connection to be made, and retry on failure if requested
    TimePoint connectionStartTime = Clock::now();
    while (socket->state() != QAbstractSocket::ConnectedState &&
          MillisecondsDuration(Clock::now() - connectionStartTime).count() <= timeout) {
      qApp->processEvents(QEventLoop::AllEvents);
      QThread::msleep(1);
      
      if (socket->state() == QAbstractSocket::UnconnectedState &&
          retryUntilTimeout) {
        // Retry connecting.
        socket->connectToHost(serverAddress, serverPort, QIODevice::ReadWrite);
      }
    }
    
    if (socket->state() == QAbstractSocket::ConnectedState) {
      // This was set after allocating the QTcpSocket already, but set it here again (after connecting) to be sure.
      socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
      
      // Define the client time.
      connectionStartTime = Clock::now();
      
      // Clear ping / offset filter state.
      lastSmoothedTimeOffset = -1;
      lastSmoothedPing = -1;
      robustOffsetAverage = -1;
      robustPingAverage = -1;
      
      // Set up connection monitoring.
      // NOTE: In Qt, a QThread object belongs to the thread >> that created this QThread <<, not to the thread that this QThread is running!
      //       As a consequence, if we connect e.g., QTimer signals to a member slot of ServerConnectionThread here,
      //       then this slot will be executed in the wrong thread (the main thread)! That is quite unintuitive behavior
      //       and super dangerous. As a workaround we connect to lambdas instead that call the desired member functions.
      lastPingResponseTime = connectionStartTime;
      
      pingAndConnectionCheckTimer = new QTimer();
      connect(pingAndConnectionCheckTimer, &QTimer::timeout, [&]() {
        PingAndCheckConnection();
      });
      pingAndConnectionCheckTimer->start(500);
      
      connect(socket, &QTcpSocket::readyRead, [&]() {
        TryParseMessages();
      });
      TryParseMessages();
    } else {
      LOG(WARNING) << "Connection to server failed. Socket state is: " << socket->state();
    }
    
    return socket->state() == QAbstractSocket::ConnectedState;
  }
  
  void Shutdown() {
    if (pingAndConnectionCheckTimer) {
      pingAndConnectionCheckTimer->stop();
    }
    socket->disconnectFromHost();
  }
  
  /// Writes to the connection's socket and flushes it.
  inline void Write(const QByteArray& message) {
    qint64 result = socket->write(message);
    if (result != message.size()) {
      LOG(ERROR) << "Error sending message: write() returned " << result << ", but the message size is " << message.size();
    }
    
    // We generally want to send inputs to the server immediately to minimize the delay,
    // so flush the socket. Without flushing, I observed a ~16.5 ms delay for sending
    // while the game loop was running. For some reason, this did not happen during the
    // match setup stage though.
    socket->flush();
  }
  
 private slots:
  void TryParseMessages() {
    TimePoint receiveTime = Clock::now();
    
    QByteArray& buffer = unparsedReceivedBuffer;
    buffer += socket->readAll();
    
    while (true) {
      if (buffer.size() < 3) {
        return;
      }
      
      char* data = buffer.data();
      u16 msgLength = mango::uload16(data + 1);
      
      if (buffer.size() < msgLength) {
        return;
      }
      
      if (msgLength < 3) {
        LOG(ERROR) << "Received a too short message. The given message length is (should be at least 3): " << msgLength;
      } else {
        ServerToClientMessage msgType = static_cast<ServerToClientMessage>(data[0]);
        
        if (msgType == ServerToClientMessage::PingResponse) {
          HandlePingResponseMessage(buffer, receiveTime);
        } else {
          receivedMessagesMutex.lock();
          receivedMessages.emplace_back(msgType, buffer.mid(3, msgLength - 3));
          receivedMessagesMutex.unlock();
          emit NewMessage();
        }
      }
      
      buffer.remove(0, msgLength);
    }
  }
  
  void PingAndCheckConnection() {
    // If we did not receive a ping response in some time, assume that the connection dropped.
    constexpr int kNoPingTimeout = 5000;
    if (socket->state() != QTcpSocket::ConnectedState ||
        std::chrono::duration<double, std::milli>(Clock::now() - lastPingResponseTime).count() > kNoPingTimeout) {
      LOG(INFO) << "Connection to server lost.";
      emit ConnectionLost();
      pingAndConnectionCheckTimer->stop();
      return;
    }
    
    // Send a ping message.
    sentPings.emplace_back(nextPingNumber, Clock::now());
    Write(CreatePingMessage(nextPingNumber));
    ++ nextPingNumber;
  }
  
  void HandlePingResponseMessage(const QByteArray& msg, const TimePoint& receiveTime) {
    if (msg.size() < 3 + 8 + 8) {
      LOG(ERROR) << "Received a too short PingResponse message";
      return;
    }
    
    u64 number = mango::uload64(msg.data() + 3);
    double serverTimeSeconds;
    memcpy(&serverTimeSeconds, msg.data() + 3 + 8, 8);
    
    for (usize i = 0; i < sentPings.size(); ++ i) {
      const auto& item = sentPings[i];
      if (item.first == number) {
        double pingInMilliseconds = MillisecondsDuration(receiveTime - item.second).count();
        sentPings.erase(sentPings.begin() + i);
        
        emit NewPingMeasurement(static_cast<int>(pingInMilliseconds + 0.5));
        
        // Store offset and ping measurements.
        double clientTimeSeconds = SecondsDuration(receiveTime - connectionStartTime).count();
        double timeOffset = serverTimeSeconds - clientTimeSeconds;
        
        pingAndOffsetsMutex.lock();
        
        lastTimeOffsets.push_back(timeOffset);
        if (lastTimeOffsets.size() > 10) {
          lastTimeOffsets.erase(lastTimeOffsets.begin());
        }
        
        lastPings.push_back(0.001 * pingInMilliseconds);
        if (lastPings.size() > 10) {
          lastPings.erase(lastPings.begin());
        }
        
        // Initialize or update the last smoothed values.
        if (lastSmoothedPing < 0) {
          // Initialize.
          lastSmoothedTimeOffset = lastTimeOffsets.back();
          lastSmoothedPing = lastPings.back();
        } else {
          // Update.
          if (robustPingAverage > 0) {
            // Perform the update for the last measurement interval.
            GetCurrentSmoothedPingAndOffset(receiveTime, &lastSmoothedTimeOffset, &lastSmoothedPing);
          }
          
          // Obtain new target values for the smoothing.
          EstimateRobustPingAndOffsetAverages(&robustPingAverage, &robustOffsetAverage);
        }
        
        pingAndOffsetsMutex.unlock();
        
        if (debugNetworking) {
          networkingDebugFile << "offset " << timeOffset << std::endl;
          networkingDebugFile << "ping " << (0.001 * pingInMilliseconds) << std::endl;
          networkingDebugFile.flush();
        }
        
        lastPingResponseTime = receiveTime;
        return;
      }
    }
    
    LOG(ERROR) << "Received a ping response for a ping number that is not in sentPings";
  }
  
 private:
  // Main function of the thread.
  void run() override {
    socket = new QTcpSocket();
    
    // Try to reduce the delay for sending messages.
    socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    
    // Run this QThread's event loop
    exec();
    
    // Clean up.
    if (pingAndConnectionCheckTimer) {
      pingAndConnectionCheckTimer->stop();
      delete pingAndConnectionCheckTimer;
      pingAndConnectionCheckTimer = nullptr;
    }
    
    delete socket;
    socket = nullptr;
  }
  
  void EstimateRobustPingAndOffsetAverages(double* filteredPing, double* filteredOffset) {
    // If there are not enough measurements yet, simply take the average of all measurements.
    if (lastTimeOffsets.size() < 3) {
      double pingSum = 0;
      double offsetSum = 0;
      
      for (usize i = 0; i < lastTimeOffsets.size(); ++ i) {
        pingSum += lastPings[i];
        offsetSum += lastTimeOffsets[i];
      }
      *filteredOffset = offsetSum / lastTimeOffsets.size();
      *filteredPing = pingSum / lastPings.size();
    } else {
      // Drop outliers: find the smallest and the largest value and discard them.
      usize minPingIndex = 0;
      usize maxPingIndex = 0;
      
      usize minOffsetIndex = 0;
      usize maxOffsetIndex = 0;
      
      for (usize i = 1; i < lastTimeOffsets.size(); ++ i) {
        if (lastPings[i] < lastPings[minPingIndex]) {
          minPingIndex = i;
        }
        if (lastPings[i] > lastPings[maxPingIndex]) {
          maxPingIndex = i;
        }
        
        if (lastTimeOffsets[i] < lastTimeOffsets[minOffsetIndex]) {
          minOffsetIndex = i;
        }
        if (lastTimeOffsets[i] > lastTimeOffsets[maxOffsetIndex]) {
          maxOffsetIndex = i;
        }
      }
      
      // Obtain the average of the remaining measurements.
      double pingSum = 0;
      double offsetSum = 0;
      
      for (usize i = 0; i < lastTimeOffsets.size(); ++ i) {
        if (i != minPingIndex && i != maxPingIndex) {
          pingSum += lastPings[i];
        }
        
        if (i != minOffsetIndex && i != maxOffsetIndex) {
          offsetSum += lastTimeOffsets[i];
        }
      }
      *filteredOffset = offsetSum / (lastTimeOffsets.size() - 2);
      *filteredPing = pingSum / (lastPings.size() - 2);
    }
  }
  
  // -- Connection --
  
  /// Socket which is connected to the server.
  /// Note: This must be a pointer, otherwise it is wrongly created on the main thread.
  QTcpSocket* socket = nullptr;
  
  /// Contains data which has been received from the server but was not parsed yet.
  QByteArray unparsedReceivedBuffer;
  
  /// Array of messages that were extracted from unparsedReceivedBuffer but have not been
  /// further processed yet.
  std::vector<ReceivedMessage> receivedMessages;
  
  /// Guards access to the receivedMessages vector.
  std::mutex receivedMessagesMutex;
  
  
  // -- Time synchronization --
  
  /// Start time of the connection, defines the client time as the seconds that
  /// passed from this time point on.
  TimePoint connectionStartTime;
  
  /// Last obtained time offset measurements.
  /// The offset represents the duration that has to be added to the client time,
  /// i.e., the time passed since connectionStartTime, to obtain the server time
  /// for which we expect to receive sent messages now. Note that this is not the
  /// server time which the server has right now, but the current server time minus the
  /// time it takes for a message to be transmitted from the server to the client.
  /// A single offset value may be computed by filtering the entries in this vector
  /// somehow, e.g., by dropping outliers and averaging the rest.
  std::vector<double> lastTimeOffsets;
  std::vector<double> lastPings;
  
  /// Last smoothed time offset and ping values.
  /// To prevent visual jumps, the raw measurements in lastTimeOffsets and lastPings are
  /// smoothly filtered over time, always moving towards an outlier-robust average of
  /// the raw measurements. The values stored here represent the smoothed values
  /// at the time at which the last new measurements were added (stored in lastPingResponseTime).
  /// They can be used to compute the current smoothed values.
  double lastSmoothedTimeOffset = -1;
  double lastSmoothedPing = -1;
  
  /// The smoothed values change with 2 millisecond per second, a 0.2% deviation from the desired speed.
  const double smoothedValueChangeSpeed = 0.001;
  
  /// The current robust averages of the values in lastTimeOffsets and lastPings.
  /// These are the "target" values that the smoothed values, lastSmoothedTimeOffset and
  /// lastSmoothedPing, smoothly move towards.
  double robustOffsetAverage = -1;
  double robustPingAverage = -1;
  
  /// Guards access to the lastTimeOffsets and lastPings vectors.
  std::mutex pingAndOffsetsMutex;
  
  
  // -- Ping --
  
  /// The last time point at which a ping response was received.
  TimePoint lastPingResponseTime;
  
  /// Numbers and times of previously sent ping messages.
  std::vector<std::pair<u64, TimePoint>> sentPings;
  
  /// Number of the next ping message to send.
  u64 nextPingNumber;
  
  /// Timer which repeatedly calls PingAndCheckConnection().
  /// Note: This must be a pointer, otherwise it is wrongly created on the main thread.
  QTimer* pingAndConnectionCheckTimer = nullptr;
  
  
  // -- Debug --
  bool debugNetworking = false;
  std::ofstream networkingDebugFile;
};


struct ThreadWorker : public QObject {
 Q_OBJECT
 public:
  ThreadWorker(ServerConnectionThread* thread)
      : thread(thread) {}
  
 public slots:
  void ExitThread() {
    // exit() makes the thread exit its Qt event loop (i.e., the call to exec()).
    thread->exit();
  }
  
  void SetDebugNetworking(bool enable) {
    thread->SetDebugNetworking(enable);
  }
  
  bool ConnectToServer(const QString& serverAddress, int timeout, bool retryUntilTimeout) {
    return thread->ConnectToServer(serverAddress, timeout, retryUntilTimeout);
  }
  
  void Shutdown() {
    thread->Shutdown();
  }
  
  void Write(QByteArray data) {
    thread->Write(data);
  }
  
 private:
  ServerConnectionThread* thread;
};

ServerConnection::ServerConnection() {
  // Start the connection thread.
  thread = new ServerConnectionThread();
  connect(thread, &ServerConnectionThread::NewMessage, this, &ServerConnection::NewMessageInternal, Qt::QueuedConnection);
  connect(thread, &ServerConnectionThread::NewPingMeasurement, this, &ServerConnection::NewPingMeasurementInternal, Qt::QueuedConnection);
  connect(thread, &ServerConnectionThread::ConnectionLost, this, &ServerConnection::ConnectionLostInternal, Qt::QueuedConnection);
  thread->start();
  
  // Create a dummy QObject living in the thread. This allows to run code in the
  // thread by calling QMetaObject::invokeMethod() with the dummyWorker as context object.
  dummyWorker = new ThreadWorker(thread);
  dummyWorker->moveToThread(thread);
}

ServerConnection::~ServerConnection() {
  // Issue thread exit and wait for it to happen
  QMetaObject::invokeMethod(dummyWorker, "ExitThread", Qt::BlockingQueuedConnection);
  
  thread->wait();
  
  delete dummyWorker;
}

void ServerConnection::SetDebugNetworking(bool enable) {
  // Issue thread exit and wait for it to happen
  QMetaObject::invokeMethod(dummyWorker, "SetDebugNetworking", Qt::BlockingQueuedConnection, Q_ARG(bool, enable));
}

bool ServerConnection::ConnectToServer(const QString& serverAddress, int timeout, bool retryUntilTimeout) {
  connectionToServerLost = false;
  
  bool result;
  QMetaObject::invokeMethod(dummyWorker, "ConnectToServer", Qt::BlockingQueuedConnection, Q_RETURN_ARG(bool, result), Q_ARG(QString, serverAddress), Q_ARG(int, timeout), Q_ARG(bool, retryUntilTimeout));
  return result;
}

void ServerConnection::Shutdown() {
  QMetaObject::invokeMethod(dummyWorker, "Shutdown", Qt::BlockingQueuedConnection);
}

bool ServerConnection::WaitForWelcomeMessage(int timeout) {
  TimePoint welcomeWaitStartTime = Clock::now();
  
  while (MillisecondsDuration(Clock::now() - welcomeWaitStartTime).count() <= timeout) {
    Lock();
    
    auto* messages = GetReceivedMessages();
    
    for (usize i = 0; i < messages->size(); ++ i) {
      const ReceivedMessage& msg  = messages->at(i);
      if (msg.type == ServerToClientMessage::Welcome) {
        messages->erase(messages->begin() + i);
        Unlock();
        return true;
      }
    }
    
    Unlock();
    QThread::msleep(1);
  }
  
  return false;
}

void ServerConnection::Write(const QByteArray& message) {
  QByteArray persistentReference = message;
  QMetaObject::invokeMethod(dummyWorker, "Write", Qt::QueuedConnection, Q_ARG(QByteArray, persistentReference));
}

void ServerConnection::WriteBlocking(const QByteArray& message) {
  QMetaObject::invokeMethod(dummyWorker, "Write", Qt::BlockingQueuedConnection, Q_ARG(QByteArray, message));
}

void ServerConnection::Lock() {
  thread->GetReceivedMessagesMutex().lock();
}

void ServerConnection::Unlock() {
  thread->GetReceivedMessagesMutex().unlock();
}

std::vector<ReceivedMessage>* ServerConnection::GetReceivedMessages() {
  return &thread->GetReceivedMessages();
}

void ServerConnection::EstimateCurrentPingAndOffset(double* filteredPing, double* filteredOffset) {
  thread->GetPingAndOffsetsMutex().lock();
  thread->GetCurrentSmoothedPingAndOffset(Clock::now(), filteredOffset, filteredPing);
  thread->GetPingAndOffsetsMutex().unlock();
}

double ServerConnection::GetServerTimeToDisplayNow() {
  double filteredPing;
  double filteredOffset;
  EstimateCurrentPingAndOffset(&filteredPing, &filteredOffset);
  
  // First, estimate the server time up to which we expect to have received messages.
  double clientTimeSeconds = GetClientTimeNow();
  double estimatedLastReceiveServerTime = clientTimeSeconds + filteredOffset;
  
  // Second, subtract some safety margin (to account for jitter).
  constexpr double kSafetyMargin = 0.015;  // 15 milliseconds
  return estimatedLastReceiveServerTime - kSafetyMargin;
}

double ServerConnection::GetClientTimeNow() {
  return SecondsDuration(Clock::now() - thread->GetConnectionStartTime()).count();
}

void ServerConnection::NewMessageInternal() {
  emit NewMessage();
}

void ServerConnection::NewPingMeasurementInternal(int milliseconds) {
  emit NewPingMeasurement(milliseconds);
}

void ServerConnection::ConnectionLostInternal() {
  connectionToServerLost = true;
  emit ConnectionLost();
}

#include "server_connection.moc"
