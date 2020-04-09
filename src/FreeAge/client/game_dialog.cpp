// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/client/game_dialog.hpp"

#include <mango/core/endian.hpp>
#include <QBoxLayout>
#include <QCheckBox>
#include <QGroupBox>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTcpSocket>
#include <QTextEdit>

#include "FreeAge/common/free_age.hpp"
#include "FreeAge/common/logging.hpp"
#include "FreeAge/common/messages.hpp"

GameDialog::GameDialog(
    bool isHost,
    ServerConnection* connection,
    QFont georgiaFont,
    const std::vector<QRgb>& playerColors,
    QWidget* parent)
    : QDialog(parent),
      georgiaFont(georgiaFont),
      playerColors(playerColors),
      connection(connection) {
  setWindowIcon(QIcon(":/free_age/free_age.png"));
  setWindowTitle(tr("FreeAge"));
  
  // Players
  playerListLayout = new QVBoxLayout();
  playerListLayout->setContentsMargins(0, 0, 0, 0);
  playerListLayout->setSpacing(0);
  QGroupBox* playersGroup = new QGroupBox(tr("Players"));
  playersGroup->setLayout(playerListLayout);
  
  QHBoxLayout* playerToolsLayout = nullptr;
  
  allowJoinCheck = new QCheckBox(isHost ? tr("Allow more players to join") : tr("More players are allowed to join"));
  allowJoinCheck->setChecked(true);
  allowJoinCheck->setEnabled(isHost);
  // TODO: Add AI button again
  // QPushButton* addAIButton = new QPushButton(tr("Add AI"));
  
  playerToolsLayout = new QHBoxLayout();
  playerToolsLayout->addWidget(allowJoinCheck);
  playerToolsLayout->addStretch(1);
  // playerToolsLayout->addWidget(addAIButton);
  
  if (isHost) {
    // --- Connections ---
    connect(allowJoinCheck, &QCheckBox::stateChanged, this, &GameDialog::SendSettingsUpdate);
  }
  
  QVBoxLayout* playersLayout = new QVBoxLayout();
  playersLayout->addWidget(playersGroup);
  playersLayout->addLayout(playerToolsLayout);
  
  // Settings
  QLabel* mapSizeLabel = new QLabel(tr("Map size: "));
  mapSizeEdit = new QLineEdit("75");  // TODO: Make sure that this is the same default as on the server (settings.hpp); or make the host send a settings update right at the start.
  if (isHost) {
    mapSizeEdit->setValidator(new QIntValidator(10, 99999, mapSizeEdit));
    
    // --- Connections ---
    connect(mapSizeEdit, &QLineEdit::textChanged, this, &GameDialog::SendSettingsUpdate);
  } else {
    mapSizeEdit->setEnabled(false);
  }
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
  
  // Chat layout
  chatDisplay = new QTextEdit();
  chatDisplay->setReadOnly(true);
  
  QLabel* chatLabel = new QLabel(tr("Chat: "));
  chatEdit = new QLineEdit();
  chatButton = new QPushButton(tr("Send"));
  chatButton->setEnabled(false);
  QHBoxLayout* chatLineLayout = new QHBoxLayout();
  chatLineLayout->addWidget(chatLabel);
  chatLineLayout->addWidget(chatEdit, 1);
  chatLineLayout->addWidget(chatButton);
  
  // Bottom row with buttons, ping label and ready check
  QPushButton* exitButton = new QPushButton(isHost ? tr("Abort game") : tr("Leave game"));
  pingLabel = new QLabel("");
  readyCheck = new QCheckBox(tr("Ready"));
  
  startButton = nullptr;
  if (isHost) {
    startButton = new QPushButton(tr("Start"));
    startButton->setEnabled(false);
  }
  
  QHBoxLayout* buttonsLayout = new QHBoxLayout();
  buttonsLayout->addWidget(exitButton);
  buttonsLayout->addWidget(pingLabel);
  buttonsLayout->addStretch(1);
  buttonsLayout->addWidget(readyCheck);
  if (isHost) {
    buttonsLayout->addWidget(startButton);
  }
  
  QVBoxLayout* layout = new QVBoxLayout();
  layout->addLayout(mainLayout, 2);
  layout->addWidget(chatDisplay, 1);
  layout->addLayout(chatLineLayout);
  layout->addLayout(buttonsLayout);
  setLayout(layout);
  
  resize(std::max(800, width()), std::max(600, height()));
  
  // --- Connections ---
  connect(connection, &ServerConnection::NewPingMeasurement, this, &GameDialog::NewPingMeasurement);
  connect(connection, &ServerConnection::NewMessage, this, &GameDialog::TryParseServerMessages);
  TryParseServerMessages();
  
  connect(chatEdit, &QLineEdit::textChanged, [&]() {
    chatButton->setEnabled(!chatEdit->text().isEmpty());
  });
  connect(chatEdit, &QLineEdit::returnPressed, this, &GameDialog::SendChat);
  connect(chatButton, &QPushButton::clicked, this, &GameDialog::SendChat);
  
  connect(exitButton, &QPushButton::clicked, this, &QDialog::reject);
  connect(readyCheck, &QCheckBox::stateChanged, this, &GameDialog::ReadyCheckChanged);
  if (isHost) {
    connect(startButton, &QPushButton::clicked, this, &GameDialog::StartGame);
  }
}

