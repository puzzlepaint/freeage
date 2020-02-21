#pragma once

#include <QTcpSocket>

/// Handles the basics of the connection to the server:
/// * Ping handling
/// * Synchronization with the server time
class ServerConnection {
 public:
  ServerConnection();
  
  bool ConnectToServer(const QString& serverAddress, int timeout, bool retryUntilTimeout);
  
 private:
  /// Socket which is connected to the server.
  QTcpSocket socket;
  
  /// Contains data which has been received from the server but was not parsed yet.
  QByteArray unparsedReceivedBuffer;
};
