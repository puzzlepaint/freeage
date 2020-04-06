#include "FreeAge/client/render_window.hpp"

#include <math.h>

#include <QApplication>
#include <QFontDatabase>
#include <QIcon>
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
#include "FreeAge/client/mod_manager.hpp"
#include "FreeAge/client/opengl.hpp"
#include "FreeAge/client/shader_color_dilation.hpp"
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
      const std::filesystem::path& graphicsSubPath,
      const std::filesystem::path& cachePath,
      QWindow* parent)
    : QOpenGLWindow(QOpenGLWindow::NoPartialUpdate, parent),
      uiScale(uiScale),
      match(match),
      gameController(gameController),
      connection(connection),
      georgiaFont(QFont(QFontDatabase::applicationFontFamilies(georgiaFontID)[0])),
      palettes(palettes),
      graphicsSubPath(graphicsSubPath),
      cachePath(cachePath) {
  setIcon(QIcon(":/free_age/free_age.png"));
  setTitle(tr("FreeAge"));
  
  georgiaFontLarger = georgiaFont;
  georgiaFontLarger.setPixelSize(uiScale * 2*17);
  georgiaFontLarger.setBold(true);
  
  georgiaFontLargerStrikeOut = georgiaFontLarger;
  georgiaFontLargerStrikeOut.setStrikeOut(true);
  
  georgiaFontSmaller = georgiaFont;
  georgiaFontSmaller.setPixelSize(uiScale * 2*15);
  
  georgiaFontHuge = georgiaFont;
  georgiaFontHuge.setPixelSize(uiScale * 2*40);
  georgiaFontHuge.setBold(true);
  
  connect(this, &RenderWindow::LoadingProgressUpdated, this, &RenderWindow::SendLoadingProgress, Qt::QueuedConnection);
  
  // Set the default cursor
  defaultCursor = QCursor(QPixmap::fromImage(QImage(
      GetModdedPathAsQString(std::filesystem::path("widgetui") / "textures" / "ingame" / "cursor" / "default32x32.cur"))),
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
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  // Initialize command buttons.
  for (int row = 0; row < kCommandButtonRows; ++ row) {
    for (int col = 0; col < kCommandButtonCols; ++ col) {
      commandButtons[row][col].UnloadPointBuffers();
    }
  }
  
  for (auto& item : bufferObjects) {
    f->glDeleteBuffers(1, &item.name);
  }
  
  loadingIcon.Unload();
  for (usize i = 0; i < playerNames.size(); ++ i) {
    playerNames[i].Destroy();
    playerNameShadowPointBuffers[i].Destroy();
  }
  
  menuDialog.Unload();
  menuTextDisplay.Destroy();
  menuButtonExit.Destroy();
  menuButtonExitText.Destroy();
  menuButtonResign.Destroy();
  menuButtonResignText.Destroy();
  menuButtonCancel.Destroy();
  menuButtonCancelText.Destroy();
  
  gameEndTextDisplay.Destroy();
  gameEndTextDisplayShadowPointBuffer.Destroy();
  
  menuPanel.Unload();
  menuButton.Destroy();
  objectivesButtonPointBuffer.Destroy();
  objectivesButtonDisabledTexture.reset();
  chatButtonPointBuffer.Destroy();
  chatButtonDisabledTexture.reset();
  diplomacyButtonPointBuffer.Destroy();
  diplomacyButtonDisabledTexture.reset();
  settingsButtonPointBuffer.Destroy();
  settingsButtonDisabledTexture.reset();
  
  resourcePanel.Unload();
  resourceWood.Unload();
  woodTextDisplay.Destroy();
  resourceFood.Unload();
  foodTextDisplay.Destroy();
  resourceGold.Unload();
  goldTextDisplay.Destroy();
  resourceStone.Unload();
  stoneTextDisplay.Destroy();
  pop.Unload();
  popTextDisplay.Destroy();
  idleVillagerDisabled.Unload();
  currentAgeShield.Unload();
  currentAgeTextDisplay.Destroy();
  
  gameTimeDisplay.Destroy();
  gameTimeDisplayShadowPointBuffer.Destroy();
  fpsAndPingDisplay.Destroy();
  fpsAndPingDisplayShadowPointBuffer.Destroy();
  
  commandPanel.Unload();
  buildEconomyBuildings.Unload();
  buildMilitaryBuildings.Unload();
  toggleBuildingsCategory.Unload();
  quit.Unload();
  
  selectionPanel.Unload();
  singleObjectNameDisplay.Destroy();
  hpDisplay.Destroy();
  carriedResourcesDisplay.Destroy();
  selectionPanelIconPointBuffer.Destroy();
  selectionPanelIconOverlayPointBuffer.Destroy();
  
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
  colorDilationShader.reset();
  
  if (map) {
    map->UnloadRenderResources();
    map.reset();
  }
  
  for (Decal* decal : groundDecals) {
    delete decal;
  }
  for (Decal* decal : occludingDecals) {
    delete decal;
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
  
  LOG(1) << "LoadResource() start";
  
  // Load cursors.
  std::filesystem::path cursorsSubPath = std::filesystem::path("widgetui") / "textures" / "ingame" / "cursor";
  attackCursor = QCursor(QPixmap::fromImage(QImage(GetModdedPathAsQString(cursorsSubPath / "attack32x32.cur"))), 0, 0);
  buildCursor = QCursor(QPixmap::fromImage(QImage(GetModdedPathAsQString(cursorsSubPath / "build32x32.cur"))), 0, 0);
  chopCursor = QCursor(QPixmap::fromImage(QImage(GetModdedPathAsQString(cursorsSubPath / "chop32x32.cur"))), 0, 0);
  gatherCursor = QCursor(QPixmap::fromImage(QImage(GetModdedPathAsQString(cursorsSubPath / "gather32x32.cur"))), 0, 0);
  mineGoldCursor = QCursor(QPixmap::fromImage(QImage(GetModdedPathAsQString(cursorsSubPath / "mine_gold32x32.cur"))), 0, 0);
  mineStoneCursor = QCursor(QPixmap::fromImage(QImage(GetModdedPathAsQString(cursorsSubPath / "mine_stone32x32.cur"))), 0, 0);
  didLoadingStep();
  
  LOG(1) << "LoadResource(): Cursors loaded";
  
  // Create shaders.
  colorDilationShader.reset(new ColorDilationShader());
  
  spriteShader.reset(new SpriteShader(false, false));
  spriteShader->GetProgram()->UseProgram(f);
  f->glUniform1i(spriteShader->GetTextureLocation(), 0);  // use GL_TEXTURE0
  didLoadingStep();
  LOG(1) << "LoadResource(): SpriteShader(false, false) loaded";
  
  shadowShader.reset(new SpriteShader(true, false));
  shadowShader->GetProgram()->UseProgram(f);
  f->glUniform1i(shadowShader->GetTextureLocation(), 0);  // use GL_TEXTURE0
  didLoadingStep();
  LOG(1) << "LoadResource(): SpriteShader(true, false) loaded";
  
  outlineShader.reset(new SpriteShader(false, true));
  outlineShader->GetProgram()->UseProgram(f);
  f->glUniform1i(outlineShader->GetTextureLocation(), 0);  // use GL_TEXTURE0
  didLoadingStep();
  LOG(1) << "LoadResource(): SpriteShader(false, true) loaded";
  
  healthBarShader.reset(new HealthBarShader());
  didLoadingStep();
  LOG(1) << "LoadResource(): Shaders loaded";
  
  // Create player color palette texture.
  spriteShader->GetProgram()->UseProgram(f);
  f->glUniform2f(spriteShader->GetPlayerColorsTextureSizeLocation(), playerColorsTextureWidth, playerColorsTextureHeight);
  f->glUniform1i(spriteShader->GetPlayerColorsTextureLocation(), 1);  // use GL_TEXTURE1
  f->glActiveTexture(GL_TEXTURE0 + 1);
  f->glBindTexture(GL_TEXTURE_2D, playerColorsTexture->GetId());
  f->glActiveTexture(GL_TEXTURE0);
  didLoadingStep();
  
  // Initialize command buttons.
  for (int row = 0; row < kCommandButtonRows; ++ row) {
    for (int col = 0; col < kCommandButtonCols; ++ col) {
      commandButtons[row][col].InitializePointBuffers();
    }
  }
  
  // Initialize text displays.
  woodTextDisplay.Initialize();
  foodTextDisplay.Initialize();
  goldTextDisplay.Initialize();
  stoneTextDisplay.Initialize();
  popTextDisplay.Initialize();
  currentAgeTextDisplay.Initialize();
  gameTimeDisplay.Initialize();
  gameTimeDisplayShadowPointBuffer.Initialize();
  fpsAndPingDisplay.Initialize();
  fpsAndPingDisplayShadowPointBuffer.Initialize();
  singleObjectNameDisplay.Initialize();
  hpDisplay.Initialize();
  carriedResourcesDisplay.Initialize();
  
  // Load unit resources.
  auto& unitTypes = ClientUnitType::GetUnitTypes();
  unitTypes.resize(static_cast<int>(UnitType::NumUnits));
  for (int unitType = 0; unitType < static_cast<int>(UnitType::NumUnits); ++ unitType) {
    if (!unitTypes[unitType].Load(static_cast<UnitType>(unitType), graphicsSubPath, cachePath, colorDilationShader.get(), palettes)) {
      LOG(ERROR) << "Exiting because of a resource load error for unit " << unitType << ".";
      exit(1);  // TODO: Exit gracefully
    }
    
    didLoadingStep();
  }
  
  // Load building resources.
  auto& buildingTypes = ClientBuildingType::GetBuildingTypes();
  buildingTypes.resize(static_cast<int>(BuildingType::NumBuildings));
  for (int buildingType = 0; buildingType < static_cast<int>(BuildingType::NumBuildings); ++ buildingType) {
    if (!buildingTypes[buildingType].Load(static_cast<BuildingType>(buildingType), graphicsSubPath, cachePath, colorDilationShader.get(), palettes)) {
      LOG(ERROR) << "Exiting because of a resource load error for building " << buildingType << ".";
      exit(1);  // TODO: Exit gracefully
    }
    
    didLoadingStep();
  }
  
  // Load "move to" sprite.
  moveToSprite.reset(new SpriteAndTextures());
  LoadSpriteAndTexture(
      GetModdedPath(graphicsSubPath.parent_path().parent_path() / "particles" / "textures" / "test_move" / "p_all_move_%04i.png").string().c_str(),
      (cachePath / "p_all_move_0000.png").string().c_str(),
      GL_CLAMP_TO_EDGE,
      colorDilationShader.get(),
      &moveToSprite->sprite,
      &moveToSprite->graphicTexture,
      &moveToSprite->shadowTexture,
      palettes);
  didLoadingStep();
  
  // Load game UI textures.
  const std::string architectureNameCaps = "ASIA";  // TODO: Choose depending on civilization
  const std::string architectureNameLower = "asia";  // TODO: Choose depending on civilization
  
  std::filesystem::path widgetuiTexturesSubPath = std::filesystem::path("widgetui") / "textures";
  std::filesystem::path architecturePanelsSubPath = widgetuiTexturesSubPath / "ingame" / "panels" / architectureNameCaps;
  std::filesystem::path ingameIconsSubPath = widgetuiTexturesSubPath / "ingame" / "icons";
  std::filesystem::path ingameActionsSubPath = widgetuiTexturesSubPath / "ingame" / "actions";
  std::filesystem::path menuButtonsSubPath = widgetuiTexturesSubPath / "menu" / "buttons";
  
  // Note: I profiled the loading below and replacing the QImage() variants with the mango variants
  // was significantly slower.
  // Initial times:
  //   0.0421275, 0.0420974, 0.0429374
  // With QImage loading replaced by mango loading:
  //   0.286818,  0.285423
  
  menuDialog.Load(GetModdedPath(widgetuiTexturesSubPath / "ingame" / "panels" / "menu_bg.png"));
  menuTextDisplay.Initialize();
  menuButtonExit.Load(
      menuButtonsSubPath / "button_wide_normal.png",
      menuButtonsSubPath / "button_wide_hover.png",
      menuButtonsSubPath / "button_wide_active.png",
      menuButtonsSubPath / "button_wide_disabled.png");
  menuButtonExitText.Initialize();
  // TODO: Do not load these button textures multiple times!
  menuButtonResign.Load(
      menuButtonsSubPath / "button_wide_normal.png",
      menuButtonsSubPath / "button_wide_hover.png",
      menuButtonsSubPath / "button_wide_active.png",
      menuButtonsSubPath / "button_wide_disabled.png");
  menuButtonResignText.Initialize();
  // TODO: Do not load these button textures multiple times!
  menuButtonCancel.Load(
      menuButtonsSubPath / "button_wide_normal.png",
      menuButtonsSubPath / "button_wide_hover.png",
      menuButtonsSubPath / "button_wide_active.png",
      menuButtonsSubPath / "button_wide_disabled.png");
  menuButtonCancelText.Initialize();
  didLoadingStep();
  
  gameEndTextDisplay.Initialize();
  gameEndTextDisplayShadowPointBuffer.Initialize();
  
  QImage menuPanelImage;
  menuPanel.Load(GetModdedPath(architecturePanelsSubPath / "menu-panel.png"), &menuPanelImage);
  menuPanelOpaquenessMap.Create(menuPanelImage);
  didLoadingStep();
  
  menuButton.Load(
      ingameIconsSubPath / "menu_menu_normal.png",
      ingameIconsSubPath / "menu_menu_hover.png",
      ingameIconsSubPath / "menu_menu_active.png",
      "");
  didLoadingStep();
  
  objectivesButtonPointBuffer.Initialize();
  objectivesButtonDisabledTexture.reset(new Texture());
  objectivesButtonDisabledTexture->Load(QImage(GetModdedPathAsQString(ingameIconsSubPath / "menu_objectives_disabled.png")), GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR);
  
  chatButtonPointBuffer.Initialize();
  chatButtonDisabledTexture.reset(new Texture());
  chatButtonDisabledTexture->Load(QImage(GetModdedPathAsQString(ingameIconsSubPath / "menu_chat_disabled.png")), GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR);
  
  diplomacyButtonPointBuffer.Initialize();
  diplomacyButtonDisabledTexture.reset(new Texture());
  diplomacyButtonDisabledTexture->Load(QImage(GetModdedPathAsQString(ingameIconsSubPath / "menu_diplomacy_disabled.png")), GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR);
  
  settingsButtonPointBuffer.Initialize();
  settingsButtonDisabledTexture.reset(new Texture());
  settingsButtonDisabledTexture->Load(QImage(GetModdedPathAsQString(ingameIconsSubPath / "menu_settings_disabled.png")), GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR);
  didLoadingStep();
  
  QImage resourcePanelImage;
  resourcePanel.Load(GetModdedPath(architecturePanelsSubPath / "resource-panel.png"), &resourcePanelImage);
  resourcePanelOpaquenessMap.Create(resourcePanelImage);
  didLoadingStep();
  
  resourceWood.Load(GetModdedPath(ingameIconsSubPath / "resource_wood.png"));
  didLoadingStep();
  
  resourceFood.Load(GetModdedPath(ingameIconsSubPath / "resource_food.png"));
  didLoadingStep();
  
  resourceGold.Load(GetModdedPath(ingameIconsSubPath / "resource_gold.png"));
  didLoadingStep();
  
  resourceStone.Load(GetModdedPath(ingameIconsSubPath / "resource_stone.png"));
  didLoadingStep();
  
  pop.Load(GetModdedPath(ingameIconsSubPath / "pop.png"));
  didLoadingStep();
  
  idleVillagerDisabled.Load(GetModdedPath(ingameIconsSubPath / "idle-villager_disabled.png"));
  didLoadingStep();
  
  currentAgeShield.Load(GetModdedPath(architecturePanelsSubPath / ("shield_dark_age_" + architectureNameLower + "_normal.png")));
  didLoadingStep();
  
  QImage commandPanelImage;
  commandPanel.Load(GetModdedPath(architecturePanelsSubPath / "command-panel_extended.png"), &commandPanelImage);
  commandPanelOpaquenessMap.Create(commandPanelImage);
  didLoadingStep();
  
  buildEconomyBuildings.Load(GetModdedPath(ingameActionsSubPath / "030_.png"), nullptr, TextureManager::Loader::Mango);
  didLoadingStep();
  
  buildMilitaryBuildings.Load(GetModdedPath(ingameActionsSubPath / "031_.png"), nullptr, TextureManager::Loader::Mango);
  didLoadingStep();
  
  toggleBuildingsCategory.Load(GetModdedPath(ingameActionsSubPath / "032_.png"), nullptr, TextureManager::Loader::Mango);
  didLoadingStep();
  
  quit.Load(GetModdedPath(ingameActionsSubPath / "000_.png"), nullptr, TextureManager::Loader::Mango);
  didLoadingStep();
  
  QImage selectionPanelImage;
  selectionPanel.Load(GetModdedPath(architecturePanelsSubPath / "single-selection-panel.png"), &selectionPanelImage);
  selectionPanelOpaquenessMap.Create(selectionPanelImage);
  didLoadingStep();
  
  selectionPanelIconPointBuffer.Initialize();
  selectionPanelIconOverlayPointBuffer.Initialize();
  
  iconOverlayNormalTexture.reset(new Texture());
  iconOverlayNormalTexture->Load(QImage(GetModdedPathAsQString(ingameIconsSubPath / "icon_overlay_normal.png")), GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR);
  didLoadingStep();
  
  iconOverlayNormalExpensiveTexture.reset(new Texture());
  iconOverlayNormalExpensiveTexture->Load(QImage(GetModdedPathAsQString(ingameIconsSubPath / "icon_overlay_normal_expensive.png")), GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR);
  didLoadingStep();
  
  iconOverlayHoverTexture.reset(new Texture());
  iconOverlayHoverTexture->Load(QImage(GetModdedPathAsQString(ingameIconsSubPath / "icon_overlay_hover.png")), GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR);
  didLoadingStep();
  
  iconOverlayActiveTexture.reset(new Texture());
  iconOverlayActiveTexture->Load(QImage(GetModdedPathAsQString(ingameIconsSubPath / "icon_overlay_active.png")), GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR);
  didLoadingStep();
  
  // Output timings of the resource loading processes and clear those statistics from further timing prints.
  LOG(INFO) << "Loading timings:";
  Timing::print(std::cout, kSortByTotal);
  Timing::reset();
  
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

QPointF RenderWindow::GetCurrentScroll(const TimePoint& atTime, bool* scrollApplied) {
  *scrollApplied = false;
  float effectiveZoom = ComputeEffectiveZoom();
  
  QPointF projectedCoord = map->MapCoordToProjectedCoord(scroll);
  if (scrollRightPressed) {
    double seconds = SecondsDuration(atTime - scrollRightPressTime).count();
    projectedCoord += QPointF(scrollDistancePerSecond / effectiveZoom * seconds, 0);
    *scrollApplied = true;
  }
  if (scrollLeftPressed) {
    double seconds = SecondsDuration(atTime - scrollLeftPressTime).count();
    projectedCoord += QPointF(-scrollDistancePerSecond / effectiveZoom * seconds, 0);
    *scrollApplied = true;
  }
  if (scrollDownPressed) {
    double seconds = SecondsDuration(atTime - scrollDownPressTime).count();
    projectedCoord += QPointF(0, scrollDistancePerSecond / effectiveZoom * seconds);
    *scrollApplied = true;
  }
  if (scrollUpPressed) {
    double seconds = SecondsDuration(atTime - scrollUpPressTime).count();
    projectedCoord += QPointF(0, -scrollDistancePerSecond / effectiveZoom * seconds);
    *scrollApplied = true;
  }
  
  if (borderScrollingEnabled) {
    double mouseImpactSeconds = SecondsDuration(atTime - lastScrollGetTime).count();
    
    if (lastCursorPos.x() == widgetWidth - 1) {
      projectedCoord += QPointF(scrollDistancePerSecond / effectiveZoom * mouseImpactSeconds, 0);
      *scrollApplied = true;
    }
    if (lastCursorPos.x() == 0) {
      projectedCoord += QPointF(-scrollDistancePerSecond / effectiveZoom * mouseImpactSeconds, 0);
      *scrollApplied = true;
    }
    if (lastCursorPos.y() == widgetHeight - 1) {
      projectedCoord += QPointF(0, scrollDistancePerSecond / effectiveZoom * mouseImpactSeconds);
      *scrollApplied = true;
    }
    if (lastCursorPos.y() == 0) {
      projectedCoord += QPointF(0, -scrollDistancePerSecond / effectiveZoom * mouseImpactSeconds);
      *scrollApplied = true;
    }
  }
  
  QPointF result;
  if (scrollApplied) {
    map->ProjectedCoordToMapCoord(projectedCoord, &result);
  } else {
    result = scroll;
  }
  return result;
}

void RenderWindow::AddDecal(Decal* decal) {
  if (decal->MayOccludeSprites()) {
    occludingDecals.push_back(decal);
  } else {
    groundDecals.push_back(decal);
  }
}

void RenderWindow::SendLoadingProgress(int progress) {
  connection->Write(CreateLoadingProgressMessage(progress));
}

void RenderWindow::LoadingFinished() {
  delete loadingSurface;
  delete loadingThread;
  
  // Notify the server about the loading being finished
  connection->Write(CreateLoadingFinishedMessage());
  
  LOG(INFO) << "DEBUG: Loading finished.";
}


RenderWindow::TextureAndPointBuffer::~TextureAndPointBuffer() {
  if (texture != nullptr) {
    LOG(ERROR) << "TextureAndPointBuffer object was destroyed without Unload() being called first.";
  }
}

bool RenderWindow::TextureAndPointBuffer::Load(const std::filesystem::path& path, QImage* qimage, TextureManager::Loader loader) {
  if (texture != nullptr) {
    LOG(ERROR) << "Load() called on already initialized TextureAndPointBuffer";
    return false;
  }
  
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  // Load the texture.
  texture.reset(new Texture());
  if (loader == TextureManager::Loader::QImage) {
    QImage image(path.string().c_str());
    if (image.isNull()) {
      return false;
    }
    if (qimage) {
      *qimage = image;
    }
    texture->Load(image, GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR);
  } else {
    CHECK(qimage == nullptr);
    texture->Load(path, GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR);
  }
  
  // Initialize the point buffer.
  f->glGenBuffers(1, &pointBuffer);
  f->glBindBuffer(GL_ARRAY_BUFFER, pointBuffer);
  int elementSizeInBytes = 3 * sizeof(float);
  f->glBufferData(GL_ARRAY_BUFFER, 1 * elementSizeInBytes, nullptr, GL_STREAM_DRAW);
  
  return true;
}

void RenderWindow::TextureAndPointBuffer::Unload() {
  if (texture) {
    QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
    
    f->glDeleteBuffers(1, &pointBuffer);
    texture.reset();
  }
}


RenderWindow::TextDisplayAndPointBuffer::~TextDisplayAndPointBuffer() {
  if (textDisplay) {
    LOG(ERROR) << "TextDisplayAndPointBuffer object was destroyed without Destroy() being called first.";
  }
}

void RenderWindow::TextDisplayAndPointBuffer::Initialize() {
  if (textDisplay) {
    LOG(ERROR) << "Initialize() called on already initialized TextDisplayAndPointBuffer";
    return;
  }
  
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  f->glGenBuffers(1, &pointBuffer);
  f->glBindBuffer(GL_ARRAY_BUFFER, pointBuffer);
  int elementSizeInBytes = 3 * sizeof(float);
  f->glBufferData(GL_ARRAY_BUFFER, 1 * elementSizeInBytes, nullptr, GL_STREAM_DRAW);
  
  textDisplay.reset(new TextDisplay());
}

void RenderWindow::TextDisplayAndPointBuffer::Destroy() {
  if (textDisplay) {
    QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
    
    f->glDeleteBuffers(1, &pointBuffer);
    
    textDisplay.reset();
  }
}


RenderWindow::PointBuffer::~PointBuffer() {
  if (initialized) {
    LOG(ERROR) << "PointBuffer object was destroyed without Destroy() being called first.";
  }
}

void RenderWindow::PointBuffer::Initialize() {
  if (initialized) {
    LOG(ERROR) << "Initialize() called on already initialized PointBuffer";
    return;
  }
  
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  f->glGenBuffers(1, &buffer);
  f->glBindBuffer(GL_ARRAY_BUFFER, buffer);
  int elementSizeInBytes = 3 * sizeof(float);
  f->glBufferData(GL_ARRAY_BUFFER, 1 * elementSizeInBytes, nullptr, GL_STREAM_DRAW);
  
  initialized = true;
}

void RenderWindow::PointBuffer::Destroy() {
  if (initialized) {
    QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
    
    f->glDeleteBuffers(1, &buffer);
    
    initialized = false;
  }
}


void RenderWindow::Button::Load(const std::filesystem::path& defaultSubPath, const std::filesystem::path& hoverSubPath, const std::filesystem::path& activeSubPath, const std::filesystem::path& disabledSubPath) {
  pointBuffer.Initialize();
  
  defaultTexture.reset(new Texture());
  QImage image(GetModdedPathAsQString(defaultSubPath));
  opaquenessMap.Create(image);
  defaultTexture->Load(image, GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR);
  
  hoverTexture.reset(new Texture());
  hoverTexture->Load(QImage(GetModdedPathAsQString(hoverSubPath)), GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR);
  
  activeTexture.reset(new Texture());
  activeTexture->Load(QImage(GetModdedPathAsQString(activeSubPath)), GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR);
  
  if (!disabledSubPath.empty()) {
    disabledTexture.reset(new Texture());
    disabledTexture->Load(QImage(GetModdedPathAsQString(disabledSubPath)), GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR);
  }
}

void RenderWindow::Button::Render(float x, float y, float width, float height, RenderWindow* renderWindow, QOpenGLFunctions_3_2_Core* f) {
  lastX = x;
  lastY = y;
  lastWidth = width;
  lastHeight = height;
  
  Texture* menuButtonTex = (state == 3) ? disabledTexture.get() : ((state == 2) ? activeTexture.get() : ((state == 1) ? hoverTexture.get() : defaultTexture.get()));
  RenderUIGraphic(
      x,
      y,
      width,
      height,
      qRgba(255, 255, 255, 255), pointBuffer.buffer,
      *menuButtonTex,
      renderWindow->uiShader.get(), renderWindow->widgetWidth, renderWindow->widgetHeight, f);
}

void RenderWindow::Button::MouseMove(const QPoint& pos) {
  if (state == 3) {
    return;
  }
  
  if (IsInButton(pos)) {
    if (state == 0) {
      state = 1;
    }
  } else {
    state = 0;
  }
}

void RenderWindow::Button::MousePress(const QPoint& pos) {
  if (state == 3) {
    return;
  }
  
  if (IsInButton(pos)) {
    state = 2;
  }
}

bool RenderWindow::Button::MouseRelease(const QPoint& pos) {
  if (state == 3) {
    return false;
  }
  
  if (IsInButton(pos)) {
    bool clicked = state == 2;
    state = 1;
    return clicked;
  }
  
  state = 0;
  return false;
}

void RenderWindow::Button::SetEnabled(bool enabled) {
  if (enabled) {
    if (state == 3) {
      state = 0;
    }
  } else {
    state = 3;
  }
}

bool RenderWindow::Button::IsInButton(const QPoint& pos) {
  if (pos.x() >= lastX && pos.y() >= lastY &&
      pos.x() < lastX + lastWidth && pos.y() < lastY + lastHeight) {
    int ix = (pos.x() - lastX) * defaultTexture->GetWidth() / lastWidth;
    int iy = (pos.y() - lastY) * defaultTexture->GetHeight() / lastHeight;
    if (opaquenessMap.IsOpaque(ix, iy)) {
      return true;
    }
  }
  
  return false;
}

void RenderWindow::Button::Destroy() {
  pointBuffer.Destroy();
  defaultTexture.reset();
  hoverTexture.reset();
  activeTexture.reset();
  disabledTexture.reset();
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
  playerColorsTexture->Load(image, GL_CLAMP_TO_EDGE, GL_NEAREST, GL_NEAREST);
  
  playerColors.resize(maxNumPlayers);
  for (int i = 0; i < maxNumPlayers; ++ i) {
    // NOTE: We simply use the first palette entry as the player color.
    //       The player color is used for outlines.
    playerColors[i] = playerColorPalettes[i]->at(0);
  }
}

usize RenderWindow::PrepareBufferObject(usize size, QOpenGLFunctions_3_2_Core* f) {
  CHECK_LE(nextBufferObject, bufferObjects.size());
  if (nextBufferObject == bufferObjects.size()) {
    bufferObjects.emplace_back();
    
    bufferObjects[nextBufferObject].size = 0;
    f->glGenBuffers(1, &bufferObjects[nextBufferObject].name);
  }
  
  f->glBindBuffer(GL_ARRAY_BUFFER, bufferObjects[nextBufferObject].name);
  
  if (bufferObjects[nextBufferObject].size < size) {
    f->glBufferData(GL_ARRAY_BUFFER, size, nullptr, GL_STREAM_DRAW);
    bufferObjects[nextBufferObject].size = size;
  }
  
  ++ nextBufferObject;
  return nextBufferObject - 1;
}

void RenderWindow::ComputePixelToOpenGLMatrix(QOpenGLFunctions_3_2_Core* f) {
  float pixelToOpenGLMatrix[4];
  pixelToOpenGLMatrix[0] = 2.f / widgetWidth;
  pixelToOpenGLMatrix[1] = -2.f / widgetHeight;
  pixelToOpenGLMatrix[2] = -pixelToOpenGLMatrix[0] * 0.5f * widgetWidth;
  pixelToOpenGLMatrix[3] = -pixelToOpenGLMatrix[1] * 0.5f * widgetHeight;
  
  uiShader->GetProgram()->UseProgram(f);
  uiShader->GetProgram()->SetUniformMatrix2fv(uiShader->GetViewMatrixLocation(), pixelToOpenGLMatrix, true, f);
  
  uiSingleColorShader->GetProgram()->UseProgram(f);
  uiSingleColorShader->GetProgram()->SetUniformMatrix2fv(uiSingleColorShader->GetViewMatrixLocation(), pixelToOpenGLMatrix, true, f);
}

void RenderWindow::UpdateViewMatrix() {
  float effectiveZoom = ComputeEffectiveZoom();
  
  // Projected coordinates: arbitrary origin, +x goes right, +y goes down, scale is the default scale.
  // OpenGL normalized device coordinates: top-left widget corner is (-1, 1), bottom-right widget corner is (1, -1).
  // The transformation is stored as a matrix but applied as follows:
  // opengl_x = viewMatrix[0] * projected_x + viewMatrix[2];
  // opengl_y = viewMatrix[1] * projected_y + viewMatrix[3];
  QPointF projectedCoordAtScreenCenter = map->MapCoordToProjectedCoord(scroll) + scrollProjectedCoordOffset;
  float scalingX = effectiveZoom * 2.f / widgetWidth;
  float scalingY = effectiveZoom * -2.f / widgetHeight;
  
  viewMatrix[0] = scalingX;
  viewMatrix[1] = scalingY;
  viewMatrix[2] = -scalingX * projectedCoordAtScreenCenter.x();
  viewMatrix[3] = -scalingY * projectedCoordAtScreenCenter.y();
}

float RenderWindow::ComputeEffectiveZoom() {
  if (smoothZooming) {
    double scaleFactor = std::pow(std::sqrt(2.0), remainingZoomStepOffset);
    return zoom * scaleFactor;
  } else {
    return zoom;
  }
}

void RenderWindow::UpdateView(const TimePoint& now, QOpenGLFunctions_3_2_Core* f) {
  // Update scrolling state
  bool scrollApplied = false;
  if (!isLoading) {
    QPointF newScroll = GetCurrentScroll(now, &scrollApplied);
    if (scrollApplied) {
      scroll = newScroll;
    }
    lastScrollGetTime = now;
    if (scrollRightPressed) { scrollRightPressTime = now; }
    if (scrollLeftPressed) { scrollLeftPressTime = now; }
    if (scrollUpPressed) { scrollUpPressTime = now; }
    if (scrollDownPressed) { scrollDownPressTime = now; }
  }
  
  // Compute the pixel-to-OpenGL transformation for the UI shader.
  ComputePixelToOpenGLMatrix(f);
  
  // Compute the view (projected-to-OpenGL) transformation.
  if (!isLoading) {
    UpdateViewMatrix();
    
    // Apply the view transformation to all shaders.
    // TODO: Use a uniform buffer object for that.
    spriteShader->UseProgram(f);
    spriteShader->GetProgram()->SetUniformMatrix2fv(spriteShader->GetViewMatrixLocation(), viewMatrix, true, f);
    
    shadowShader->UseProgram(f);
    shadowShader->GetProgram()->SetUniformMatrix2fv(shadowShader->GetViewMatrixLocation(), viewMatrix, true, f);
    
    outlineShader->UseProgram(f);
    outlineShader->GetProgram()->SetUniformMatrix2fv(outlineShader->GetViewMatrixLocation(), viewMatrix, true, f);
    
    healthBarShader->GetProgram()->UseProgram(f);
    healthBarShader->GetProgram()->SetUniformMatrix2fv(healthBarShader->GetViewMatrixLocation(), viewMatrix, true, f);
    
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
  
  if (scrollApplied) {
    UpdateCursor();
  }
}

void RenderWindow::RenderClosedPath(float halfLineWidth, const QRgb& color, const std::vector<QPointF>& vertices, const QPointF& offset, QOpenGLFunctions_3_2_Core* f) {
  CHECK_OPENGL_NO_ERROR();
  
  // Repeat the first 2 vertices to close the path and get information
  // on the bend direction at the end.
  int numVertices = 2 * (vertices.size() + 1);
  
  // Buffer geometry data.
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
  usize bufferSize = numVertices * elementSizeInBytes;
  PrepareBufferObject(bufferSize, f);
  
  // Set shader (must be done after PrepareBufferObject() to set up the vertex attributes for the correct buffer).
  uiSingleColorShader->GetProgram()->UseProgram(f);
  f->glUniform4f(uiSingleColorShader->GetColorLocation(), qRed(color) / 255.f, qGreen(color) / 255.f, qBlue(color) / 255.f, qAlpha(color) / 255.f);
  
  void* data = f->glMapBufferRange(GL_ARRAY_BUFFER, 0, bufferSize, GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
  memcpy(data, vertexData.data(), bufferSize);
  f->glUnmapBuffer(GL_ARRAY_BUFFER);
  CHECK_OPENGL_NO_ERROR();
  uiSingleColorShader->GetProgram()->SetPositionAttribute(
      3,
      GetGLType<float>::value,
      3 * sizeof(float),
      0,
      f);
  
  // Draw lines.
  f->glDrawArrays(GL_TRIANGLE_STRIP, 0, numVertices);
  CHECK_OPENGL_NO_ERROR();
  
  f->glBindBuffer(GL_ARRAY_BUFFER, pointBuffer);  // TODO: remove this
}

void RenderWindow::RenderSprites(std::vector<Texture*>* textures, const std::shared_ptr<SpriteShader>& shader, QOpenGLFunctions_3_2_Core* f) {
  for (Texture* texture : *textures) {
    // Bind the texture
    f->glBindTexture(GL_TEXTURE_2D, texture->GetId());
    f->glUniform2f(shader->GetTextureSizeLocation(), texture->GetWidth(), texture->GetHeight());
    
    // Issue the render call
    int vertexSize = shader->GetVertexSize();
    if (texture->DrawCallBuffer().size() % vertexSize != 0) {
      LOG(ERROR) << "Unexpected vertex data size in draw call buffer: " << texture->DrawCallBuffer().size() << " % " << vertexSize << " = " << (texture->DrawCallBuffer().size() % vertexSize) << " != 0";
    } else {
      usize bufferSize = texture->DrawCallBuffer().size();
      PrepareBufferObject(bufferSize, f);
      shader->UseProgram(f);  // TODO: We only need to set up the vertex attributes again after changing the GL_ARRAY_BUFFER buffer; we would not need to "use" the program again
      
      void* data = f->glMapBufferRange(GL_ARRAY_BUFFER, 0, bufferSize, GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
      memcpy(data, texture->DrawCallBuffer().data(), bufferSize);
      f->glUnmapBuffer(GL_ARRAY_BUFFER);
      
      f->glDrawArrays(GL_POINTS, 0, bufferSize / vertexSize);
    }
    
    texture->DrawCallBuffer().clear();
  }
  
  f->glBindBuffer(GL_ARRAY_BUFFER, pointBuffer);  // TODO: remove this
}

void RenderWindow::RenderShadows(double displayedServerTime, QOpenGLFunctions_3_2_Core* f) {
  auto& unitTypes = ClientUnitType::GetUnitTypes();
  
  shadowShader->UseProgram(f);
  
  std::vector<Texture*> textures;
  textures.reserve(64);
  
  float effectiveZoom = ComputeEffectiveZoom();
  
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
        Texture* texture = &building.GetTexture(/*shadow*/ true);
        if (texture->DrawCallBuffer().isEmpty()) {
          textures.push_back(texture);
        }
        
        building.Render(
            map.get(),
            qRgb(255, 255, 255),
            shadowShader.get(),
            viewMatrix,
            effectiveZoom,
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
        Texture* texture = &unit.GetTexture(/*shadow*/ true);
        if (texture->DrawCallBuffer().isEmpty()) {
          textures.push_back(texture);
        }
        
        unit.Render(
            map.get(),
            qRgb(255, 255, 255),
            shadowShader.get(),
            viewMatrix,
            effectiveZoom,
            widgetWidth,
            widgetHeight,
            displayedServerTime,
            true,
            false);
      }
    }
  }
  
  RenderSprites(&textures, shadowShader, f);
}

void RenderWindow::RenderBuildings(double displayedServerTime, bool buildingsThatCauseOutlines, QOpenGLFunctions_3_2_Core* f) {
  spriteShader->UseProgram(f);
  
  Timer preparationTimer("RenderBuildings() preparation");
  
  std::vector<Texture*> textures;
  textures.reserve(64);
  
  float effectiveZoom = ComputeEffectiveZoom();
  
  for (auto& object : map->GetObjects()) {
    if (!object.second->isBuilding()) {
      continue;
    }
    ClientBuilding& building = *static_cast<ClientBuilding*>(object.second);
    if (buildingsThatCauseOutlines != ClientBuildingType::GetBuildingTypes()[static_cast<int>(building.GetType())].DoesCauseOutlines()) {
      continue;
    }
    
    QRectF projectedCoordsRect = building.GetRectInProjectedCoords(
        map.get(),
        displayedServerTime,
        false,
        false);
    if (projectedCoordsRect.intersects(projectedCoordsViewRect)) {
      Texture* texture = &building.GetTexture(/*shadow*/ false);
      if (texture->DrawCallBuffer().isEmpty()) {
        textures.push_back(texture);
      }
      
      // TODO: Multiple sprites may have nearly the same y-coordinate, as a result there can be flickering currently. Avoid this.
      building.Render(
          map.get(),
          qRgb(255, 255, 255),
          spriteShader.get(),
          viewMatrix,
          effectiveZoom,
          widgetWidth,
          widgetHeight,
          displayedServerTime,
          false,
          false);
    }
  }
  
  preparationTimer.Stop();
  Timer drawCallTimer("RenderBuildings() drawing");
  
  RenderSprites(&textures, spriteShader, f);
  
  drawCallTimer.Stop();
}

void RenderWindow::RenderBuildingFoundation(double displayedServerTime, QOpenGLFunctions_3_2_Core* f) {
  spriteShader->UseProgram(f);
  
  QPoint foundationBaseTile(-1, -1);
  bool canBePlacedHere = CanBuildingFoundationBePlacedHere(constructBuildingType, lastCursorPos, &foundationBaseTile);
  
  float effectiveZoom = ComputeEffectiveZoom();
  
  if (foundationBaseTile.x() >= 0 && foundationBaseTile.y() >= 0) {
    // Render the building foundation, colored either in gray if it can be placed at this location,
    // or in red if it cannot be placed there.
    ClientBuilding* tempBuilding = new ClientBuilding(match->GetPlayerIndex(), constructBuildingType, foundationBaseTile.x(), foundationBaseTile.y(), 100, /*hp*/ 0);
    tempBuilding->SetFixedFrameIndex(0);
    
    QRgb modulationColor =
        canBePlacedHere ?
        qRgb(0.8 * 255, 0.8 * 255, 0.8 * 255) :
        qRgb(255, 0.4 * 255, 0.4 * 255);
    tempBuilding->Render(
        map.get(),
        modulationColor,
        spriteShader.get(),
        viewMatrix,
        effectiveZoom,
        widgetWidth,
        widgetHeight,
        displayedServerTime,
        false,
        false);
    
    std::vector<Texture*> textures(1);
    textures[0] = &tempBuilding->GetTexture(/*shadow*/ false);
    RenderSprites(&textures, spriteShader, f);
    
    delete tempBuilding;
  }
}

void RenderWindow::RenderSelectionGroundOutlines(QOpenGLFunctions_3_2_Core* f) {
  for (u32 objectId : selection) {
    auto it = map->GetObjects().find(objectId);
    if (it != map->GetObjects().end()) {
      RenderSelectionGroundOutline(qRgba(255, 255, 255, 255), it->second, f);
    }
  }
  
  if (flashingObjectId != kInvalidObjectId &&
      IsObjectFlashActive()) {
    auto flashingObjectIt = map->GetObjects().find(flashingObjectId);
    if (flashingObjectIt != map->GetObjects().end()) {
      RenderSelectionGroundOutline(qRgba(80, 255, 80, 255), flashingObjectIt->second, f);
    }
  }
}

void RenderWindow::RenderSelectionGroundOutline(QRgb color, ClientObject* object, QOpenGLFunctions_3_2_Core* f) {
  float effectiveZoom = ComputeEffectiveZoom();
  
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
    
    RenderClosedPath(effectiveZoom * 1.1f, qRgba(0, 0, 0, 255), outlineVertices, QPointF(0, effectiveZoom * 2), f);
    RenderClosedPath(effectiveZoom * 1.1f, color, outlineVertices, QPointF(0, 0), f);
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
    
    RenderClosedPath(effectiveZoom * 1.1f, qRgba(0, 0, 0, 255), outlineVertices, QPointF(0, effectiveZoom * 2), f);
    RenderClosedPath(effectiveZoom * 1.1f, color, outlineVertices, QPointF(0, 0), f);
  }
}

void RenderWindow::RenderOutlines(double displayedServerTime, QOpenGLFunctions_3_2_Core* f) {
  auto& unitTypes = ClientUnitType::GetUnitTypes();
  
  outlineShader->UseProgram(f);
  
  std::vector<Texture*> textures;
  textures.reserve(64);
  
  float effectiveZoom = ComputeEffectiveZoom();
  
  for (auto& object : map->GetObjects()) {
    // TODO: Use virtual functions here to reduce duplicated code among buildings and units?
    
    QRgb outlineColor;
    if (object.second->GetPlayerIndex() == kGaiaPlayerIndex) {
      // Hard-code white as the outline color for "Gaia" objects
      outlineColor = qRgb(255, 255, 255);
    } else {
      outlineColor = playerColors[object.second->GetPlayerIndex()];
    }
    
    if (object.first == flashingObjectId &&
        IsObjectFlashActive()) {
      outlineColor = qRgb(255 - qRed(outlineColor),
                          255 - qGreen(outlineColor),
                          255 - qBlue(outlineColor));
    }
    
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
        Texture* texture = &building.GetTexture(/*shadow*/ false);
        if (texture->DrawCallBuffer().isEmpty()) {
          textures.push_back(texture);
        }
        
        building.Render(
            map.get(),
            outlineColor,
            outlineShader.get(),
            viewMatrix,
            effectiveZoom,
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
        Texture* texture = &unit.GetTexture(/*shadow*/ false);
        if (texture->DrawCallBuffer().isEmpty()) {
          textures.push_back(texture);
        }
        
        unit.Render(
            map.get(),
            outlineColor,
            outlineShader.get(),
            viewMatrix,
            effectiveZoom,
            widgetWidth,
            widgetHeight,
            displayedServerTime,
            false,
            true);
      }
    }
  }
  
  RenderSprites(&textures, outlineShader, f);
}

void RenderWindow::RenderUnits(double displayedServerTime, QOpenGLFunctions_3_2_Core* f) {
  spriteShader->UseProgram(f);
  
  std::vector<Texture*> textures;
  textures.reserve(64);
  
  float effectiveZoom = ComputeEffectiveZoom();
  
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
      Texture* texture = &unit.GetTexture(/*shadow*/ false);
      if (texture->DrawCallBuffer().isEmpty()) {
        textures.push_back(texture);
      }
      
      unit.Render(
          map.get(),
          qRgb(255, 255, 255),
          spriteShader.get(),
          viewMatrix,
          effectiveZoom,
          widgetWidth,
          widgetHeight,
          displayedServerTime,
          false,
          false);
    }
  }
  
  RenderSprites(&textures, spriteShader, f);
}

