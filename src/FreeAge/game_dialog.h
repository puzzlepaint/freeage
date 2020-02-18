#pragma once

#include <QDialog>

class QVBoxLayout;

struct PlayerInMatch {
  inline PlayerInMatch(const QString& name, int playerColorIndex, int ping)
      : name(name),
        playerColorIndex(playerColorIndex),
        ping(ping) {}
  
  QString name;
  int playerColorIndex;
  int ping;
};

/// Dialog showing the players that joined the match, the map type, etc.,
/// allowing to start the game once all players are ready.
class GameDialog : public QDialog {
 public:
  GameDialog(bool isHost, const std::vector<PlayerInMatch>& playersInMatch, QFont georgiaFont, const std::vector<QRgb>& playerColors, QWidget* parent = nullptr);
  
 private:
  void AddPlayerWidget(const PlayerInMatch& player);
  
  std::vector<PlayerInMatch> playersInMatch;
  
  QFont georgiaFont;
  std::vector<QRgb> playerColors;
  QVBoxLayout* playerListLayout;
};
