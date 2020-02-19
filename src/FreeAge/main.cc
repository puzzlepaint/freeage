#include <filesystem>
#include <iostream>
#include <unordered_map>

#include <stdio.h>
#include <vector>

#include <mango/core/endian.hpp>
#include <QApplication>
#include <QDir>
#include <QFontDatabase>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QOpenGLWidget>
#include <QPushButton>
#include <QProcess>
#include <QTcpSocket>
#include <QThread>

#include "FreeAge/free_age.h"
#include "FreeAge/game_dialog.h"
#include "FreeAge/logging.h"
#include "FreeAge/map.h"
#include "FreeAge/messages.h"
#include "FreeAge/render_window.h"
#include "FreeAge/settings_dialog.h"

bool TryParseWelcomeMessage(QByteArray* buffer) {
  if (buffer->size() < 3) {
    return false;
  }
  
  if (buffer->at(0) == static_cast<char>(ServerToClientMessage::Welcome)) {
    u16 msgLength = mango::uload16(buffer->data() + 1);
    if (msgLength == 3) {
      buffer->remove(0, 3);
      return true;
    }
  }
  
  return false;
}

int main(int argc, char** argv) {
  // Seed the random number generator.
  srand(time(nullptr));
  
  // Initialize loguru.
  loguru::g_preamble_date = false;
  loguru::g_preamble_thread = false;
  loguru::g_preamble_uptime = false;
  loguru::g_stderr_verbosity = 2;
  if (argc > 0) {
    loguru::init(argc, argv, /*verbosity_flag*/ nullptr);
  }
  
  // Set the default OpenGL format *before* creating a QApplication.
  QSurfaceFormat format;
  // format.setDepthBufferSize(24);
  // format.setStencilBufferSize(8);
  format.setVersion(3, 2);
  format.setProfile(QSurfaceFormat::CoreProfile);
  QSurfaceFormat::setDefaultFormat(format);
  
  // Create and initialize a QApplication.
  QApplication qapp(argc, argv);
  QCoreApplication::setOrganizationName("FreeAge");
  QCoreApplication::setOrganizationDomain("free-age.org");
  QCoreApplication::setApplicationName("FreeAge");
  // We would like to get all input events immediately to be able to react quickly.
  // At least in Qt 5.12.0, there is some behavior that seems like a bug which sometimes
  // groups together mouse wheel events that are far apart in time if this setting is at its default.
  qapp.setAttribute(Qt::AA_CompressHighFrequencyEvents, false);
  
  // Resources to be loaded later.
  std::filesystem::path commonResourcesPath;
  Palettes palettes;
  QProcess serverProcess;
  
  // Socket for communicating with the server.
  QTcpSocket socket;
  socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);
  QByteArray unparsedReceivedBuffer;
  
  // Load settings.
  Settings settings;
  if (!settings.TryLoad()) {
    settings.InitializeWithDefaults();
  }
  
  // Settings loop.
  while (true) {
    // Show the settings dialog.
    SettingsDialog settingsDialog(&settings);
    if (settingsDialog.exec() == QDialog::Rejected) {
      return 0;
    }
    settings.Save();
    bool isHost = settingsDialog.HostGameChosen();
    
    // Load some initial basic game resources that are required for the game dialog.
    commonResourcesPath = std::filesystem::path(settingsDialog.GetDataPath().toStdString()) / "resources" / "_common";
    if (!std::filesystem::exists(commonResourcesPath)) {
      QMessageBox::warning(nullptr, QObject::tr("Error"), QObject::tr("The common resources path (%1) does not exist.").arg(QString::fromStdString(commonResourcesPath)));
      continue;
    }
    
    // Load palettes (to get the player colors).
    if (!ReadPalettesConf((commonResourcesPath / "palettes" / "palettes.conf").c_str(), &palettes)) {
      QMessageBox::warning(nullptr, QObject::tr("Error"), QObject::tr("Failed to load palettes."));
      continue;
    }
    
    // Extract the player colors.
    std::vector<QRgb> playerColors(8);
    for (int i = 0; i < 8; ++ i) {
      playerColors[i] = palettes.at(55 + i)[0];
    }
    
    // Load fonts (to use them in the dialog).
    std::filesystem::path fontsPath = commonResourcesPath / "fonts";
    QString georgiaFontPath = QString::fromStdString((fontsPath / "georgia.ttf").string());
    int georgiaFontID = QFontDatabase::addApplicationFont(georgiaFontPath);
    if (georgiaFontID == -1) {
      QMessageBox::warning(nullptr, QObject::tr("Error"), QObject::tr("Failed to load the Georgia font from %1.").arg(georgiaFontPath));
      continue;
    }
    QFont georgiaFont = QFont(QFontDatabase::applicationFontFamilies(georgiaFontID)[0]);
    
    // Start the server if being host, and in either case, try to connect to it.
    if (isHost) {
      // Start the server.
      QByteArray hostToken;
      hostToken.resize(hostTokenLength);
      for (int i = 0; i < hostTokenLength; ++ i) {
        hostToken[i] = 'a' + (rand() % ('z' + 1 - 'a'));
      }
      
      QString serverPath = QDir(qapp.applicationDirPath()).filePath("FreeAgeServer");
      serverProcess.start(serverPath, QStringList() << hostToken);
      if (!serverProcess.waitForStarted(10000)) {
        QMessageBox::warning(nullptr, QObject::tr("Error"), QObject::tr("Failed to start the server (path: %1).").arg(serverPath));
        QFontDatabase::removeApplicationFont(georgiaFontID);
        continue;
      }
      
      // For debugging, forward the server's output to our stdout:
      QObject::connect(&serverProcess, &QProcess::readyReadStandardOutput, [&]() {
        std::cout << serverProcess.readAllStandardOutput().data();
      });
      QObject::connect(&serverProcess, &QProcess::readyReadStandardError, [&]() {
        std::cout << serverProcess.readAllStandardError().data();
      });
      
      // Connect to the server.
      socket.connectToHost("127.0.0.1", serverPort, QIODevice::ReadWrite);
      // socket.setLocalPort(TODO);
      TimePoint connectionStartTime = Clock::now();
      constexpr int kConnectTimeout = 5000;
      while (socket.state() != QAbstractSocket::ConnectedState &&
             std::chrono::duration<double, std::milli>(Clock::now() - connectionStartTime).count() <= kConnectTimeout) {
        qapp.processEvents(QEventLoop::AllEvents);
        QThread::msleep(1);
        
        if (socket.state() == QAbstractSocket::UnconnectedState) {
          // Retry connecting.
          socket.connectToHost("127.0.0.1", serverPort, QIODevice::ReadWrite);
        }
      }
      if (socket.state() != QAbstractSocket::ConnectedState) {
        QMessageBox::warning(nullptr, QObject::tr("Error"), QObject::tr("Failed to connect to the server."));
        QFontDatabase::removeApplicationFont(georgiaFontID);
        continue;
      }
      
      // Send the HostConnect message.
      socket.write(CreateHostConnectMessage(hostToken, settings.playerName));
    } else {
      // Try to connect to the server.
      socket.connectToHost(settingsDialog.GetIP(), serverPort, QIODevice::ReadWrite);
      // socket.setLocalPort(TODO);
      TimePoint connectionStartTime = Clock::now();
      constexpr int kConnectTimeout = 5000;
      while (socket.state() != QAbstractSocket::ConnectedState &&
             std::chrono::duration<double, std::milli>(Clock::now() - connectionStartTime).count() <= kConnectTimeout) {
        qapp.processEvents(QEventLoop::AllEvents);
        QThread::msleep(1);
      }
      if (socket.state() != QAbstractSocket::ConnectedState) {
        QMessageBox::warning(nullptr, QObject::tr("Error"), QObject::tr("Failed to connect to the server."));
        QFontDatabase::removeApplicationFont(georgiaFontID);
        continue;
      }
      
      // Send the Connect message.
      socket.write(CreateConnectMessage(settings.playerName));
    }
    
    socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);
    
    // Wait for the server's welcome message.
    TimePoint welcomeWaitStartTime = Clock::now();
    constexpr int kWelcomeWaitTimeout = 5000;
    bool welcomeReceived = false;
    while (std::chrono::duration<double, std::milli>(Clock::now() - welcomeWaitStartTime).count() <= kWelcomeWaitTimeout) {
      unparsedReceivedBuffer += socket.readAll();
      if (TryParseWelcomeMessage(&unparsedReceivedBuffer)) {
        welcomeReceived = true;
        break;
      }
      
      qapp.processEvents(QEventLoop::AllEvents);
      QThread::msleep(1);
    }
    if (!welcomeReceived) {
      QMessageBox::warning(nullptr, QObject::tr("Error"), QObject::tr("Did not receive the welcome message from the server."));
      QFontDatabase::removeApplicationFont(georgiaFontID);
      if (isHost) {
        serverProcess.terminate();
      }
      continue;
    }
    
    // Show the game dialog.
    GameDialog gameDialog(isHost, &socket, &unparsedReceivedBuffer, georgiaFont, playerColors);
    if (gameDialog.exec() == QDialog::Accepted) {
      // The game has been started.
      break;
    }
    
    // The game dialog was canceled.
    if (!gameDialog.GameWasAborted()) {
      socket.write(CreateLeaveMessage());
    }
    
    // If we are the host, shut down the server.
    if (isHost) {
      // The leave message to the server will make it notify all
      // other clients that the match was aborted, and exit. Wait for this
      // to happen.
      TimePoint exitWaitStartTime = Clock::now();
      constexpr int kExitWaitTimeout = 1000;
      while (serverProcess.state() != QProcess::NotRunning &&
             std::chrono::duration<double, std::milli>(Clock::now() - exitWaitStartTime).count() <= kExitWaitTimeout) {
        qapp.processEvents(QEventLoop::AllEvents);
        QThread::msleep(1);
      }
      
      // If the server did not exit in time, terminate it.
      if (serverProcess.state() != QProcess::NotRunning) {
        serverProcess.terminate();
      }
    } else if (gameDialog.GameWasAborted()) {
      QMessageBox::information(nullptr, QObject::tr("Game cancelled"), QObject::tr("The game was cancelled by the host."));
    }
    
    if (gameDialog.ConnectionToServerLost()) {
      QMessageBox::information(nullptr, QObject::tr("Game cancelled"), QObject::tr("The connection to the server was lost."));
    }
    
    QFontDatabase::removeApplicationFont(georgiaFontID);
  }
  
  // Start the game.
  // TODO
  
  // Get the graphics path
  std::filesystem::path graphicsPath = commonResourcesPath / "drs" / "graphics";
  std::filesystem::path cachePath = std::filesystem::path(argv[0]).parent_path() / "graphics_cache";
  if (!std::filesystem::exists(cachePath)) {
    std::filesystem::create_directories(cachePath);
  }
  
  
  // Create an OpenGL render window using Qt
  RenderWindow renderWindow(palettes, graphicsPath, cachePath);
  renderWindow.show();
  
  // Run the Qt event loop
  qapp.exec();
  return 0;
}