void RenderWindow::RenderMoveToMarker(const TimePoint& now, QOpenGLFunctions_3_2_Core* f) {
  spriteShader->UseProgram(f);
  
  float effectiveZoom = ComputeEffectiveZoom();
  
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
        viewMatrix,
        effectiveZoom,
        widgetWidth,
        widgetHeight,
        moveToFrameIndex,
        /*shadow*/ false,
        /*outline*/ false,
        qRgb(255, 255, 255),
        /*playerIndex*/ 0,
        /*scaling*/ 0.5f);
    
    std::vector<Texture*> textures(1);
    textures[0] = &moveToSprite->graphicTexture;
    RenderSprites(&textures, spriteShader, f);
  }
}

void RenderWindow::RenderHealthBars(double displayedServerTime, QOpenGLFunctions_3_2_Core* f) {
  auto& buildingTypes = ClientBuildingType::GetBuildingTypes();
  auto& unitTypes = ClientUnitType::GetUnitTypes();
  QRgb gaiaColor = qRgb(255, 255, 255);
  
  float effectiveZoom = ComputeEffectiveZoom();
  
  for (auto& object : map->GetObjects()) {
    if (!object.second->IsSelected()) {
      continue;
    }
    
    // TODO: Use virtual functions here to reduce duplicated code among buildings and units?
    
    if (object.second->isBuilding()) {
      ClientBuilding& building = *static_cast<ClientBuilding*>(object.second);
      const ClientBuildingType& buildingType = buildingTypes[static_cast<int>(building.GetType())];
      
      QPointF centerProjectedCoord = map->MapCoordToProjectedCoord(building.GetCenterMapCoord());
      QPointF healthBarCenter =
          centerProjectedCoord +
          QPointF(0, -1 * buildingType.GetHealthBarHeightAboveCenter(building.GetFrameIndex(displayedServerTime)));
      
      constexpr float kHealthBarWidth = 60;  // TODO: Smaller bar for trees
      constexpr float kHealthBarHeight = 4;
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
            (building.GetPlayerIndex() == kGaiaPlayerIndex) ? gaiaColor : playerColors[building.GetPlayerIndex()],
            healthBarShader.get(),
            viewMatrix,
            effectiveZoom,
            widgetWidth,
            widgetHeight,
            f);
      }
    } else {  // if (object.second->isUnit()) {
      ClientUnit& unit = *static_cast<ClientUnit*>(object.second);
      const ClientUnitType& unitType = unitTypes[static_cast<int>(unit.GetType())];
      
      QPointF centerProjectedCoord = unit.GetCenterProjectedCoord(map.get());
      QPointF healthBarCenter =
          centerProjectedCoord +
          QPointF(0, -1 * unitType.GetHealthBarHeightAboveCenter());
      
      constexpr float kHealthBarWidth = 30;
      constexpr float kHealthBarHeight = 4;
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
            (unit.GetPlayerIndex() == kGaiaPlayerIndex) ? gaiaColor : playerColors[unit.GetPlayerIndex()],
            healthBarShader.get(),
            viewMatrix,
            effectiveZoom,
            widgetWidth,
            widgetHeight,
            f);
      }
    }
  }
}

