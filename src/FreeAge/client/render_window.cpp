#include "FreeAge/client/render_window.hpp"

#include <math.h>

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
      int georgiaFontID,
      const Palettes& palettes,
      const std::filesystem::path& graphicsPath,
      const std::filesystem::path& cachePath,
      QWidget* parent)
    : QOpenGLWidget(parent),
      match(match),
      gameController(gameController),
      connection(connection),
      georgiaFont(QFont(QFontDatabase::applicationFontFamilies(georgiaFontID)[0])),
      palettes(palettes),
      graphicsPath(graphicsPath),
      cachePath(cachePath) {
  georgiaFontLarger = georgiaFont;
  georgiaFontLarger.setPixelSize(17);
  georgiaFontLarger.setBold(true);
  
  georgiaFontSmaller = georgiaFont;
  georgiaFontSmaller.setPixelSize(15);
  
  setAttribute(Qt::WA_OpaquePaintEvent);
  setAutoFillBackground(false);
  
  connect(this, &RenderWindow::LoadingProgressUpdated, this, &RenderWindow::SendLoadingProgress, Qt::QueuedConnection);
  
  // Receive mouse move events even if no mouse button is pressed
  setMouseTracking(true);
  
  // This may be faster than keeping partial updates possible
  setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
  
  // Set the default cursor
  setCursor(QCursor(QPixmap::fromImage(QImage((
      graphicsPath.parent_path().parent_path().parent_path().parent_path() / "widgetui" / "textures" / "ingame" / "cursor" / "default32x32.cur").c_str())),
      0, 0));
  
  // Do continuous rendering via a timer
  float framesPerSecondCap = 120;
  QTimer* timer = new QTimer(this);
  connect(timer, &QTimer::timeout, this, QOverload<>::of(&RenderWindow::update));
  // TODO: This is rounded to milliseconds, thus the FPS cap will be approximate
  timer->start(1000.f / framesPerSecondCap + 0.5f);
  
  // Initialize the view settings
  scroll = QPointF(0, 0);
  zoom = 1;
  
  
  map = nullptr;
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
  
  commandPanelTexture.reset();
  
  selectionPanelTexture.reset();
  singleObjectNameDisplay.reset();
  
  iconOverlayNormalTexture.reset();
  iconOverlayNormalExpensiveTexture.reset();
  iconOverlayHoverTexture.reset();
  iconOverlayActiveTexture.reset();
  
  uiShader.reset();
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
  
  resourcePanelTexture.reset(new Texture());
  resourcePanelTexture->Load(QImage((architecturePanelsPath / "resource-panel.png").c_str()), GL_CLAMP, GL_LINEAR, GL_LINEAR);
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
  commandPanelTexture->Load(QImage((architecturePanelsPath / "command-panel_extended.png").c_str()), GL_CLAMP, GL_LINEAR, GL_LINEAR);
  didLoadingStep();
  
  selectionPanelTexture.reset(new Texture());
  selectionPanelTexture->Load(QImage((architecturePanelsPath / "single-selection-panel.png").c_str()), GL_CLAMP, GL_LINEAR, GL_LINEAR);
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
    double seconds = std::chrono::duration<double>(atTime - scrollRightPressTime).count();
    projectedCoord += QPointF(scrollDistancePerSecond / zoom * seconds, 0);
  }
  if (scrollLeftPressed) {
    double seconds = std::chrono::duration<double>(atTime - scrollLeftPressTime).count();
    projectedCoord += QPointF(-scrollDistancePerSecond / zoom * seconds, 0);
  }
  if (scrollDownPressed) {
    double seconds = std::chrono::duration<double>(atTime - scrollDownPressTime).count();
    projectedCoord += QPointF(0, scrollDistancePerSecond / zoom * seconds);
  }
  if (scrollUpPressed) {
    double seconds = std::chrono::duration<double>(atTime - scrollUpPressTime).count();
    projectedCoord += QPointF(0, -scrollDistancePerSecond / zoom * seconds);
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
}

