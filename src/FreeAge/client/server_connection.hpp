// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <mutex>

#include <QObject>
#include <QTcpSocket>
#include <QTimer>

#include "FreeAge/common/free_age.hpp"
#include "FreeAge/common/logging.hpp"
#include "FreeAge/common/messages.hpp"

class ServerConnectionThread;

struct ReceivedMessage {
  inline ReceivedMessage(ServerToClientMessage type, const QByteArray& data)
      : type(type),
        data(data) {}
  
  ServerToClientMessage type;
  
  QByteArray data;
};

/// Handles the basics of the connection to the server:
/// * Ping handling
/// * Synchronization with the server time
///
/// Communication works via a QTcpSocket that lives in a separate QThread.
/// This is because receiving data only works when the Qt event loop is active,
/// which we can never guarantee in the main thread since it might be spending most
/// of its time rendering the game. A separate QThread can provide its own event loop
/// that can react quickly.
class ServerConnection : public QObject {
 Q_OBJECT
 public:
  ServerConnection();
  
  ~ServerConnection();
  
  void SetDebugNetworking(bool enable);
  
  bool ConnectToServer(const QString& serverAddress, int timeout, bool retryUntilTimeout);
  
  void Shutdown();
  
  bool WaitForWelcomeMessage(int timeout, u32* serverNetworkProtocolVersion);
  
  /// Sends the given message to the server. The connection object does not need to be locked when calling this.
  void Write(const QByteArray& message);
  
  /// Variant of Write() which blocks until finished.
  void WriteBlocking(const QByteArray& message);
  
  /// Locks the connection object for calling GetReceivedMessages().
  void Lock();
  
  /// Unlocks the connection object.
  void Unlock();
  
  /// Returns the vector of received messages. The connection object must be locked while accessing
  /// this vector. All messages that were handled should be deleted from the vector to prevent them
  /// from accumulating there infinitely.
  std::vector<ReceivedMessage>* GetReceivedMessages();
  
  // Estimates the current ping and the offset to the server, while smoothly filtering the raw measurements.
  void EstimateCurrentPingAndOffset(double* filteredPing, double* filteredOffset);
  
  /// Returns the server time at which the game state should be displayed by the client right now.
  double GetServerTimeToDisplayNow();
  
  /// Returns the client time now. This should only be needed for debugging. Normally, only the server time is relevant.
  double GetClientTimeNow();
  
  inline bool ConnectionToServerLost() const { return connectionToServerLost; }
  
 signals:
  /// Signals that a new message has arrived. Slots connected to this signal should call
  /// Lock() and GetReceivedMessages() to get the vector of messages and process one or more
  /// messages in this vector. They then must call Unlock().
  void NewMessage();
  
  void NewPingMeasurement(int milliseconds);
  
  void ConnectionLost();
  
 private slots:
  void NewMessageInternal();
  void NewPingMeasurementInternal(int milliseconds);
  void ConnectionLostInternal();
  
 private:
  ServerConnectionThread* thread;
  QObject* dummyWorker;
  
  /// Whether the connection to the server has been lost (either due to a straight
  /// disconnect, or because there was no reply to a ping in some time).
  bool connectionToServerLost = false;
};
