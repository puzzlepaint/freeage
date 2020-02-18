#include <filesystem>
#include <iostream>
#include <unordered_map>

#include <stdio.h>
#include <vector>

#include <QApplication>
#include <QFontDatabase>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QOpenGLWidget>
#include <QPushButton>

#include "FreeAge/free_age.h"
#include "FreeAge/game_dialog.h"
#include "FreeAge/logging.h"
#include "FreeAge/map.h"
#include "FreeAge/render_window.h"
#include "FreeAge/settings_dialog.h"

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
      QMessageBox::warning(nullptr, QObject::tr("Error"), QObject::tr("The common resources path (%1) does not exist. Exiting.").arg(QString::fromStdString(commonResourcesPath)));
      return 1;
    }
    
    // Load palettes (to get the player colors).
    if (!ReadPalettesConf((commonResourcesPath / "palettes" / "palettes.conf").c_str(), &palettes)) {
      QMessageBox::warning(nullptr, QObject::tr("Error"), QObject::tr("Failed to load palettes. Exiting."));
      return 1;
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
      QMessageBox::warning(nullptr, QObject::tr("Error"), QObject::tr("Failed to load the Georgia font from %1. Exiting.").arg(georgiaFontPath));
      return 1;
    }
    QFont georgiaFont = QFont(QFontDatabase::applicationFontFamilies(georgiaFontID)[0]);
    
    // Show the game dialog.
    std::vector<PlayerInMatch> playersInMatch;
    if (isHost) {
      playersInMatch.emplace_back(QString::fromStdString(settings.playerName), 0, 0);
    } else {
      // TODO: Get players in match
    }
    
    GameDialog gameDialog(isHost, playersInMatch, georgiaFont, playerColors);
    if (gameDialog.exec() == QDialog::Accepted) {
      // The game has been started.
      break;
    }
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
