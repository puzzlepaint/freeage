#include "FreeAge/game_dialog.h"

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

#include "FreeAge/free_age.h"
#include "FreeAge/logging.h"
#include "FreeAge/messages.h"

GameDialog::GameDialog(
    bool isHost,
    QTcpSocket* socket,
    QByteArray* unparsedReceivedBuffer,
    QFont georgiaFont,
    const std::vector<QRgb>& playerColors,
    QWidget* parent)
    : QDialog(parent),
      georgiaFont(georgiaFont),
      playerColors(playerColors),
      socket(socket),
      unparsedReceivedBuffer(unparsedReceivedBuffer) {
  setWindowIcon(QIcon(":/free_age/free_age.png"));
  setWindowTitle(tr("FreeAge"));
  
  // Players
  playerListLayout = new QVBoxLayout();
  playerListLayout->setContentsMargins(0, 0, 0, 0);
  playerListLayout->setSpacing(2);
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
  mapSizeEdit = new QLineEdit("50");
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
  lastPingTime = Clock::now();
  
  connect(&connectionCheckTimer, &QTimer::timeout, this, &GameDialog::CheckConnection);
  connectionCheckTimer.start(500);
  
  QPushButton* exitButton = new QPushButton(isHost ? tr("Abort game") : tr("Leave game"));
  pingLabel = new QLabel("");
  readyCheck = new QCheckBox(tr("Ready"));
  
  QPushButton* startButton = nullptr;
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
  connect(socket, &QTcpSocket::readyRead, this, &GameDialog::TryParseServerMessages);
  TryParseServerMessages();
  
  connect(chatEdit, &QLineEdit::textChanged, [&]() {
    chatButton->setEnabled(!chatEdit->text().isEmpty());
  });
  connect(chatEdit, &QLineEdit::returnPressed, this, &GameDialog::SendChat);
  connect(chatButton, &QPushButton::clicked, this, &GameDialog::SendChat);
  
  connect(exitButton, &QPushButton::clicked, this, &QDialog::reject);
  connect(readyCheck, &QCheckBox::stateChanged, this, &GameDialog::ReadyCheckChanged);
}

QString ColorToHTML(const QRgb& color) {
  return QStringLiteral("%1%2%3")
      .arg(static_cast<uint>(qRed(color)), 2, 16, QLatin1Char('0'))
      .arg(static_cast<uint>(qGreen(color)), 2, 16, QLatin1Char('0'))
      .arg(static_cast<uint>(qBlue(color)), 2, 16, QLatin1Char('0'));
}

void GameDialog::TryParseServerMessages() {
  QByteArray& buffer = *unparsedReceivedBuffer;
  buffer += socket->readAll();
  
  while (true) {
    if (buffer.size() < 3) {
      return;
    }
    
    char* data = buffer.data();
    u16 msgLength = mango::uload16(data + 1);
    
    if (buffer.size() < msgLength) {
      return;
    }
    
    ServerToClientMessage msgType = static_cast<ServerToClientMessage>(data[0]);
    
    switch (msgType) {
    case ServerToClientMessage::Welcome:
      // We do not expect to get a(nother) welcome message, but we do not
      // treat it as an error either.
      LOG(WARNING) << "Received an extra welcome message";
      break;
    case ServerToClientMessage::SettingsUpdateBroadcast:
      HandleSettingsUpdateBroadcast(buffer);
      break;
    case ServerToClientMessage::GameAborted:
      LOG(INFO) << "Got game aborted message";
      gameWasAborted = true;
      connectionCheckTimer.stop();
      connectionToServerLost = false;
      reject();
      break;
    case ServerToClientMessage::PlayerList:
      HandlePlayerListMessage(buffer, msgLength);
      break;
    case ServerToClientMessage::ChatBroadcast:
      HandleChatBroadcastMessage(buffer, msgLength);
      break;
    case ServerToClientMessage::Ping:
      HandlePingMessage(buffer);
      break;
    case ServerToClientMessage::PingNotify:
      HandlePingNotifyMessage(buffer);
      break;
    }
    
    buffer.remove(0, msgLength);
  }
}

void GameDialog::CheckConnection() {
  constexpr int kNoPingTimeout = 5000;
  if (socket->state() != QTcpSocket::ConnectedState ||
      std::chrono::duration<double, std::milli>(Clock::now() - lastPingTime).count() > kNoPingTimeout) {
    connectionToServerLost = true;
    reject();
  }
}

void GameDialog::SendPingResponse(u64 number) {
  socket->write(CreatePingResponseMessage(number));
}

void GameDialog::SendSettingsUpdate() {
  socket->write(CreateSettingsUpdateMessage(allowJoinCheck->isChecked(), mapSizeEdit->text().toInt(), false));
}

void GameDialog::SendChat() {
  if (chatEdit->text().isEmpty()) {
    return;
  }
  
  socket->write(CreateChatMessage(chatEdit->text()));
  chatEdit->setText("");
  chatButton->setEnabled(false);
}

void GameDialog::ReadyCheckChanged() {
  socket->write(CreateReadyUpMessage(readyCheck->isChecked()));
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

void GameDialog::HandlePingMessage(const QByteArray& msg) {
  lastPingTime = Clock::now();
  
  u64 number = mango::uload64(msg.data() + 3);
  SendPingResponse(number);
}

void GameDialog::HandlePingNotifyMessage(const QByteArray& msg) {
  u16 milliseconds = mango::uload16(msg.data() + 3);
  pingLabel->setText(tr("Ping to server: %1").arg(milliseconds));
}

void GameDialog::HandleSettingsUpdateBroadcast(const QByteArray& msg) {
  bool allowMorePlayersToJoin = msg.data()[3] > 0;
  u16 mapSize = mango::uload16(msg.data() + 4);
  
  allowJoinCheck->setChecked(allowMorePlayersToJoin);
  mapSizeEdit->setText(QString::number(mapSize));
}

void GameDialog::HandlePlayerListMessage(const QByteArray& msg, int len) {
  LOG(INFO) << "Got player list message";
  
  // Parse the message to update playersInMatch
  playersInMatch.clear();
  int index = 3;
  while (index < len) {
    u16 nameLength = mango::uload16(msg.data() + index);
    index += 2;
    
    QString name = QString::fromUtf8(msg.mid(index, nameLength));
    index += nameLength;
    
    u16 playerColorIndex = mango::uload16(msg.data() + index);
    index += 2;
    
    bool playerIsReady = msg.data()[index] > 0;
    ++ index;
    
    playersInMatch.emplace_back(name, playerColorIndex, playerIsReady);
  }
  if (index != len) {
    LOG(WARNING) << "index != len after parsing a PlayerList message";
  }
  
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
}

void GameDialog::HandleChatBroadcastMessage(const QByteArray& msg, int len) {
  LOG(INFO) << "Got chat broadcast message";
  
  int index = 3;
  u16 sendingPlayerIndex = mango::uload16(msg.data() + index);
  index += 2;
  
  QString chatText = QString::fromUtf8(msg.mid(index, len - index));
  
  if (sendingPlayerIndex == std::numeric_limits<u16>::max()) {
    // Use the chatText without modification.
  } else if (sendingPlayerIndex >= playersInMatch.size()) {
    LOG(WARNING) << "Chat broadcast message has an out-of-bounds player index";
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
