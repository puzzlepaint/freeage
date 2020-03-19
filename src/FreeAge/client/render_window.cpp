#include "FreeAge/client/render_window.hpp"

#include <math.h>

#include <QApplication>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_2_Core>
#include <QPainter>
#include <QThread>
#include <QTimer>

#include "FreeAge/client/game_controller.hpp"
#include "FreeAge/client/health_bar.hpp"
#include "FreeAge/common/logging.hpp"
#include "FreeAge/client/opengl.hpp"
#include "FreeAge/client/sprite.hpp"
#include "FreeAge/client/sprite_atlas.hpp"
#include "FreeAge/common/timing.hpp"


class LoadingThread : public QThread {
 Q_OBJECT
 public:
  inline LoadingThread(RenderWindow* window, QOpenGLContext* loadingContext, QOffscreenSurface* loadingSurface)
      : window(window),
        loadingContext(loadingContext),
        loadingSurface(loadingSurface) {}
  
  void run() override {
    loadingContext->makeCurrent(loadingSurface);
    window->LoadResources();
    loadingContext->doneCurrent();
    delete loadingContext;
  }
  
 private:
  RenderWindow* window;
  QOpenGLContext* loadingContext;
  QOffscreenSurface* loadingSurface;
};


RenderWindow::RenderWindow(
      const std::shared_ptr<Match>& match,
      const std::shared_ptr<GameController>& gameController,
      const std::shared_ptr<ServerConnection>& connection,
      float uiScale,
      int georgiaFontID,
      const Palettes& palettes,
      const std::filesystem::path& graphicsPath,
      const std::filesystem::path& cachePath,
      QWidget* parent)
    : QOpenGLWidget(parent),
      uiScale(uiScale),
      match(match),
      gameController(gameController),
      connection(connection),
      georgiaFont(QFont(QFontDatabase::applicationFontFamilies(georgiaFontID)[0])),
      palettes(palettes),
      graphicsPath(graphicsPath),
      cachePath(cachePath) {
  georgiaFontLarger = georgiaFont;
  georgiaFontLarger.setPixelSize(uiScale * 2*17);
  georgiaFontLarger.setBold(true);
  
  georgiaFontSmaller = georgiaFont;
  georgiaFontSmaller.setPixelSize(uiScale * 2*15);
  
  setAttribute(Qt::WA_OpaquePaintEvent);
  setAutoFillBackground(false);
  
  connect(this, &RenderWindow::LoadingProgressUpdated, this, &RenderWindow::SendLoadingProgress, Qt::QueuedConnection);
  
  // Receive mouse move events even if no mouse button is pressed
  setMouseTracking(true);
  
  // This may be faster than keeping partial updates possible
  setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
  
  // Set the default cursor
  defaultCursor = QCursor(QPixmap::fromImage(QImage((
      graphicsPath.parent_path().parent_path().parent_path().parent_path() / "widgetui" / "textures" / "ingame" / "cursor" / "default32x32.cur").c_str())),
      0, 0);
  setCursor(defaultCursor);
  
  // Do continuous rendering via a timer
  float framesPerSecondCap = 120;
  QTimer* timer = new QTimer(this);
  connect(timer, &QTimer::timeout, this, QOverload<>::of(&RenderWindow::update));
  // TODO: This is rounded to milliseconds, thus the FPS cap will be approximate
  timer->start(1000.f / framesPerSecondCap + 0.5f);
  
  // Initialize the view settings
  lastScrollGetTime = Clock::now();
  scroll = QPointF(0, 0);
  zoom = 1;
  
  map = nullptr;
  
  resize(std::max(800, width()), std::max(600, height()));
}

RenderWindow::~RenderWindow() {
  // Destroy OpenGL resources here, after makeCurrent() and before doneCurrent().
  makeCurrent();
  
  loadingIcon.reset();
  loadingTextDisplay.reset();
  
  resourcePanelTexture.reset();
  resourceWoodTexture.reset();
  woodTextDisplay.reset();
  resourceFoodTexture.reset();
  foodTextDisplay.reset();
  resourceGoldTexture.reset();
  goldTextDisplay.reset();
  resourceStoneTexture.reset();
  stoneTextDisplay.reset();
  popTexture.reset();
  popTextDisplay.reset();
  idleVillagerDisabledTexture.reset();
  currentAgeShieldTexture.reset();
  currentAgeTextDisplay.reset();
  
  gameTimeDisplay.reset();
  pingDisplay.reset();
  
  commandPanelTexture.reset();
  buildEconomyBuildingsTexture.reset();
  buildMilitaryBuildingsTexture.reset();
  toggleBuildingsCategoryTexture.reset();
  quitTexture.reset();
  
  selectionPanelTexture.reset();
  singleObjectNameDisplay.reset();
  hpDisplay.reset();
  carriedResourcesDisplay.reset();
  
  iconOverlayNormalTexture.reset();
  iconOverlayNormalExpensiveTexture.reset();
  iconOverlayHoverTexture.reset();
  iconOverlayActiveTexture.reset();
  
  uiShader.reset();
  uiSingleColorShader.reset();
  spriteShader.reset();
  shadowShader.reset();
  outlineShader.reset();
  healthBarShader.reset();
  
  if (map) {
    map->UnloadRenderResources();
    map.reset();
  }
  
  ClientUnitType::GetUnitTypes().clear();
  ClientBuildingType::GetBuildingTypes().clear();
  
  playerColorsTexture.reset();
  moveToSprite.reset();
  
  doneCurrent();
}

void RenderWindow::LoadResources() {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  auto didLoadingStep = [&]() {
    ++ loadingStep;
    // We cannot directly send the loading progress message here, since a QTcpSocket can only be accessed from
    // one thread. So, we notify the main thread via a queued signal/slot connection.
    emit LoadingProgressUpdated(static_cast<int>(100 * loadingStep / static_cast<float>(maxLoadingStep) + 0.5f));
  };
  
  // Load cursors.
  std::filesystem::path cursorsPath =
      graphicsPath.parent_path().parent_path().parent_path().parent_path() / "widgetui" / "textures" / "ingame" / "cursor";
  attackCursor = QCursor(QPixmap::fromImage(QImage((cursorsPath / "attack32x32.cur").c_str())), 0, 0);
  buildCursor = QCursor(QPixmap::fromImage(QImage((cursorsPath / "build32x32.cur").c_str())), 0, 0);
  chopCursor = QCursor(QPixmap::fromImage(QImage((cursorsPath / "chop32x32.cur").c_str())), 0, 0);
  gatherCursor = QCursor(QPixmap::fromImage(QImage((cursorsPath / "gather32x32.cur").c_str())), 0, 0);
  mineGoldCursor = QCursor(QPixmap::fromImage(QImage((cursorsPath / "mine_gold32x32.cur").c_str())), 0, 0);
  mineStoneCursor = QCursor(QPixmap::fromImage(QImage((cursorsPath / "mine_stone32x32.cur").c_str())), 0, 0);
  didLoadingStep();
  
  // Create shaders.
  spriteShader.reset(new SpriteShader(false, false));
  didLoadingStep();
  
  shadowShader.reset(new SpriteShader(true, false));
  didLoadingStep();
  
  outlineShader.reset(new SpriteShader(false, true));
  didLoadingStep();
  
  healthBarShader.reset(new HealthBarShader());
  didLoadingStep();
  
  // Create player color palette texture.
  CreatePlayerColorPaletteTexture();
  spriteShader->GetProgram()->UseProgram();
  spriteShader->GetProgram()->SetUniform2f(spriteShader->GetPlayerColorsTextureSizeLocation(), playerColorsTextureWidth, playerColorsTextureHeight);
  spriteShader->GetProgram()->SetUniform1i(spriteShader->GetPlayerColorsTextureLocation(), 1);  // use GL_TEXTURE1
  f->glActiveTexture(GL_TEXTURE0 + 1);
  f->glBindTexture(GL_TEXTURE_2D, playerColorsTexture->GetId());
  f->glActiveTexture(GL_TEXTURE0);
  
  // Set the sprite modulation color to the default.
  spriteShader->GetProgram()->SetUniform4f(spriteShader->GetModulationColorLocation(), 1, 1, 1, 1);
  didLoadingStep();
  
  // Load unit resources.
  auto& unitTypes = ClientUnitType::GetUnitTypes();
  unitTypes.resize(static_cast<int>(UnitType::NumUnits));
  for (int unitType = 0; unitType < static_cast<int>(UnitType::NumUnits); ++ unitType) {
    if (!unitTypes[unitType].Load(static_cast<UnitType>(unitType), graphicsPath, cachePath, palettes)) {
      LOG(ERROR) << "Exiting because of a resource load error for unit " << unitType << ".";
      exit(1);  // TODO: Exit gracefully
    }
    
    didLoadingStep();
  }
  
  // Load building resources.
  auto& buildingTypes = ClientBuildingType::GetBuildingTypes();
  buildingTypes.resize(static_cast<int>(BuildingType::NumBuildings));
  for (int buildingType = 0; buildingType < static_cast<int>(BuildingType::NumBuildings); ++ buildingType) {
    if (!buildingTypes[buildingType].Load(static_cast<BuildingType>(buildingType), graphicsPath, cachePath, palettes)) {
      LOG(ERROR) << "Exiting because of a resource load error for building " << buildingType << ".";
      exit(1);  // TODO: Exit gracefully
    }
    
    didLoadingStep();
  }
  
  // Load "move to" sprite.
  moveToSprite.reset(new SpriteAndTextures());
  LoadSpriteAndTexture(
      (graphicsPath.parent_path().parent_path() / "particles" / "textures" / "test_move" / "p_all_move_%04i.png").c_str(),
      (cachePath / "p_all_move_0000.png").c_str(),
      GL_CLAMP,
      GL_NEAREST,
      GL_NEAREST,
      &moveToSprite->sprite,
      &moveToSprite->graphicTexture,
      &moveToSprite->shadowTexture,
      palettes);
  didLoadingStep();
  
  // Load game UI textures.
  const std::string architectureNameCaps = "ASIA";  // TODO: Choose depending on civilization
  const std::string architectureNameLower = "asia";  // TODO: Choose depending on civilization
  
  std::filesystem::path widgetuiTexturesPath =
      graphicsPath.parent_path().parent_path().parent_path().parent_path() / "widgetui" / "textures";
  std::filesystem::path architecturePanelsPath =
      widgetuiTexturesPath / "ingame" / "panels" / architectureNameCaps;
  std::filesystem::path ingameIconsPath =
      widgetuiTexturesPath / "ingame" / "icons";
  std::filesystem::path ingameActionsPath =
      widgetuiTexturesPath / "ingame" / "actions";
  
  // Note: I profiled the loading below and replacing the QImage() variants with the mango variants
  // was significantly slower.
  // Initial times:
  //   0.0421275, 0.0420974, 0.0429374
  // With QImage loading replaced by mango loading:
  //   0.286818,  0.285423
  
  resourcePanelTexture.reset(new Texture());
  QImage resourcePanelImage((architecturePanelsPath / "resource-panel.png").c_str());
  resourcePanelTexture->Load(resourcePanelImage, GL_CLAMP, GL_LINEAR, GL_LINEAR);
  resourcePanelOpaquenessMap.Create(resourcePanelImage);
  didLoadingStep();
  
  resourceWoodTexture.reset(new Texture());
  resourceWoodTexture->Load(QImage((ingameIconsPath / "resource_wood.png").c_str()), GL_CLAMP, GL_LINEAR, GL_LINEAR);
  didLoadingStep();
  
  resourceFoodTexture.reset(new Texture());
  resourceFoodTexture->Load(QImage((ingameIconsPath / "resource_food.png").c_str()), GL_CLAMP, GL_LINEAR, GL_LINEAR);
  didLoadingStep();
  
  resourceGoldTexture.reset(new Texture());
  resourceGoldTexture->Load(QImage((ingameIconsPath / "resource_gold.png").c_str()), GL_CLAMP, GL_LINEAR, GL_LINEAR);
  didLoadingStep();
  
  resourceStoneTexture.reset(new Texture());
  resourceStoneTexture->Load(QImage((ingameIconsPath / "resource_stone.png").c_str()), GL_CLAMP, GL_LINEAR, GL_LINEAR);
  didLoadingStep();
  
  popTexture.reset(new Texture());
  popTexture->Load(QImage((ingameIconsPath / "pop.png").c_str()), GL_CLAMP, GL_LINEAR, GL_LINEAR);
  didLoadingStep();
  
  idleVillagerDisabledTexture.reset(new Texture());
  idleVillagerDisabledTexture->Load(QImage((ingameIconsPath / "idle-villager_disabled.png").c_str()), GL_CLAMP, GL_LINEAR, GL_LINEAR);
  didLoadingStep();
  
  currentAgeShieldTexture.reset(new Texture());
  currentAgeShieldTexture->Load(QImage((architecturePanelsPath / ("shield_dark_age_" + architectureNameLower + "_normal.png")).c_str()), GL_CLAMP, GL_LINEAR, GL_LINEAR);
  didLoadingStep();
  
  commandPanelTexture.reset(new Texture());
  QImage commandPanelImage((architecturePanelsPath / "command-panel_extended.png").c_str());
  commandPanelTexture->Load(commandPanelImage, GL_CLAMP, GL_LINEAR, GL_LINEAR);
  commandPanelOpaquenessMap.Create(commandPanelImage);
  didLoadingStep();
  
  buildEconomyBuildingsTexture.reset(new Texture());
  buildEconomyBuildingsTexture->Load(ingameActionsPath / "030_.png", GL_CLAMP, GL_LINEAR, GL_LINEAR);
  didLoadingStep();
  
  buildMilitaryBuildingsTexture.reset(new Texture());
  buildMilitaryBuildingsTexture->Load(ingameActionsPath / "031_.png", GL_CLAMP, GL_LINEAR, GL_LINEAR);
  didLoadingStep();
  
  toggleBuildingsCategoryTexture.reset(new Texture());
  toggleBuildingsCategoryTexture->Load(ingameActionsPath / "032_.png", GL_CLAMP, GL_LINEAR, GL_LINEAR);
  didLoadingStep();
  
  quitTexture.reset(new Texture());
  quitTexture->Load(ingameActionsPath / "000_.png", GL_CLAMP, GL_LINEAR, GL_LINEAR);
  didLoadingStep();
  
  selectionPanelTexture.reset(new Texture());
  QImage selectionPanelImage((architecturePanelsPath / "single-selection-panel.png").c_str());
  selectionPanelTexture->Load(selectionPanelImage, GL_CLAMP, GL_LINEAR, GL_LINEAR);
  selectionPanelOpaquenessMap.Create(selectionPanelImage);
  didLoadingStep();
  
  iconOverlayNormalTexture.reset(new Texture());
  iconOverlayNormalTexture->Load(QImage((ingameIconsPath / "icon_overlay_normal.png").c_str()), GL_CLAMP, GL_LINEAR, GL_LINEAR);
  didLoadingStep();
  
  iconOverlayNormalExpensiveTexture.reset(new Texture());
  iconOverlayNormalExpensiveTexture->Load(QImage((ingameIconsPath / "icon_overlay_normal_expensive.png").c_str()), GL_CLAMP, GL_LINEAR, GL_LINEAR);
  didLoadingStep();
  
  iconOverlayHoverTexture.reset(new Texture());
  iconOverlayHoverTexture->Load(QImage((ingameIconsPath / "icon_overlay_hover.png").c_str()), GL_CLAMP, GL_LINEAR, GL_LINEAR);
  didLoadingStep();
  
  iconOverlayActiveTexture.reset(new Texture());
  iconOverlayActiveTexture->Load(QImage((ingameIconsPath / "icon_overlay_active.png").c_str()), GL_CLAMP, GL_LINEAR, GL_LINEAR);
  didLoadingStep();
  
  // Output timings of the resource loading processes.
  Timing::print(std::cout);
  
  // Check that the value of maxLoadingStep is correct.
  if (loadingStep != maxLoadingStep) {
    LOG(ERROR) << "DEBUG: After loading, loadingStep (" << loadingStep << ") != maxLoadingStep (" << maxLoadingStep << "). Please set the value of maxLoadingStep to " << loadingStep << " in render_window.cpp.";
  }
}