void RenderWindow::UpdateView(const TimePoint& now) {
  // Update scrolling state
  if (map) {
    scroll = GetCurrentScroll(now);
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

void RenderWindow::RenderShadows(double displayedServerTime) {
  auto& buildingTypes = ClientBuildingType::GetBuildingTypes();
  auto& unitTypes = ClientUnitType::GetUnitTypes();
  
  for (auto& object : map->GetObjects()) {
    // TODO: Use virtual functions here to reduce duplicated code among buildings and units?
    
    if (object.second->isBuilding()) {
      ClientBuilding& building = *static_cast<ClientBuilding*>(object.second);
      if (!buildingTypes[static_cast<int>(building.GetType())].GetSprite().HasShadow()) {
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
      if (!unitTypes[static_cast<int>(unit.GetType())].GetAnimations(unit.GetCurrentAnimation()).front().sprite.HasShadow()) {
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

void RenderWindow::RenderOutlines(double displayedServerTime) {
  auto& buildingTypes = ClientBuildingType::GetBuildingTypes();
  auto& unitTypes = ClientUnitType::GetUnitTypes();
  
  // TODO: Sort to minmize texture switches.
  for (auto& object : map->GetObjects()) {
    // TODO: Use virtual functions here to reduce duplicated code among buildings and units?
    
    if (object.second->isBuilding()) {
      ClientBuilding& building = *static_cast<ClientBuilding*>(object.second);
      if (!buildingTypes[static_cast<int>(building.GetType())].GetSprite().HasOutline()) {
        continue;
      }
      LOG(WARNING) << "DEBUG: Buildings with outline exist!";
      
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
      if (!unitTypes[static_cast<int>(unit.GetType())].GetAnimations(unit.GetCurrentAnimation()).front().sprite.HasOutline()) {
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
            1.f,  // TODO: Determine fill amount based on HP
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
            1.f,  // TODO: Determine fill amount based on HP
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

void RenderWindow::RenderGameUI() {
  constexpr float kUIScale = 0.5f;  // TODO: Make configurable
  
  const ResourceAmount& resources = gameController->GetCurrentResourceAmount(lastDisplayedServerTime);
  
  // Render the resource panel.
  RenderUIGraphic(
      0,
      0,
      kUIScale * resourcePanelTexture->GetWidth(),
      kUIScale * resourcePanelTexture->GetHeight(),
      *resourcePanelTexture,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  
  RenderUIGraphic(
      kUIScale * (17 + 0 * 200), kUIScale * 16,
      kUIScale * 83,
      kUIScale * 83,
      *resourceWoodTexture,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  if (!woodTextDisplay) {
    woodTextDisplay.reset(new TextDisplay());
  }
  woodTextDisplay->Render(
      georgiaFontSmaller,
      qRgba(255, 255, 255, 255),
      QString::number(resources.wood()),
      QRect(kUIScale * (17 + 0 * 200 + 83 + 16),
            kUIScale * 16,
            kUIScale * 82,
            kUIScale * 83),
      Qt::AlignLeft | Qt::AlignVCenter,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  
  RenderUIGraphic(
      kUIScale * (17 + 1 * 200), kUIScale * 16,
      kUIScale * 83,
      kUIScale * 83,
      *resourceFoodTexture,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  if (!foodTextDisplay) {
    foodTextDisplay.reset(new TextDisplay());
  }
  foodTextDisplay->Render(
      georgiaFontSmaller,
      qRgba(255, 255, 255, 255),
      QString::number(resources.food()),
      QRect(kUIScale * (17 + 1 * 200 + 83 + 16),
            kUIScale * 16,
            kUIScale * 82,
            kUIScale * 83),
      Qt::AlignLeft | Qt::AlignVCenter,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  
  RenderUIGraphic(
      kUIScale * (17 + 2 * 200), kUIScale * 16,
      kUIScale * 83,
      kUIScale * 83,
      *resourceGoldTexture,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  if (!goldTextDisplay) {
    goldTextDisplay.reset(new TextDisplay());
  }
  goldTextDisplay->Render(
      georgiaFontSmaller,
      qRgba(255, 255, 255, 255),
      QString::number(resources.gold()),
      QRect(kUIScale * (17 + 2 * 200 + 83 + 16),
            kUIScale * 16,
            kUIScale * 82,
            kUIScale * 83),
      Qt::AlignLeft | Qt::AlignVCenter,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  
  RenderUIGraphic(
      kUIScale * (17 + 3 * 200), kUIScale * 16,
      kUIScale * 83,
      kUIScale * 83,
      *resourceStoneTexture,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  if (!stoneTextDisplay) {
    stoneTextDisplay.reset(new TextDisplay());
  }
  stoneTextDisplay->Render(
      georgiaFontSmaller,
      qRgba(255, 255, 255, 255),
      QString::number(resources.stone()),
      QRect(kUIScale * (17 + 3 * 200 + 83 + 16),
            kUIScale * 16,
            kUIScale * 82,
            kUIScale * 83),
      Qt::AlignLeft | Qt::AlignVCenter,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  
  RenderUIGraphic(
      kUIScale * (17 + 4 * 200), kUIScale * 16,
      kUIScale * 83,
      kUIScale * 83,
      *popTexture,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  if (!popTextDisplay) {
    popTextDisplay.reset(new TextDisplay());
  }
  popTextDisplay->Render(
      georgiaFontSmaller,
      qRgba(255, 255, 255, 255),
      "4 / 5",  // TODO
      QRect(kUIScale * (17 + 4 * 200 + 83 + 16),
            kUIScale * 16,
            kUIScale * 82,
            kUIScale * 83),
      Qt::AlignLeft | Qt::AlignVCenter,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  
  RenderUIGraphic(
      kUIScale * (17 + 4 * 200 + 234), kUIScale * 24,
      kUIScale * 2 * 34,
      kUIScale * 2 * 34,
      *idleVillagerDisabledTexture,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  RenderUIGraphic(
      kUIScale * (17 + 4 * 200 + 234 + 154 - currentAgeShieldTexture->GetWidth() / 2), kUIScale * 0,
      kUIScale * currentAgeShieldTexture->GetWidth(),
      kUIScale * currentAgeShieldTexture->GetHeight(),
      *currentAgeShieldTexture,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  if (!currentAgeTextDisplay) {
    currentAgeTextDisplay.reset(new TextDisplay());
  }
  float currentAgeTextLeft = kUIScale * (17 + 4 * 200 + 234 + 154 + currentAgeShieldTexture->GetWidth() / 2);
  currentAgeTextDisplay->Render(
      georgiaFontLarger,
      qRgba(255, 255, 255, 255),
      tr("Dark Age"),
      QRect(currentAgeTextLeft,
            kUIScale * 16,
            kUIScale * (1623 - 8) - currentAgeTextLeft,
            kUIScale * 83),
      Qt::AlignHCenter | Qt::AlignVCenter,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  
  // Render the selection panel.
  float selectionPanelLeft = kUIScale * 539;
  float selectionPanelTop = widgetHeight - kUIScale * selectionPanelTexture->GetHeight();
  RenderUIGraphic(
      selectionPanelLeft,
      selectionPanelTop,
      kUIScale * selectionPanelTexture->GetWidth(),
      kUIScale * selectionPanelTexture->GetHeight(),
      *selectionPanelTexture,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  
  if (selection.size() == 1) {
    ClientObject* singleSelectedObject = map->GetObjects().at(selection.front());
    
    // Render name of single selected object
    if (!singleObjectNameDisplay) {
      singleObjectNameDisplay.reset(new TextDisplay());
    }
    singleObjectNameDisplay->Render(
        georgiaFontLarger,
        qRgba(58, 29, 21, 255),
        singleSelectedObject->GetObjectName(),
        QRect(selectionPanelLeft + kUIScale * 2*32,
              selectionPanelTop + kUIScale * 50 + kUIScale * 2*25,
              kUIScale * 2*172,
              kUIScale * 2*16),
        Qt::AlignLeft | Qt::AlignTop,
        uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
    
    // Render icon of single selected object
    const Texture* iconTexture = singleSelectedObject->GetIconTexture();
    if (iconTexture) {
      float iconInset = kUIScale * 4;
      RenderUIGraphic(
          selectionPanelLeft + kUIScale * 2*32 + iconInset,
          selectionPanelTop + kUIScale * 50 + kUIScale * 2*46 + iconInset,
          kUIScale * 2*60 - 2 * iconInset,
          kUIScale * 2*60 - 2 * iconInset,
          *iconTexture,
          uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
      RenderUIGraphic(
          selectionPanelLeft + kUIScale * 2*32,
          selectionPanelTop + kUIScale * 50 + kUIScale * 2*46,
          kUIScale * 2*60,
          kUIScale * 2*60,
          *iconOverlayNormalTexture,
          uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
    }
  }
  
  // Render the command panel.
  float commandPanelTop = widgetHeight - kUIScale * commandPanelTexture->GetHeight();
  RenderUIGraphic(
      0,
      commandPanelTop,
      kUIScale * commandPanelTexture->GetWidth(),
      kUIScale * commandPanelTexture->GetHeight(),
      *commandPanelTexture,
      uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
  
  float commandButtonsLeft = kUIScale * 49;
  float commandButtonsTop = commandPanelTop + kUIScale * 93;
  float commandButtonsRight = kUIScale * 499;
  float commandButtonsBottom = commandPanelTop + kUIScale * 370;
  
  float commandButtonSize = kUIScale * 80;
  
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
      
      commandButtons[row][col].Render(
          buttonLeft,
          buttonTop,
          commandButtonSize,
          kUIScale * 4,
          pressed ? *iconOverlayActiveTexture :
              (mouseOver ? *iconOverlayHoverTexture : *iconOverlayNormalTexture),
          uiShader.get(), widgetWidth, widgetHeight, pointBuffer);
    }
  }
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

bool RenderWindow::GetObjectToSelectAt(float x, float y, u32* objectId) {
  auto& buildingTypes = ClientBuildingType::GetBuildingTypes();
  
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
    
    if (object.second->isBuilding()) {
      ClientBuilding& building = *static_cast<ClientBuilding*>(object.second);
      const ClientBuildingType& buildingType = buildingTypes[static_cast<int>(building.GetType())];
      
      // Is the position within the tiles which the building stands on?
      bool addToList = false;
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
      QRectF projectedCoordsRect = building.GetRectInProjectedCoords(
          map.get(),
          lastDisplayedServerTime,
          false,
          false);
      if (!addToList && projectedCoordsRect.contains(projectedCoord)) {
        const Sprite::Frame& frame = buildingType.GetSprite().frame(building.GetFrameIndex(buildingType, lastDisplayedServerTime));
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
      
      if (addToList) {
        // TODO: Also consider distance between given position and sprite center in score?
        possibleSelectedObjects.emplace_back(object.first, computeScore(projectedCoordsRect, projectedCoord));
      }
    } else {  // if (object.second->isUnit()) {
      ClientUnit& unit = *static_cast<ClientUnit*>(object.second);
      
      // Is the position close to the unit sprite?
      constexpr float kExtendSize = 8;
      
      bool addToList = false;
      QRectF projectedCoordsRect = unit.GetRectInProjectedCoords(
          map.get(),
          lastDisplayedServerTime,
          false,
          false);
      projectedCoordsRect.adjust(-kExtendSize, -kExtendSize, kExtendSize, kExtendSize);
      if (!addToList && projectedCoordsRect.contains(projectedCoord)) {
        addToList = true;
      }
      
      if (addToList) {
        // TODO: Also consider distance between given position and sprite center in score?
        possibleSelectedObjects.emplace_back(object.first, computeScore(projectedCoordsRect, projectedCoord));
      }
    }
  }
  
  // Sort the detected objects by score.
  std::sort(possibleSelectedObjects.begin(), possibleSelectedObjects.end());
  
  // Given the list of objects at the given position, and considering the current selection,
  // return the next object to select.
  if (selection.size() == 1) {
    // If the selection is in the list, select the next object.
    for (usize i = 0; i < possibleSelectedObjects.size(); ++ i) {
      if (possibleSelectedObjects[i].id == selection.front()) {
        *objectId = possibleSelectedObjects[(i + 1) % possibleSelectedObjects.size()].id;
        return true;
      }
    }
  }
  
  if (!possibleSelectedObjects.empty()) {
    // NOTE: In this case, we don't need to have all possibleSelectedObjects sorted.
    //       We only need to get the one with the best score. However, it is not expected
    //       to get lots of objects in this list, so it probably does not matter.
    *objectId = possibleSelectedObjects.front().id;
    return true;
  }
  
  return false;
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
  for (int row = 0; row < kCommandButtonRows; ++ row) {
    for (int col = 0; col < kCommandButtonCols; ++ col) {
      commandButtons[row][col].SetInvisible();
    }
  }
  
  // Check whether a single type of building is selected only.
  // In this case, show the buttons corresponding to this building type.
  bool singleBuildingTypeSelected = true;
  BuildingType selectedBuildingType = BuildingType::NumBuildings;
  for (usize i = 0; i < selection.size(); ++ i) {
    u32 objectId = selection[i];
    ClientObject* object = map->GetObjects().at(objectId);
    
    if (object->isUnit()) {
      singleBuildingTypeSelected = false;
      break;
    } else if (object->isBuilding()) {
      ClientBuilding* building = static_cast<ClientBuilding*>(object);
      if (i == 0) {
        selectedBuildingType = building->GetType();
      } else if (selectedBuildingType != building->GetType()) {
        singleBuildingTypeSelected = false;
        break;
      }
    }
  }
  if (!selection.empty() && singleBuildingTypeSelected) {
    ClientBuildingType::GetBuildingTypes()[static_cast<int>(selectedBuildingType)].SetCommandButtons(commandButtons);
  }
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
  maxLoadingStep = 30;
  loadingThread->start();
  
  // Create resources right now which are required for rendering the loading screen:
  
  // Load the UI shader.
  uiShader.reset(new UIShader());
  
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
  if (isLoading) {
    // Switch to the game once it starts.
    if (connection->GetDisplayedServerTime() >= gameController->GetGameStartServerTimeSeconds()) {
      isLoading = false;
      
      // Unload loading screen resources
      loadingIcon.reset();
      loadingTextDisplay.reset();
    } else {
      RenderLoadingScreen();
      return;
    }
  }
  
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  CHECK_OPENGL_NO_ERROR();
  
  // Get the time for which to render the game state.
  // TODO: Predict the time at which the rendered frame will be displayed rather than taking the current time.
  TimePoint now = Clock::now();
  // TODO: Using elapsedSeconds for animation has been replaced with using displayedServerTime.
  // double elapsedSeconds = std::chrono::duration<double>(now - renderStartTime).count();
  
  // Update the game state to the server time that should be displayed.
  double displayedServerTime = connection->GetDisplayedServerTime();
  if (displayedServerTime > lastDisplayedServerTime) {
    UpdateGameState(displayedServerTime);
    lastDisplayedServerTime = displayedServerTime;
  }
  
  // Update scrolling and compute the view transformation.
  UpdateView(now);
  
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
  
  RenderShadows(displayedServerTime);
  
  // Render the map terrain.
  f->glBlendFunc(GL_ONE_MINUS_DST_ALPHA, GL_DST_ALPHA);  // blend with the shadows
  
  map->Render(viewMatrix);
  
  f->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);  // reset the blend func to standard
  
  // Enable the depth buffer for sprite rendering.
  f->glEnable(GL_DEPTH_TEST);
  f->glDepthFunc(GL_LEQUAL);
  
  // Render buildings.
  RenderBuildings(displayedServerTime);
  
  // Render outlines.
  // Disable depth writing.
  f->glDepthMask(GL_FALSE);
  // Let only pass through those fragments which are *behind* the depth values in the depth buffer.
  // So we only render outlines in places where something is occluded.
  f->glDepthFunc(GL_GREATER);
  
  RenderOutlines(displayedServerTime);
  
  // Render units.
  f->glDepthMask(GL_TRUE);
  f->glDepthFunc(GL_LEQUAL);
  
  RenderUnits(displayedServerTime);
  
  // Render move-to marker.
  // This should be rendered after the last unit at the moment, since it contains semi-transparent
  // pixels which do currenly write to the z-buffer.
  RenderMoveToMarker(now);
  
  // Render health bars.
  f->glClear(GL_DEPTH_BUFFER_BIT);
  f->glDisable(GL_BLEND);
  
  RenderHealthBars(displayedServerTime);
  
  // Render game UI.
  // TODO: Would it be faster to render this at the start and then prevent rendering over the UI pixels,
  //       for example by setting the z-buffer such that no further pixel will be rendered there?
  RenderGameUI();
}

void RenderWindow::resizeGL(int width, int height) {
  widgetWidth = width;
  widgetHeight = height;
}

void RenderWindow::mousePressEvent(QMouseEvent* event) {
  if (!map) {
    return;
  }
  
  if (event->button() == Qt::LeftButton) {
    for (int row = 0; row < kCommandButtonRows; ++ row) {
      for (int col = 0; col < kCommandButtonCols; ++ col) {
        if (commandButtons[row][col].IsPointInButton(event->pos())) {
          pressedCommandButtonRow = row;
          pressedCommandButtonCol = col;
        }
      }
    }
    
    // TODO: Remember position for dragging
  } else if (event->button() == Qt::RightButton) {
    bool haveOwnUnitSelected = false;
    bool haveBuildingSelected = false;
    
    for (u32 id : selection) {
      auto objectIt = map->GetObjects().find(id);
      if (objectIt == map->GetObjects().end()) {
        LOG(ERROR) << "Selected object ID not found in map->GetObjects().";
      } else {
        haveBuildingSelected |= objectIt->second->isBuilding();
        haveOwnUnitSelected |= objectIt->second->isUnit() && objectIt->second->GetPlayerIndex() == match->GetPlayerIndex();
      }
    }
    
    if (haveOwnUnitSelected && !haveBuildingSelected) {
      QPointF projectedCoord = ScreenCoordToProjectedCoord(event->x(), event->y());
      if (map->ProjectedCoordToMapCoord(projectedCoord, &moveToMapCoord)) {
        // Send the move command to the server.
        connection->Write(CreateMoveToMapCoordMessage(selection, moveToMapCoord));
        
        // Show the move-to marker.
        moveToTime = Clock::now();
        haveMoveTo = true;
      }
    }
  }
}

void RenderWindow::mouseMoveEvent(QMouseEvent* event) {
  if (!map) {
    return;
  }
  
  // TODO: Possibly manually batch these events together, since we disabled event batching globally.
  
  lastCursorPos = event->pos();
  
  // If a command button has been pressed but the cursor moves away from it, abort the button press.
  if (pressedCommandButtonRow >= 0 &&
      pressedCommandButtonCol >= 0 &&
      !commandButtons[pressedCommandButtonRow][pressedCommandButtonCol].IsPointInButton(event->pos())) {
    pressedCommandButtonRow = -1;
    pressedCommandButtonCol = -1;
  }
}

void RenderWindow::mouseReleaseEvent(QMouseEvent* event) {
  if (!map) {
    return;
  }
  
  if (event->button() == Qt::LeftButton) {
    if (pressedCommandButtonRow >= 0 &&
        pressedCommandButtonCol >= 0) {
      commandButtons[pressedCommandButtonRow][pressedCommandButtonCol].Pressed(selection, gameController.get());
      pressedCommandButtonRow = -1;
      pressedCommandButtonCol = -1;
      return;
    }
    
    // TODO: Only do this when not dragging and not clicking the UI
    
    u32 objectId;
    if (GetObjectToSelectAt(event->x(), event->y(), &objectId)) {
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
