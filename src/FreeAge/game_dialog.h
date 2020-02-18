#pragma once

#include <QDialog>

class QVBoxLayout;

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
  GameDialog(bool isHost, QFont georgiaFont, const std::vector<QRgb>& playerColors, QWidget* parent = nullptr);
  
 private:
  void AddPlayerWidget(const PlayerInMatch& player);
  
  std::vector<PlayerInMatch> playersInMatch;
  
  QFont georgiaFont;
  std::vector<QRgb> playerColors;
  QVBoxLayout* playerListLayout;
};