void RenderWindow::Scroll(float x, float y, QPointF* mapCoord) {
  QPointF projectedCoord = map->MapCoordToProjectedCoord(*mapCoord);
  projectedCoord += QPointF(x, y);
  map->ProjectedCoordToMapCoord(projectedCoord, mapCoord);
}

QPointF RenderWindow::GetCurrentScroll(const TimePoint& atTime) {
  QPointF projectedCoord = map->MapCoordToProjectedCoord(scroll);
  if (scrollRightPressed) {
    double seconds = SecondsDuration(atTime - scrollRightPressTime).count();
    projectedCoord += QPointF(scrollDistancePerSecond / zoom * seconds, 0);
  }
  if (scrollLeftPressed) {
    double seconds = SecondsDuration(atTime - scrollLeftPressTime).count();
    projectedCoord += QPointF(-scrollDistancePerSecond / zoom * seconds, 0);
  }
  if (scrollDownPressed) {
    double seconds = SecondsDuration(atTime - scrollDownPressTime).count();
    projectedCoord += QPointF(0, scrollDistancePerSecond / zoom * seconds);
  }
  if (scrollUpPressed) {
    double seconds = SecondsDuration(atTime - scrollUpPressTime).count();
    projectedCoord += QPointF(0, -scrollDistancePerSecond / zoom * seconds);
  }
  
  if (borderScrollingEnabled) {
    double mouseImpactSeconds = SecondsDuration(atTime - lastScrollGetTime).count();
    
    if (lastCursorPos.x() == widgetWidth - 1) {
      projectedCoord += QPointF(scrollDistancePerSecond / zoom * mouseImpactSeconds, 0);
    }
    if (lastCursorPos.x() == 0) {
      projectedCoord += QPointF(-scrollDistancePerSecond / zoom * mouseImpactSeconds, 0);
    }
    if (lastCursorPos.y() == widgetHeight - 1) {
      projectedCoord += QPointF(0, scrollDistancePerSecond / zoom * mouseImpactSeconds);
    }
    if (lastCursorPos.y() == 0) {
      projectedCoord += QPointF(0, -scrollDistancePerSecond / zoom * mouseImpactSeconds);
    }
  }
  
  QPointF result;
  map->ProjectedCoordToMapCoord(projectedCoord, &result);
  return result;
}

void RenderWindow::SendLoadingProgress(int progress) {
  connection->Write(CreateLoadingProgressMessage(progress));
}

void RenderWindow::LoadingFinished() {
  delete loadingSurface;
  delete loadingThread;
  
  // Notify the server about the loading being finished by sending a
  // loading progress message with the progress above 100%.
  SendLoadingProgress(101);
  
  LOG(INFO) << "DEBUG: Loading finished.";
}

void RenderWindow::CreatePlayerColorPaletteTexture() {
  constexpr int maxNumPlayers = 8;
  
  const Palette* playerColorPalettes[maxNumPlayers] = {
    &palettes.at(55),  // blue,
    &palettes.at(56),  // red,
    &palettes.at(57),  // green,
    &palettes.at(58),  // yellow,
    &palettes.at(60),  // teal
    &palettes.at(61),  // purple
    &palettes.at(62),  // grey
    &palettes.at(59)   // orange
  };
  
  int maxNumColors = 0;
  for (int i = 0; i < maxNumPlayers; ++ i) {
    maxNumColors = std::max<int>(maxNumColors, playerColorPalettes[i]->size());
  }
  
  // Each row contains the colors for one player.
  playerColorsTextureWidth = maxNumColors;
  playerColorsTextureHeight = maxNumPlayers;
  QImage image(maxNumColors, maxNumPlayers, QImage::Format_ARGB32);
  for (int i = 0; i < maxNumPlayers; ++ i) {
    const Palette* palette = playerColorPalettes[i];
    QRgb* ptr = reinterpret_cast<QRgb*>(image.scanLine(i));
    
    for (usize c = 0; c < palette->size(); ++ c) {
      *ptr++ = palette->at(c);
    }
  }
  
  playerColorsTexture.reset(new Texture());
  playerColorsTexture->Load(image, GL_CLAMP, GL_NEAREST, GL_NEAREST);
  
  playerColors.resize(maxNumPlayers);
  for (int i = 0; i < maxNumPlayers; ++ i) {
    // NOTE: We simply use the first palette entry as the player color.
    //       The player color is used for outlines.
    playerColors[i] = playerColorPalettes[i]->at(0);
  }
}

void RenderWindow::ComputePixelToOpenGLMatrix() {
  float pixelToOpenGLMatrix[4];
  pixelToOpenGLMatrix[0] = 2.f / widgetWidth;
  pixelToOpenGLMatrix[1] = -2.f / widgetHeight;
  pixelToOpenGLMatrix[2] = -pixelToOpenGLMatrix[0] * 0.5f * widgetWidth;
  pixelToOpenGLMatrix[3] = -pixelToOpenGLMatrix[1] * 0.5f * widgetHeight;
  
  uiShader->GetProgram()->UseProgram();
  uiShader->GetProgram()->setUniformMatrix2fv(uiShader->GetViewMatrixLocation(), pixelToOpenGLMatrix);
  
  uiSingleColorShader->GetProgram()->UseProgram();
  uiSingleColorShader->GetProgram()->setUniformMatrix2fv(uiSingleColorShader->GetViewMatrixLocation(), pixelToOpenGLMatrix);
}

void RenderWindow::UpdateView(const TimePoint& now) {
  // Update scrolling state
  if (map) {
    scroll = GetCurrentScroll(now);
    lastScrollGetTime = now;
    if (scrollRightPressed) { scrollRightPressTime = now; }
    if (scrollLeftPressed) { scrollLeftPressTime = now; }
    if (scrollUpPressed) { scrollUpPressTime = now; }
    if (scrollDownPressed) { scrollDownPressTime = now; }
  }
  
  // Compute the pixel-to-OpenGL transformation for the UI shader.
  ComputePixelToOpenGLMatrix();
  
  // Compute the view (projected-to-OpenGL) transformation.
  if (map) {
    // Projected coordinates: arbitrary origin, +x goes right, +y goes down, scale is the default scale.
    // OpenGL normalized device coordinates: top-left widget corner is (-1, 1), bottom-right widget corner is (1, -1).
    // The transformation is stored as a matrix but applied as follows:
    // opengl_x = viewMatrix[0] * projected_x + viewMatrix[2];
    // opengl_y = viewMatrix[1] * projected_y + viewMatrix[3];
    QPointF projectedCoordAtScreenCenter = map->MapCoordToProjectedCoord(scroll);
    float scalingX = zoom * 2.f / widgetWidth;
    float scalingY = zoom * -2.f / widgetHeight;
    
    viewMatrix[0] = scalingX;
    viewMatrix[1] = scalingY;
    viewMatrix[2] = -scalingX * projectedCoordAtScreenCenter.x();
    viewMatrix[3] = -scalingY * projectedCoordAtScreenCenter.y();
    
    // Apply the view transformation to all shaders.
    // TODO: Use a uniform buffer object for that.
    spriteShader->GetProgram()->UseProgram();
    spriteShader->GetProgram()->setUniformMatrix2fv(spriteShader->GetViewMatrixLocation(), viewMatrix);
    
    shadowShader->GetProgram()->UseProgram();
    shadowShader->GetProgram()->setUniformMatrix2fv(shadowShader->GetViewMatrixLocation(), viewMatrix);
    
    outlineShader->GetProgram()->UseProgram();
    outlineShader->GetProgram()->setUniformMatrix2fv(outlineShader->GetViewMatrixLocation(), viewMatrix);
    
    healthBarShader->GetProgram()->UseProgram();
    healthBarShader->GetProgram()->setUniformMatrix2fv(healthBarShader->GetViewMatrixLocation(), viewMatrix);
    
    // Determine the view rect in projected coordinates.
    // opengl_x = viewMatrix[0] * projected_x + viewMatrix[2];
    // opengl_y = viewMatrix[1] * projected_y + viewMatrix[3];
    // -->
    // projected_x = (opengl_x - viewMatrix[2]) / viewMatrix[0]
    // projected_y = (opengl_y - viewMatrix[3]) / viewMatrix[1];
    float leftProjectedCoords = ((-1) - viewMatrix[2]) / viewMatrix[0];
    float rightProjectedCoords = ((1) - viewMatrix[2]) / viewMatrix[0];
    float topProjectedCoords = ((1) - viewMatrix[3]) / viewMatrix[1];
    float bottomProjectedCoords = ((-1) - viewMatrix[3]) / viewMatrix[1];
    projectedCoordsViewRect = QRectF(
        leftProjectedCoords,
        topProjectedCoords,
        rightProjectedCoords - leftProjectedCoords,
        bottomProjectedCoords - topProjectedCoords);
  }
}

