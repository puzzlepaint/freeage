#include "FreeAge/client/server_connection.hpp"

#include <QApplication>
#include <QThread>

#include "FreeAge/common/logging.hpp"

// TODO (puzzlepaint): For some reason, this include needed to be at the end using clang-10-rc2 on my laptop to not cause weird errors in CIDE. Why?
#include <mango/core/endian.hpp>

ServerConnection::ServerConnection() {
  // Try to reduce the delay for sending messages.
  socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);
}

bool ServerConnection::ConnectToServer(const QString& serverAddress, int timeout, bool retryUntilTimeout) {
  // Issue the connection request
  socket.connectToHost(serverAddress, serverPort, QIODevice::ReadWrite);
  // socket.setLocalPort(TODO);
  
  // Wait for the connection to be made, and retry on failure if requested
  TimePoint connectionStartTime = Clock::now();
  while (socket.state() != QAbstractSocket::ConnectedState &&
         MillisecondsDuration(Clock::now() - connectionStartTime).count() <= timeout) {
    qApp->processEvents(QEventLoop::AllEvents);
    QThread::msleep(1);
    
    if (socket.state() == QAbstractSocket::UnconnectedState &&
        retryUntilTimeout) {
      // Retry connecting.
      socket.connectToHost(serverAddress, serverPort, QIODevice::ReadWrite);
    }
  }
  
  if (socket.state() == QAbstractSocket::ConnectedState) {
    // This was set in the constructor already, but set it here again (after connecting) to be sure.
    socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);
    
    // Define the client time.
    connectionStartTime = Clock::now();
    
    // Set up connection monitoring.
    lastPingResponseTime = connectionStartTime;
    
    connect(&pingAndConnectionCheckTimer, &QTimer::timeout, this, &ServerConnection::PingAndCheckConnection);
    pingAndConnectionCheckTimer.start(500);
  }
  
  return socket.state() == QAbstractSocket::ConnectedState;
}

void ServerConnection::Shutdown() {
  pingAndConnectionCheckTimer.stop();
  socket.disconnectFromHost();
}

bool ServerConnection::WaitForWelcomeMessage(int timeout) {
  TimePoint welcomeWaitStartTime = Clock::now();
  bool welcomeReceived = false;
  
  while (MillisecondsDuration(Clock::now() - welcomeWaitStartTime).count() <= timeout) {
    unparsedReceivedBuffer += socket.readAll();
    
    if (unparsedReceivedBuffer.size() >= 3) {
      if (unparsedReceivedBuffer.at(0) == static_cast<char>(ServerToClientMessage::Welcome)) {
        u16 msgLength = mango::uload16(unparsedReceivedBuffer.data() + 1);
        if (msgLength == 3) {
          unparsedReceivedBuffer.remove(0, 3);
          
          welcomeReceived = true;
          break;
        }
      }
    }
    
    qApp->processEvents(QEventLoop::AllEvents);
    QThread::msleep(1);
  }
  
  return welcomeReceived;
}

void ServerConnection::SetParseMessages(bool enable) {
  if (enable) {
    connect(&socket, &QTcpSocket::readyRead, this, &ServerConnection::TryParseMessages);
    TryParseMessages();
  } else {
    disconnect(&socket, &QTcpSocket::readyRead, this, &ServerConnection::TryParseMessages);
  }
}

void ServerConnection::TryParseMessages() {
  TimePoint receiveTime = Clock::now();
  
  QByteArray& buffer = unparsedReceivedBuffer;
  buffer += socket.readAll();
  
  while (true) {
    if (buffer.size() < 3) {
      return;
    }
    
    char* data = buffer.data();
    u16 msgLength = mango::uload16(data + 1);
    
    if (buffer.size() < msgLength) {
      return;
    }
    
    ServerToClientMessage msgType = static_cast<ServerToClientMessage>(data[0]);
    
    if (msgType == ServerToClientMessage::PingResponse) {
      HandlePingResponseMessage(buffer, receiveTime);
    } else {
      emit NewMessage(buffer, msgType, msgLength);
    }
    
    buffer.remove(0, msgLength);
  }
}

void ServerConnection::PingAndCheckConnection() {
  // If we did not receive a ping response in some time, assume that the connection dropped.
  constexpr int kNoPingTimeout = 5000;
  if (socket.state() != QTcpSocket::ConnectedState ||
      std::chrono::duration<double, std::milli>(Clock::now() - lastPingResponseTime).count() > kNoPingTimeout) {
    connectionToServerLost = true;
    emit ConnectionLost();
    return;
  }
  
  // Send a ping message.
  sentPings.emplace_back(nextPingNumber, Clock::now());
  Write(CreatePingMessage(nextPingNumber));
  ++ nextPingNumber;
}

void ServerConnection::HandlePingResponseMessage(const QByteArray& msg, const TimePoint& receiveTime) {
  lastPingResponseTime = receiveTime;
  
  u64 number = mango::uload64(msg.data() + 3);
  double serverTimeSeconds;
  memcpy(&serverTimeSeconds, msg.data() + 3 + 8, 8);
  
  for (usize i = 0; i < sentPings.size(); ++ i) {
    const auto& item = sentPings[i];
    if (item.first == number) {
      double elapsedMilliseconds = MillisecondsDuration(receiveTime - item.second).count();
      sentPings.erase(sentPings.begin() + i);
      
      emit NewPingMeasurement(static_cast<int>(elapsedMilliseconds + 0.5));
      
      // Adjust time synchronization.
      // The computed client time matches the given server time from the message
      // (under the assumption that the half ping time from the client to the server is
      //  equal to the half ping time from the server to the client). Add this as
      // a measurement to the lastTimeOffsets vector.
      double clientTimeSeconds = SecondsDuration(item.second - connectionStartTime).count() + 0.5 * 0.001 * elapsedMilliseconds;
      double timeOffset = serverTimeSeconds - clientTimeSeconds;
      
      lastTimeOffsets.push_back(timeOffset);
      if (lastTimeOffsets.size() > 10) {
        lastTimeOffsets.erase(lastTimeOffsets.begin());
      }
      
      lastPings.push_back(0.001 * elapsedMilliseconds);
      if (lastPings.size() > 10) {
        lastPings.erase(lastPings.begin());
      }
      
      return;
    }
  }
  
  LOG(ERROR) << "Received a ping response for a ping number that is not in sentPings";
}