void GameDialog::GetPlayerList(Match* match) {
  std::vector<Match::Player> players;
  for (const auto& player : playersInMatch) {
    players.emplace_back();
    auto& newPlayer = players.back();
    newPlayer.name = player.name;
    newPlayer.playerColorIndex = player.playerColorIndex;
  }
  match->SetPlayerInfo(players, playerIndexInList);
}

QString ColorToHTML(const QRgb& color) {
  return QStringLiteral("%1%2%3")
      .arg(static_cast<uint>(qRed(color)), 2, 16, QLatin1Char('0'))
      .arg(static_cast<uint>(qGreen(color)), 2, 16, QLatin1Char('0'))
      .arg(static_cast<uint>(qBlue(color)), 2, 16, QLatin1Char('0'));
}

void GameDialog::TryParseServerMessages() {
  connection->Lock();
  
  auto* messages = connection->GetReceivedMessages();
  for (const ReceivedMessage& msg : *messages) {
    switch (msg.type) {
    case ServerToClientMessage::Welcome:
      // We do not expect to get a(nother) welcome message, but we do not
      // treat it as an error either.
      LOG(WARNING) << "Received an extra welcome message";
      break;
    case ServerToClientMessage::SettingsUpdateBroadcast:
      HandleSettingsUpdateBroadcast(msg.data);
      break;
    case ServerToClientMessage::GameAborted:
      LOG(INFO) << "Got game aborted message";
      gameWasAborted = true;
      connection->Shutdown();
      reject();
      break;
    case ServerToClientMessage::PlayerList:
      HandlePlayerListMessage(msg.data);
      break;
    case ServerToClientMessage::ChatBroadcast:
      HandleChatBroadcastMessage(msg.data);
      break;
    case ServerToClientMessage::StartGameBroadcast:
      accept();
      break;
    default:;
    }
  }
  messages->clear();
  
  connection->Unlock();
}

void GameDialog::NewPingMeasurement(int milliseconds) {
  pingLabel->setText(tr("Ping to server: %1").arg(milliseconds));
}

void GameDialog::SendPing(u64 number) {
  connection->Write(CreatePingMessage(number));
}

void GameDialog::SendSettingsUpdate() {
  connection->Write(CreateSettingsUpdateMessage(allowJoinCheck->isChecked(), mapSizeEdit->text().toInt(), false));
}

void GameDialog::SendChat() {
  if (chatEdit->text().isEmpty()) {
    return;
  }
  
  connection->Write(CreateChatMessage(chatEdit->text()));
  chatEdit->setText("");
  chatButton->setEnabled(false);
}

void GameDialog::ReadyCheckChanged() {
  connection->Write(CreateReadyUpMessage(readyCheck->isChecked()));
}

void GameDialog::StartGame() {
  connection->Write(CreateStartGameMessage());
}

void GameDialog::AddPlayerWidget(const PlayerInMatch& player) {
  QWidget* playerWidget = new QWidget();
  QPalette palette = playerWidget->palette();
  palette.setColor(QPalette::Background, qRgb(127, 127, 127));
  playerWidget->setPalette(palette);
  
  QLabel* playerColorLabel = new QLabel(QString::number(player.playerColorIndex + 1));
  QRgb playerColor = playerColors[player.playerColorIndex % playerColors.size()];
  QString playerColorHtml = ColorToHTML(playerColor);
  QString invPlayerColorHtml = ColorToHTML(qRgb(255 - qRed(playerColor), 255 - qGreen(playerColor), 255 - qBlue(playerColor)));
  playerColorLabel->setStyleSheet(QStringLiteral("QLabel{border-radius:5px;background-color:#%1;color:#%2;padding:5px;}").arg(playerColorHtml).arg(invPlayerColorHtml));
  playerColorLabel->setFont(georgiaFont);
  
  QLabel* playerNameLabel = new QLabel(player.name);
  playerNameLabel->setFont(georgiaFont);
  
  QHBoxLayout* layout = new QHBoxLayout();
  layout->addWidget(playerColorLabel);
  layout->addWidget(playerNameLabel);
  layout->addStretch(1);
  if (player.isReady) {
    QLabel* readyLabel = new QLabel(QChar(0x2713));
    layout->addWidget(readyLabel);
  }
  playerWidget->setLayout(layout);
  
  playerListLayout->addWidget(playerWidget);
}