void RenderWindow::RenderGroundDecals(QOpenGLFunctions_3_2_Core* f) {
  RenderDecals(groundDecals, f);
}

void RenderWindow::RenderOccludingDecals(QOpenGLFunctions_3_2_Core* f) {
  RenderDecals(occludingDecals, f);
}

void RenderWindow::RenderDecals(std::vector<Decal*>& decals, QOpenGLFunctions_3_2_Core* f) {
  spriteShader->UseProgram(f);
  
  std::vector<Texture*> textures;
  textures.reserve(64);
  
  float effectiveZoom = ComputeEffectiveZoom();
  
  for (auto& decal : decals) {
    QRectF projectedCoordsRect = decal->GetRectInProjectedCoords(
        false,
        false);
    if (projectedCoordsRect.intersects(projectedCoordsViewRect)) {
      Texture* texture;
      decal->Render(
          qRgb(255, 255, 255),
          spriteShader.get(),
          viewMatrix,
          effectiveZoom,
          widgetWidth,
          widgetHeight,
          false,
          false,
          &texture);
      if (texture->DrawCallBuffer().size() == spriteShader->GetVertexSize()) {
        textures.push_back(texture);
      }
    }
  }
  
  RenderSprites(&textures, spriteShader, f);
}

void RenderWindow::RenderOccludingDecalShadows(QOpenGLFunctions_3_2_Core* f) {
  shadowShader->UseProgram(f);
  
  std::vector<Texture*> textures;
  textures.reserve(64);
  
  float effectiveZoom = ComputeEffectiveZoom();
  
  for (auto& decal : occludingDecals) {
    QRectF projectedCoordsRect = decal->GetRectInProjectedCoords(
        true,
        false);
    if (projectedCoordsRect.intersects(projectedCoordsViewRect)) {
      Texture* texture;
      decal->Render(
          qRgb(255, 255, 255),
          shadowShader.get(),
          viewMatrix,
          effectiveZoom,
          widgetWidth,
          widgetHeight,
          true,
          false,
          &texture);
      if (texture->DrawCallBuffer().size() == shadowShader->GetVertexSize()) {
        textures.push_back(texture);
      }
    }
  }
  
  RenderSprites(&textures, shadowShader, f);
}

