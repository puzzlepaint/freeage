// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <QDialog>
#include <QTimer>

#include "FreeAge/common/free_age.hpp"
#include "FreeAge/client/match.hpp"
#include "FreeAge/client/server_connection.hpp"

class QCheckBox;
class QLabel;
class QLineEdit;
class QTcpSocket;
class QTextEdit;
class QVBoxLayout;

struct PlayerInMatch {
  inline PlayerInMatch(const QString& name, int playerColorIndex, bool isReady)
      : name(name),
        playerColorIndex(playerColorIndex),
        isReady(isReady) {}
  
  QString name;
  int playerColorIndex;
  bool isReady;
};

/// Dialog showing the players that joined the match, the map type, etc.,
/// allowing to start the game once all players are ready.
class LobbyDialog : public QDialog {
 public:
  LobbyDialog(
      bool isHost,
      ServerConnection* connection,
      QFont georgiaFont,
      const std::vector<QRgb>& playerColors,
      QScreen** outScreen,
      QWidget* parent = nullptr);
  
  void GetPlayerList(Match* match);
  
  inline bool GameWasAborted() const { return gameWasAborted; }
  
 private slots:
  void TryParseServerMessages();
  void NewPingMeasurement(int milliseconds);
  
  void SendPing(u64 number);
  void SendSettingsUpdate();
  void SendChat();
  
  void ReadyCheckChanged();
  
  void StartGame();
  
 private:
  void AddPlayerWidget(const PlayerInMatch& player);
  
  void HandleSettingsUpdateBroadcast(const QByteArray& msg);
  void HandlePlayerListMessage(const QByteArray& msg);
  void HandleChatBroadcastMessage(const QByteArray& msg);
  
  std::vector<PlayerInMatch> playersInMatch;
  int playerIndexInList = -1;
  
  QVBoxLayout* playerListLayout;
  
  QCheckBox* allowJoinCheck;
  QLineEdit* mapSizeEdit;
  
  QTextEdit* chatDisplay;
  QLineEdit* chatEdit;
  QPushButton* chatButton;
  
  QLabel* pingLabel;
  
  QCheckBox* readyCheck;
  QPushButton* startButton;
  
  bool gameWasAborted = false;
  
  // Resources
  QFont georgiaFont;
  std::vector<QRgb> playerColors;
  
  ServerConnection* connection;  // not owned
  QScreen** outScreen;  // not owned
};