void GameDialog::HandleSettingsUpdateBroadcast(const QByteArray& msg) {
  if (msg.size() < 3) {
    LOG(ERROR) << "Received a too short SettingsUpdateBroadcast message";
    return;
  }
  
  bool allowMorePlayersToJoin = msg.data()[0] > 0;
  u16 mapSize = mango::uload16(msg.data() + 1);
  
  allowJoinCheck->setChecked(allowMorePlayersToJoin);
  mapSizeEdit->setText(QString::number(mapSize));
}

void GameDialog::HandlePlayerListMessage(const QByteArray& msg) {
  LOG(INFO) << "Got player list message";
  
  // Parse the message to update playersInMatch
  bool allPlayersAreReady = true;
  playersInMatch.clear();
  int index = 1;
  while (index < msg.size()) {
    if (index + 2 > msg.size()) {
      LOG(ERROR) << "Received PlayerList message ends unexpectedly (1)";
      return;
    }
    u16 nameLength = mango::uload16(msg.data() + index);
    index += 2;
    
    if (index + nameLength > msg.size()) {
      LOG(ERROR) << "Received PlayerList message ends unexpectedly (2)";
      return;
    }
    QString name = QString::fromUtf8(msg.mid(index, nameLength));
    index += nameLength;
    
    if (index + 2 > msg.size()) {
      LOG(ERROR) << "Received PlayerList message ends unexpectedly (3)";
      return;
    }
    u16 playerColorIndex = mango::uload16(msg.data() + index);
    index += 2;
    
    if (index + 1 > msg.size()) {
      LOG(ERROR) << "Received PlayerList message ends unexpectedly (4)";
      return;
    }
    bool playerIsReady = msg.data()[index] > 0;
    allPlayersAreReady &= playerIsReady;
    ++ index;
    
    playersInMatch.emplace_back(name, playerColorIndex, playerIsReady);
  }
  if (playersInMatch.empty()) {
    LOG(ERROR) << "Received PlayerList message with empty player list";
    return;
  }
  if (index != msg.size()) {
    LOG(ERROR) << "index != len after parsing a PlayerList message";
  }
  
  playerIndexInList = std::max<int>(0, std::min<int>(msg.data()[0], playersInMatch.size() - 1));
  
  // Remove all items from playerListLayout
  while (QLayoutItem* item = playerListLayout->takeAt(0)) {
    Q_ASSERT(!item->layout());  // otherwise the layout will leak
    delete item->widget();
    delete item;
  }
  
  LOG(INFO) << "- number of players in list: " << playersInMatch.size();
  
  // Create new items for all playersInMatch
  for (const auto& playerInMatch : playersInMatch) {
    AddPlayerWidget(playerInMatch);
  }
  playerListLayout->addStretch(1);
  
  // For the host, enable the start button if all players are ready
  if (startButton) {
    startButton->setEnabled(allPlayersAreReady);
  }
}

void GameDialog::HandleChatBroadcastMessage(const QByteArray& msg) {
  if (msg.size() < 2) {
    LOG(ERROR) << "Received a too short ChatBroadcast message";
    return;
  }
  
  int index = 0;
  u16 sendingPlayerIndex = mango::uload16(msg.data() + index);
  index += 2;
  
  QString chatText = QString::fromUtf8(msg.mid(index, msg.size() - index));
  
  if (sendingPlayerIndex == std::numeric_limits<u16>::max()) {
    // Use the chatText without modification.
  } else if (sendingPlayerIndex >= playersInMatch.size()) {
    LOG(ERROR) << "Chat broadcast message has an out-of-bounds player index";
    chatText = tr("???: %1").arg(chatText);
  } else {
    const PlayerInMatch& sendingPlayer = playersInMatch[sendingPlayerIndex];
    chatText = tr("<span style=\"color:#%1\">[%2] %3: %4</span>")
        .arg(ColorToHTML(playerColors[sendingPlayer.playerColorIndex]))
        .arg(sendingPlayer.playerColorIndex + 1)
        .arg(sendingPlayer.name)
        .arg(chatText);
  }
  
  chatDisplay->append(chatText);
}
