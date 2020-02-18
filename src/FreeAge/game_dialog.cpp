#include "FreeAge/game_dialog.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QGroupBox>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

GameDialog::GameDialog(bool isHost, const std::vector<PlayerInMatch>& playersInMatch, QFont georgiaFont, const std::vector<QRgb>& playerColors, QWidget* parent)
    : QDialog(parent),
      playersInMatch(playersInMatch),
      georgiaFont(georgiaFont),
      playerColors(playerColors) {
  // setWindowIcon(TODO);
  setWindowTitle(tr("FreeAge"));
  
  // Players
  playerListLayout = new QVBoxLayout();
  QGroupBox* playersGroup = new QGroupBox(tr("Players"));
  playersGroup->setLayout(playerListLayout);
  
  QCheckBox* allowJoinCheck = new QCheckBox(tr("Allow more players to join"));
  allowJoinCheck->setChecked(true);
  QPushButton* addAIButton = new QPushButton(tr("Add AI"));
  
  QHBoxLayout* playerToolsLayout = new QHBoxLayout();
  playerToolsLayout->addWidget(allowJoinCheck);
  playerToolsLayout->addStretch(1);
  playerToolsLayout->addWidget(addAIButton);
  
  QVBoxLayout* playersLayout = new QVBoxLayout();
  playersLayout->addWidget(playersGroup);
  playersLayout->addLayout(playerToolsLayout);
  playersLayout->addLayout(playerListLayout);
  
  // Settings
  QLabel* mapSizeLabel = new QLabel(tr("Map size: "));
  QLineEdit* mapSizeEdit = new QLineEdit("50");
  mapSizeEdit->setValidator(new QIntValidator(10, 99999, mapSizeEdit));
  QHBoxLayout* mapSizeLayout = new QHBoxLayout();
  mapSizeLayout->addWidget(mapSizeLabel);
  mapSizeLayout->addWidget(mapSizeEdit);
  
  QVBoxLayout* settingsLayout = new QVBoxLayout();
  settingsLayout->addLayout(mapSizeLayout);
  settingsLayout->addStretch(1);
  
  QGroupBox* settingsGroup = new QGroupBox(tr("Settings"));
  settingsGroup->setLayout(settingsLayout);
  
  // Main layout
  QHBoxLayout* mainLayout = new QHBoxLayout();
  mainLayout->addLayout(playersLayout, 2);
  mainLayout->addWidget(settingsGroup, 1);
  
  QHBoxLayout* buttonsLayout = new QHBoxLayout();
  QPushButton* exitButton = new QPushButton(isHost ? tr("Abort game") : tr("Leave game"));
  buttonsLayout->addWidget(exitButton);
  buttonsLayout->addStretch(1);
  QCheckBox* readyCheck = new QCheckBox(tr("Ready"));
  buttonsLayout->addWidget(readyCheck);
  if (isHost) {
    QPushButton* startButton = new QPushButton(tr("Start"));
    startButton->setEnabled(false);
    buttonsLayout->addWidget(startButton);
  }
  
  QVBoxLayout* layout = new QVBoxLayout();
  layout->addLayout(mainLayout);
  layout->addLayout(buttonsLayout);
  setLayout(layout);
  
  resize(std::max(800, width()), std::max(480, height()));
  
  // --- Connections ---
  connect(exitButton, &QPushButton::clicked, this, &QDialog::reject);
  
  for (const auto& playerInMatch : playersInMatch) {
    AddPlayerWidget(playerInMatch);
  }
  playerListLayout->addStretch(1);
}

QString ColorToHTML(const QRgb& color) {
  return QStringLiteral("%1%2%3")
      .arg(static_cast<uint>(qRed(color)), 2, 16, QLatin1Char('0'))
      .arg(static_cast<uint>(qGreen(color)), 2, 16, QLatin1Char('0'))
      .arg(static_cast<uint>(qBlue(color)), 2, 16, QLatin1Char('0'));
}

void GameDialog::AddPlayerWidget(const PlayerInMatch& player) {
  QWidget* playerWidget = new QWidget();
  QPalette palette = playerWidget->palette();
  palette.setColor(QPalette::Background, qRgb(127, 127, 127));
  playerWidget->setPalette(palette);
  
  QLabel* playerColorLabel = new QLabel(QString::number(player.playerColorIndex + 1));
  QString playerColorHtml = ColorToHTML(playerColors[player.playerColorIndex % playerColors.size()]);
  playerColorLabel->setStyleSheet(QStringLiteral("QLabel{border-radius:5px;background-color:#%1;padding:5px;}").arg(playerColorHtml));
  playerColorLabel->setFont(georgiaFont);
  
  QLabel* playerNameLabel = new QLabel(player.name);
  playerNameLabel->setFont(georgiaFont);
  
  QLabel* pingLabel = new QLabel((player.ping > 0) ? tr("ping: %1").arg(player.ping) : "");
  pingLabel->setFont(georgiaFont);
  
  QHBoxLayout* layout = new QHBoxLayout();
  layout->addWidget(playerColorLabel);
  layout->addWidget(playerNameLabel);
  layout->addStretch(1);
  layout->addWidget(pingLabel);
  playerWidget->setLayout(layout);
  
  playerListLayout->addWidget(playerWidget);
}
