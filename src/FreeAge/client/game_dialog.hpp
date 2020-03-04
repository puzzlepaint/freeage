#pragma once

#include <QDialog>
#include <QTimer>

#include "FreeAge/common/free_age.hpp"
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
class GameDialog : public QDialog {
 public:
  GameDialog(
      bool isHost,
      ServerConnection* connection,
      QFont georgiaFont,
      const std::vector<QRgb>& playerColors,
      QWidget* parent = nullptr);
  
  ~GameDialog();
  
  inline bool GameWasAborted() const { return gameWasAborted; }
  
 private slots:
  void TryParseServerMessage(const QByteArray& buffer, ServerToClientMessage msgType, u16 msgLength);
  void NewPingMeasurement(int milliseconds);
  
  void SendPing(u64 number);
  void SendSettingsUpdate();
  void SendChat();
  
  void ReadyCheckChanged();
  
  void StartGame();
  
 private:
  void AddPlayerWidget(const PlayerInMatch& player);
  
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
  
  QCheckBox* readyCheck;
  
  bool gameWasAborted = false;
  
  // Resources
  QFont georgiaFont;
  std::vector<QRgb> playerColors;
  
  ServerConnection* connection;
};
