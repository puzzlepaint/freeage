#include "FreeAge/messages.h"

#include <mango/core/endian.hpp>
#include <QString>

// TODO: Create a helper function to create the base of all of these similar messages

QByteArray CreateHostConnectMessage(const QByteArray& hostToken, const QString& playerName) {
  // Prepare
  QByteArray playerNameUtf8 = playerName.toUtf8();
  
  // Create buffer
  QByteArray msg(1 + 2 + hostToken.size() + playerNameUtf8.size(), Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ClientToServerMessage::HostConnect);
  mango::ustore16(data + 1, msg.size());
  
  // Fill buffer
  memcpy(data + 3, hostToken.data(), hostToken.size());
  memcpy(data + 3 + hostToken.size(), playerNameUtf8.data(), playerNameUtf8.size());
  
  return msg;
}

QByteArray CreateConnectMessage(const QString& playerName) {
  // Prepare
  QByteArray playerNameUtf8 = playerName.toUtf8();
  
  // Create buffer
  QByteArray msg(1 + 2 + playerNameUtf8.size(), Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ClientToServerMessage::Connect);
  mango::ustore16(data + 1, msg.size());
  
  // Fill buffer
  memcpy(data + 3, playerNameUtf8.data(), playerNameUtf8.size());
  
  return msg;
}

QByteArray CreateSettingsUpdateMessage(bool allowMorePlayersToJoin, u16 mapSize, bool isBroadcast) {
  // Create buffer
  QByteArray msg(1 + 2 + 1 + 2, Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = isBroadcast ? static_cast<char>(ServerToClientMessage::SettingsUpdateBroadcast) : static_cast<char>(ClientToServerMessage::SettingsUpdate);
  mango::ustore16(data + 1, msg.size());
  
  // Fill buffer
  data[3] = allowMorePlayersToJoin ? 1 : 0;
  mango::ustore16(data + 4, mapSize);
  
  return msg;
}

QByteArray CreateChatMessage(const QString& text) {
  // Prepare
  QByteArray textUtf8 = text.toUtf8();
  
  // Create buffer
  QByteArray msg(1 + 2 + textUtf8.size(), Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ClientToServerMessage::Chat);
  mango::ustore16(data + 1, msg.size());
  
  // Fill buffer
  memcpy(data + 3, textUtf8.data(), textUtf8.size());
  
  return msg;
}

QByteArray CreatePingMessage() {
  // Create buffer
  QByteArray msg(1 + 2, Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ClientToServerMessage::Ping);
  mango::ustore16(data + 1, msg.size());
  
  return msg;
}

QByteArray CreateLeaveMessage() {
  // Create buffer
  QByteArray msg(1 + 2, Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ClientToServerMessage::Leave);
  mango::ustore16(data + 1, msg.size());
  
  return msg;
}


QByteArray CreateWelcomeMessage() {
  // Create buffer
  QByteArray msg(1 + 2, Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ServerToClientMessage::Welcome);
  mango::ustore16(data + 1, msg.size());
  
  return msg;
}

QByteArray CreateGameAbortedMessage() {
  // Create buffer
  QByteArray msg(1 + 2, Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ServerToClientMessage::GameAborted);
  mango::ustore16(data + 1, msg.size());
  
  return msg;
}

QByteArray CreateChatBroadcastMessage(u16 sendingPlayerIndex, const QString& text) {
  // Prepare
  QByteArray textUtf8 = text.toUtf8();
  
  // Create buffer
  QByteArray msg(1 + 2 + 2 + textUtf8.size(), Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ServerToClientMessage::ChatBroadcast);
  mango::ustore16(data + 1, msg.size());
  
  // Fill buffer
  mango::ustore16(data + 3, sendingPlayerIndex);
  
  memcpy(data + 3 + 2, textUtf8.data(), textUtf8.size());
  
  return msg;
}

QByteArray CreatePingResponseMessage() {
  // Create buffer
  QByteArray msg(1 + 2, Qt::Initialization::Uninitialized);
  char* data = msg.data();
  
  // Set buffer header (3 bytes)
  data[0] = static_cast<char>(ServerToClientMessage::PingResponse);
  mango::ustore16(data + 1, msg.size());
  
  return msg;
}