void RenderWindow::RenderOccludingDecalOutlines(QOpenGLFunctions_3_2_Core* f) {
  outlineShader->UseProgram(f);
  
  std::vector<Texture*> textures;
  textures.reserve(64);
  
  float effectiveZoom = ComputeEffectiveZoom();
  
  for (auto& decal : occludingDecals) {
    QRgb outlineColor = qRgb(255, 255, 255);
    if (decal->GetPlayerIndex() < playerColors.size()) {
      outlineColor = playerColors[decal->GetPlayerIndex()];
    }
    
    QRectF projectedCoordsRect = decal->GetRectInProjectedCoords(
        false,
        true);
    if (projectedCoordsRect.intersects(projectedCoordsViewRect)) {
      Texture* texture;
      decal->Render(
          outlineColor,
          outlineShader.get(),
          viewMatrix,
          effectiveZoom,
          widgetWidth,
          widgetHeight,
          false,
          true,
          &texture);
      if (texture->DrawCallBuffer().size() == outlineShader->GetVertexSize()) {
        textures.push_back(texture);
      }
    }
  }
  
  RenderSprites(&textures, outlineShader, f);
}

void RenderWindow::RenderGameUI(double displayedServerTime, QOpenGLFunctions_3_2_Core* f) {
  RenderMenuPanel(f);
  
  RenderResourcePanel(f);
  
  RenderSelectionPanel(f);
  
  RenderCommandPanel(f);
  
  // Render the current game time
  double timeSinceGameStart = displayedServerTime - gameController->GetGameStartServerTimeSeconds();
  int seconds = fmod(timeSinceGameStart, 60.0);
  int minutes = fmod(std::floor(timeSinceGameStart / 60.0), 60);
  int hours = std::floor(timeSinceGameStart / (60.0 * 60.0));
  QString timeString = QObject::tr("%1:%2:%3").arg(hours, 2, 10, QChar('0')).arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
  
  for (int i = 0; i < 2; ++ i) {
    gameTimeDisplay.textDisplay->Render(
      georgiaFontSmaller,
      (i == 0) ? qRgba(0, 0, 0, 255) : qRgba(255, 255, 255, 255),
      timeString,
      QRect(uiScale * (2*851 + ((i == 0) ? 2 : 0)),
            uiScale * (8 + ((i == 0) ? 2 : 0)),
            0,
            0),
      Qt::AlignTop | Qt::AlignLeft,
      (i == 0) ? gameTimeDisplayShadowPointBuffer.buffer : gameTimeDisplay.pointBuffer,
      uiShader.get(), widgetWidth, widgetHeight, f);
  }
  
  // Render the current FPS and ping
  double filteredPing, filteredOffset;
  connection->EstimateCurrentPingAndOffset(&filteredPing, &filteredOffset);
  QString fpsAndPingString;
  if (roundedFPS >= 0) {
    fpsAndPingString = QObject::tr("%1 FPS | %2 ms").arg(roundedFPS).arg(static_cast<int>(1000 * filteredPing + 0.5f));
  } else {
    fpsAndPingString = QObject::tr("%1 ms").arg(static_cast<int>(1000 * filteredPing + 0.5f));
  }
  
  for (int i = 0; i < 2; ++ i) {
    fpsAndPingDisplay.textDisplay->Render(
      georgiaFontSmaller,
      (i == 0) ? qRgba(0, 0, 0, 255) : qRgba(255, 255, 255, 255),
      fpsAndPingString,
      QRect(uiScale * (2*851 + ((i == 0) ? 2 : 0)),
            uiScale * (40 + 8 + ((i == 0) ? 2 : 0)),
            0,
            0),
      Qt::AlignTop | Qt::AlignLeft,
      (i == 0) ? fpsAndPingDisplayShadowPointBuffer.buffer : fpsAndPingDisplay.pointBuffer,
      uiShader.get(), widgetWidth, widgetHeight, f);
  }
  
  // Render the player names in the bottom-right
  const auto& players = match->GetPlayers();
  int currentY = widgetHeight - uiScale * 4;
  for (int i = static_cast<int>(players.size()) - 1; i >= 0; -- i) {
    bool isPlayingOrHasWon =
        (match->GetPlayers()[i].state == Match::PlayerState::Playing) ||
        (match->GetPlayers()[i].state == Match::PlayerState::Won);
    
    for (int shadow = 0; shadow < 2; ++ shadow) {
      playerNames[i].textDisplay->Render(
          isPlayingOrHasWon ? georgiaFontLarger : georgiaFontLargerStrikeOut,
          (shadow == 0) ? qRgba(0, 0, 0, 255) : playerColors[i],
          players[i].name,
          QRect(0, 0, widgetWidth - uiScale * 10 - ((shadow == 0) ? 0 : (uiScale * 2)), currentY - ((shadow == 0) ? 0 : (uiScale * 2))),
          Qt::AlignRight | Qt::AlignBottom,
          (shadow == 0) ? playerNameShadowPointBuffers[i].buffer : playerNames[i].pointBuffer,
          uiShader.get(), widgetWidth, widgetHeight, f);
    }
    currentY = playerNames[i].textDisplay->GetBounds().y();
  }
  
  if (menuShown) {
    RenderMenu(f);
  } else if (match->GetThisPlayer().state != Match::PlayerState::Playing) {
    // Render the game end text display ("Victory!" or "Defeat!")
    for (int shadow = 0; shadow < 2; ++ shadow) {
      int offset = (shadow == 0) ? (uiScale * 8) : 0;
      gameEndTextDisplay.textDisplay->Render(
          georgiaFontHuge,
          (shadow == 0) ? qRgba(0, 0, 0, 255) : qRgba(255, 255, 255, 255),
          (match->GetThisPlayer().state == Match::PlayerState::Won) ? tr("Victory!") : tr("Defeat!"),
          QRect(offset, offset, widgetWidth, widgetHeight),
          Qt::AlignHCenter | Qt::AlignVCenter,
          (shadow == 0) ? gameEndTextDisplayShadowPointBuffer.buffer : gameEndTextDisplay.pointBuffer,
          uiShader.get(), widgetWidth, widgetHeight, f);
    }
  }
}

