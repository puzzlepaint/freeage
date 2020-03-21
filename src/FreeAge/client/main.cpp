#include <filesystem>
#include <iostream>
#include <unordered_map>

#include <stdio.h>
#include <vector>

#include <QApplication>
#include <QCommandLineParser>
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

#include "FreeAge/common/free_age.hpp"
#include "FreeAge/client/game_dialog.hpp"
#include "FreeAge/client/game_controller.hpp"
#include "FreeAge/common/logging.hpp"
#include "FreeAge/client/map.hpp"
#include "FreeAge/client/match.hpp"
#include "FreeAge/common/messages.hpp"
#include "FreeAge/client/render_window.hpp"
#include "FreeAge/client/server_connection.hpp"
#include "FreeAge/client/settings_dialog.hpp"

// TODO (puzzlepaint): For some reason, this include needed to be at the end using clang-10-rc2 on my laptop to not cause weird errors in CIDE. Why?
#include <mango/core/endian.hpp>

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
  int georgiaFontID;
  
  // Communication with the server.
  std::shared_ptr<ServerConnection> connection(new ServerConnection());
  
  // Parse command line options.
  QCommandLineParser parser;
  
  QCommandLineOption noServerOption("no-server", QObject::tr("Do not start a server when hosting, but connect to an existing server process instead using the host token 'aaaaaa'."));
  parser.addOption(noServerOption);
  
  QCommandLineOption playerOption("player", QObject::tr("Sets the initial player name"), QObject::tr("Player name"));
  parser.addOption(playerOption);
  
  parser.process(qapp);
  
  bool noServer = parser.isSet(noServerOption);
  QString initialPlayerName = parser.value(playerOption);
  
  // Load settings.
  Settings settings;
  if (!settings.TryLoad()) {
    settings.InitializeWithDefaults();
  }
  if (!initialPlayerName.isEmpty()) {
    settings.playerName = initialPlayerName;
  }
  
  // Match info, later returned by the game dialog.
  std::shared_ptr<Match> match(new Match());
  
  // Settings loop.
  while (true) {
    // Show the settings dialog.
    SettingsDialog settingsDialog(&settings);
    if (settingsDialog.exec() == QDialog::Rejected) {
      return 0;
    }
    settings.Save();
    bool isHost = settingsDialog.HostGameChosen();
    
    connection->SetDebugNetworking(settings.debugNetworking);
    
    // Load some initial basic game resources that are required for the game dialog.
    commonResourcesPath = std::filesystem::path(settingsDialog.GetDataPath().toStdString()) / "resources" / "_common";
    if (!std::filesystem::exists(commonResourcesPath)) {
      QMessageBox::warning(nullptr, QObject::tr("Error"), QObject::tr("The common resources path (%1) does not exist.").arg(QString::fromStdString(commonResourcesPath.string())));
      continue;
    }
    
    // Load palettes (to get the player colors).
    if (!ReadPalettesConf((commonResourcesPath / "palettes" / "palettes.conf").string().c_str(), &palettes)) {
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
    georgiaFontID = QFontDatabase::addApplicationFont(georgiaFontPath);
    if (georgiaFontID == -1) {
      QMessageBox::warning(nullptr, QObject::tr("Error"), QObject::tr("Failed to load the Georgia font from %1.").arg(georgiaFontPath));
      continue;
    }
    QFont georgiaFont = QFont(QFontDatabase::applicationFontFamilies(georgiaFontID)[0]);
    
    // Start the server if being host, and in either case, try to connect to it.
    if (isHost) {
      // Start the server.
      QByteArray hostToken;
      if (noServer) {
        hostToken = "aaaaaa";
      } else {
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
      }
      
      // Connect to the server.
      constexpr int kConnectTimeout = 5000;
      if (!connection->ConnectToServer("127.0.0.1", kConnectTimeout, /*retryUntilTimeout*/ true)) {
        QMessageBox::warning(nullptr, QObject::tr("Error"), QObject::tr("Failed to connect to the server."));
        QFontDatabase::removeApplicationFont(georgiaFontID);
        continue;
      }
      
      // Send the HostConnect message.
      connection->Write(CreateHostConnectMessage(hostToken, settings.playerName));
    } else {
      // Try to connect to the server.
      constexpr int kConnectTimeout = 5000;
      if (!connection->ConnectToServer(settingsDialog.GetIP(), kConnectTimeout, /*retryUntilTimeout*/ false)) {
        QMessageBox::warning(nullptr, QObject::tr("Error"), QObject::tr("Failed to connect to the server."));
        QFontDatabase::removeApplicationFont(georgiaFontID);
        continue;
      }
      
      // Send the Connect message.
      connection->Write(CreateConnectMessage(settings.playerName));
    }
    
    // Wait for the server's welcome message.
    constexpr int kWelcomeWaitTimeout = 5000;
    if (!connection->WaitForWelcomeMessage(kWelcomeWaitTimeout)) {
      QMessageBox::warning(nullptr, QObject::tr("Error"), QObject::tr("Did not receive a welcome message from the server."));
      QFontDatabase::removeApplicationFont(georgiaFontID);
      if (isHost) {
        serverProcess.terminate();
      }
      continue;
    }
    
    // Show the game dialog.
    // Note that the GameDialog object will parse ServerConnection messages as long as it exists.
    GameDialog gameDialog(isHost, connection.get(), georgiaFont, playerColors);
    if (gameDialog.exec() == QDialog::Accepted) {
      // The game has been started.
      gameDialog.GetPlayerList(match.get());
      break;
    }
    
    // The game dialog was canceled.
    if (!gameDialog.GameWasAborted()) {
      connection->Write(CreateLeaveMessage());
    }
    connection->Shutdown();
    
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
    
    if (connection->ConnectionToServerLost()) {
      QMessageBox::information(nullptr, QObject::tr("Game cancelled"), QObject::tr("The connection to the server was lost."));
    }
    
    QFontDatabase::removeApplicationFont(georgiaFontID);
  }
  
  // Create the game controller. It will start listening for network messages.
  std::shared_ptr<GameController> gameController(new GameController(match, connection, settings.debugNetworking));
  
  // Get the graphics path.
  std::filesystem::path graphicsPath = commonResourcesPath / "drs" / "graphics";
  std::filesystem::path cachePath = std::filesystem::path(argv[0]).parent_path() / "graphics_cache";
  if (!std::filesystem::exists(cachePath)) {
    std::filesystem::create_directories(cachePath);
  }
  
  // Create an OpenGL render window using Qt.
  std::shared_ptr<RenderWindow> renderWindow(new RenderWindow(match, gameController, connection, settings.uiScale, georgiaFontID, palettes, graphicsPath, cachePath));
  gameController->SetRenderWindow(renderWindow);
  if (settings.fullscreen) {
    renderWindow->showFullScreen();
    renderWindow->EnableBorderScrolling(true);
  } else {
    renderWindow->show();
  }
  
  // Run the Qt event loop.
  qapp.exec();
  
  // Manually delete the render window before the ClientUnitType or ClientBuildingType singleton vectors
  // are deleted automatically (without an active OpenGL context, causing this to fail).
  // We have to delete the game controller first since it keeps a shared_ptr of the RenderWindow.
  gameController.reset();
  renderWindow->SetGameController(nullptr);
  renderWindow.reset();
  
  return 0;
}
