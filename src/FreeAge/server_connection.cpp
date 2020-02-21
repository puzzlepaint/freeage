#include "FreeAge/server_connection.h"

ServerConnection::ServerConnection() {
  // Try to reduce the delay for sending messages.
  socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);
}

bool ServerConnection::ConnectToServer(const QString& serverAddress, int timeout, bool retryUntilTimeout) {
  static constexpr quint16 serverPort = 49100;  // TODO: Make configurable
  
  socket.connectToHost("127.0.0.1", serverPort, QIODevice::ReadWrite);
  // socket.setLocalPort(TODO);
  TimePoint connectionStartTime = Clock::now();
  constexpr int kConnectTimeout = 5000;
  while (socket.state() != QAbstractSocket::ConnectedState &&
          std::chrono::duration<double, std::milli>(Clock::now() - connectionStartTime).count() <= kConnectTimeout) {
    qapp.processEvents(QEventLoop::AllEvents);
    QThread::msleep(1);
    
    if (socket.state() == QAbstractSocket::UnconnectedState) {
      // Retry connecting.
      socket.connectToHost("127.0.0.1", serverPort, QIODevice::ReadWrite);
    }
  }
  if (socket.state() != QAbstractSocket::ConnectedState) {
    QMessageBox::warning(nullptr, QObject::tr("Error"), QObject::tr("Failed to connect to the server."));
    QFontDatabase::removeApplicationFont(georgiaFontID);
    continue;
  }
}