QPointF RenderWindow::GetMenuPanelTopLeft() {
  return QPointF(
      widgetWidth - uiScale * menuPanel.texture->GetWidth(),
      0);
}

void RenderWindow::RenderMenuPanel(QOpenGLFunctions_3_2_Core* f) {
  QPointF topLeft = GetMenuPanelTopLeft();
  
  RenderUIGraphic(
      topLeft.x(),
      topLeft.y(),
      uiScale * menuPanel.texture->GetWidth(),
      uiScale * menuPanel.texture->GetHeight(),
      qRgba(255, 255, 255, 255), menuPanel.pointBuffer,
      *menuPanel.texture,
      uiShader.get(), widgetWidth, widgetHeight, f);
  
  constexpr int kButtonSize = 70;
  constexpr int kButtonLeftRightMargin = 22;
  constexpr int kNumButtons = 5;
  
  RenderUIGraphic(
      topLeft.x() + uiScale * (270 + kButtonLeftRightMargin + (0 / (kNumButtons - 1.f)) * (454 - 2 * kButtonLeftRightMargin - kButtonSize)),
      topLeft.y() + uiScale * (23),
      uiScale * kButtonSize,
      uiScale * kButtonSize,
      qRgba(255, 255, 255, 255), objectivesButtonPointBuffer.buffer,
      *objectivesButtonDisabledTexture,
      uiShader.get(), widgetWidth, widgetHeight, f);
  
  RenderUIGraphic(
      topLeft.x() + uiScale * (270 + kButtonLeftRightMargin + (1 / (kNumButtons - 1.f)) * (454 - 2 * kButtonLeftRightMargin - kButtonSize)),
      topLeft.y() + uiScale * (23),
      uiScale * kButtonSize,
      uiScale * kButtonSize,
      qRgba(255, 255, 255, 255), chatButtonPointBuffer.buffer,
      *chatButtonDisabledTexture,
      uiShader.get(), widgetWidth, widgetHeight, f);
  
  RenderUIGraphic(
      topLeft.x() + uiScale * (270 + kButtonLeftRightMargin + (2 / (kNumButtons - 1.f)) * (454 - 2 * kButtonLeftRightMargin - kButtonSize)),
      topLeft.y() + uiScale * (23),
      uiScale * kButtonSize,
      uiScale * kButtonSize,
      qRgba(255, 255, 255, 255), diplomacyButtonPointBuffer.buffer,
      *diplomacyButtonDisabledTexture,
      uiShader.get(), widgetWidth, widgetHeight, f);
  
  RenderUIGraphic(
      topLeft.x() + uiScale * (270 + kButtonLeftRightMargin + (3 / (kNumButtons - 1.f)) * (454 - 2 * kButtonLeftRightMargin - kButtonSize)),
      topLeft.y() + uiScale * (23),
      uiScale * kButtonSize,
      uiScale * kButtonSize,
      qRgba(255, 255, 255, 255), settingsButtonPointBuffer.buffer,
      *settingsButtonDisabledTexture,
      uiShader.get(), widgetWidth, widgetHeight, f);
  
  menuButton.Render(
      topLeft.x() + uiScale * (270 + kButtonLeftRightMargin + (4 / (kNumButtons - 1.f)) * (454 - 2 * kButtonLeftRightMargin - kButtonSize)),
      topLeft.y() + uiScale * (23),
      uiScale * kButtonSize,
      uiScale * kButtonSize,
      this, f);
}

QPointF RenderWindow::GetResourcePanelTopLeft() {
  return QPointF(0, 0);
}

void RenderWindow::RenderResourcePanel(QOpenGLFunctions_3_2_Core* f) {
  const ResourceAmount& resources = gameController->GetCurrentResourceAmount();
  
  QPointF topLeft = GetResourcePanelTopLeft();
  
  RenderUIGraphic(
      topLeft.x(),
      topLeft.y(),
      uiScale * resourcePanel.texture->GetWidth(),
      uiScale * resourcePanel.texture->GetHeight(),
      qRgba(255, 255, 255, 255), resourcePanel.pointBuffer,
      *resourcePanel.texture,
      uiShader.get(), widgetWidth, widgetHeight, f);
  
  RenderUIGraphic(
      topLeft.x() + uiScale * (17 + 0 * 200),
      topLeft.y() + uiScale * 16,
      uiScale * 83,
      uiScale * 83,
      qRgba(255, 255, 255, 255), resourceWood.pointBuffer,
      *resourceWood.texture,
      uiShader.get(), widgetWidth, widgetHeight, f);
  woodTextDisplay.textDisplay->Render(
      georgiaFontSmaller,
      qRgba(255, 255, 255, 255),
      QString::number(resources.wood()),
      QRect(topLeft.x() + uiScale * (17 + 0 * 200 + 83 + 16),
            topLeft.y() + uiScale * 16,
            uiScale * 82,
            uiScale * 83),
      Qt::AlignLeft | Qt::AlignVCenter,
      woodTextDisplay.pointBuffer,
      uiShader.get(), widgetWidth, widgetHeight, f);
  
  RenderUIGraphic(
      topLeft.x() + uiScale * (17 + 1 * 200),
      topLeft.y() + uiScale * 16,
      uiScale * 83,
      uiScale * 83,
      qRgba(255, 255, 255, 255), resourceFood.pointBuffer,
      *resourceFood.texture,
      uiShader.get(), widgetWidth, widgetHeight, f);
  foodTextDisplay.textDisplay->Render(
      georgiaFontSmaller,
      qRgba(255, 255, 255, 255),
      QString::number(resources.food()),
      QRect(topLeft.x() + uiScale * (17 + 1 * 200 + 83 + 16),
            topLeft.y() + uiScale * 16,
            uiScale * 82,
            uiScale * 83),
      Qt::AlignLeft | Qt::AlignVCenter,
      foodTextDisplay.pointBuffer,
      uiShader.get(), widgetWidth, widgetHeight, f);
  
  RenderUIGraphic(
      topLeft.x() + uiScale * (17 + 2 * 200),
      topLeft.y() + uiScale * 16,
      uiScale * 83,
      uiScale * 83,
      qRgba(255, 255, 255, 255), resourceGold.pointBuffer,
      *resourceGold.texture,
      uiShader.get(), widgetWidth, widgetHeight, f);
  goldTextDisplay.textDisplay->Render(
      georgiaFontSmaller,
      qRgba(255, 255, 255, 255),
      QString::number(resources.gold()),
      QRect(topLeft.x() + uiScale * (17 + 2 * 200 + 83 + 16),
            topLeft.y() + uiScale * 16,
            uiScale * 82,
            uiScale * 83),
      Qt::AlignLeft | Qt::AlignVCenter,
      goldTextDisplay.pointBuffer,
      uiShader.get(), widgetWidth, widgetHeight, f);
  
  RenderUIGraphic(
      topLeft.x() + uiScale * (17 + 3 * 200),
      topLeft.y() + uiScale * 16,
      uiScale * 83,
      uiScale * 83,
      qRgba(255, 255, 255, 255), resourceStone.pointBuffer,
      *resourceStone.texture,
      uiShader.get(), widgetWidth, widgetHeight, f);
  stoneTextDisplay.textDisplay->Render(
      georgiaFontSmaller,
      qRgba(255, 255, 255, 255),
      QString::number(resources.stone()),
      QRect(topLeft.x() + uiScale * (17 + 3 * 200 + 83 + 16),
            topLeft.y() + uiScale * 16,
            uiScale * 82,
            uiScale * 83),
      Qt::AlignLeft | Qt::AlignVCenter,
      stoneTextDisplay.pointBuffer,
      uiShader.get(), widgetWidth, widgetHeight, f);
  
  RenderUIGraphic(
      topLeft.x() + uiScale * (17 + 4 * 200),
      topLeft.y() + uiScale * 16,
      uiScale * 83,
      uiScale * 83,
      qRgba(255, 255, 255, 255), pop.pointBuffer,
      *pop.texture,
      uiShader.get(), widgetWidth, widgetHeight, f);
  popTextDisplay.textDisplay->Render(
      georgiaFontSmaller,
      qRgba(255, 255, 255, 255),
      "-",  // TODO
      QRect(topLeft.x() + uiScale * (17 + 4 * 200 + 83 + 16),
            topLeft.y() + uiScale * 16,
            uiScale * 82,
            uiScale * 83),
      Qt::AlignLeft | Qt::AlignVCenter,
      popTextDisplay.pointBuffer,
      uiShader.get(), widgetWidth, widgetHeight, f);
  
  RenderUIGraphic(
      topLeft.x() + uiScale * (17 + 4 * 200 + 234),
      topLeft.y() + uiScale * 24,
      uiScale * 2 * 34,
      uiScale * 2 * 34,
      qRgba(255, 255, 255, 255), idleVillagerDisabled.pointBuffer,
      *idleVillagerDisabled.texture,
      uiShader.get(), widgetWidth, widgetHeight, f);
  RenderUIGraphic(
      topLeft.x() + uiScale * (17 + 4 * 200 + 234 + 154 - currentAgeShield.texture->GetWidth() / 2),
      topLeft.y() + uiScale * 0,
      uiScale * currentAgeShield.texture->GetWidth(),
      uiScale * currentAgeShield.texture->GetHeight(),
      qRgba(255, 255, 255, 255), currentAgeShield.pointBuffer,
      *currentAgeShield.texture,
      uiShader.get(), widgetWidth, widgetHeight, f);
  float currentAgeTextLeft = topLeft.x() + uiScale * (17 + 4 * 200 + 234 + 154 + currentAgeShield.texture->GetWidth() / 2);
  currentAgeTextDisplay.textDisplay->Render(
      georgiaFontLarger,
      qRgba(255, 255, 255, 255),
      tr("Dark Age"),
      QRect(currentAgeTextLeft,
            topLeft.y() + uiScale * 16,
            uiScale * (1623 - 8) - currentAgeTextLeft,
            uiScale * 83),
      Qt::AlignHCenter | Qt::AlignVCenter,
      currentAgeTextDisplay.pointBuffer,
      uiShader.get(), widgetWidth, widgetHeight, f);
}

QPointF RenderWindow::GetSelectionPanelTopLeft() {
  return QPointF(
      uiScale * 539,
      widgetHeight - uiScale * selectionPanel.texture->GetHeight());
}