void RenderWindow::RenderClosedPath(float halfLineWidth, const QRgb& color, const std::vector<QPointF>& vertices, const QPointF& offset) {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  CHECK_OPENGL_NO_ERROR();
  
  // Set shader.
  uiSingleColorShader->GetProgram()->UseProgram();
  uiSingleColorShader->GetProgram()->SetUniform4f(uiSingleColorShader->GetColorLocation(), qRed(color) / 255.f, qGreen(color) / 255.f, qBlue(color) / 255.f, qAlpha(color) / 255.f);
  
  // Repeat the first 2 vertices to close the path and get information
  // on the bend direction at the end.
  int numVertices = 2 * (vertices.size() + 1);
  
  // Buffer geometry data.
  f->glBindBuffer(GL_ARRAY_BUFFER, pointBuffer);
  int elementSizeInBytes = 3 * sizeof(float);
  std::vector<float> vertexData(3 * numVertices);  // TODO: Could skip the 3rd dimension
  int lastVertex = vertices.size() - 1;
  for (usize i = 0; i <= vertices.size(); ++ i) {
    int thisVertex = i % vertices.size();
    int nextVertex = (i + 1) % vertices.size();
    
    QPointF prevToCur = vertices[thisVertex] - vertices[lastVertex];
    QPointF curToNext = vertices[nextVertex] - vertices[thisVertex];
    
    lastVertex = thisVertex;
    
    float prevToCurNormalizer = 1.f / sqrtf(prevToCur.x() * prevToCur.x() + prevToCur.y() * prevToCur.y());
    prevToCur.setX(prevToCurNormalizer * prevToCur.x());
    prevToCur.setY(prevToCurNormalizer * prevToCur.y());
    
    float curToNextNormalizer = 1.f / sqrtf(curToNext.x() * curToNext.x() + curToNext.y() * curToNext.y());
    curToNext.setX(curToNextNormalizer * curToNext.x());
    curToNext.setY(curToNextNormalizer * curToNext.y());
    
    QPointF prevToCurRight(halfLineWidth * -prevToCur.y(), halfLineWidth * prevToCur.x());
    int bendDirection = ((prevToCurRight.x() * curToNext.x() + prevToCurRight.y() * curToNext.y()) > 0) ? 1 : -1;
    
    float halfBendAngle = std::max<float>(1e-4f, 0.5f * acos(std::max<float>(-1, std::min<float>(1, prevToCur.x() * -curToNext.x() + prevToCur.y() * -curToNext.y()))));
    float length = halfLineWidth / tan(halfBendAngle);
    
    // Vertex to the left of the line
    vertexData[6 * i + 0] = vertices[thisVertex].x() - prevToCurRight.x() + bendDirection * length * prevToCur.x() + offset.x();
    vertexData[6 * i + 1] = vertices[thisVertex].y() - prevToCurRight.y() + bendDirection * length * prevToCur.y() + offset.y();
    vertexData[6 * i + 2] = 0;
    
    // Vertex to the right of the line
    vertexData[6 * i + 3] = vertices[thisVertex].x() + prevToCurRight.x() - bendDirection * length * prevToCur.x() + offset.x();
    vertexData[6 * i + 4] = vertices[thisVertex].y() + prevToCurRight.y() - bendDirection * length * prevToCur.y() + offset.y();
    vertexData[6 * i + 5] = 0;
  }
  f->glBufferData(GL_ARRAY_BUFFER, numVertices * elementSizeInBytes, vertexData.data(), GL_DYNAMIC_DRAW);
  CHECK_OPENGL_NO_ERROR();
  uiSingleColorShader->GetProgram()->SetPositionAttribute(
      3,
      GetGLType<float>::value,
      3 * sizeof(float),
      0);
  
  // Draw lines.
  f->glDrawArrays(GL_TRIANGLE_STRIP, 0, numVertices);
  CHECK_OPENGL_NO_ERROR();
}

void RenderWindow::RenderShadows(double displayedServerTime) {
  auto& unitTypes = ClientUnitType::GetUnitTypes();
  
  for (auto& object : map->GetObjects()) {
    // TODO: Use virtual functions here to reduce duplicated code among buildings and units?
    
    if (object.second->isBuilding()) {
      ClientBuilding& building = *static_cast<ClientBuilding*>(object.second);
      if (!building.GetSprite().HasShadow()) {
        continue;
      }
      
      QRectF projectedCoordsRect = building.GetRectInProjectedCoords(
          map.get(),
          displayedServerTime,
          true,
          false);
      if (projectedCoordsRect.intersects(projectedCoordsViewRect)) {
        building.Render(
            map.get(),
            playerColors,
            shadowShader.get(),
            pointBuffer,
            viewMatrix,
            zoom,
            widgetWidth,
            widgetHeight,
            displayedServerTime,
            true,
            false);
      }
    } else {  // if (object.second->isUnit()) {
      ClientUnit& unit = *static_cast<ClientUnit*>(object.second);
      if (!unitTypes[static_cast<int>(unit.GetType())].GetAnimations(unit.GetCurrentAnimation()).front()->sprite.HasShadow()) {
        continue;
      }
      
      QRectF projectedCoordsRect = unit.GetRectInProjectedCoords(
          map.get(),
          displayedServerTime,
          true,
          false);
      if (projectedCoordsRect.intersects(projectedCoordsViewRect)) {
        unit.Render(
            map.get(),
            playerColors,
            shadowShader.get(),
            pointBuffer,
            viewMatrix,
            zoom,
            widgetWidth,
            widgetHeight,
            displayedServerTime,
            true,
            false);
      }
    }
  }
}

void RenderWindow::RenderBuildings(double displayedServerTime) {
  // TODO: Sort to minmize texture switches.
  for (auto& object : map->GetObjects()) {
    if (!object.second->isBuilding()) {
      continue;
    }
    ClientBuilding& building = *static_cast<ClientBuilding*>(object.second);
    
    QRectF projectedCoordsRect = building.GetRectInProjectedCoords(
        map.get(),
        displayedServerTime,
        false,
        false);
    if (projectedCoordsRect.intersects(projectedCoordsViewRect)) {
      // TODO: Multiple sprites may have nearly the same y-coordinate, as a result there can be flickering currently. Avoid this.
      building.Render(
          map.get(),
          playerColors,
          spriteShader.get(),
          pointBuffer,
          viewMatrix,
          zoom,
          widgetWidth,
          widgetHeight,
          displayedServerTime,
          false,
          false);
    }
  }
}

void RenderWindow::RenderBuildingFoundation(double displayedServerTime) {
  QPoint foundationBaseTile(-1, -1);
  bool canBePlacedHere = CanBuildingFoundationBePlacedHere(constructBuildingType, lastCursorPos, &foundationBaseTile);
  
  if (foundationBaseTile.x() >= 0 && foundationBaseTile.y() >= 0) {
    // Render the building foundation, colored either in gray if it can be placed at this location,
    // or in red if it cannot be placed there.
    ClientBuilding* tempBuilding = new ClientBuilding(match->GetPlayerIndex(), constructBuildingType, foundationBaseTile.x(), foundationBaseTile.y(), 100, /*hp*/ 0);
    tempBuilding->SetFixedFrameIndex(0);
    
    if (canBePlacedHere) {
      spriteShader->GetProgram()->SetUniform4f(spriteShader->GetModulationColorLocation(), 0.8, 0.8, 0.8, 1);
    } else {
      spriteShader->GetProgram()->SetUniform4f(spriteShader->GetModulationColorLocation(), 1, 0.4, 0.4, 1);
    }
    tempBuilding->Render(
        map.get(),
        playerColors,
        spriteShader.get(),
        pointBuffer,
        viewMatrix,
        zoom,
        widgetWidth,
        widgetHeight,
        displayedServerTime,
        false,
        false);
    spriteShader->GetProgram()->SetUniform4f(spriteShader->GetModulationColorLocation(), 1, 1, 1, 1);
    
    delete tempBuilding;
  }
}

void RenderWindow::RenderSelectionGroundOutlines(double displayedServerTime) {
  for (u32 objectId : selection) {
    auto it = map->GetObjects().find(objectId);
    if (it != map->GetObjects().end()) {
      RenderSelectionGroundOutline(qRgba(255, 255, 255, 255), it->second);
    }
  }
  
  if (flashingObjectId != kInvalidObjectId) {
    auto flashingObjectIt = map->GetObjects().find(flashingObjectId);
    if (flashingObjectIt != map->GetObjects().end()) {
      constexpr int kFlashCount = 3;
      constexpr double kFlashShowDuration = 0.2;
      constexpr double kFlashHideDuration = 0.1;
      
      double timeSinceFlashStart = displayedServerTime - flashingObjectStartTime;
      if (timeSinceFlashStart > 0 &&
          timeSinceFlashStart < kFlashCount * (kFlashShowDuration + kFlashHideDuration)) {
        double phase = fmod(timeSinceFlashStart, kFlashShowDuration + kFlashHideDuration);
        if (phase <= kFlashShowDuration) {
          RenderSelectionGroundOutline(qRgba(80, 255, 80, 255), flashingObjectIt->second);
        }
      }
    }
  }
}

void RenderWindow::RenderSelectionGroundOutline(QRgb color, ClientObject* object) {
  if (object->isBuilding()) {
    ClientBuilding& building = *static_cast<ClientBuilding*>(object);
    
    QSize size = GetBuildingSize(building.GetType());
    std::vector<QPointF> outlineVertices(4 + 2 * (size.width() - 1) + 2 * (size.height() - 1));
    
    QPointF base = building.GetBaseTile();
    int index = 0;
    for (int x = 0; x <= size.width(); ++ x) {
      outlineVertices[index++] = map->MapCoordToProjectedCoord(base + QPointF(x, 0));
    }
    for (int y = 1; y <= size.height(); ++ y) {
      outlineVertices[index++] = map->MapCoordToProjectedCoord(base + QPointF(size.width(), y));
    }
    for (int x = static_cast<int>(size.width()) - 1; x >= 0; -- x) {
      outlineVertices[index++] = map->MapCoordToProjectedCoord(base + QPointF(x, size.height()));
    }
    for (int y = static_cast<int>(size.height()) - 1; y > 0; -- y) {
      outlineVertices[index++] = map->MapCoordToProjectedCoord(base + QPointF(0, y));
    }
    CHECK_EQ(index, outlineVertices.size());
    for (usize i = 0; i < outlineVertices.size(); ++ i) {
      outlineVertices[i] =
          QPointF(((viewMatrix[0] * outlineVertices[i].x() + viewMatrix[2]) * 0.5f + 0.5f) * width(),
                  ((viewMatrix[1] * outlineVertices[i].y() + viewMatrix[3]) * -0.5f + 0.5f) * height());
    }
    
    RenderClosedPath(zoom * 1.1f, qRgba(0, 0, 0, 255), outlineVertices, QPointF(0, zoom * 2));
    RenderClosedPath(zoom * 1.1f, color, outlineVertices, QPointF(0, 0));
  } else if (object->isUnit()) {
    ClientUnit& unit = *static_cast<ClientUnit*>(object);
    
    float radius = GetUnitRadius(unit.GetType());
    
    std::vector<QPointF> outlineVertices(16);
    for (usize i = 0; i < outlineVertices.size(); ++ i) {
      float angle = (2 * M_PI) * i / (1.f * outlineVertices.size());
      outlineVertices[i] =
          map->MapCoordToProjectedCoord(unit.GetMapCoord() + radius * QPointF(sin(angle), cos(angle)));
      outlineVertices[i] =
          QPointF(((viewMatrix[0] * outlineVertices[i].x() + viewMatrix[2]) * 0.5f + 0.5f) * width(),
                  ((viewMatrix[1] * outlineVertices[i].y() + viewMatrix[3]) * -0.5f + 0.5f) * height());
    }
    
    RenderClosedPath(zoom * 1.1f, qRgba(0, 0, 0, 255), outlineVertices, QPointF(0, zoom * 2));
    RenderClosedPath(zoom * 1.1f, color, outlineVertices, QPointF(0, 0));
  }
}

