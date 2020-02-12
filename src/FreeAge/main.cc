#include <filesystem>
#include <iostream>
#include <unordered_map>

#include <stdio.h>
#include <vector>

#include <QApplication>
#include <QImage>
#include <QLabel>
#include <QOpenGLWidget>
#include <QPushButton>

#include "FreeAge/free_age.h"
#include "FreeAge/logging.h"
#include "FreeAge/map.h"
#include "FreeAge/render_window.h"

int main(int argc, char** argv) {
  // Initialize loguru
  loguru::g_preamble_date = false;
  loguru::g_preamble_thread = false;
  loguru::g_preamble_uptime = false;
  loguru::g_stderr_verbosity = 2;
  if (argc > 0) {
    loguru::init(argc, argv, /*verbosity_flag*/ nullptr);
  }
  
  // Parse the command-line arguments
  if (argc != 2) {
    // For example:
    // ./FreeAge /home/thomas/.local/share/Steam/steamapps/common/AoE2DE/resources/_common
    LOG(INFO) << "Usage: <Program> <common_resources_path>";
    return 1;
  }
  std::string commonResourcesPathStr = argv[1];
  // Account for the annoying behavior of Linux file managers to prepend copied paths with "file://"
  if (commonResourcesPathStr.size() >= 8 && commonResourcesPathStr.substr(0, 7) == "file://") {
    commonResourcesPathStr = commonResourcesPathStr.substr(7);
  }
  std::filesystem::path commonResourcesPath = commonResourcesPathStr;
  
  // Set the default OpenGL format *before* creating a QApplication.
  QSurfaceFormat format;
  // format.setDepthBufferSize(24);
  // format.setStencilBufferSize(8);
  format.setVersion(3, 2);
  format.setProfile(QSurfaceFormat::CoreProfile);
  QSurfaceFormat::setDefaultFormat(format);
  
  // Initialize QApplication.
  QApplication qapp(argc, argv);
  QCoreApplication::setOrganizationName("FreeAge");
  QCoreApplication::setOrganizationDomain("free-age.org");
  QCoreApplication::setApplicationName("FreeAge");
  
  // We would like to get all input events immediately to be able to react quickly.
  qapp.setAttribute(Qt::AA_CompressHighFrequencyEvents, false);
  
  // Get the graphics path
  std::filesystem::path graphicsPath = commonResourcesPath / "drs" / "graphics";
  
  // Load palettes
  Palettes palettes;
  if (!ReadPalettesConf((commonResourcesPath / "palettes" / "palettes.conf").c_str(), &palettes)) {
    std::cout << "Failed to load palettes. Exiting.\n";
    return 1;
  }
  
  // Generate a map
  Map* testMap = new Map(50, 50);
  testMap->GenerateRandomMap();
  
  // Create an OpenGL render window using Qt
  RenderWindow renderWindow(palettes, graphicsPath);
  renderWindow.SetMap(testMap);  // transferring ownership
  renderWindow.SetScroll(testMap->GetTownCenterLocation(0));
  renderWindow.show();
  
  // Run the Qt event loop
  qapp.exec();
  return 0;
}
