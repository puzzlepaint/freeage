#pragma once

#include <QDialog>

class QLineEdit;
class QVBoxLayout;
class QTcpSocket;
class QTextEdit;

struct PlayerInMatch {
  inline PlayerInMatch(const QString& name, int playerColorIndex)
      : name(name),
        playerColorIndex(playerColorIndex) {}
  
  QString name;
  int playerColorIndex;
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
  
  inline bool GameWasAborted() const { return gameWasAborted; }
  
 private slots:
  void TryParseServerMessages();
  
  void SendChat();
  
 private:
  void AddPlayerWidget(const PlayerInMatch& player);
  
  void HandlePlayerListMessage(const QByteArray& msg, int len);
  void HandleChatBroadcastMessage(const QByteArray& msg, int len);
  
  std::vector<PlayerInMatch> playersInMatch;
  
  QVBoxLayout* playerListLayout;
  
  QTextEdit* chatDisplay;
  QLineEdit* chatEdit;
  QPushButton* chatButton;
  
  bool gameWasAborted = false;
  
  // Resources
  QFont georgiaFont;
  std::vector<QRgb> playerColors;
  
  // Socket for communicating with the server.
  QTcpSocket* socket;
  QByteArray* unparsedReceivedBuffer;
};