void RenderWindow::RenderOutlines(double displayedServerTime) {
  auto& unitTypes = ClientUnitType::GetUnitTypes();
  
  // TODO: Sort to minmize texture switches.
  for (auto& object : map->GetObjects()) {
    // TODO: Use virtual functions here to reduce duplicated code among buildings and units?
    
    if (object.second->isBuilding()) {
      ClientBuilding& building = *static_cast<ClientBuilding*>(object.second);
      if (!building.GetSprite().HasOutline()) {
        continue;
      }
      
      QRectF projectedCoordsRect = building.GetRectInProjectedCoords(
          map.get(),
          displayedServerTime,
          false,
          true);
      if (projectedCoordsRect.intersects(projectedCoordsViewRect)) {
        building.Render(
            map.get(),
            playerColors,
            outlineShader.get(),
            pointBuffer,
            viewMatrix,
            zoom,
            widgetWidth,
            widgetHeight,
            displayedServerTime,
            false,
            true);
      }
    } else {  // if (object.second->isUnit()) {
      ClientUnit& unit = *static_cast<ClientUnit*>(object.second);
      if (!unitTypes[static_cast<int>(unit.GetType())].GetAnimations(unit.GetCurrentAnimation()).front()->sprite.HasOutline()) {
        continue;
      }
      
      QRectF projectedCoordsRect = unit.GetRectInProjectedCoords(
          map.get(),
          displayedServerTime,
          false,
          true);
      if (projectedCoordsRect.intersects(projectedCoordsViewRect)) {
        unit.Render(
            map.get(),
            playerColors,
            outlineShader.get(),
            pointBuffer,
            viewMatrix,
            zoom,
            widgetWidth,
            widgetHeight,
            displayedServerTime,
            false,
            true);
      }
    }
  }
}

void RenderWindow::RenderUnits(double displayedServerTime) {
  // TODO: Sort to minmize texture switches.
  for (auto& object : map->GetObjects()) {
    if (!object.second->isUnit()) {
      continue;
    }
    ClientUnit& unit = *static_cast<ClientUnit*>(object.second);
    
    QRectF projectedCoordsRect = unit.GetRectInProjectedCoords(
        map.get(),
        displayedServerTime,
        false,
        false);
    if (projectedCoordsRect.intersects(projectedCoordsViewRect)) {
      unit.Render(
          map.get(),
          playerColors,
          spriteShader.get(),
          pointBuffer,
          viewMatrix,
          zoom,
          widgetWidth,
          widgetHeight,
          displayedServerTime,
          false,
          false);
    }
  }
}

void RenderWindow::RenderMoveToMarker(const TimePoint& now) {
  // Update move-to sprite.
  int moveToFrameIndex = -1;
  if (haveMoveTo) {
    double moveToAnimationTime = std::chrono::duration<double>(now - moveToTime).count();
    float framesPerSecond = 30.f;
    moveToFrameIndex = std::max(0, static_cast<int>(framesPerSecond * moveToAnimationTime + 0.5f));
    if (moveToFrameIndex >= moveToSprite->sprite.NumFrames()) {
      haveMoveTo = false;
      moveToFrameIndex = -1;
    }
  }
  
  if (moveToFrameIndex >= 0) {
    QPointF projectedCoord = map->MapCoordToProjectedCoord(moveToMapCoord);
    DrawSprite(
        moveToSprite->sprite,
        moveToSprite->graphicTexture,
        spriteShader.get(),
        projectedCoord,
        pointBuffer,
        viewMatrix,
        zoom,
        widgetWidth,
        widgetHeight,
        moveToFrameIndex,
        /*shadow*/ false,
        /*outline*/ false,
        playerColors,
        /*playerIndex*/ 0,
        /*scaling*/ 0.5f);
  }
}

void RenderWindow::RenderHealthBars(double displayedServerTime) {
  auto& buildingTypes = ClientBuildingType::GetBuildingTypes();
  auto& unitTypes = ClientUnitType::GetUnitTypes();
  QRgb gaiaColor = qRgb(255, 255, 255);
  
  for (auto& object : map->GetObjects()) {
    if (!object.second->IsSelected()) {
      continue;
    }
    
    // TODO: Use virtual functions here to reduce duplicated code among buildings and units?
    
    if (object.second->isBuilding()) {
      ClientBuilding& building = *static_cast<ClientBuilding*>(object.second);
      const ClientBuildingType& buildingType = buildingTypes[static_cast<int>(building.GetType())];
      
      QPointF centerProjectedCoord = building.GetCenterProjectedCoord(map.get());
      QPointF healthBarCenter =
          centerProjectedCoord +
          QPointF(0, -1 * buildingType.GetHealthBarHeightAboveCenter(building.GetFrameIndex(buildingType, displayedServerTime)));
      
      constexpr float kHealthBarWidth = 60;  // TODO: Smaller bar for trees
      constexpr float kHealthBarHeight = 3;
      QRectF barRect(
          std::round(healthBarCenter.x() - 0.5f * kHealthBarWidth),
          std::round(healthBarCenter.y() - 0.5f * kHealthBarHeight),
          kHealthBarWidth,
          kHealthBarHeight);
      if (barRect.intersects(projectedCoordsViewRect)) {
        RenderHealthBar(
            barRect,
            centerProjectedCoord.y(),
            building.GetHP() / (1.f * GetBuildingMaxHP(building.GetType())),
            (building.GetPlayerIndex() < 0) ? gaiaColor : playerColors[building.GetPlayerIndex()],
            healthBarShader.get(),
            pointBuffer,
            viewMatrix,
            zoom,
            widgetWidth,
            widgetHeight);
      }
    } else {  // if (object.second->isUnit()) {
      ClientUnit& unit = *static_cast<ClientUnit*>(object.second);
      const ClientUnitType& unitType = unitTypes[static_cast<int>(unit.GetType())];
      
      QPointF centerProjectedCoord = unit.GetCenterProjectedCoord(map.get());
      QPointF healthBarCenter =
          centerProjectedCoord +
          QPointF(0, -1 * unitType.GetHealthBarHeightAboveCenter());
      
      constexpr float kHealthBarWidth = 30;
      constexpr float kHealthBarHeight = 3;
      QRectF barRect(
          std::round(healthBarCenter.x() - 0.5f * kHealthBarWidth),
          std::round(healthBarCenter.y() - 0.5f * kHealthBarHeight),
          kHealthBarWidth,
          kHealthBarHeight);
      if (barRect.intersects(projectedCoordsViewRect)) {
        RenderHealthBar(
            barRect,
            centerProjectedCoord.y(),
            unit.GetHP() / (1.f * GetUnitMaxHP(unit.GetType())),
            (unit.GetPlayerIndex() < 0) ? gaiaColor : playerColors[unit.GetPlayerIndex()],
            healthBarShader.get(),
            pointBuffer,
            viewMatrix,
            zoom,
            widgetWidth,
            widgetHeight);
      }
    }
  }
}