void RenderWindow::RenderSelectionPanel(QOpenGLFunctions_3_2_Core* f) {
  QPointF topLeft = GetSelectionPanelTopLeft();
  
  RenderUIGraphic(
      topLeft.x(),
      topLeft.y(),
      uiScale * selectionPanel.texture->GetWidth(),
      uiScale * selectionPanel.texture->GetHeight(),
      qRgba(255, 255, 255, 255), selectionPanel.pointBuffer,
      *selectionPanel.texture,
      uiShader.get(), widgetWidth, widgetHeight, f);
  
  // Is only a single object selected?
  if (selection.size() == 1) {
    ClientObject* singleSelectedObject = map->GetObjects().at(selection.front());
    
    // Display the object name
    singleObjectNameDisplay.textDisplay->Render(
        georgiaFontLarger,
        qRgba(58, 29, 21, 255),
        singleSelectedObject->GetObjectName(),
        QRect(topLeft.x() + uiScale * 2*32,
              topLeft.y() + uiScale * 50 + uiScale * 2*25,
              uiScale * 2*172,
              uiScale * 2*16),
        Qt::AlignLeft | Qt::AlignTop,
        singleObjectNameDisplay.pointBuffer,
        uiShader.get(), widgetWidth, widgetHeight, f);
    
    // Display the object's HP
    if (singleSelectedObject->GetHP() > 0) {
      u32 maxHP;
      if (singleSelectedObject->isUnit()) {
        maxHP = GetUnitMaxHP(static_cast<ClientUnit*>(singleSelectedObject)->GetType());
      } else {
        CHECK(singleSelectedObject->isBuilding());
        maxHP = GetBuildingMaxHP(static_cast<ClientBuilding*>(singleSelectedObject)->GetType());
      }
      
      hpDisplay.textDisplay->Render(
          georgiaFontSmaller,
          qRgba(58, 29, 21, 255),
          QStringLiteral("%1 / %2").arg(singleSelectedObject->GetHP()).arg(maxHP),
          QRect(topLeft.x() + uiScale * 2*32,
                topLeft.y() + uiScale * 50 + uiScale * 2*46 + uiScale * 2*60,
                uiScale * 2*172,
                uiScale * 2*16),
          Qt::AlignLeft | Qt::AlignTop,
          hpDisplay.pointBuffer,
          uiShader.get(), widgetWidth, widgetHeight, f);
    }
    
    // Display unit details?
    if (singleSelectedObject->isUnit()) {
      ClientUnit* singleSelectedUnit = static_cast<ClientUnit*>(singleSelectedObject);
      if (IsVillager(singleSelectedUnit->GetType())) {
        // Display the villager's carried resources?
        if (singleSelectedUnit->GetCarriedResourceAmount() > 0) {
          carriedResourcesDisplay.textDisplay->Render(
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
              carriedResourcesDisplay.pointBuffer,
              uiShader.get(), widgetWidth, widgetHeight, f);
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
          qRgba(255, 255, 255, 255), selectionPanelIconPointBuffer.buffer,
          *iconTexture,
          uiShader.get(), widgetWidth, widgetHeight, f);
      RenderUIGraphic(
          topLeft.x() + uiScale * 2*32,
          topLeft.y() + uiScale * 50 + uiScale * 2*46,
          uiScale * 2*60,
          uiScale * 2*60,
          qRgba(255, 255, 255, 255), selectionPanelIconOverlayPointBuffer.buffer,
          *iconOverlayNormalTexture,
          uiShader.get(), widgetWidth, widgetHeight, f);
    }
  }
}

QPointF RenderWindow::GetCommandPanelTopLeft() {
  return QPointF(
      0,
      widgetHeight - uiScale * commandPanel.texture->GetHeight());
}

void RenderWindow::RenderCommandPanel(QOpenGLFunctions_3_2_Core* f) {
  QPointF topLeft = GetCommandPanelTopLeft();
  
  RenderUIGraphic(
      topLeft.x(),
      topLeft.y(),
      uiScale * commandPanel.texture->GetWidth(),
      uiScale * commandPanel.texture->GetHeight(),
      qRgba(255, 255, 255, 255), commandPanel.pointBuffer,
      *commandPanel.texture,
      uiShader.get(), widgetWidth, widgetHeight, f);
  
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
          uiShader.get(), widgetWidth, widgetHeight, f);
    }
  }
}

void RenderWindow::RenderMenu(QOpenGLFunctions_3_2_Core* f) {
  // Update the enabled state of the resign button
  menuButtonResign.SetEnabled(match->GetThisPlayer().state == Match::PlayerState::Playing);
  
  // Render.
  QPointF topLeft(
      0.5f * widgetWidth - uiScale * 0.5f * menuDialog.texture->GetWidth(),
      0.5f * widgetHeight - uiScale * 0.5f * menuDialog.texture->GetHeight());
  
  // Dialog background and "Menu" text in its title bar
  RenderUIGraphic(
      topLeft.x(),
      topLeft.y(),
      uiScale * menuDialog.texture->GetWidth(),
      uiScale * menuDialog.texture->GetHeight(),
      qRgba(255, 255, 255, 255), menuDialog.pointBuffer,
      *menuDialog.texture,
      uiShader.get(), widgetWidth, widgetHeight, f);
  menuTextDisplay.textDisplay->Render(
      georgiaFontLarger,
      qRgba(54, 18, 18, 255),
      QObject::tr("Menu"),
      QRect(topLeft.x() + uiScale * 228,
            topLeft.y() + uiScale * 101,
            uiScale * (655 - 228),
            uiScale * (164 - 101)),
      Qt::AlignHCenter | Qt::AlignVCenter,
      menuTextDisplay.pointBuffer,
      uiShader.get(), widgetWidth, widgetHeight, f);
  
  // Exit button
  QRect menuButtonExitRect(
      0.5f * widgetWidth - uiScale * 0.5f * menuButtonExit.defaultTexture->GetWidth(),
      topLeft.y() + uiScale * 220,
      uiScale * menuButtonExit.defaultTexture->GetWidth(),
      uiScale * menuButtonExit.defaultTexture->GetHeight());
  menuButtonExit.Render(
      menuButtonExitRect.x(), menuButtonExitRect.y(),
      menuButtonExitRect.width(), menuButtonExitRect.height(),
      this, f);
  menuButtonExitText.textDisplay->Render(
      georgiaFontLarger,
      qRgba(252, 201, 172, 255),
      QObject::tr("Exit"),
      menuButtonExitRect,
      Qt::AlignHCenter | Qt::AlignVCenter,
      menuButtonExitText.pointBuffer,
      uiShader.get(), widgetWidth, widgetHeight, f);
  
  // Resign button
  QRect menuButtonResignRect(
      0.5f * widgetWidth - uiScale * 0.5f * menuButtonResign.defaultTexture->GetWidth(),
      topLeft.y() + uiScale * (220 + menuButtonResign.defaultTexture->GetHeight() + 26),
      uiScale * menuButtonResign.defaultTexture->GetWidth(),
      uiScale * menuButtonResign.defaultTexture->GetHeight());
  menuButtonResign.Render(
      menuButtonResignRect.x(), menuButtonResignRect.y(),
      menuButtonResignRect.width(), menuButtonResignRect.height(),
      this, f);
  menuButtonResignText.textDisplay->Render(
      georgiaFontLarger,
      qRgba(252, 201, 172, 255),
      QObject::tr("Resign"),
      menuButtonResignRect,
      Qt::AlignHCenter | Qt::AlignVCenter,
      menuButtonResignText.pointBuffer,
      uiShader.get(), widgetWidth, widgetHeight, f);
  
  // Cancel button
  QRect menuButtonCancelRect(
      0.5f * widgetWidth - uiScale * 0.5f * menuButtonCancel.defaultTexture->GetWidth(),
      topLeft.y() + uiScale * (763 - menuButtonResign.defaultTexture->GetHeight()),
      uiScale * menuButtonCancel.defaultTexture->GetWidth(),
      uiScale * menuButtonCancel.defaultTexture->GetHeight());
  menuButtonCancel.Render(
      menuButtonCancelRect.x(), menuButtonCancelRect.y(),
      menuButtonCancelRect.width(), menuButtonCancelRect.height(),
      this, f);
  menuButtonCancelText.textDisplay->Render(
      georgiaFontLarger,
      qRgba(252, 201, 172, 255),
      QObject::tr("Cancel"),
      menuButtonCancelRect,
      Qt::AlignHCenter | Qt::AlignVCenter,
      menuButtonCancelText.pointBuffer,
      uiShader.get(), widgetWidth, widgetHeight, f);
}

