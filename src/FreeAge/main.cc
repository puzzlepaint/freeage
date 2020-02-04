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

#include "FreeAge/FreeAge.h"
#include "FreeAge/Sprite.h"
#include "FreeAge/SpriteAtlas.h"

int main(int argc, char** argv) {
  QApplication qapp(argc, argv);
  QCoreApplication::setOrganizationName("FreeAge");
  QCoreApplication::setOrganizationDomain("free-age.org");
  QCoreApplication::setApplicationName("FreeAge");
  
  // We would like to get all input events immediately
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
  
  // DEBUG
  QLabel* testLabel = new QLabel();
  testLabel->setPixmap(QPixmap::fromImage(atlasImage));
  testLabel->show();
  qapp.exec();
  
  // Create an OpenGL render window using Qt
  // TODO
  
  // Render the sprite animation
  // TODO
  
  return 0;
}
