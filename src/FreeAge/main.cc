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
  std::filesystem::path commonResourcesPath = "/home/thomas/.local/share/Steam/steamapps/common/AoE2DE/resources/_common";
  std::filesystem::path graphicsPath = commonResourcesPath / "drs" / "graphics";
  
  // Load palettes
  Palettes palettes;
  if (!ReadPalettesConf((commonResourcesPath / "palettes" / "palettes.conf").c_str(), &palettes)) {
    std::cout << "Failed to load palettes. Exiting.\n";
    return 1;
  }
  
  // Load sprite from SMX
  // std::filesystem::path smxPath = graphicsPath / "u_cav_warwagon_idleA_x1.smx";
  // std::filesystem::path smxPath = graphicsPath / "n_tree_palm_x1.smx"";
  std::filesystem::path smxPath = graphicsPath / "b_indi_mill_age3_x1.smx";
  // std::filesystem::path smxPath = graphicsPath / "b_meso_wonder_mayans_destruction_x1.smx";
  
  Sprite sprite;
  if (!sprite.LoadFromFile(smxPath.c_str(), palettes)) {
    std::cout << "Failed to load sprite. Exiting.\n";
    return 1;
  }
  
  // Create a sprite atlas texture containing all frames of the SMX animation.
  // TODO: This generally takes a LOT of memory. We probably want to do a dense packing of the images using
  //       non-rectangular geometry to save some more space.
  SpriteAtlas atlas;
  atlas.AddSprite(&sprite);
  QImage atlasImage = atlas.BuildAtlas(2 * 2048, 2048);
  if (atlasImage.isNull()) {
    std::cout << "Failed to build atlas: Not enough space in given area. Exiting.\n";
    return 1;
  }
  
  // Create an OpenGL render window using Qt
  RenderWindow renderWindow;
  renderWindow.SetSprite(&sprite, atlasImage);
  renderWindow.show();
  
  qapp.exec();
  return 0;
}