bool RenderWindow::IsUIAt(int x, int y) {
  float factor = 1.f / uiScale;
  
  QPointF menuPanelTopLeft = GetMenuPanelTopLeft();
  if (menuPanelOpaquenessMap.IsOpaque(factor * (x - menuPanelTopLeft.x()), factor * (y - menuPanelTopLeft.y()))) {
    return true;
  }
  
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

void RenderWindow::ShowMenu(bool show) {
  menuShown = show;
  
  if (menuShown) {
    setCursor(defaultCursor);
    menuButtonResign.SetEnabled(match->GetThisPlayer().state == Match::PlayerState::Playing);
  } else {
    UpdateCursor();
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
        const Sprite::Frame& frame = building.GetSprite().frame(building.GetFrameIndex(lastDisplayedServerTime));
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
  
  std::vector<std::pair<u32, ClientObject*>> objects;
  bool haveOwnObject = false;
  
  for (auto& object : map->GetObjects()) {
    if (object.second->isUnit()) {
      ClientUnit& unit = *static_cast<ClientUnit*>(object.second);
      
      QRectF projectedCoordsRect = unit.GetRectInProjectedCoords(
          map.get(),
          lastDisplayedServerTime,
          false,
          false);
      
      if (projectedCoordsRect.intersects(selectionRect)) {
        objects.push_back(object);
        if (object.second->GetPlayerIndex() == match->GetPlayerIndex()) {
          haveOwnObject = true;
        }
      }
    }
  }
  
  // If at least one own object is there, select only the own objects.
  // Else, select a single object only (TODO: Any preference for which single one to select?)
  if (haveOwnObject) {
    for (auto& object : objects) {
      if (object.second->GetPlayerIndex() == match->GetPlayerIndex()) {
        AddToSelection(object.first);
      }
    }
  } else if (!objects.empty()) {
    AddToSelection(objects.front().first);
  }
  
  SelectionChanged();
}

QPointF RenderWindow::ScreenCoordToProjectedCoord(float x, float y) {
  return QPointF(
      ((-1 + 2 * x / (1.f * width())) - viewMatrix[2]) / viewMatrix[0],
      ((1 - 2 * y / (1.f * height())) - viewMatrix[3]) / viewMatrix[1]);
}

QPointF RenderWindow::ProjectedCoordToScreenCoord(float x, float y) {
  return QPointF(
      width() * (0.5f * (viewMatrix[0] * x + viewMatrix[2]) + 0.5f),
      height() * (-0.5f * (viewMatrix[1] * y + viewMatrix[3]) + 0.5f));
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

void RenderWindow::LetObjectFlash(u32 objectId) {
  flashingObjectId = objectId;
  // NOTE: We could use a local time here to make it a bit more smooth than with the server time.
  //       It will not matter in practice though.
  flashingObjectStartTime = lastDisplayedServerTime;
}

bool RenderWindow::IsObjectFlashActive() {
  constexpr int kFlashCount = 3;
  constexpr double kFlashShowDuration = 0.2;
  constexpr double kFlashHideDuration = 0.2;
  
  double timeSinceFlashStart = lastDisplayedServerTime - flashingObjectStartTime;
  if (timeSinceFlashStart > 0 &&
      timeSinceFlashStart < kFlashCount * (kFlashShowDuration + kFlashHideDuration)) {
    double phase = fmod(timeSinceFlashStart, kFlashShowDuration + kFlashHideDuration);
    return (phase <= kFlashShowDuration);
  }
  
  return false;
}

void RenderWindow::RenderLoadingScreen(QOpenGLFunctions_3_2_Core* f) {
  CHECK_OPENGL_NO_ERROR();
  
  ComputePixelToOpenGLMatrix(f);
  
  f->glEnable(GL_BLEND);
  f->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  
  // Clear background.
  f->glClearColor(0.2f, 0.2f, 0.2f, 0.0f);
  f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  CHECK_OPENGL_NO_ERROR();
  
  // Render the loading state text.
  const float lineHeight = uiScale * 40;
  float totalHeight = match->GetPlayers().size() * lineHeight;
  
  const auto& players = match->GetPlayers();
  for (int i = 0; i < static_cast<int>(players.size()); ++ i) {
    int loadingPercentage;
    if (i == match->GetPlayerIndex()) {
      loadingPercentage = static_cast<int>(100 * loadingStep / static_cast<float>(maxLoadingStep) + 0.5f);
    } else {
      loadingPercentage = players[i].loadingPercentage;
    }
    QString text = tr("%1: %2%")
        .arg(players[i].name)
        .arg(loadingPercentage, 3, 10, QChar(' '));
    
    playerNames[i].textDisplay->Render(
        georgiaFont,
        playerColors[i],
        text,
        QRect(0, 0.5f * widgetHeight - 0.5f * totalHeight + i * lineHeight + 0.5f * lineHeight, widgetWidth, lineHeight),
        Qt::AlignHCenter | Qt::AlignVCenter,
        playerNames[i].pointBuffer,
        uiShader.get(), widgetWidth, widgetHeight, f);
  }
  
  // Render the loading icon.
  RenderUIGraphic(
      widgetWidth / 2 - loadingIcon.texture->GetWidth() / 2,
      0.5f * widgetHeight - 0.5f * totalHeight - loadingIcon.texture->GetHeight(),
      loadingIcon.texture->GetWidth(),
      loadingIcon.texture->GetHeight(),
      qRgba(255, 255, 255, 255), loadingIcon.pointBuffer,
      *loadingIcon.texture,
      uiShader.get(), widgetWidth, widgetHeight, f);

  // Set the alpha to 255 everywhere to prevent parts of the window from being
  // transparent on window managers which use that in their compositing (e.g., on Windows).
  f->glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
  f->glClearColor(0, 0, 0, 1.f);
  f->glClear(GL_COLOR_BUFFER_BIT);
  f->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
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
  
  // Update ground decals.
  usize outputIndex = 0;
  for (usize i = 0; i < groundDecals.size(); ++ i) {
    Decal* decal = groundDecals[i];
    
    if (decal->Update(displayedServerTime)) {
      // Keep the decal.
      if (outputIndex != i) {
        groundDecals[outputIndex] = decal;
      }
      
      ++ outputIndex;
    } else {
      // The decal has expired.
      delete decal;
    }
  }
  groundDecals.resize(outputIndex);
  
  // Update occluding decals.
  outputIndex = 0;
  for (usize i = 0; i < occludingDecals.size(); ++ i) {
    Decal* decal = occludingDecals[i];
    
    if (decal->Update(displayedServerTime)) {
      if (!decal->MayOccludeSprites()) {
        // Move the decal to the groundDecals list.
        groundDecals.push_back(decal);
      } else {
        // Keep the decal.
        if (outputIndex != i) {
          occludingDecals[outputIndex] = decal;
        }
        
        ++ outputIndex;
      }
    } else {
      // The decal has expired.
      delete decal;
    }
  }
  occludingDecals.resize(outputIndex);
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

void RenderWindow::PressCommandButton(CommandButton* button) {
  button->Pressed(selection, gameController.get());
  
  // Handle building construction.
  if (button->GetType() == CommandButton::Type::ConstructBuilding &&
      gameController->GetLatestKnownResourceAmount().CanAfford(GetBuildingCost(button->GetBuildingConstructionType()))) {
    constructBuildingType = button->GetBuildingConstructionType();
  }
  
  // "Action" buttons are handled here.
  if (button->GetType() == CommandButton::Type::Action) {
    switch (button->GetActionType()) {
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
}

void RenderWindow::ShowDefaultCommandButtonsForSelection() {
  for (int row = 0; row < kCommandButtonRows; ++ row) {
    for (int col = 0; col < kCommandButtonCols; ++ col) {
      commandButtons[row][col].SetInvisible();
    }
  }
  
  // Check whether a single type of own building is selected only.
  // In this case, show the buttons corresponding to this building type.
  bool singleOwnBuildingTypeSelected = true;
  bool atLeastOneOwnBuildingFullyConstructed = false;
  BuildingType selectedBuildingType = BuildingType::NumBuildings;
  for (usize i = 0; i < selection.size(); ++ i) {
    u32 objectId = selection[i];
    ClientObject* object = map->GetObjects().at(objectId);
    
    if (object->isUnit()) {
      singleOwnBuildingTypeSelected = false;
      break;
    } else if (object->isBuilding()) {
      ClientBuilding* building = static_cast<ClientBuilding*>(object);
      
      if (building->GetPlayerIndex() == match->GetPlayerIndex()) {
        if (building->GetBuildPercentage() == 100) {
          atLeastOneOwnBuildingFullyConstructed = true;
        }
        
        if (i == 0) {
          selectedBuildingType = building->GetType();
        } else if (selectedBuildingType != building->GetType()) {
          singleOwnBuildingTypeSelected = false;
          break;
        }
      } else {
        singleOwnBuildingTypeSelected = false;
      }
    }
  }
  if (!selection.empty() && singleOwnBuildingTypeSelected && atLeastOneOwnBuildingFullyConstructed) {
    GetClientBuildingType(selectedBuildingType).SetCommandButtons(commandButtons);
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
    commandButtons[0][0].SetAction(CommandButton::ActionType::BuildEconomyBuilding, buildEconomyBuildings.texture.get(), Qt::Key_A);
    commandButtons[0][1].SetAction(CommandButton::ActionType::BuildMilitaryBuilding, buildMilitaryBuildings.texture.get(), Qt::Key_S);
  }
}

void RenderWindow::ShowEconomyBuildingCommandButtons() {
  showingEconomyBuildingCommandButtons = true;
  
  for (int row = 0; row < kCommandButtonRows; ++ row) {
    for (int col = 0; col < kCommandButtonCols; ++ col) {
      commandButtons[row][col].SetInvisible();
    }
  }
  
  commandButtons[0][0].SetBuilding(BuildingType::House, Qt::Key_Q);
  commandButtons[0][1].SetBuilding(BuildingType::Mill, Qt::Key_W);
  commandButtons[0][2].SetBuilding(BuildingType::MiningCamp, Qt::Key_E);
  commandButtons[0][3].SetBuilding(BuildingType::LumberCamp, Qt::Key_R);
  commandButtons[0][4].SetBuilding(BuildingType::Dock, Qt::Key_T);
  
  commandButtons[2][3].SetAction(CommandButton::ActionType::ToggleBuildingsCategory, toggleBuildingsCategory.texture.get());
  commandButtons[2][4].SetAction(CommandButton::ActionType::Quit, quit.texture.get(), Qt::Key_Escape);
}

void RenderWindow::ShowMilitaryBuildingCommandButtons() {
  showingEconomyBuildingCommandButtons = false;
  
  for (int row = 0; row < kCommandButtonRows; ++ row) {
    for (int col = 0; col < kCommandButtonCols; ++ col) {
      commandButtons[row][col].SetInvisible();
    }
  }
  
  commandButtons[0][0].SetBuilding(BuildingType::Barracks, Qt::Key_Q);
  commandButtons[1][0].SetBuilding(BuildingType::Outpost, Qt::Key_A);
  
  // TODO: Temporarily deactivated since they don't work as expected yet.
  // commandButtons[1][1].SetBuilding(BuildingType::PalisadeWall, Qt::Key_S);
  // commandButtons[2][1].SetBuilding(BuildingType::PalisadeGate, Qt::Key_X);
  
  commandButtons[2][3].SetAction(CommandButton::ActionType::ToggleBuildingsCategory, toggleBuildingsCategory.texture.get());
  commandButtons[2][4].SetAction(CommandButton::ActionType::Quit, quit.texture.get(), Qt::Key_Escape);
}

void RenderWindow::JumpToNextTownCenter() {
  std::vector<std::pair<u32, ClientBuilding*>> townCenters;
  
  for (const auto& item : map->GetObjects()) {
    if (item.second->GetPlayerIndex() == match->GetPlayerIndex() &&
        item.second->isBuilding()) {
      ClientBuilding* building = static_cast<ClientBuilding*>(item.second);
      if (building->GetType() == BuildingType::TownCenter) {
        townCenters.push_back(std::make_pair(item.first, building));
      }
    }
  }
  
  if (townCenters.empty()) {
    return;
  }
  
  if (selection.size() == 1) {
    for (usize i = 0; i < townCenters.size(); ++ i) {
      if (townCenters[i].first == selection.front()) {
        const auto& target = townCenters[(i + 1) % townCenters.size()];
        JumpToObject(target.first, target.second);
        return;
      }
    }
  }
  
  const auto& target = townCenters.front();
  JumpToObject(target.first, target.second);
}

void RenderWindow::JumpToObject(u32 objectId, ClientObject* object) {
  ClearSelection();
  AddToSelection(objectId);
  SelectionChanged();
  
  if (object->isBuilding()) {
    ClientBuilding* building = static_cast<ClientBuilding*>(object);
    scroll = building->GetCenterMapCoord();
    scrollProjectedCoordOffset = QPointF(0, 0);
    UpdateViewMatrix();
    UpdateCursor();
  } else if (object->isUnit()) {
    ClientUnit* unit = static_cast<ClientUnit*>(object);
    scroll = unit->GetMapCoord();
    scrollProjectedCoordOffset = QPointF(0, 0);
    UpdateViewMatrix();
    UpdateCursor();
  }
}

void RenderWindow::DeleteSelectedObjects() {
  if (selection.empty()) {
    return;
  }
  
  std::vector<u32> remainingObjects;
  for (u32 id : selection) {
    auto it = map->GetObjects().find(id);
    if (it != map->GetObjects().end() &&
        it->second->GetPlayerIndex() == match->GetPlayerIndex()) {
      connection->Write(CreateDeleteObjectMessage(id));
    } else {
      remainingObjects.push_back(id);
    }
  }
  
  ClearSelection();
  for (u32 id : remainingObjects) {
    AddToSelection(id);
  }
  SelectionChanged();
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
  maxLoadingStep = 61;
  loadingThread->start();
  
  // Create resources right now which are required for rendering the loading screen:
  
  // Load the UI shaders.
  uiShader.reset(new UIShader());
  uiSingleColorShader.reset(new UISingleColorShader());
  
  // Create a buffer containing a single point for sprite rendering. TODO: Remove this
  f->glGenBuffers(1, &pointBuffer);
  f->glBindBuffer(GL_ARRAY_BUFFER, pointBuffer);
  int elementSizeInBytes = 3 * sizeof(float);
  float data[] = {
      0.f, 0.f, 0};
  f->glBufferData(GL_ARRAY_BUFFER, 1 * elementSizeInBytes, data, GL_STREAM_DRAW);
  CHECK_OPENGL_NO_ERROR();
  
  // Load the loading icon.
  loadingIcon.Load(GetModdedPath(graphicsSubPath.parent_path().parent_path() / "wpfg" / "resources" / "campaign" / "campaign_icon_2swords.png"));
  
  // Create the loading text display.
  playerNames.resize(match->GetPlayers().size());
  playerNameShadowPointBuffers.resize(playerNames.size());
  for (usize i = 0; i < playerNames.size(); ++ i) {
    playerNames[i].Initialize();
    playerNameShadowPointBuffers[i].Initialize();
  }
  
  // Define the player colors.
  CreatePlayerColorPaletteTexture();
  
  // Remember the render start time.
  renderStartTime = Clock::now();
}

void RenderWindow::paintGL() {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  CHECK_OPENGL_NO_ERROR();
  
  // Regularly print timing info
  static int renderStatisticsCounter = 0;
  ++ renderStatisticsCounter;
  if (renderStatisticsCounter % (3 * 120) == 0) {
    Timing::print(std::cout, kSortByTotal);
    // Timing::reset();
  }
  
  // Wait for the previous frame to finish rendering.
  // This allows us to map OpenGL buffer objects while disabling synchronization, avoiding CPU stalls due to impclicit synchronization.
  // This should also prevent the GPU driver from queuing up multiple future frames. On the one hand this reduces rendering lag,
  // on the other hand, it reduces the robustness against individual frames taking a long time to render.
  if (haveSyncObject) {
    GLenum result = f->glClientWaitSync(syncObject, 0, std::numeric_limits<GLuint64>::max());
    if (result == GL_TIMEOUT_EXPIRED || result == GL_WAIT_FAILED) {
      LOG(ERROR) << "glClientWaitSync() failed; result code: " << result;
    }
    
    f->glDeleteSync(syncObject);
    haveSyncObject = false;
  }
  
  // By default, use pointBuffer as the array buffer
  f->glBindBuffer(GL_ARRAY_BUFFER, pointBuffer);
  
  // Render loading screen?
  if (isLoading) {
    // Parse server messages.
    gameController->ParseMessagesUntil(/*displayedServerTime*/ 0);
    
    // Switch to the game once it starts.
    if (connection->GetServerTimeToDisplayNow() >= gameController->GetGameStartServerTimeSeconds()) {
      isLoading = false;
      
      // Unload loading screen resources
      loadingIcon.Unload();
      
      // Avoid possible jumps directly after the game start
      lastScrollGetTime = Clock::now();
    } else {
      RenderLoadingScreen(f);
      return;
    }
  }
  
  // FPS computation
  constexpr int kUpdateFPSEveryXthFrame = 30;  // update FPS every 30 frames
  
  if (framesAfterFPSMeasuringStartTime < 0) {
    fpsMeasuringFrameStartTime = Clock::now();
    framesAfterFPSMeasuringStartTime = 0;
  } else {
    ++ framesAfterFPSMeasuringStartTime;
    if (framesAfterFPSMeasuringStartTime == kUpdateFPSEveryXthFrame) {
      double elapsedSeconds = SecondsDuration(Clock::now() - fpsMeasuringFrameStartTime).count();
      roundedFPS = static_cast<int>(kUpdateFPSEveryXthFrame / elapsedSeconds + 0.5f);
      
      fpsMeasuringFrameStartTime = Clock::now();
      framesAfterFPSMeasuringStartTime = 0;
    }
  }
  
  // Render game.
  Timer renderTimer("paintGL()");
  
  Timer gameStateUpdateTimer("paintGL() - game state update");
  
  // Get the time for which to render the game state.
  // TODO: Predict the time at which the rendered frame will be displayed rather than taking the current time.
  TimePoint now = Clock::now();
  double secondsSinceLastFrame = haveLastFrameTime ? SecondsDuration(now - lastFrameTime).count() : 0;
  lastFrameTime = now;
  haveLastFrameTime = true;
  
  // Update the game state to the server time that should be displayed.
  double displayedServerTime = connection->ConnectionToServerLost() ? lastDisplayedServerTime : connection->GetServerTimeToDisplayNow();
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
  
  // Update smooth zooming.
  if (smoothZooming) {
    constexpr float zoomUpdateRate = 0.003f;
    constexpr float projectedCoordUpdateRate = 0.003f;
    
    remainingZoomStepOffset = remainingZoomStepOffset * std::pow(zoomUpdateRate, secondsSinceLastFrame);
    if (fabs(remainingZoomStepOffset) < 0.001f) {
      remainingZoomStepOffset = 0;
    }
    
    float effectiveZoom = ComputeEffectiveZoom();
    
    float projectedCoordFactor = std::pow(projectedCoordUpdateRate, secondsSinceLastFrame);
    scrollProjectedCoordOffset.setX(scrollProjectedCoordOffset.x() * projectedCoordFactor);
    if (fabs(effectiveZoom * scrollProjectedCoordOffset.x()) < 0.1f) {
      scrollProjectedCoordOffset.setX(0);
    }
    scrollProjectedCoordOffset.setY(scrollProjectedCoordOffset.y() * projectedCoordFactor);
    if (fabs(effectiveZoom * scrollProjectedCoordOffset.y()) < 0.1f) {
      scrollProjectedCoordOffset.setY(0);
    }
  }
  
  gameStateUpdateTimer.Stop();
  Timer initialStatesAndClearTimer("paintGL() - initial state setting & clear");
  
  // Update scrolling and compute the view transformation.
  UpdateView(now, f);
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
  
  initialStatesAndClearTimer.Stop();
  Timer shadowTimer("paintGL() - shadow rendering");
  
  // Render the shadows.
  f->glEnable(GL_BLEND);
  f->glDisable(GL_DEPTH_TEST);
  // Set up blending such that colors are added (does not matter since we do not render colors),
  // and for alpha values, the maximum is used.
  f->glBlendEquationSeparate(GL_FUNC_ADD, GL_MAX);
  
  CHECK_OPENGL_NO_ERROR();
  RenderShadows(displayedServerTime, f);
  RenderOccludingDecalShadows(f);
  CHECK_OPENGL_NO_ERROR();
  
  shadowTimer.Stop();
  Timer mapTimer("paintGL() - map rendering");
  
  // Render the map terrain.
  f->glBlendFuncSeparate(
      GL_ONE_MINUS_DST_ALPHA, GL_ZERO,  // blend with the shadows
      GL_ZERO, GL_ONE);  // keep the existing alpha (such that more objects can be rendered while applying the shadows)
  f->glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);  // reset to default
  
  CHECK_OPENGL_NO_ERROR();
  map->Render(viewMatrix, graphicsSubPath, f);
  mapTimer.Stop();
  Timer groundDecalTimer("paintGL() - ground decal rendering");
  RenderGroundDecals(f);
  CHECK_OPENGL_NO_ERROR();
  
  f->glBlendFuncSeparate(
      GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
      GL_ONE, GL_ZERO);  // reset the blend func to standard
  
  groundDecalTimer.Stop();
  Timer selectionOutlinesTimer("paintGL() - selection outlines rendering");
  
  // Render selection outlines below buildings.
  CHECK_OPENGL_NO_ERROR();
  RenderSelectionGroundOutlines(f);
  CHECK_OPENGL_NO_ERROR();
  
  // Enable the depth buffer for sprite rendering.
  f->glEnable(GL_DEPTH_TEST);
  f->glDepthFunc(GL_LEQUAL);
  
  selectionOutlinesTimer.Stop();
  Timer buildingsCausingOutlinesTimer("paintGL() - buildings that cause outlines rendering");
  
  // Render buildings that cause outlines.
  CHECK_OPENGL_NO_ERROR();
  RenderBuildings(displayedServerTime, true, f);
  CHECK_OPENGL_NO_ERROR();
  
  // Render the building foundation under the cursor.
  if (constructBuildingType != BuildingType::NumBuildings) {
    CHECK_OPENGL_NO_ERROR();
    RenderBuildingFoundation(displayedServerTime, f);
    CHECK_OPENGL_NO_ERROR();
  }
  
  buildingsCausingOutlinesTimer.Stop();
  Timer outlinesTimer("paintGL() - outlines rendering");
  
  // Render outlines.
  // Disable depth writing.
  f->glDepthMask(GL_FALSE);
  // Let only pass through those fragments which are *behind* the depth values in the depth buffer.
  // So we only render outlines in places where something is occluded.
  f->glDepthFunc(GL_GREATER);
  
  CHECK_OPENGL_NO_ERROR();
  RenderOutlines(displayedServerTime, f);
  RenderOccludingDecalOutlines(f);
  CHECK_OPENGL_NO_ERROR();
  
  outlinesTimer.Stop();
  Timer objectsNotCausingOutlinesTimer("paintGL() - objects not causing outlines rendering");
  
  // Render units and buildings that do not cause outlines.
  f->glDepthMask(GL_TRUE);
  f->glDepthFunc(GL_LEQUAL);
  
  CHECK_OPENGL_NO_ERROR();
  RenderBuildings(displayedServerTime, false, f);
  RenderUnits(displayedServerTime, f);
  RenderOccludingDecals(f);
  CHECK_OPENGL_NO_ERROR();
  
  // Render move-to marker.
  // This should be rendered after the last unit at the moment, since it contains semi-transparent
  // pixels which do currenly write to the z-buffer.
  CHECK_OPENGL_NO_ERROR();
  RenderMoveToMarker(now, f);
  CHECK_OPENGL_NO_ERROR();
  
  objectsNotCausingOutlinesTimer.Stop();
  Timer healthBarsTimer("paintGL() - health bars rendering");
  
  // Render health bars.
  f->glClear(GL_DEPTH_BUFFER_BIT);
  f->glDisable(GL_BLEND);
  
  CHECK_OPENGL_NO_ERROR();
  RenderHealthBars(displayedServerTime, f);
  CHECK_OPENGL_NO_ERROR();
  
  healthBarsTimer.Stop();
  Timer selectionBoxTimer("paintGL() - selection box rendering");
  
  // Render selection box.
  if (dragging) {
    std::vector<QPointF> vertices(4);
    
    vertices[0] = dragStartPos;
    vertices[1] = QPointF(dragStartPos.x(), lastCursorPos.y());
    vertices[2] = lastCursorPos;
    vertices[3] = QPointF(lastCursorPos.x(), dragStartPos.y());
    
    RenderClosedPath(1.1f, qRgba(0, 0, 0, 255), vertices, QPointF(2, 2), f);
    RenderClosedPath(1.1f, qRgba(255, 255, 255, 255), vertices, QPointF(0, 0), f);
  }
  
  selectionBoxTimer.Stop();
  Timer uiTimer("paintGL() - UI rendering");
  
  // Render game UI.
  f->glEnable(GL_BLEND);
  
  // TODO: Would it be faster to render this at the start and then prevent rendering over the UI pixels,
  //       for example by setting the z-buffer such that no further pixel will be rendered there?
  CHECK_OPENGL_NO_ERROR();
  RenderGameUI(displayedServerTime, f);
  CHECK_OPENGL_NO_ERROR();
  
  uiTimer.Stop();

  // Set the alpha to 255 everywhere to prevent parts of the window from being
  // transparent on window managers which use that in their compositing (e.g., on Windows).
  f->glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
  f->glClearColor(0, 0, 0, 1.f);
  f->glClear(GL_COLOR_BUFFER_BIT);
  f->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  
  syncObject = f->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  haveSyncObject = true;
  
  nextBufferObject = 0;
}

void RenderWindow::resizeGL(int width, int height) {
  widgetWidth = width;
  widgetHeight = height;
}

void RenderWindow::mousePressEvent(QMouseEvent* event) {
  if (isLoading) {
    return;
  }
  
  if (menuShown) {
    menuButtonExit.MousePress(event->pos());
    menuButtonResign.MousePress(event->pos());
    menuButtonCancel.MousePress(event->pos());
    return;
  }
  
  bool isUIClick = IsUIAt(event->x(), event->y());
  
  if (event->button() == Qt::LeftButton) {
    // Has a command button been pressed?
    if (match->IsPlayerStillInGame()) {
      for (int row = 0; row < kCommandButtonRows; ++ row) {
        for (int col = 0; col < kCommandButtonCols; ++ col) {
          if (commandButtons[row][col].IsPointInButton(event->pos())) {
            pressedCommandButtonRow = row;
            pressedCommandButtonCol = col;
            return;
          }
        }
      }
    }
    
    if (isUIClick) {
      menuButton.MousePress(event->pos());
      
      return;
    }
    
    // Place a building foundation?
    if (match->IsPlayerStillInGame() && constructBuildingType != BuildingType::NumBuildings) {
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
             !isUIClick &&
             match->IsPlayerStillInGame()) {
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
          LetObjectFlash(targetObjectId);
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
  
  if (isLoading) {
    return;
  }
  
  lastCursorPos = lastMouseMoveEventPos;
  
  if (menuShown) {
    menuButtonExit.MouseMove(lastCursorPos);
    menuButtonResign.MouseMove(lastCursorPos);
    menuButtonCancel.MouseMove(lastCursorPos);
    return;
  }
  
  menuButton.MouseMove(lastCursorPos);
  
  if (possibleDragStart) {
    if ((lastCursorPos - dragStartPos).manhattanLength() >= QApplication::startDragDistance()) {
      dragging = true;
    }
  }
  
  // If a command button has been pressed but the cursor moves away from it, abort the button press.
  if (pressedCommandButtonRow >= 0 &&
      pressedCommandButtonCol >= 0 &&
      !commandButtonPressedByHotkey &&
      !commandButtons[pressedCommandButtonRow][pressedCommandButtonCol].IsPointInButton(lastMouseMoveEventPos)) {
    pressedCommandButtonRow = -1;
    pressedCommandButtonCol = -1;
  }
  
  // If hovering over the game area, possibly change the cursor to indicate possible interactions.
  UpdateCursor();
}

void RenderWindow::UpdateCursor() {
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
  if (isLoading) {
    return;
  }
  
  if (menuShown) {
    if (menuButtonExit.MouseRelease(event->pos())) {
      close();
      qApp->exit();
    }
    if (menuButtonResign.MouseRelease(event->pos())) {
      connection->Write(CreateLeaveMessage());
      match->GetThisPlayer().state = Match::PlayerState::Resigned;
      ShowMenu(false);
    }
    if (menuButtonCancel.MouseRelease(event->pos())) {
      ShowMenu(false);
    }
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
        pressedCommandButtonCol >= 0 &&
        match->IsPlayerStillInGame()) {
      PressCommandButton(&commandButtons[pressedCommandButtonRow][pressedCommandButtonCol]);
      
      pressedCommandButtonRow = -1;
      pressedCommandButtonCol = -1;
      return;
    }
    
    if (isUIClick) {
      if (menuButton.MouseRelease(event->pos())) {
        // The menu button was clicked, show the menu dialog.
        ShowMenu(true);
      }
      
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
  if (isLoading) {
    return;
  }
  if (menuShown) {
    return;
  }
  
  // Compute new zoom
  double degrees = event->angleDelta().y() / 8.0;
  double numSteps = degrees / 15.0;
  
  double scaleFactor = std::pow(std::sqrt(2.0), numSteps);
  double newZoom = zoom * scaleFactor;
  
  // Compute new scroll value to keep the map coord under the cursor fixed (if possible)
  QPointF cursorProjectedCoord = ScreenCoordToProjectedCoord(lastCursorPos.x(), lastCursorPos.y());
  
  zoom = newZoom;
  UpdateViewMatrix();
  
  QPointF screenCoordDifference = lastCursorPos - ProjectedCoordToScreenCoord(cursorProjectedCoord);
  QPointF requiredProjectedCoordDifference(
       2.f / (widgetWidth * viewMatrix[0]) * screenCoordDifference.x(),
      -2.f / (widgetHeight * viewMatrix[1]) * screenCoordDifference.y());
  
  QPointF newScreenCenterProjectedCoords = map->MapCoordToProjectedCoord(scroll) - requiredProjectedCoordDifference;
  map->ProjectedCoordToMapCoord(newScreenCenterProjectedCoords, &scroll);
  
  if (smoothZooming) {
    scrollProjectedCoordOffset += requiredProjectedCoordDifference;
    remainingZoomStepOffset += -numSteps;
  }
  UpdateViewMatrix();
}

void RenderWindow::keyPressEvent(QKeyEvent* event) {
  if (isLoading) {
    return;
  }
  
  if (event->key() == Qt::Key_Escape) {
    ShowMenu(!menuShown);
  }
  
  if (menuShown) {
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
  } else if (event->key() == Qt::Key_Delete) {
    if (match->IsPlayerStillInGame()) {
      DeleteSelectedObjects();
    }
  } else if (event->key() == Qt::Key_H) {
    JumpToNextTownCenter();
  } else if (match->IsPlayerStillInGame()) {
    // Check whether a hotkey for a command button was pressed.
    for (int row = 0; row < kCommandButtonRows; ++ row) {
      for (int col = 0; col < kCommandButtonCols; ++ col) {
        if (commandButtons[row][col].GetHotkey() != Qt::Key_unknown &&
            commandButtons[row][col].GetHotkey() == event->key()) {
          pressedCommandButtonRow = row;
          pressedCommandButtonCol = col;
          commandButtonPressedByHotkey = true;
          break;
        }
      }
    }
  }
}

void RenderWindow::keyReleaseEvent(QKeyEvent* event) {
  if (isLoading) {
    return;
  }
  
  if (menuShown) {
    return;
  }
  
  float effectiveZoom = ComputeEffectiveZoom();
  
  if (event->key() == Qt::Key_Right) {
    scrollRightPressed = false;
    TimePoint now = Clock::now();
    double seconds = std::chrono::duration<double>(now - scrollRightPressTime).count();
    Scroll(scrollDistancePerSecond / effectiveZoom * seconds, 0, &scroll);
    UpdateViewMatrix();
    UpdateCursor();
  } else if (event->key() == Qt::Key_Left) {
    scrollLeftPressed = false;
    TimePoint now = Clock::now();
    double seconds = std::chrono::duration<double>(now - scrollLeftPressTime).count();
    Scroll(-scrollDistancePerSecond / effectiveZoom * seconds, 0, &scroll);
    UpdateViewMatrix();
    UpdateCursor();
  } else if (event->key() == Qt::Key_Up) {
    scrollUpPressed = false;
    TimePoint now = Clock::now();
    double seconds = std::chrono::duration<double>(now - scrollUpPressTime).count();
    Scroll(0, -scrollDistancePerSecond / effectiveZoom * seconds, &scroll);
    UpdateViewMatrix();
    UpdateCursor();
  } else if (event->key() == Qt::Key_Down) {
    scrollDownPressed = false;
    TimePoint now = Clock::now();
    double seconds = std::chrono::duration<double>(now - scrollDownPressTime).count();
    Scroll(0, scrollDistancePerSecond / effectiveZoom * seconds, &scroll);
    UpdateViewMatrix();
    UpdateCursor();
  } else if (match->IsPlayerStillInGame()) {
    // Check whether a hotkey for a command button was released.
    for (int row = 0; row < kCommandButtonRows; ++ row) {
      for (int col = 0; col < kCommandButtonCols; ++ col) {
        if (commandButtons[row][col].GetHotkey() != Qt::Key_unknown &&
            commandButtons[row][col].GetHotkey() == event->key()) {
          PressCommandButton(&commandButtons[row][col]);
          
          pressedCommandButtonRow = -1;
          pressedCommandButtonCol = -1;
          commandButtonPressedByHotkey = false;
          return;
        }
      }
    }
  }
}

#include "render_window.moc"
