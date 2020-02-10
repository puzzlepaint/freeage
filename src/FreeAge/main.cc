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
#include "FreeAge/sprite.h"
#include "FreeAge/sprite_atlas.h"

int main(int argc, char** argv) {
  // Initialize loguru
  loguru::g_preamble_date = false;
  loguru::g_preamble_thread = false;
  loguru::g_preamble_uptime = false;
  loguru::g_stderr_verbosity = 2;
  if (argc > 0) {
    loguru::init(argc, argv, /*verbosity_flag*/ nullptr);
  }
  
  if (argc != 3) {
    // For example:
    //
    // ./FreeAge /home/thomas/.local/share/Steam/steamapps/common/AoE2DE/resources/_common b_indi_mill_age3_x1.smx
    LOG(INFO) << "Usage: <Program> <common_resources_path> <smx_filename>";
    return 1;
  }
  std::string commonResourcesPathStr = argv[1];
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
  
  // TODO: Make configurable
  std::filesystem::path graphicsPath = commonResourcesPath / "drs" / "graphics";
  
  // Load palettes
  Palettes palettes;
  if (!ReadPalettesConf((commonResourcesPath / "palettes" / "palettes.conf").c_str(), &palettes)) {
    std::cout << "Failed to load palettes. Exiting.\n";
    return 1;
  }
  
  // Load sprite from SMX
  Sprite sprite;
  if (!sprite.LoadFromFile((graphicsPath / argv[2]).c_str(), palettes)) {
    std::cout << "Failed to load sprite. Exiting.\n";
    return 1;
  }
  
  // Create a sprite atlas texture containing all frames of the SMX animation.
  // TODO: This generally takes a LOT of memory. We probably want to do a dense packing of the images using
  //       non-rectangular geometry to save some more space.
  SpriteAtlas atlas;
  atlas.AddSprite(&sprite);
  
  int textureSize = 2048;
  int largestTooSmallSize = -1;
  int smallestAcceptableSize = -1;
  for (int attempt = 0; attempt < 8; ++ attempt) {
    if (!atlas.BuildAtlas(textureSize, textureSize, nullptr)) {
      // The size is too small.
      LOG(INFO) << "Size " << textureSize << " is too small.";
      largestTooSmallSize = textureSize;
      if (smallestAcceptableSize >= 0) {
        textureSize = (largestTooSmallSize + smallestAcceptableSize) / 2;
      } else {
        textureSize = 2 * largestTooSmallSize;
      }
    } else {
      // The size is large enough.
      LOG(INFO) << "Size " << textureSize << " is okay.";
      smallestAcceptableSize = textureSize;
      if (smallestAcceptableSize >= 0) {
        textureSize = (largestTooSmallSize + smallestAcceptableSize) / 2;
      } else {
        textureSize = smallestAcceptableSize / 2;
      }
    }
  }
  if (smallestAcceptableSize <= 0) {
    std::cout << "Unable to find a texture size which all animation frames can be packed into. Exiting.";
    return 1;
  }
  
  LOG(INFO) << "--> Using size: " << smallestAcceptableSize;
  QImage atlasImage;
  if (!atlas.BuildAtlas(smallestAcceptableSize, smallestAcceptableSize, &atlasImage)) {
    LOG(ERROR) << "Unexpected error while building an atlas image.";
    return 1;
  }
  
  // Create a terain
  Map* testMap = new Map(15, 15);
  testMap->GenerateRandomMap();
  
  // Create an OpenGL render window using Qt
  RenderWindow renderWindow;
  renderWindow.SetSprite(&sprite, atlasImage);
  renderWindow.SetMap(testMap);  // transferring ownership
  renderWindow.show();
  
  qapp.exec();
  return 0;
}
