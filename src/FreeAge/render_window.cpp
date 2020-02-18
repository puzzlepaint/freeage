#include "FreeAge/render_window.h"

#include <math.h>

#include <QKeyEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_2_Core>
#include <QTimer>

#include "FreeAge/health_bar.h"
#include "FreeAge/logging.h"
#include "FreeAge/opengl.h"
#include "FreeAge/sprite.h"
#include "FreeAge/sprite_atlas.h"
#include "FreeAge/timing.h"

RenderWindow::RenderWindow(const Palettes& palettes, const std::filesystem::path& graphicsPath, const std::filesystem::path& cachePath, QWidget* parent)
    : QOpenGLWidget(parent),
      palettes(palettes),
      graphicsPath(graphicsPath),
      cachePath(cachePath) {
  setAttribute(Qt::WA_OpaquePaintEvent);
  setAutoFillBackground(false);
  
  // Receive mouse move events even if no mouse button is pressed
  setMouseTracking(true);
  
  // This may be faster than keeping partial updates possible
  setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
  
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
  makeCurrent();
  // Destroy OpenGL resources here
  spriteShader.reset();
  shadowShader.reset();
  outlineShader.reset();
  healthBarShader.reset();
  if (map) {
    delete map;
  }
  unitTypes.clear();
  buildingTypes.clear();
  playerColorsTexture.reset();
  moveToSprite.reset();
  doneCurrent();
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

void RenderWindow::UpdateView(const TimePoint& now) {
  // Update scrolling state
  scroll = GetCurrentScroll(now);
  if (scrollRightPressed) { scrollRightPressTime = now; }
  if (scrollLeftPressed) { scrollLeftPressTime = now; }
  if (scrollUpPressed) { scrollUpPressTime = now; }
  if (scrollDownPressed) { scrollDownPressTime = now; }
  
  // Compute the view (projected-to-OpenGL) transformation.
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

void RenderWindow::RenderShadows(double elapsedSeconds) {
  // Building shadows.
  for (auto& buildingEntry : map->GetBuildings()) {
    ClientBuilding& building = buildingEntry.second;
    if (!buildingTypes[static_cast<int>(building.GetType())].GetSprite().HasShadow()) {
      continue;
    }
    
    QRectF projectedCoordsRect = building.GetRectInProjectedCoords(
        map,
        buildingTypes,
        elapsedSeconds,
        true,
        false);
    if (projectedCoordsRect.intersects(projectedCoordsViewRect)) {
      building.Render(
          map,
          buildingTypes,
          playerColors,
          shadowShader.get(),
          pointBuffer,
          viewMatrix,
          zoom,
          widgetWidth,
          widgetHeight,
          elapsedSeconds,
          true,
          false);
    }
  }
  
  // Unit shadows.
  for (auto& item : map->GetUnits()) {
    if (!unitTypes[static_cast<int>(item.second.GetType())].GetAnimations(item.second.GetCurrentAnimation()).front().sprite.HasShadow()) {
      continue;
    }
    
    QRectF projectedCoordsRect = item.second.GetRectInProjectedCoords(
        map,
        unitTypes,
        elapsedSeconds,
        true,
        false);
    if (projectedCoordsRect.intersects(projectedCoordsViewRect)) {
      item.second.Render(
          map,
          unitTypes,
          playerColors,
          shadowShader.get(),
          pointBuffer,
          viewMatrix,
          zoom,
          widgetWidth,
          widgetHeight,
          elapsedSeconds,
          true,
          false);
    }
  }
}

void RenderWindow::RenderBuildings(double elapsedSeconds) {
  // TODO: Sort to minmize texture switches.
  for (auto& buildingEntry : map->GetBuildings()) {
    ClientBuilding& building = buildingEntry.second;
    
    QRectF projectedCoordsRect = building.GetRectInProjectedCoords(
        map,
        buildingTypes,
        elapsedSeconds,
        false,
        false);
    if (projectedCoordsRect.intersects(projectedCoordsViewRect)) {
      // TODO: Multiple sprites may have nearly the same y-coordinate, as a result there can be flickering currently. Avoid this.
      building.Render(
          map,
          buildingTypes,
          playerColors,
          spriteShader.get(),
          pointBuffer,
          viewMatrix,
          zoom,
          widgetWidth,
          widgetHeight,
          elapsedSeconds,
          false,
          false);
    }
  }
}

void RenderWindow::RenderOutlines(double elapsedSeconds) {
  // Render the building outlines.
  // TODO: Sort to minmize texture switches.
  for (auto& buildingEntry : map->GetBuildings()) {
    ClientBuilding& building = buildingEntry.second;
    if (!buildingTypes[static_cast<int>(building.GetType())].GetSprite().HasOutline()) {
      continue;
    }
    LOG(WARNING) << "DEBUG: Buildings with outline exist!";
    
    QRectF projectedCoordsRect = building.GetRectInProjectedCoords(
        map,
        buildingTypes,
        elapsedSeconds,
        false,
        true);
    if (projectedCoordsRect.intersects(projectedCoordsViewRect)) {
      building.Render(
          map,
          buildingTypes,
          playerColors,
          outlineShader.get(),
          pointBuffer,
          viewMatrix,
          zoom,
          widgetWidth,
          widgetHeight,
          elapsedSeconds,
          false,
          true);
    }
  }
  
  // Render the unit outlines.
  // TODO: Sort to minmize texture switches.
  for (auto& item : map->GetUnits()) {
    ClientUnit& unit = item.second;
    if (!unitTypes[static_cast<int>(unit.GetType())].GetAnimations(unit.GetCurrentAnimation()).front().sprite.HasOutline()) {
      continue;
    }
    
    QRectF projectedCoordsRect = unit.GetRectInProjectedCoords(
        map,
        unitTypes,
        elapsedSeconds,
        false,
        true);
    if (projectedCoordsRect.intersects(projectedCoordsViewRect)) {
      unit.Render(
          map,
          unitTypes,
          playerColors,
          outlineShader.get(),
          pointBuffer,
          viewMatrix,
          zoom,
          widgetWidth,
          widgetHeight,
          elapsedSeconds,
          false,
          true);
    }
  }
}

void RenderWindow::RenderUnits(double elapsedSeconds) {
  // TODO: Sort to minmize texture switches.
  for (auto& item : map->GetUnits()) {
    ClientUnit& unit = item.second;
    
    QRectF projectedCoordsRect = unit.GetRectInProjectedCoords(
        map,
        unitTypes,
        elapsedSeconds,
        false,
        false);
    if (projectedCoordsRect.intersects(projectedCoordsViewRect)) {
      unit.Render(
          map,
          unitTypes,
          playerColors,
          spriteShader.get(),
          pointBuffer,
          viewMatrix,
          zoom,
          widgetWidth,
          widgetHeight,
          elapsedSeconds,
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

void RenderWindow::RenderHealthBars(double elapsedSeconds) {
  QRgb gaiaColor = qRgb(255, 255, 255);
  
  // Render health bars for buildings.
  for (auto& buildingEntry : map->GetBuildings()) {
    ClientBuilding& building = buildingEntry.second;
    if (!building.IsSelected()) {
      continue;
    }
    const ClientBuildingType& buildingType = buildingTypes[static_cast<int>(building.GetType())];
    
    QPointF centerProjectedCoord = building.GetCenterProjectedCoord(map, buildingTypes);
    QPointF healthBarCenter =
        centerProjectedCoord +
        QPointF(0, -1 * buildingType.GetHealthBarHeightAboveCenter(building.GetFrameIndex(buildingType, elapsedSeconds)));
    
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
  }
  
  // Render health bars for units.
  for (auto& item : map->GetUnits()) {
    ClientUnit& unit = item.second;
    if (!unit.IsSelected()) {
      continue;
    }
    const ClientUnitType& unitType = unitTypes[static_cast<int>(unit.GetType())];
    
    QPointF centerProjectedCoord = unit.GetCenterProjectedCoord(map);
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

struct PossibleSelectedObject {
  inline PossibleSelectedObject(ClientUnit* unit, int id, float score)
      : unit(unit),
        building(nullptr),
        id(id),
        score(score) {}
  
  inline PossibleSelectedObject(ClientBuilding* building, int id, float score)
      : unit(nullptr),
        building(building),
        id(id),
        score(score) {}
  
  inline bool operator< (const PossibleSelectedObject& other) const {
    return score < other.score;
  }
  
  ClientUnit* unit;
  ClientBuilding* building;
  int id;
  
  /// The smaller, the better.
  float score;
};

bool RenderWindow::GetObjectToSelectAt(float x, float y, int* objectId) {
  TimePoint now = Clock::now();
  double elapsedSeconds = std::chrono::duration<double>(now - gameStartTime).count();
  
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
  
  // Check buildings.
  for (auto& buildingEntry : map->GetBuildings()) {
    ClientBuilding& building = buildingEntry.second;
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
        map,
        buildingTypes,
        elapsedSeconds,
        false,
        false);
    if (!addToList && projectedCoordsRect.contains(projectedCoord)) {
      const Sprite::Frame& frame = buildingType.GetSprite().frame(building.GetFrameIndex(buildingType, elapsedSeconds));
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
      possibleSelectedObjects.emplace_back(&building, buildingEntry.first, computeScore(projectedCoordsRect, projectedCoord));
    }
  }
  
  // Check units.
  for (auto& item : map->GetUnits()) {
    ClientUnit& unit = item.second;
    
    // Is the position close to the unit sprite?
    constexpr float kExtendSize = 8;
    
    bool addToList = false;
    QRectF projectedCoordsRect = unit.GetRectInProjectedCoords(
        map,
        unitTypes,
        elapsedSeconds,
        false,
        false);
    projectedCoordsRect.adjust(-kExtendSize, -kExtendSize, kExtendSize, kExtendSize);
    if (!addToList && projectedCoordsRect.contains(projectedCoord)) {
      addToList = true;
    }
    
    if (addToList) {
      // TODO: Also consider distance between given position and sprite center in score?
      possibleSelectedObjects.emplace_back(&unit, item.first, computeScore(projectedCoordsRect, projectedCoord));
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
  for (int id : selection) {
    auto buildingIt = map->GetBuildings().find(id);
    if (buildingIt != map->GetBuildings().end()) {
      buildingIt->second.SetIsSelected(false);
    } else {
      auto unitIt = map->GetUnits().find(id);
      if (unitIt != map->GetUnits().end()) {
        unitIt->second.SetIsSelected(false);
      }
    }
  }
  
  selection.clear();
}

void RenderWindow::AddToSelection(int objectId) {
  selection.push_back(objectId);
  
  auto buildingIt = map->GetBuildings().find(objectId);
  if (buildingIt != map->GetBuildings().end()) {
    buildingIt->second.SetIsSelected(true);
  } else {
    auto unitIt = map->GetUnits().find(objectId);
    if (unitIt != map->GetUnits().end()) {
      unitIt->second.SetIsSelected(true);
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
  
  // Create shaders.
  spriteShader.reset(new SpriteShader(false, false));
  shadowShader.reset(new SpriteShader(true, false));
  outlineShader.reset(new SpriteShader(false, true));
  healthBarShader.reset(new HealthBarShader());
  
  // Create player color palette texture.
  CreatePlayerColorPaletteTexture();
  spriteShader->GetProgram()->UseProgram();
  spriteShader->GetProgram()->SetUniform2f(spriteShader->GetPlayerColorsTextureSizeLocation(), playerColorsTextureWidth, playerColorsTextureHeight);
  spriteShader->GetProgram()->SetUniform1i(spriteShader->GetPlayerColorsTextureLocation(), 1);  // use GL_TEXTURE1
  f->glActiveTexture(GL_TEXTURE0 + 1);
  f->glBindTexture(GL_TEXTURE_2D, playerColorsTexture->GetId());
  f->glActiveTexture(GL_TEXTURE0);
  
  // Load unit resources.
  unitTypes.resize(static_cast<int>(UnitType::NumUnits));
  for (int unitType = 0; unitType < static_cast<int>(UnitType::NumUnits); ++ unitType) {
    if (!unitTypes[unitType].Load(static_cast<UnitType>(unitType), graphicsPath, cachePath, palettes)) {
      LOG(ERROR) << "Exiting because of a resource load error for unit " << unitType << ".";
      exit(1);  // TODO: Exit gracefully
    }
  }
  
  // Load building resources.
  buildingTypes.resize(static_cast<int>(BuildingType::NumBuildings));
  for (int buildingType = 0; buildingType < static_cast<int>(BuildingType::NumBuildings); ++ buildingType) {
    if (!buildingTypes[buildingType].Load(static_cast<BuildingType>(buildingType), graphicsPath, cachePath, palettes)) {
      LOG(ERROR) << "Exiting because of a resource load error for building " << buildingType << ".";
      exit(1);  // TODO: Exit gracefully
    }
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
  
  // Create a buffer containing a single point for sprite rendering
  f->glGenBuffers(1, &pointBuffer);
  f->glBindBuffer(GL_ARRAY_BUFFER, pointBuffer);
  int elementSizeInBytes = 3 * sizeof(float);
  float data[] = {
      0.f, 0.f, 0};
  f->glBufferData(GL_ARRAY_BUFFER, 1 * elementSizeInBytes, data, GL_DYNAMIC_DRAW);
  CHECK_OPENGL_NO_ERROR();
  
  // Output timings of the resource loading processes.
  Timing::print(std::cout);
  
  // Generate a map
  map = new Map(50, 50);
  map->GenerateRandomMap(buildingTypes);
  SetScroll(map->GetTownCenterLocation(0));
  map->LoadRenderResources();
  
  // Remember the game start time.
  gameStartTime = Clock::now();
}

void RenderWindow::paintGL() {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  CHECK_OPENGL_NO_ERROR();
  
  // Get the time for which to render the game state.
  // TODO: Predict the time at which the rendered frame will be displayed rather than taking the current time.
  TimePoint now = Clock::now();
  double elapsedSeconds = std::chrono::duration<double>(now - gameStartTime).count();
  
  // Update scrolling and compute the view transformation.
  UpdateView(now);
  
  // Set states for rendering.
  f->glDisable(GL_CULL_FACE);
  
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
  
  RenderShadows(elapsedSeconds);
  
  // Render the map terrain.
  f->glBlendFunc(GL_ONE_MINUS_DST_ALPHA, GL_DST_ALPHA);  // blend with the shadows
  
  map->Render(viewMatrix);
  
  f->glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA);  // reset the blend func to standard
  
  // Enable the depth buffer for sprite rendering.
  f->glEnable(GL_DEPTH_TEST);
  f->glDepthFunc(GL_LEQUAL);
  
  // Render buildings.
  RenderBuildings(elapsedSeconds);
  
  // Render outlines.
  // Disable depth writing.
  f->glDepthMask(GL_FALSE);
  // Let only pass through those fragments which are *behind* the depth values in the depth buffer.
  // So we only render outlines in places where something is occluded.
  f->glDepthFunc(GL_GREATER);
  
  RenderOutlines(elapsedSeconds);
  
  // Render units.
  f->glDepthMask(GL_TRUE);
  f->glDepthFunc(GL_LEQUAL);
  
  RenderUnits(elapsedSeconds);
  
  // Render move-to marker.
  // This should be rendered after the last unit at the moment, since it contains semi-transparent
  // pixels which do currenly write to the z-buffer.
  RenderMoveToMarker(now);
  
  // Render health bars.
  f->glClear(GL_DEPTH_BUFFER_BIT);
  f->glDisable(GL_BLEND);
  
  RenderHealthBars(elapsedSeconds);
}

void RenderWindow::resizeGL(int width, int height) {
  widgetWidth = width;
  widgetHeight = height;
}

void RenderWindow::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    // TODO: Remember position for dragging
  } else if (event->button() == Qt::RightButton) {
    bool haveUnitSelected = false;
    bool haveBuildingSelected = false;
    
    for (int id : selection) {
      auto buildingIt = map->GetBuildings().find(id);
      if (buildingIt != map->GetBuildings().end()) {
        haveBuildingSelected = true;
      } else {
        auto unitIt = map->GetUnits().find(id);
        if (unitIt != map->GetUnits().end()) {
          haveUnitSelected = true;
        }
      }
    }
    
    if (haveUnitSelected && !haveBuildingSelected) {
      QPointF projectedCoord = ScreenCoordToProjectedCoord(event->x(), event->y());
      if (map->ProjectedCoordToMapCoord(projectedCoord, &moveToMapCoord)) {
        moveToTime = Clock::now();
        haveMoveTo = true;
      }
    }
  }
}

void RenderWindow::mouseMoveEvent(QMouseEvent* /*event*/) {
  // TODO: Possibly manually batch these events together, since we disabled event batching globally.
}

void RenderWindow::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    // TODO: Only do this when not dragging
    
    int objectId;
    if (GetObjectToSelectAt(event->x(), event->y(), &objectId)) {
      // Note: We need to keep the selection during GetObjectToSelectAt() to make the
      // mechanism work which selects the next object on repeated clicks.
      ClearSelection();
      AddToSelection(objectId);
    } else {
      ClearSelection();
    }
  }
}

void RenderWindow::wheelEvent(QWheelEvent* event) {
  double degrees = event->angleDelta().y() / 8.0;
  double numSteps = degrees / 15.0;
  
  double scaleFactor = std::pow(std::sqrt(2.0), numSteps);
  zoom *= scaleFactor;
}

void RenderWindow::keyPressEvent(QKeyEvent* event) {
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
