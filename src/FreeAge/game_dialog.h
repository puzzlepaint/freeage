#pragma once

#include <QDialog>
#include <QTimer>

#include "FreeAge/free_age.h"

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
class GameDialog : public QDialog {
 public:
  GameDialog(
      bool isHost,
      QTcpSocket* socket,
      QByteArray* unparsedReceivedBuffer,
      QFont georgiaFont,
      const std::vector<QRgb>& playerColors,
      QWidget* parent = nullptr);
  
  inline bool ConnectionToServerLost() const { return connectionToServerLost; };
  inline bool GameWasAborted() const { return gameWasAborted; }
  
 private slots:
  void TryParseServerMessages();
  
  void CheckConnection();
  void SendPing(u64 number);
  void SendSettingsUpdate();
  void SendChat();
  
  void ReadyCheckChanged();
  
 private:
  void AddPlayerWidget(const PlayerInMatch& player);
  
  void HandlePingResponseMessage(const QByteArray& msg);
  void HandlePingNotifyMessage(const QByteArray& msg);
  void HandleSettingsUpdateBroadcast(const QByteArray& msg);
  void HandlePlayerListMessage(const QByteArray& msg, int len);
  void HandleChatBroadcastMessage(const QByteArray& msg, int len);
  
  std::vector<PlayerInMatch> playersInMatch;
  
  QVBoxLayout* playerListLayout;
  
  QCheckBox* allowJoinCheck;
  QLineEdit* mapSizeEdit;
  
  QTextEdit* chatDisplay;
  QLineEdit* chatEdit;
  QPushButton* chatButton;
  
  QLabel* pingLabel;
  typedef std::chrono::steady_clock Clock;
  typedef Clock::time_point TimePoint;
  TimePoint lastPingTime;
  QTimer connectionCheckTimer;
  
  QCheckBox* readyCheck;
  
  bool connectionToServerLost = false;
  bool gameWasAborted = false;
  
  // Resources
  QFont georgiaFont;
  std::vector<QRgb> playerColors;
  
  // Socket for communicating with the server.
  QTcpSocket* socket;
  QByteArray* unparsedReceivedBuffer;
};