void RenderWindow::RenderGameUI(double displayedServerTime) {
  RenderResourcePanel();
  
  RenderSelectionPanel();
  
  RenderCommandPanel();
  
  // Render the current game time
  double timeSinceGameStart = displayedServerTime - gameController->GetGameStartServerTimeSeconds();
  int seconds = fmod(timeSinceGameStart, 60.0);
  int minutes = fmod(std::floor(timeSinceGameStart / 60.0), 60);
  int hours = std::floor(timeSinceGameStart / (60.0 * 60.0));
  QString timeString = QObject::tr("%1:%2:%3").arg(hours, 2, 10, QChar('0')).arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
  
  if (!gameTimeDisplay) {
    gameTimeDisplay.reset(new TextDisplay());
  }
  for (int i = 0; i < 2; ++ i) {
    gameTimeDisplay->Render(
      georgiaFontSmaller,
      (i == 0) ? qRgba(0, 0, 0, 255) : qRgba(255, 255, 255, 255),
      timeString,
      QRect(uiScale * (2*851 + ((i == 0) ? 2 : 0)),
            uiScale * (8 + ((i == 0) ? 2 : 0)),
            0,
            0),
      Qt::AlignTop | Qt::AlignLeft,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  }
  
  // Render the current ping
  double filteredPing, filteredOffset;
  connection->EstimateCurrentPingAndOffset(&filteredPing, &filteredOffset);
  QString pingString = QObject::tr("%1 ms").arg(static_cast<int>(1000 * filteredPing + 0.5f));
  
  if (!pingDisplay) {
    pingDisplay.reset(new TextDisplay());
  }
  for (int i = 0; i < 2; ++ i) {
    pingDisplay->Render(
      georgiaFontSmaller,
      (i == 0) ? qRgba(0, 0, 0, 255) : qRgba(255, 255, 255, 255),
      pingString,
      QRect(uiScale * (2*851 + ((i == 0) ? 2 : 0)),
            uiScale * (40 + 8 + ((i == 0) ? 2 : 0)),
            0,
            0),
      Qt::AlignTop | Qt::AlignLeft,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  }
}

QPointF RenderWindow::GetResourcePanelTopLeft() {
  return QPointF(0, 0);
}

void RenderWindow::RenderResourcePanel() {
  const ResourceAmount& resources = gameController->GetCurrentResourceAmount();
  
  QPointF topLeft = GetResourcePanelTopLeft();
  
  RenderUIGraphic(
      topLeft.x(),
      topLeft.y(),
      uiScale * resourcePanelTexture->GetWidth(),
      uiScale * resourcePanelTexture->GetHeight(),
      *resourcePanelTexture,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  
  RenderUIGraphic(
      topLeft.x() + uiScale * (17 + 0 * 200),
      topLeft.y() + uiScale * 16,
      uiScale * 83,
      uiScale * 83,
      *resourceWoodTexture,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  if (!woodTextDisplay) {
    woodTextDisplay.reset(new TextDisplay());
  }
  woodTextDisplay->Render(
      georgiaFontSmaller,
      qRgba(255, 255, 255, 255),
      QString::number(resources.wood()),
      QRect(topLeft.x() + uiScale * (17 + 0 * 200 + 83 + 16),
            topLeft.y() + uiScale * 16,
            uiScale * 82,
            uiScale * 83),
      Qt::AlignLeft | Qt::AlignVCenter,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  
  RenderUIGraphic(
      topLeft.x() + uiScale * (17 + 1 * 200),
      topLeft.y() + uiScale * 16,
      uiScale * 83,
      uiScale * 83,
      *resourceFoodTexture,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  if (!foodTextDisplay) {
    foodTextDisplay.reset(new TextDisplay());
  }
  foodTextDisplay->Render(
      georgiaFontSmaller,
      qRgba(255, 255, 255, 255),
      QString::number(resources.food()),
      QRect(topLeft.x() + uiScale * (17 + 1 * 200 + 83 + 16),
            topLeft.y() + uiScale * 16,
            uiScale * 82,
            uiScale * 83),
      Qt::AlignLeft | Qt::AlignVCenter,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  
  RenderUIGraphic(
      topLeft.x() + uiScale * (17 + 2 * 200),
      topLeft.y() + uiScale * 16,
      uiScale * 83,
      uiScale * 83,
      *resourceGoldTexture,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  if (!goldTextDisplay) {
    goldTextDisplay.reset(new TextDisplay());
  }
  goldTextDisplay->Render(
      georgiaFontSmaller,
      qRgba(255, 255, 255, 255),
      QString::number(resources.gold()),
      QRect(topLeft.x() + uiScale * (17 + 2 * 200 + 83 + 16),
            topLeft.y() + uiScale * 16,
            uiScale * 82,
            uiScale * 83),
      Qt::AlignLeft | Qt::AlignVCenter,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  
  RenderUIGraphic(
      topLeft.x() + uiScale * (17 + 3 * 200),
      topLeft.y() + uiScale * 16,
      uiScale * 83,
      uiScale * 83,
      *resourceStoneTexture,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  if (!stoneTextDisplay) {
    stoneTextDisplay.reset(new TextDisplay());
  }
  stoneTextDisplay->Render(
      georgiaFontSmaller,
      qRgba(255, 255, 255, 255),
      QString::number(resources.stone()),
      QRect(topLeft.x() + uiScale * (17 + 3 * 200 + 83 + 16),
            topLeft.y() + uiScale * 16,
            uiScale * 82,
            uiScale * 83),
      Qt::AlignLeft | Qt::AlignVCenter,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  
  RenderUIGraphic(
      topLeft.x() + uiScale * (17 + 4 * 200),
      topLeft.y() + uiScale * 16,
      uiScale * 83,
      uiScale * 83,
      *popTexture,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  if (!popTextDisplay) {
    popTextDisplay.reset(new TextDisplay());
  }
  popTextDisplay->Render(
      georgiaFontSmaller,
      qRgba(255, 255, 255, 255),
      "4 / 5",  // TODO
      QRect(topLeft.x() + uiScale * (17 + 4 * 200 + 83 + 16),
            topLeft.y() + uiScale * 16,
            uiScale * 82,
            uiScale * 83),
      Qt::AlignLeft | Qt::AlignVCenter,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  
  RenderUIGraphic(
      topLeft.x() + uiScale * (17 + 4 * 200 + 234),
      topLeft.y() + uiScale * 24,
      uiScale * 2 * 34,
      uiScale * 2 * 34,
      *idleVillagerDisabledTexture,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  RenderUIGraphic(
      topLeft.x() + uiScale * (17 + 4 * 200 + 234 + 154 - currentAgeShieldTexture->GetWidth() / 2),
      topLeft.y() + uiScale * 0,
      uiScale * currentAgeShieldTexture->GetWidth(),
      uiScale * currentAgeShieldTexture->GetHeight(),
      *currentAgeShieldTexture,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  if (!currentAgeTextDisplay) {
    currentAgeTextDisplay.reset(new TextDisplay());
  }
  float currentAgeTextLeft = topLeft.x() + uiScale * (17 + 4 * 200 + 234 + 154 + currentAgeShieldTexture->GetWidth() / 2);
  currentAgeTextDisplay->Render(
      georgiaFontLarger,
      qRgba(255, 255, 255, 255),
      tr("Dark Age"),
      QRect(currentAgeTextLeft,
            topLeft.y() + uiScale * 16,
            uiScale * (1623 - 8) - currentAgeTextLeft,
            uiScale * 83),
      Qt::AlignHCenter | Qt::AlignVCenter,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
}

QPointF RenderWindow::GetSelectionPanelTopLeft() {
  return QPointF(
      uiScale * 539,
      widgetHeight - uiScale * selectionPanelTexture->GetHeight());
}

void RenderWindow::RenderSelectionPanel() {
  QPointF topLeft = GetSelectionPanelTopLeft();
  
  RenderUIGraphic(
      topLeft.x(),
      topLeft.y(),
      uiScale * selectionPanelTexture->GetWidth(),
      uiScale * selectionPanelTexture->GetHeight(),
      *selectionPanelTexture,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  
  // Is only a single object selected?
  if (selection.size() == 1) {
    ClientObject* singleSelectedObject = map->GetObjects().at(selection.front());
    
    // Display the object name
    if (!singleObjectNameDisplay) {
      singleObjectNameDisplay.reset(new TextDisplay());
    }
    singleObjectNameDisplay->Render(
        georgiaFontLarger,
        qRgba(58, 29, 21, 255),
        singleSelectedObject->GetObjectName(),
        QRect(topLeft.x() + uiScale * 2*32,
              topLeft.y() + uiScale * 50 + uiScale * 2*25,
              uiScale * 2*172,
              uiScale * 2*16),
        Qt::AlignLeft | Qt::AlignTop,
        uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
    
    // Display the object's HP
    if (singleSelectedObject->GetHP() > 0) {
      u32 maxHP;
      if (singleSelectedObject->isUnit()) {
        maxHP = GetUnitMaxHP(static_cast<ClientUnit*>(singleSelectedObject)->GetType());
      } else {
        CHECK(singleSelectedObject->isBuilding());
        maxHP = GetBuildingMaxHP(static_cast<ClientBuilding*>(singleSelectedObject)->GetType());
      }
      
      if (!hpDisplay) {
        hpDisplay.reset(new TextDisplay());
      }
      hpDisplay->Render(
          georgiaFontSmaller,
          qRgba(58, 29, 21, 255),
          QStringLiteral("%1 / %2").arg(singleSelectedObject->GetHP()).arg(maxHP),
          QRect(topLeft.x() + uiScale * 2*32,
                topLeft.y() + uiScale * 50 + uiScale * 2*46 + uiScale * 2*60,
                uiScale * 2*172,
                uiScale * 2*16),
          Qt::AlignLeft | Qt::AlignTop,
          uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
    }
    
    // Display unit details?
    if (singleSelectedObject->isUnit()) {
      ClientUnit* singleSelectedUnit = static_cast<ClientUnit*>(singleSelectedObject);
      if (IsVillager(singleSelectedUnit->GetType())) {
        // Display the villager's carried resources?
        if (singleSelectedUnit->GetCarriedResourceAmount() > 0) {
          if (!carriedResourcesDisplay) {
            carriedResourcesDisplay.reset(new TextDisplay());
          }
          carriedResourcesDisplay->Render(
              georgiaFontSmaller,
              qRgba(58, 29, 21, 255),
              QObject::tr("Carries %1 %2")
                  .arg(singleSelectedUnit->GetCarriedResourceAmount())
                  .arg(GetResourceName(singleSelectedUnit->GetCarriedResourceType())),
              QRect(topLeft.x() + uiScale * 2*32,
                    topLeft.y() + uiScale * 50 + uiScale * 2*46 + uiScale * 2*60 + uiScale * 2*20,
                    uiScale * 2*172,
                    uiScale * 2*16),
              Qt::AlignLeft | Qt::AlignTop,
              uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
        }
      }
    }
    
    // Render icon of single selected object
    const Texture* iconTexture = singleSelectedObject->GetIconTexture();
    if (iconTexture) {
      float iconInset = uiScale * 4;
      RenderUIGraphic(
          topLeft.x() + uiScale * 2*32 + iconInset,
          topLeft.y() + uiScale * 50 + uiScale * 2*46 + iconInset,
          uiScale * 2*60 - 2 * iconInset,
          uiScale * 2*60 - 2 * iconInset,
          *iconTexture,
          uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
      RenderUIGraphic(
          topLeft.x() + uiScale * 2*32,
          topLeft.y() + uiScale * 50 + uiScale * 2*46,
          uiScale * 2*60,
          uiScale * 2*60,
          *iconOverlayNormalTexture,
          uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
    }
  }
}

QPointF RenderWindow::GetCommandPanelTopLeft() {
  return QPointF(
      0,
      widgetHeight - uiScale * commandPanelTexture->GetHeight());
}

void RenderWindow::RenderCommandPanel() {
  QPointF topLeft = GetCommandPanelTopLeft();
  
  RenderUIGraphic(
      topLeft.x(),
      topLeft.y(),
      uiScale * commandPanelTexture->GetWidth(),
      uiScale * commandPanelTexture->GetHeight(),
      *commandPanelTexture,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  
  float commandButtonsLeft = topLeft.x() + uiScale * 49;
  float commandButtonsTop = topLeft.y() + uiScale * 93;
  float commandButtonsRight = topLeft.x() + uiScale * 499;
  float commandButtonsBottom = topLeft.y() + uiScale * 370;
  
  float commandButtonSize = uiScale * 80;
  
  for (int row = 0; row < kCommandButtonRows; ++ row) {
    for (int col = 0; col < kCommandButtonCols; ++ col) {
      float buttonLeft = commandButtonsLeft + (commandButtonsRight - commandButtonSize - commandButtonsLeft) * (col / (kCommandButtonCols - 1.));
      float buttonTop = commandButtonsTop + (commandButtonsBottom - commandButtonSize - commandButtonsTop) * (row / (kCommandButtonRows - 1.));
      
      bool pressed =
          pressedCommandButtonRow == row &&
          pressedCommandButtonCol == col;
      bool mouseOver =
          lastCursorPos.x() >= buttonLeft &&
          lastCursorPos.y() >= buttonTop &&
          lastCursorPos.x() < buttonLeft + commandButtonSize &&
          lastCursorPos.y() < buttonTop + commandButtonSize;
      
      bool disabled = false;
      if (commandButtons[row][col].GetType() == CommandButton::Type::ProduceUnit) {
        disabled =
            !gameController->GetLatestKnownResourceAmount().CanAfford(
                GetUnitCost(commandButtons[row][col].GetUnitProductionType()));
      } else if (commandButtons[row][col].GetType() == CommandButton::Type::ConstructBuilding) {
        disabled =
            !gameController->GetLatestKnownResourceAmount().CanAfford(
                GetBuildingCost(commandButtons[row][col].GetBuildingConstructionType()));
      }
      
      commandButtons[row][col].Render(
          buttonLeft,
          buttonTop,
          commandButtonSize,
          uiScale * 4,
          disabled ? *iconOverlayNormalExpensiveTexture :
              (pressed ? *iconOverlayActiveTexture :
                  (mouseOver ? *iconOverlayHoverTexture : *iconOverlayNormalTexture)),
          uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
    }
  }
}

bool RenderWindow::IsUIAt(int x, int y) {
  float factor = 1.f / uiScale;
  
  QPointF resourcePanelTopLeft = GetResourcePanelTopLeft();
  if (resourcePanelOpaquenessMap.IsOpaque(factor * (x - resourcePanelTopLeft.x()), factor * (y - resourcePanelTopLeft.y()))) {
    return true;
  }
  
  QPointF selectionPanelTopLeft = GetSelectionPanelTopLeft();
  if (selectionPanelOpaquenessMap.IsOpaque(factor * (x - selectionPanelTopLeft.x()), factor * (y - selectionPanelTopLeft.y()))) {
    return true;
  }
  
  QPointF commandPanelTopLeft = GetCommandPanelTopLeft();
  if (commandPanelOpaquenessMap.IsOpaque(factor * (x - commandPanelTopLeft.x()), factor * (y - commandPanelTopLeft.y()))) {
    return true;
  }
  
  return false;
}

struct PossibleSelectedObject {
  inline PossibleSelectedObject(int id, float score)
      : id(id),
        score(score) {}
  
  inline bool operator< (const PossibleSelectedObject& other) const {
    return score < other.score;
  }
  
  u32 id;
  
  /// The smaller, the better.
  float score;
};

bool RenderWindow::GetObjectToSelectAt(float x, float y, u32* objectId, std::vector<u32>* currentSelection, bool toggleThroughObjects, bool selectSuitableTargetsOnly) {
  auto& buildingTypes = ClientBuildingType::GetBuildingTypes();
  
  std::vector<ClientObject*> currentSelectedObjects;
  if (selectSuitableTargetsOnly) {
    currentSelectedObjects.resize(currentSelection->size());
    for (usize i = 0; i < currentSelection->size(); ++ i) {
      auto it = map->GetObjects().find(currentSelection->at(i));
      if (it == map->GetObjects().end()) {
        currentSelectedObjects[i] = nullptr;
      } else {
        currentSelectedObjects[i] = it->second;
      }
    }
  }
  
  // First, collect all objects at the given position.
  std::vector<PossibleSelectedObject> possibleSelectedObjects;
  
  QPointF projectedCoord = ScreenCoordToProjectedCoord(x, y);
  QPointF mapCoord;
  bool haveMapCoord = map->ProjectedCoordToMapCoord(projectedCoord, &mapCoord);
  
  auto computeScore = [](const QRectF& rect, const QPointF& point) {
    float area = rect.width() * rect.height();
    QPointF offset = rect.center() - point;
    float offsetLength = sqrtf(offset.x() * offset.x() + offset.y() * offset.y());
    return area * std::min<float>(1.f, offsetLength / (0.5f * std::max(rect.width(), rect.height())));
  };
  
  for (auto& object : map->GetObjects()) {
    // TODO: Use virtual functions here to reduce duplicated code among buildings and units?
    bool addToList = false;
    QRectF projectedCoordsRect;
    
    if (object.second->isBuilding()) {
      ClientBuilding& building = *static_cast<ClientBuilding*>(object.second);
      const ClientBuildingType& buildingType = buildingTypes[static_cast<int>(building.GetType())];
      
      // Is the position within the tiles which the building stands on?
      if (haveMapCoord) {
        QSize size = buildingType.GetSize();
        if (mapCoord.x() >= building.GetBaseTile().x() &&
            mapCoord.y() >= building.GetBaseTile().y() &&
            mapCoord.x() <= building.GetBaseTile().x() + size.width() &&
            mapCoord.y() <= building.GetBaseTile().y() + size.height()) {
          addToList = true;
        }
      }
      
      // Is the position within the building sprite?
      projectedCoordsRect = building.GetRectInProjectedCoords(
          map.get(),
          lastDisplayedServerTime,
          false,
          false);
      if (!addToList && projectedCoordsRect.contains(projectedCoord)) {
        const Sprite::Frame& frame = building.GetSprite().frame(building.GetFrameIndex(buildingType, lastDisplayedServerTime));
        // We add 1 here to account for the sprite border which is not included in projectedCoordsRect.
        // We further add 0.5f for rounding during the cast to integer.
        QPoint point(projectedCoord.x() - projectedCoordsRect.x() + 1 + 0.5f, projectedCoord.y() - projectedCoordsRect.y() + 1 + 0.5f);
        point.setX(std::max(0, std::min(frame.graphic.imageWidth - 1, point.x())));
        point.setY(std::max(0, std::min(frame.graphic.imageHeight - 1, point.y())));
        const SMPLayerRowEdge& rowEdge = frame.rowEdges[point.y()];
        if (point.x() >= rowEdge.leftSpace &&
            frame.graphic.imageWidth - 1 - point.x() >= rowEdge.rightSpace) {
          addToList = true;
        }
      }
    } else if (object.second->isUnit()) {
      ClientUnit& unit = *static_cast<ClientUnit*>(object.second);
      
      // Is the position close to the unit sprite?
      constexpr float kExtendSize = 8;
      
      projectedCoordsRect = unit.GetRectInProjectedCoords(
          map.get(),
          lastDisplayedServerTime,
          false,
          false);
      projectedCoordsRect.adjust(-kExtendSize, -kExtendSize, kExtendSize, kExtendSize);
      if (!addToList && projectedCoordsRect.contains(projectedCoord)) {
        addToList = true;
      }
    }
    
    if (addToList && selectSuitableTargetsOnly) {
      addToList = false;
      for (ClientObject* selectedObject : currentSelectedObjects) {
        if (selectedObject && GetInteractionType(selectedObject, object.second) != InteractionType::Invalid) {
          addToList = true;
          break;
        }
      }
    }
    
    if (addToList) {
      possibleSelectedObjects.emplace_back(object.first, computeScore(projectedCoordsRect, projectedCoord));
    }
  }
  
  if (!possibleSelectedObjects.empty()) {
    if (toggleThroughObjects && currentSelection && currentSelection->size() == 1) {
      // Sort the detected objects by score.
      std::sort(possibleSelectedObjects.begin(), possibleSelectedObjects.end());
      
      // If the current selection is in the list, then return the next object to select.
      for (usize i = 0; i < possibleSelectedObjects.size(); ++ i) {
        if (possibleSelectedObjects[i].id == currentSelection->front()) {
          *objectId = possibleSelectedObjects[(i + 1) % possibleSelectedObjects.size()].id;
          return true;
        }
      }
    } else {
      // Move the object with the highest score to the start.
      std::partial_sort(possibleSelectedObjects.begin(), possibleSelectedObjects.begin() + 1, possibleSelectedObjects.end());
    }
    
    *objectId = possibleSelectedObjects.front().id;
    return true;
  }
  
  return false;
}

void RenderWindow::BoxSelection(const QPoint& p0, const QPoint& p1) {
  ClearSelection();
  
  QPointF pr0 = ScreenCoordToProjectedCoord(p0.x(), p0.y());
  QPointF pr1 = ScreenCoordToProjectedCoord(p1.x(), p1.y());
  QRectF selectionRect(
      std::min(pr0.x(), pr1.x()),
      std::min(pr0.y(), pr1.y()),
      std::abs(pr0.x() - pr1.x()),
      std::abs(pr0.y() - pr1.y()));
  
  for (auto& object : map->GetObjects()) {
    if (object.second->isUnit()) {
      ClientUnit& unit = *static_cast<ClientUnit*>(object.second);
      
      QRectF projectedCoordsRect = unit.GetRectInProjectedCoords(
          map.get(),
          lastDisplayedServerTime,
          false,
          false);
      
      if (projectedCoordsRect.intersects(selectionRect)) {
        AddToSelection(object.first);
      }
    }
  }
  
  SelectionChanged();
}

QPointF RenderWindow::ScreenCoordToProjectedCoord(float x, float y) {
  return QPointF(
      ((-1 + 2 * x / (1.f * width())) - viewMatrix[2]) / viewMatrix[0],
      ((1 - 2 * y / (1.f * height())) - viewMatrix[3]) / viewMatrix[1]);
}

void RenderWindow::ClearSelection() {
  for (u32 objectId : selection) {
    auto objectIt = map->GetObjects().find(objectId);
    if (objectIt == map->GetObjects().end()) {
      LOG(ERROR) << "Selected object ID not found in map->GetObjects().";
    } else {
      objectIt->second->SetIsSelected(false);
    }
  }
  
  selection.clear();
}

void RenderWindow::AddToSelection(u32 objectId) {
  selection.push_back(objectId);
  
  auto objectIt = map->GetObjects().find(objectId);
  if (objectIt == map->GetObjects().end()) {
    LOG(ERROR) << "Selected object ID not found in map->GetObjects().";
  } else {
    objectIt->second->SetIsSelected(true);
  }
}

void RenderWindow::SelectionChanged() {
  ShowDefaultCommandButtonsForSelection();
}

void RenderWindow::LetObjectGroundOutlineFlash(u32 objectId) {
  flashingObjectId = objectId;
  // NOTE: We could use a local time here to make it a bit more smooth than with the server time.
  //       It will not matter in practice though.
  flashingObjectStartTime = lastDisplayedServerTime;
}

void RenderWindow::RenderLoadingScreen() {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  CHECK_OPENGL_NO_ERROR();
  
  ComputePixelToOpenGLMatrix();
  
  // Clear background.
  f->glClearColor(0.1f, 0.1f, 0.1f, 0.0f);
  f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  CHECK_OPENGL_NO_ERROR();
  
  // Render the loading state text.
  QString text;
  const auto& players = match->GetPlayers();
  for (int i = 0; i < static_cast<int>(players.size()); ++ i) {
    if (i > 0) {
      text += QStringLiteral("\n");
    }
    int loadingPercentage;
    if (i == match->GetPlayerIndex()) {
      loadingPercentage = static_cast<int>(100 * loadingStep / static_cast<float>(maxLoadingStep) + 0.5f);
    } else {
      loadingPercentage = players[i].loadingPercentage;
    }
    text += tr("%1: %2%")
        .arg(players[i].name)
        .arg(loadingPercentage, 3, 10, QChar(' '));
  }
  
  if (!loadingTextDisplay) {
    loadingTextDisplay.reset(new TextDisplay());
  }
  loadingTextDisplay->Render(
      georgiaFont,
      qRgba(255, 255, 255, 255),
      text,
      QRect(0, 0, widgetWidth, widgetHeight),
      Qt::AlignHCenter | Qt::AlignVCenter,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  
  // Render the loading icon.
  RenderUIGraphic(
      widgetWidth / 2 - loadingIcon->GetWidth() / 2,
      loadingTextDisplay->GetBounds().y() - loadingIcon->GetHeight(),
      loadingIcon->GetWidth(),
      loadingIcon->GetHeight(),
      *loadingIcon,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
}

void RenderWindow::UpdateGameState(double displayedServerTime) {
  // Iterate over all map objects and predict their state at the given server time.
  for (const auto& item : map->GetObjects()) {
    if (item.second->isUnit()) {
      ClientUnit* unit = static_cast<ClientUnit*>(item.second);
      unit->UpdateGameState(displayedServerTime);
    } else if (item.second->isBuilding()) {
      // TODO: Anything to do here?
    }
  }
}

bool RenderWindow::CanBuildingFoundationBePlacedHere(BuildingType type, const QPointF& cursorPos, QPoint* baseTile) {
QPointF projectedCoord = ScreenCoordToProjectedCoord(cursorPos.x(), cursorPos.y());
  QPointF cursorMapCoord;
  if (!map->ProjectedCoordToMapCoord(projectedCoord, &cursorMapCoord)) {
    return false;
  }
  
  QSize foundationSize = GetBuildingSize(type);
  QPoint foundationBaseTile;
  
  if (foundationSize.width() % 2 == 1) {
    // Round cursorMapCoord.x() to integer tiles.
    foundationBaseTile.setX(std::max<int>(0, std::min<int>(map->GetWidth() - 1, static_cast<int>(cursorMapCoord.x()) - (foundationSize.width() - 1) / 2)));
  } else {
    // Round cursorMapCoord.x() to tile borders.
    foundationBaseTile.setX(std::max<int>(0, std::min<int>(map->GetWidth() - 1, static_cast<int>(cursorMapCoord.x() + 0.5f) - foundationSize.width() / 2)));
  }
  
  if (foundationSize.height() % 2 == 1) {
    // Round cursorMapCoord.y() to integer tiles.
    foundationBaseTile.setY(std::max<int>(0, std::min<int>(map->GetHeight() - 1, static_cast<int>(cursorMapCoord.y()) - (foundationSize.height() - 1) / 2)));
  } else {
    // Round cursorMapCoord.y() to tile borders.
    foundationBaseTile.setY(std::max<int>(0, std::min<int>(map->GetHeight() - 1, static_cast<int>(cursorMapCoord.y() + 0.5f) - foundationSize.height() / 2)));
  }
  
  *baseTile = foundationBaseTile;
  
  // Check whether the building can be placed at the given location.
  // TODO: The same logic is implemented on the server, can that be unified?
  // TODO: Docks need a special case
  
  // 1) Check whether any map tile at this location is occupied.
  QRect foundationRect(foundationBaseTile, foundationSize);
  for (const auto& item : map->GetObjects()) {
    if (item.second->isBuilding()) {
      ClientBuilding* building = static_cast<ClientBuilding*>(item.second);
      QRect occupiedRect(building->GetBaseTile(), GetBuildingSize(building->GetType()));
      if (foundationRect.intersects(occupiedRect)) {
        return false;
      }
    }
  }
  
  // NOTE: Code if we were to track the map occupancy on the client:
  // for (int y = foundationBaseTile.y(); y < foundationBaseTile.y() + foundationSize.height(); ++ y) {
  //   for (int x = foundationBaseTile.x(); x < foundationBaseTile.x() + foundationSize.width(); ++ x) {
  //     if (map->occupiedAt(x, y)) {
  //       return false;
  //     }
  //   }
  // }
  
  // 2) Check whether the maximum elevation difference within the building space does not exceed 2.
  //    TODO: I made this criterion up without testing it; is that actually how the original game works?
  // TODO: This criterion must not apply to farms.
  int minElevation = std::numeric_limits<int>::max();
  int maxElevation = std::numeric_limits<int>::min();
  for (int y = foundationBaseTile.y(); y <= foundationBaseTile.y() + foundationSize.height(); ++ y) {
    for (int x = foundationBaseTile.x(); x <= foundationBaseTile.x() + foundationSize.width(); ++ x) {
      int elevation = map->elevationAt(x, y);
      minElevation = std::min(minElevation, elevation);
      maxElevation = std::max(maxElevation, elevation);
    }
  }
  
  if (maxElevation - minElevation > 2) {
    return false;
  }
  
  return true;
}

void RenderWindow::ShowDefaultCommandButtonsForSelection() {
  for (int row = 0; row < kCommandButtonRows; ++ row) {
    for (int col = 0; col < kCommandButtonCols; ++ col) {
      commandButtons[row][col].SetInvisible();
    }
  }
  
  // Check whether a single type of building is selected only.
  // In this case, show the buttons corresponding to this building type.
  bool singleBuildingTypeSelected = true;
  bool atLeastOneBuildingFullyConstructed = false;
  BuildingType selectedBuildingType = BuildingType::NumBuildings;
  for (usize i = 0; i < selection.size(); ++ i) {
    u32 objectId = selection[i];
    ClientObject* object = map->GetObjects().at(objectId);
    
    if (object->isUnit()) {
      singleBuildingTypeSelected = false;
      break;
    } else if (object->isBuilding()) {
      ClientBuilding* building = static_cast<ClientBuilding*>(object);
      
      if (building->GetBuildPercentage() == 100) {
        atLeastOneBuildingFullyConstructed = true;
      }
      
      if (i == 0) {
        selectedBuildingType = building->GetType();
      } else if (selectedBuildingType != building->GetType()) {
        singleBuildingTypeSelected = false;
        break;
      }
    }
  }
  if (!selection.empty() && singleBuildingTypeSelected && atLeastOneBuildingFullyConstructed) {
    ClientBuildingType::GetBuildingTypes()[static_cast<int>(selectedBuildingType)].SetCommandButtons(commandButtons);
    return;
  }
  
  // If at least one own villager is selected, show the build buttons.
  bool atLeastOneOwnVillagerSelected = false;
  for (usize i = 0; i < selection.size(); ++ i) {
    u32 objectId = selection[i];
    ClientObject* object = map->GetObjects().at(objectId);
    
    if (object->isUnit()) {
      ClientUnit* unit = static_cast<ClientUnit*>(object);
      if (unit->GetPlayerIndex() == match->GetPlayerIndex() &&
          IsVillager(unit->GetType())) {
        atLeastOneOwnVillagerSelected = true;
        break;
      }
    }
  }
  if (atLeastOneOwnVillagerSelected) {
    commandButtons[0][0].SetAction(CommandButton::ActionType::BuildEconomyBuilding, buildEconomyBuildingsTexture.get());
    commandButtons[0][1].SetAction(CommandButton::ActionType::BuildMilitaryBuilding, buildMilitaryBuildingsTexture.get());
  }
}

void RenderWindow::ShowEconomyBuildingCommandButtons() {
  showingEconomyBuildingCommandButtons = true;
  
  for (int row = 0; row < kCommandButtonRows; ++ row) {
    for (int col = 0; col < kCommandButtonCols; ++ col) {
      commandButtons[row][col].SetInvisible();
    }
  }
  
  commandButtons[0][0].SetBuilding(BuildingType::House);
  commandButtons[0][1].SetBuilding(BuildingType::Mill);
  commandButtons[0][2].SetBuilding(BuildingType::MiningCamp);
  commandButtons[0][3].SetBuilding(BuildingType::LumberCamp);
  commandButtons[0][4].SetBuilding(BuildingType::Dock);
  
  commandButtons[2][3].SetAction(CommandButton::ActionType::ToggleBuildingsCategory, toggleBuildingsCategoryTexture.get());
  commandButtons[2][4].SetAction(CommandButton::ActionType::Quit, quitTexture.get());
}

void RenderWindow::ShowMilitaryBuildingCommandButtons() {
  showingEconomyBuildingCommandButtons = false;
  
  for (int row = 0; row < kCommandButtonRows; ++ row) {
    for (int col = 0; col < kCommandButtonCols; ++ col) {
      commandButtons[row][col].SetInvisible();
    }
  }
  
  commandButtons[0][0].SetBuilding(BuildingType::Barracks);
  commandButtons[1][0].SetBuilding(BuildingType::Outpost);
  commandButtons[1][1].SetBuilding(BuildingType::PalisadeWall);
  commandButtons[2][1].SetBuilding(BuildingType::PalisadeGate);
  
  commandButtons[2][3].SetAction(CommandButton::ActionType::ToggleBuildingsCategory, toggleBuildingsCategoryTexture.get());
  commandButtons[2][4].SetAction(CommandButton::ActionType::Quit, quitTexture.get());
}

void RenderWindow::initializeGL() {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  CHECK_OPENGL_NO_ERROR();
  
  // Create a vertex array object (VAO).
  // TODO: Handle this properly instead of just creating a single global object at the start.
  GLuint vao;
  f->glGenVertexArrays(1, &vao);
  f->glBindVertexArray(vao);
  CHECK_OPENGL_NO_ERROR();
  
  // Create a second OpenGL context that shares names with the rendering context.
  // This can then be used to load resources in the background.
  QOpenGLContext* loadingContext = new QOpenGLContext();
  loadingContext->setScreen(context()->screen());
  loadingContext->setFormat(context()->format());
  loadingContext->setShareContext(context());
  if (!loadingContext->create()) {
    LOG(ERROR) << "Failed to create an OpenGL context for resource loading";
    // TODO: Exit gracefully
  }
  
  // Create the offscreen surface for resource loading. Note that for compatibility, this must
  // be created and destroyed in the main thread.
  loadingSurface = new QOffscreenSurface(loadingContext->screen());
  loadingSurface->setFormat(loadingContext->format());
  loadingSurface->create();
  if (!loadingSurface->isValid()) {
    LOG(ERROR) << "Failed to create a QOffscreenSurface for resource loading";
    // TODO: Exit gracefully
  }
  
  // Create the resource loading thread.
  loadingThread = new LoadingThread(this, loadingContext, loadingSurface);
  loadingContext->moveToThread(loadingThread);
  connect(loadingThread, &LoadingThread::finished, this, &RenderWindow::LoadingFinished);
  
  isLoading = true;
  loadingStep = 0;
  maxLoadingStep = 57;
  loadingThread->start();
  
  // Create resources right now which are required for rendering the loading screen:
  
  // Load the UI shaders.
  uiShader.reset(new UIShader());
  uiSingleColorShader.reset(new UISingleColorShader());
  
  // Create a buffer containing a single point for sprite rendering.
  f->glGenBuffers(1, &pointBuffer);
  f->glBindBuffer(GL_ARRAY_BUFFER, pointBuffer);
  int elementSizeInBytes = 3 * sizeof(float);
  float data[] = {
      0.f, 0.f, 0};
  f->glBufferData(GL_ARRAY_BUFFER, 1 * elementSizeInBytes, data, GL_DYNAMIC_DRAW);
  CHECK_OPENGL_NO_ERROR();
  
  // Load the loading icon.
  loadingIcon.reset(new Texture());
  
  loadingIcon->Load(
      QImage(QString::fromStdString((graphicsPath.parent_path().parent_path() / "wpfg" / "resources" / "campaign" / "campaign_icon_2swords.png").string())),
      GL_CLAMP, GL_NEAREST, GL_NEAREST);
  
  // Remember the render start time.
  renderStartTime = Clock::now();
}

void RenderWindow::paintGL() {
  // Regularly print timing info
  static int renderStatisticsCounter = 0;
  ++ renderStatisticsCounter;
  if (renderStatisticsCounter % (3 * 120) == 0) {
    Timing::print(std::cout);
  }
  
  // Render loading screen?
  if (isLoading) {
    // Parse server messages.
    gameController->ParseMessagesUntil(/*displayedServerTime*/ 0);
    
    // Switch to the game once it starts.
    if (connection->GetServerTimeToDisplayNow() >= gameController->GetGameStartServerTimeSeconds()) {
      isLoading = false;
      
      // Unload loading screen resources
      loadingIcon.reset();
      loadingTextDisplay.reset();
      
      // Avoid possible jumps directly after the game start
      lastScrollGetTime = Clock::now();
    } else {
      Timer renderTimer("paintGL() for loading screen");
      RenderLoadingScreen();
      return;
    }
  }
  
  // Render game.
  Timer renderTimer("paintGL() for game");
  
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  CHECK_OPENGL_NO_ERROR();
  
  // Get the time for which to render the game state.
  // TODO: Predict the time at which the rendered frame will be displayed rather than taking the current time.
  TimePoint now = Clock::now();
  // TODO: Using elapsedSeconds for animation has been replaced with using displayedServerTime.
  // double elapsedSeconds = std::chrono::duration<double>(now - renderStartTime).count();
  
  // Update the game state to the server time that should be displayed.
  double displayedServerTime = connection->GetServerTimeToDisplayNow();
  if (displayedServerTime > lastDisplayedServerTime) {
    // 1) Parse messages until the displayed server time
    gameController->ParseMessagesUntil(displayedServerTime);
    
    // 2) Smoothly update the game state to exactly the displayed time point
    UpdateGameState(displayedServerTime);
    
    lastDisplayedServerTime = displayedServerTime;
    gameController->SetLastDisplayedServerTime(displayedServerTime);
    
    // Remove any objects that have been deleted from the selection.
    usize outputIndex = 0;
    for (usize i = 0; i < selection.size(); ++ i) {
      if (map->GetObjects().find(selection[i]) == map->GetObjects().end()) {
        continue;
      }
      if (outputIndex != i) {
        selection[outputIndex] = selection[i];
      }
      ++ outputIndex;
    }
    selection.resize(outputIndex);
  }
  
  // If a building in the selection has finished construction, update the command buttons
  // TODO: Currently we always update if we have any building selected
  bool haveBuildingSelected = false;
  for (u32 id : selection) {
    auto it = map->GetObjects().find(id);
    if (it != map->GetObjects().end()) {
      if (it->second->isBuilding()) {
        haveBuildingSelected = true;
        break;
      }
    }
  }
  if (haveBuildingSelected) {
    ShowDefaultCommandButtonsForSelection();
  }
  
  // Update scrolling and compute the view transformation.
  UpdateView(now);
  CHECK_OPENGL_NO_ERROR();
  
  // Set states for rendering.
  f->glDisable(GL_CULL_FACE);
  
  f->glActiveTexture(GL_TEXTURE0 + 1);
  f->glBindTexture(GL_TEXTURE_2D, playerColorsTexture->GetId());
  f->glActiveTexture(GL_TEXTURE0);
  
  // Clear background.
  f->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  CHECK_OPENGL_NO_ERROR();
  
  // Render the shadows.
  f->glEnable(GL_BLEND);
  f->glDisable(GL_DEPTH_TEST);
  // Set up blending such that colors are added (does not matter since we do not render colors),
  // and for alpha values, the maximum is used.
  f->glBlendEquationSeparate(GL_FUNC_ADD, GL_MAX);
  
  CHECK_OPENGL_NO_ERROR();
  RenderShadows(displayedServerTime);
  CHECK_OPENGL_NO_ERROR();
  
  // Render the map terrain.
  f->glBlendFunc(GL_ONE_MINUS_DST_ALPHA, GL_DST_ALPHA);  // blend with the shadows
  
  CHECK_OPENGL_NO_ERROR();
  map->Render(viewMatrix, graphicsPath);
  CHECK_OPENGL_NO_ERROR();
  
  f->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);  // reset the blend func to standard
  
  // Render selection outlines below buildings.
  CHECK_OPENGL_NO_ERROR();
  RenderSelectionGroundOutlines(displayedServerTime);
  CHECK_OPENGL_NO_ERROR();
  
  // Enable the depth buffer for sprite rendering.
  f->glEnable(GL_DEPTH_TEST);
  f->glDepthFunc(GL_LEQUAL);
  
  // Render buildings.
  CHECK_OPENGL_NO_ERROR();
  RenderBuildings(displayedServerTime);
  CHECK_OPENGL_NO_ERROR();
  
  // Render the building foundation under the cursor.
  if (constructBuildingType != BuildingType::NumBuildings) {
    CHECK_OPENGL_NO_ERROR();
    RenderBuildingFoundation(displayedServerTime);
    CHECK_OPENGL_NO_ERROR();
  }
  
  // Render outlines.
  // Disable depth writing.
  f->glDepthMask(GL_FALSE);
  // Let only pass through those fragments which are *behind* the depth values in the depth buffer.
  // So we only render outlines in places where something is occluded.
  f->glDepthFunc(GL_GREATER);
  
  CHECK_OPENGL_NO_ERROR();
  RenderOutlines(displayedServerTime);
  CHECK_OPENGL_NO_ERROR();
  
  // Render units.
  f->glDepthMask(GL_TRUE);
  f->glDepthFunc(GL_LEQUAL);
  
  CHECK_OPENGL_NO_ERROR();
  RenderUnits(displayedServerTime);
  CHECK_OPENGL_NO_ERROR();
  
  // Render move-to marker.
  // This should be rendered after the last unit at the moment, since it contains semi-transparent
  // pixels which do currenly write to the z-buffer.
  CHECK_OPENGL_NO_ERROR();
  RenderMoveToMarker(now);
  CHECK_OPENGL_NO_ERROR();
  
  // Render health bars.
  f->glClear(GL_DEPTH_BUFFER_BIT);
  f->glDisable(GL_BLEND);
  
  CHECK_OPENGL_NO_ERROR();
  RenderHealthBars(displayedServerTime);
  CHECK_OPENGL_NO_ERROR();
  
  // Render selection box.
  if (dragging) {
    std::vector<QPointF> vertices(4);
    
    vertices[0] = dragStartPos;
    vertices[1] = QPointF(dragStartPos.x(), lastCursorPos.y());
    vertices[2] = lastCursorPos;
    vertices[3] = QPointF(lastCursorPos.x(), dragStartPos.y());
    
    RenderClosedPath(1.1f, qRgba(0, 0, 0, 255), vertices, QPointF(2, 2));
    RenderClosedPath(1.1f, qRgba(255, 255, 255, 255), vertices, QPointF(0, 0));
  }
  
  // Render game UI.
  // TODO: Would it be faster to render this at the start and then prevent rendering over the UI pixels,
  //       for example by setting the z-buffer such that no further pixel will be rendered there?
  CHECK_OPENGL_NO_ERROR();
  RenderGameUI(displayedServerTime);
  CHECK_OPENGL_NO_ERROR();
}

void RenderWindow::resizeGL(int width, int height) {
  widgetWidth = width;
  widgetHeight = height;
}

void RenderWindow::mousePressEvent(QMouseEvent* event) {
  if (!map) {
    return;
  }
  
  bool isUIClick = IsUIAt(event->x(), event->y());
  
  if (event->button() == Qt::LeftButton) {
    // Has a command button been pressed?
    for (int row = 0; row < kCommandButtonRows; ++ row) {
      for (int col = 0; col < kCommandButtonCols; ++ col) {
        if (commandButtons[row][col].IsPointInButton(event->pos())) {
          pressedCommandButtonRow = row;
          pressedCommandButtonCol = col;
          return;
        }
      }
    }
    
    if (isUIClick) {
      return;
    }
    
    // Place a building foundation?
    if (constructBuildingType != BuildingType::NumBuildings) {
      ignoreLeftMouseRelease = true;
      
      QPoint foundationBaseTile;
      bool canBePlacedHere = CanBuildingFoundationBePlacedHere(constructBuildingType, lastCursorPos, &foundationBaseTile);
      if (canBePlacedHere) {
        // Get the IDs of all selected villagers
        std::vector<u32> selectedVillagerIds;
        selectedVillagerIds.reserve(selection.size());
        for (u32 id : selection) {
          auto it = map->GetObjects().find(id);
          if (it != map->GetObjects().end()) {
            if (it->second->isUnit()) {
              ClientUnit* unit = static_cast<ClientUnit*>(it->second);
              if (IsVillager(unit->GetType())) {
                selectedVillagerIds.push_back(it->first);
              }
            }
          }
        }
        
        connection->Write(CreatePlaceBuildingFoundationMessage(constructBuildingType, foundationBaseTile, selectedVillagerIds));
        
        constructBuildingType = BuildingType::NumBuildings;
        return;
      }
    }
    
    // Clicked into the game area. Remember the position in case the user
    // starts dragging the mouse later.
    dragStartPos = event->pos();
    possibleDragStart = true;
    dragging = false;
  } else if (event->button() == Qt::RightButton &&
             !isUIClick) {
    bool haveOwnUnitSelected = false;
    bool haveBuildingSelected = false;
    std::vector<ClientObject*> selectedObject(selection.size());
    
    for (usize i = 0; i < selection.size(); ++ i) {
      u32 id = selection[i];
      auto objectIt = map->GetObjects().find(id);
      if (objectIt == map->GetObjects().end()) {
        selectedObject[i] = nullptr;
      } else {
        selectedObject[i] = objectIt->second;
        haveBuildingSelected |= objectIt->second->isBuilding();
        haveOwnUnitSelected |= objectIt->second->isUnit() && objectIt->second->GetPlayerIndex() == match->GetPlayerIndex();
      }
    }
    
    if (haveOwnUnitSelected && !haveBuildingSelected) {
      // Command units.
      std::vector<bool> unitsCommanded(selection.size(), false);
      
      // Check whether the units are right-clicked onto a suitable target object.
      // TODO: In the target selection, factor in whether villagers / military units are selected to prefer selecting suitable targets.
      //       Also, exclude own units (except when commanding monks, or targeting transport ships, siege towers, etc.)
      u32 targetObjectId;
      if (GetObjectToSelectAt(event->x(), event->y(), &targetObjectId, &selection, false, true)) {
        // Command all selected units that can interact with the returned target object to it.
        ClientObject* targetObject = map->GetObjects().at(targetObjectId);
        
        std::vector<u32> suitableUnits;
        suitableUnits.reserve(selection.size());
        for (usize i = 0; i < selection.size(); ++ i) {
          if (!unitsCommanded[i] &&
              selectedObject[i] &&
              GetInteractionType(selectedObject[i], targetObject) != InteractionType::Invalid) {
            suitableUnits.push_back(selection[i]);
            unitsCommanded[i] = true;
          }
        }
        
        if (!suitableUnits.empty()) {
          connection->Write(CreateSetTargetMessage(suitableUnits, targetObjectId));
          
          // Make the ground outline of the target flash green three times
          LetObjectGroundOutlineFlash(targetObjectId);
        }
      }
      
      // Send the remaining selected units to the clicked map coordinate.
      QPointF projectedCoord = ScreenCoordToProjectedCoord(event->x(), event->y());
      if (map->ProjectedCoordToMapCoord(projectedCoord, &moveToMapCoord)) {
        std::vector<u32> remainingUnits;
        remainingUnits.reserve(selection.size());
        for (usize i = 0; i < selection.size(); ++ i) {
          if (!unitsCommanded[i]) {
            remainingUnits.push_back(selection[i]);
            unitsCommanded[i] = true;
          }
        }
        
        if (!remainingUnits.empty()) {
          // Send the move command to the server.
          connection->Write(CreateMoveToMapCoordMessage(remainingUnits, moveToMapCoord));
          
          // Show the move-to marker.
          moveToTime = Clock::now();
          haveMoveTo = true;
        }
      }
    }
  }
}

void RenderWindow::mouseMoveEvent(QMouseEvent* event) {
  // Manually buffer the event. This is to improve performance, since we then
  // only react to the last event that is in the queue. By default, Qt would do this
  // itself, however, we explicitly disable it by disabling the Qt::AA_CompressHighFrequencyEvents
  // attribute, which was necessary to fix wheel events getting buffered over a far too long
  // time window in cases where the event loop was somewhat busy.
  if (!haveMouseMoveEvent) {
    // Queue handling the event at the back of the event queue.
    QMetaObject::invokeMethod(this, &RenderWindow::HandleMouseMoveEvent, Qt::QueuedConnection);
    haveMouseMoveEvent = true;
  }
  lastMouseMoveEventPos = event->pos();
  lastMouseMoveEventButtons = event->buttons();
}

void RenderWindow::HandleMouseMoveEvent() {
  haveMouseMoveEvent = false;
  
  if (!map) {
    return;
  }
  
  lastCursorPos = lastMouseMoveEventPos;
  
  if (possibleDragStart) {
    if ((lastCursorPos - dragStartPos).manhattanLength() >= QApplication::startDragDistance()) {
      dragging = true;
    }
  }
  
  // If a command button has been pressed but the cursor moves away from it, abort the button press.
  if (pressedCommandButtonRow >= 0 &&
      pressedCommandButtonCol >= 0 &&
      !commandButtons[pressedCommandButtonRow][pressedCommandButtonCol].IsPointInButton(lastMouseMoveEventPos)) {
    pressedCommandButtonRow = -1;
    pressedCommandButtonCol = -1;
  }
  
  // If hovering over the game area, possibly change the cursor to indicate possible interactions.
  QCursor* cursor = &defaultCursor;
  if (!IsUIAt(lastMouseMoveEventPos.x(), lastMouseMoveEventPos.y())) {
    u32 targetObjectId;
    if (GetObjectToSelectAt(lastMouseMoveEventPos.x(), lastMouseMoveEventPos.y(), &targetObjectId, &selection, false, true)) {
      ClientObject* targetObject = map->GetObjects().at(targetObjectId);
      for (u32 id : selection) {
        auto it = map->GetObjects().find(id);
        if (it != map->GetObjects().end()) {
          switch (GetInteractionType(it->second, targetObject)) {
          case InteractionType::Construct: cursor = &buildCursor; break;
          case InteractionType::Attack: cursor = &attackCursor; break;
          case InteractionType::DropOffResource: cursor = &defaultCursor; break;  // TODO: Use the different drop-off cursors
          case InteractionType::CollectBerries: cursor = &gatherCursor; break;
          case InteractionType::CollectWood: cursor = &chopCursor; break;
          case InteractionType::CollectGold: cursor = &mineGoldCursor; break;
          case InteractionType::CollectStone: cursor = &mineStoneCursor; break;
          case InteractionType::Invalid: continue;
          }
          break;
        }
      }
    }
  }
  setCursor(*cursor);
}

void RenderWindow::mouseReleaseEvent(QMouseEvent* event) {
  if (!map) {
    return;
  }
  
  bool isUIClick = IsUIAt(event->x(), event->y());
  
  if (event->button() == Qt::LeftButton) {
    possibleDragStart = false;
    
    if (ignoreLeftMouseRelease) {
      dragging = false;
      ignoreLeftMouseRelease = false;
      return;
    }
    
    if (dragging) {
      BoxSelection(dragStartPos, event->pos());
      
      dragging = false;
      return;
    }
    
    if (pressedCommandButtonRow >= 0 &&
        pressedCommandButtonCol >= 0) {
      CommandButton& button = commandButtons[pressedCommandButtonRow][pressedCommandButtonCol];
      button.Pressed(selection, gameController.get());
      pressedCommandButtonRow = -1;
      pressedCommandButtonCol = -1;
      
      // Handle building construction.
      if (button.GetType() == CommandButton::Type::ConstructBuilding &&
          gameController->GetLatestKnownResourceAmount().CanAfford(GetBuildingCost(button.GetBuildingConstructionType()))) {
        constructBuildingType = button.GetBuildingConstructionType();
      }
      
      // "Action" buttons are handled here.
      if (button.GetType() == CommandButton::Type::Action) {
        switch (button.GetActionType()) {
        case CommandButton::ActionType::BuildEconomyBuilding:
          ShowEconomyBuildingCommandButtons();
          break;
        case CommandButton::ActionType::BuildMilitaryBuilding:
          ShowMilitaryBuildingCommandButtons();
          break;
        case CommandButton::ActionType::ToggleBuildingsCategory:
          if (showingEconomyBuildingCommandButtons) {
            ShowMilitaryBuildingCommandButtons();
          } else {
            ShowEconomyBuildingCommandButtons();
          }
          break;
        case CommandButton::ActionType::Quit:
          ShowDefaultCommandButtonsForSelection();
          constructBuildingType = BuildingType::NumBuildings;
          break;
        }
      }
      
      return;
    }
    
    if (isUIClick) {
      return;
    }
    
    u32 objectId;
    if (GetObjectToSelectAt(event->x(), event->y(), &objectId, &selection, true, false)) {
      // Note: We need to keep the selection during GetObjectToSelectAt() to make the
      // mechanism work which selects the next object on repeated clicks.
      ClearSelection();
      AddToSelection(objectId);
    } else {
      ClearSelection();
    }
    SelectionChanged();
  }
}

void RenderWindow::wheelEvent(QWheelEvent* event) {
  if (!map) {
    return;
  }
  
  double degrees = event->angleDelta().y() / 8.0;
  double numSteps = degrees / 15.0;
  
  double scaleFactor = std::pow(std::sqrt(2.0), numSteps);
  zoom *= scaleFactor;
}

void RenderWindow::keyPressEvent(QKeyEvent* event) {
  if (!map) {
    return;
  }
  
  if (event->key() == Qt::Key_Right) {
    scrollRightPressed = true;
    scrollRightPressTime = Clock::now();
  } else if (event->key() == Qt::Key_Left) {
    scrollLeftPressed = true;
    scrollLeftPressTime = Clock::now();
  } else if (event->key() == Qt::Key_Up) {
    scrollUpPressed = true;
    scrollUpPressTime = Clock::now();
  } else if (event->key() == Qt::Key_Down) {
    scrollDownPressed = true;
    scrollDownPressTime = Clock::now();
  }
}

void RenderWindow::keyReleaseEvent(QKeyEvent* event) {
  if (!map) {
    return;
  }
  
  if (event->key() == Qt::Key_Right) {
    scrollRightPressed = false;
    TimePoint now = Clock::now();
    double seconds = std::chrono::duration<double>(now - scrollRightPressTime).count();
    Scroll(scrollDistancePerSecond / zoom * seconds, 0, &scroll);
  } else if (event->key() == Qt::Key_Left) {
    scrollLeftPressed = false;
    TimePoint now = Clock::now();
    double seconds = std::chrono::duration<double>(now - scrollLeftPressTime).count();
    Scroll(-scrollDistancePerSecond / zoom * seconds, 0, &scroll);
  } else if (event->key() == Qt::Key_Up) {
    scrollUpPressed = false;
    TimePoint now = Clock::now();
    double seconds = std::chrono::duration<double>(now - scrollUpPressTime).count();
    Scroll(0, -scrollDistancePerSecond / zoom * seconds, &scroll);
  } else if (event->key() == Qt::Key_Down) {
    scrollDownPressed = false;
    TimePoint now = Clock::now();
    double seconds = std::chrono::duration<double>(now - scrollDownPressTime).count();
    Scroll(0, scrollDistancePerSecond / zoom * seconds, &scroll);
  }
}

#include "render_window.moc"
