// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/client/render_window.hpp"

#include <cassert>
#include <math.h>

#include <QApplication>
#include <QFontDatabase>
#include <QIcon>
#include <QKeyEvent>
#include <QMessageBox>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_2_Core>
#include <QPainter>
#include <QThread>
#include <QTimer>

#ifdef HAVE_X11_EXTRAS
  #include <QX11Info>
  #include <X11/Xlib.h>
#elif WIN32
  #include <Windows.h>
#endif

#include "FreeAge/client/game_controller.hpp"
#include "FreeAge/client/health_bar.hpp"
#include "FreeAge/common/logging.hpp"
#include "FreeAge/client/mod_manager.hpp"
#include "FreeAge/client/opengl.hpp"
#include "FreeAge/client/shader_color_dilation.hpp"
#include "FreeAge/client/sprite.hpp"
#include "FreeAge/client/sprite_atlas.hpp"
#include "FreeAge/common/timing.hpp"
#include "FreeAge/common/util.hpp"


class LoadingThread : public QThread {
 Q_OBJECT
 public:
  inline LoadingThread(RenderWindow* window, QOpenGLContext* loadingContext, QOffscreenSurface* loadingSurface)
      : window(window),
        loadingContext(loadingContext),
        loadingSurface(loadingSurface) {}
  
  void run() override {
    loadingContext->makeCurrent(loadingSurface);
    succeeded = window->LoadResources();
    loadingContext->doneCurrent();
    delete loadingContext;
  }
  
  inline bool Succeeded() const { return succeeded; }
  
 private:
  RenderWindow* window;
  QOpenGLContext* loadingContext;
  QOffscreenSurface* loadingSurface;
  bool succeeded = false;
};


RenderWindow::RenderWindow(
      const std::shared_ptr<Match>& match,
      const std::shared_ptr<GameController>& gameController,
      const std::shared_ptr<ServerConnection>& connection,
      float uiScale,
      bool grabMouse,
      int georgiaFontID,
      const Palettes& palettes,
      const std::filesystem::path& graphicsSubPath,
      const std::filesystem::path& cachePath,
      QWindow* parent)
    : QOpenGLWindow(QOpenGLWindow::NoPartialUpdate, parent),
      grabMouse(grabMouse),
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
  
  georgiaFontTiny = georgiaFont;
  georgiaFontTiny.setPixelSize(uiScale * 2*12);
  
  georgiaFontHuge = georgiaFont;
  georgiaFontHuge.setPixelSize(uiScale * 2*40);
  georgiaFontHuge.setBold(true);
  
  connect(this, &RenderWindow::LoadingProgressUpdated, this, &RenderWindow::SendLoadingProgress, Qt::QueuedConnection);
  connect(this, &RenderWindow::LoadingError, this, &RenderWindow::LoadingErrorHandler, Qt::QueuedConnection);
  
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
  
  // Clip the cursor to the window, if requested.
  if (grabMouse) {
    QTimer::singleShot(0, [&]() {
      GrabMouse();
    });
  }
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
  woodVillagersTextDisplay.Destroy();
  resourceFood.Unload();
  foodTextDisplay.Destroy();
  foodVillagersTextDisplay.Destroy();
  resourceGold.Unload();
  goldTextDisplay.Destroy();
  goldVillagersTextDisplay.Destroy();
  resourceStone.Unload();
  stoneTextDisplay.Destroy();
  stoneVillagersTextDisplay.Destroy();
  pop.Unload();
  popTextDisplay.Destroy();
  popVillagersTextDisplay.Destroy();
  popWhiteBackgroundPointBuffer.Destroy();
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
  
  minimapPanel.Unload();
  minimap.reset();
  
  selectionPanel.Unload();
  singleObjectNameDisplay.Destroy();
  hpDisplay.Destroy();
  carriedResourcesDisplay.Destroy();
  selectionPanelIconPointBuffer.Destroy();
  selectionPanelIconOverlayPointBuffer.Destroy();
  for (int i = 0; i < kMaxProductionQueueSize; ++ i) {
    productionQueuePointBuffers[i].Destroy();
    productionQueueOverlayPointBuffers[i].Destroy();
  }
  productionProgressText.Destroy();
  productionProgressBar.Unload();
  productionProgressBarBackgroundPointBuffer.Destroy();
  
  iconOverlayNormalTexture.reset();
  iconOverlayNormalExpensiveTexture.reset();
  iconOverlayHoverTexture.reset();
  iconOverlayActiveTexture.reset();
  
  uiShader.reset();
  uiSingleColorShader.reset();
  uiSingleColorFullscreenShader.reset();
  spriteShader.reset();
  shadowShader.reset();
  outlineShader.reset();
  healthBarShader.reset();
  minimapShader.reset();
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

bool RenderWindow::LoadResources() {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  auto didLoadingStep = [&]() {
    ++ loadingStep;
    LOG(1) << "loadingStep: " << loadingStep;
    
    // We cannot directly send the loading progress message here, since a QTcpSocket can only be accessed from
    // one thread. So, we notify the main thread via a queued signal/slot connection.
    emit LoadingProgressUpdated(static_cast<int>(100 * loadingStep / static_cast<float>(maxLoadingStep) + 0.5f));
  };
  
  Timer loadResourcesTimer("LoadResources()");
  LOG(1) << "LoadResource() start";

  const GLubyte* glVendor = f->glGetString(GL_VENDOR);
  if (glVendor) {
    LOG(1) << "GL_VENDOR: " << glVendor;
  }
  const GLubyte* glRenderer = f->glGetString(GL_RENDERER);
  if (glRenderer) {
    LOG(1) << "GL_RENDERER: " << glRenderer;
  }
  const GLubyte* glVersion = f->glGetString(GL_VERSION);
  if (glVersion) {
    LOG(1) << "GL_VERSION: " << glVersion;
  }
  
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
  woodVillagersTextDisplay.Initialize();
  foodVillagersTextDisplay.Initialize();
  goldVillagersTextDisplay.Initialize();
  stoneVillagersTextDisplay.Initialize();
  popTextDisplay.Initialize();
  popVillagersTextDisplay.Initialize();
  popWhiteBackgroundPointBuffer.Initialize();
  currentAgeTextDisplay.Initialize();
  gameTimeDisplay.Initialize();
  gameTimeDisplayShadowPointBuffer.Initialize();
  fpsAndPingDisplay.Initialize();
  fpsAndPingDisplayShadowPointBuffer.Initialize();
  singleObjectNameDisplay.Initialize();
  hpDisplay.Initialize();
  carriedResourcesDisplay.Initialize();
  
  // Load unit resources.
  LOG(1) << "LoadResource(): Starting to load units";
  
  auto& unitTypes = ClientUnitType::GetUnitTypes();
  unitTypes.resize(static_cast<int>(UnitType::NumUnits));
  for (int unitType = 0; unitType < static_cast<int>(UnitType::NumUnits); ++ unitType) {
    Timer timer("LoadResources() - Load unit");
    if (!unitTypes[unitType].Load(static_cast<UnitType>(unitType), graphicsSubPath, cachePath, colorDilationShader.get(), palettes)) {
      LOG(ERROR) << "Exiting because of a resource load error for unit " << unitType << ".";
      emit LoadingError(tr("Failed to load unit type: %1. Aborting.").arg(unitType));
      return false;
    }
    
    didLoadingStep();
  }
  
  // Load building resources.
  LOG(1) << "LoadResource(): Starting to load buildings";
  
  auto& buildingTypes = ClientBuildingType::GetBuildingTypes();
  buildingTypes.resize(static_cast<int>(BuildingType::NumBuildings));
  for (int buildingType = 0; buildingType < static_cast<int>(BuildingType::NumBuildings); ++ buildingType) {
    Timer timer("LoadResources() - Load building");
    if (!buildingTypes[buildingType].Load(static_cast<BuildingType>(buildingType), graphicsSubPath, cachePath, colorDilationShader.get(), palettes)) {
      LOG(ERROR) << "Exiting because of a resource load error for building " << buildingType << ".";
      emit LoadingError(tr("Failed to load building type: %1. Aborting.").arg(buildingType));
      return false;
    }
    
    didLoadingStep();
  }
  
  // Load "move to" sprite.
  LOG(1) << "LoadResource(): Loading the 'move-to' sprite";
  
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
  
  QImage minimapPanelImage;
  minimapPanel.Load(GetModdedPath(architecturePanelsSubPath / "map-panel.png"), &minimapPanelImage);
  minimapPanelOpaquenessMap.Create(minimapPanelImage);
  
  minimap.reset(new Minimap());
  didLoadingStep();
  
  QImage selectionPanelImage;
  selectionPanel.Load(GetModdedPath(architecturePanelsSubPath / "single-selection-panel.png"), &selectionPanelImage);
  selectionPanelOpaquenessMap.Create(selectionPanelImage);
  didLoadingStep();
  
  selectionPanelIconPointBuffer.Initialize();
  selectionPanelIconOverlayPointBuffer.Initialize();
  for (int i = 0; i < kMaxProductionQueueSize; ++ i) {
    productionQueuePointBuffers[i].Initialize();
    productionQueueOverlayPointBuffers[i].Initialize();
  }
  productionProgressText.Initialize();
  productionProgressBar.Load(GetModdedPath(widgetuiTexturesSubPath / "ingame" / "panels" / "loadingbar_full.png"));
  productionProgressBarBackgroundPointBuffer.Initialize();
  
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
  
  loadResourcesTimer.Stop();
  // Output timings of the resource loading processes and clear those statistics from further timing prints.
  LOG(INFO) << "Loading timings:";
  Timing::print(std::cout, kSortByTotal);
  Timing::reset();
  
  // Check that the value of maxLoadingStep is correct.
  if (loadingStep != maxLoadingStep) {
    LOG(ERROR) << "DEBUG: After loading, loadingStep (" << loadingStep << ") != maxLoadingStep (" << maxLoadingStep << "). Please set the value of maxLoadingStep to " << loadingStep << " in render_window.cpp.";
  }
  
  return true;
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

void RenderWindow::LoadingErrorHandler(QString message) {
  QMessageBox::warning(nullptr, tr("Error while loading"), message);
  close();
}

void RenderWindow::LoadingFinished() {
  // TODO: In fullscreen mode on Windows, the deletion below causes the screen to flash black and
  //       also seemingly at least one old frame to be rendered. (In windowed mode, all is well.)
  //       The flashing also seems to happen on creation of the QOffscreenSurface, although it is not
  //       as noticeable at this point in time since this happens right after the render window
  //       is created.
  //       The current workaround is to never delete this QOffscreenSurface and let it leak.
  //       Try to find a better solution.
#ifndef WIN32
  delete loadingSurface;
#endif

  if (loadingThread->Succeeded()) {
    // Notify the server about the loading being finished
    connection->Write(CreateLoadingFinishedMessage());
  } else {
    // TODO: Notify the server about the loading failure
  }
  
  delete loadingThread;
  
  LOG(INFO) << "DEBUG: Loading finished.";
}


void RenderWindow::GrabMouse() {
  // Unfortunately, Qt does not seem to offer functionality for that, so we do it in platform-specific ways.
  #ifdef HAVE_X11_EXTRAS
    if (QX11Info::isPlatformX11()) {
      int result = -1;
      for (int attempt = 0; attempt < 20; ++ attempt) {
        result = ::XGrabPointer(
            QX11Info::display(),
            winId(),
            False,
            ButtonPressMask | ButtonReleaseMask | PointerMotionMask | FocusChangeMask | EnterWindowMask | LeaveWindowMask,
            GrabModeAsync,
            GrabModeAsync,
            winId(),
            None,
            CurrentTime);
        if (result == GrabSuccess) {
          break;
        }
        QThread::msleep(1);
      }
    }
  #elif WIN32
    RECT windowRect;
    if (GetClientRect(reinterpret_cast<HWND>(winId()), &windowRect)) {
      POINT ul;
      ul.x = windowRect.left;
      ul.y = windowRect.top;

      POINT lr;
      lr.x = windowRect.right;
      lr.y = windowRect.bottom;

      MapWindowPoints(reinterpret_cast<HWND>(winId()), nullptr, &ul, 1);
      MapWindowPoints(reinterpret_cast<HWND>(winId()), nullptr, &lr, 1);

      windowRect.left = ul.x;
      windowRect.top = ul.y;

      windowRect.right = lr.x;
      windowRect.bottom = lr.y;

      ClipCursor(&windowRect);
    }
  #endif
}

void RenderWindow::UngrabMouse() {
  // Unfortunately, Qt does not seem to offer functionality for that, so we do it in platform-specific ways.
  #ifdef HAVE_X11_EXTRAS
    if (QX11Info::isPlatformX11()) {
      XUngrabPointer(QX11Info::display(), CurrentTime);
    }
  #elif WIN32
    ClipCursor(nullptr);
  #endif
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
  
  // For debugging:
  // GLint actualBufferSize;
  // f->glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &actualBufferSize);
  // if (actualBufferSize != static_cast<int>(bufferObjects[nextBufferObject].size)) {
  //   LOG(FATAL) << "Actual buffer size (" << actualBufferSize << ") differs from expected size (" << bufferObjects[nextBufferObject].size << ")";
  // }
  
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
  
  minimapShader->GetProgram()->UseProgram(f);
  minimapShader->GetProgram()->SetUniformMatrix2fv(minimapShader->GetViewMatrixLocation(), pixelToOpenGLMatrix, true, f);
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
    spriteShader->GetProgram()->UseProgram(f);
    spriteShader->GetProgram()->SetUniformMatrix2fv(spriteShader->GetViewMatrixLocation(), viewMatrix, true, f);
    
    shadowShader->GetProgram()->UseProgram(f);
    shadowShader->GetProgram()->SetUniformMatrix2fv(shadowShader->GetViewMatrixLocation(), viewMatrix, true, f);
    
    outlineShader->GetProgram()->UseProgram(f);
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

void RenderWindow::RenderPath(float halfLineWidth, const QRgb& color, const std::vector<QPointF>& vertices, const QPointF& offset, bool closed, QOpenGLFunctions_3_2_Core* f) {
  CHECK_OPENGL_NO_ERROR();
  
  // If the path should be closed, repeat the first 2 vertices to close the path
  // and get information on the bend direction at the end.
  int numVertices = 2 * (vertices.size() + (closed ? 1 : 0));
  
  // Buffer geometry data.
  int elementSizeInBytes = 2 * sizeof(float);
  std::vector<float> vertexData(2 * numVertices);
  int lastVertex = vertices.size() - 1;
  for (usize i = 0; i < vertices.size() + (closed ? 1 : 0); ++ i) {
    int thisVertex = i % vertices.size();
    int nextVertex = (i + 1) % vertices.size();
    
    QPointF prevToCur = vertices[thisVertex] - vertices[lastVertex];
    QPointF curToNext = vertices[nextVertex] - vertices[thisVertex];
    
    lastVertex = thisVertex;
    
    float prevToCurNormalizer = 1.f / Length(prevToCur);
    prevToCur.setX(prevToCurNormalizer * prevToCur.x());
    prevToCur.setY(prevToCurNormalizer * prevToCur.y());
    
    float curToNextNormalizer = 1.f / Length(curToNext);
    curToNext.setX(curToNextNormalizer * curToNext.x());
    curToNext.setY(curToNextNormalizer * curToNext.y());
    
    QPointF prevToCurRight(halfLineWidth * -prevToCur.y(), halfLineWidth * prevToCur.x());
    
    // Special cases for the ends of open paths.
    if (!closed && i == 0) {
      QPointF curToNextRight(halfLineWidth * -curToNext.y(), halfLineWidth * curToNext.x());
      
      // Vertex to the left of the line
      vertexData[4 * i + 0] = vertices[thisVertex].x() - curToNextRight.x() + offset.x();
      vertexData[4 * i + 1] = vertices[thisVertex].y() - curToNextRight.y() + offset.y();
      
      // Vertex to the right of the line
      vertexData[4 * i + 2] = vertices[thisVertex].x() + curToNextRight.x() + offset.x();
      vertexData[4 * i + 3] = vertices[thisVertex].y() + curToNextRight.y() + offset.y();
      
      continue;
    } else if (!closed && i == vertices.size() - 1) {
      // Vertex to the left of the line
      vertexData[4 * i + 0] = vertices[thisVertex].x() - prevToCurRight.x() + offset.x();
      vertexData[4 * i + 1] = vertices[thisVertex].y() - prevToCurRight.y() + offset.y();
      
      // Vertex to the right of the line
      vertexData[4 * i + 2] = vertices[thisVertex].x() + prevToCurRight.x() + offset.x();
      vertexData[4 * i + 3] = vertices[thisVertex].y() + prevToCurRight.y() + offset.y();
      
      continue;
    }
    
    int bendDirection = ((prevToCurRight.x() * curToNext.x() + prevToCurRight.y() * curToNext.y()) > 0) ? 1 : -1;
    
    float halfBendAngle = std::max<float>(1e-4f, 0.5f * acos(std::max<float>(-1, std::min<float>(1, prevToCur.x() * -curToNext.x() + prevToCur.y() * -curToNext.y()))));
    float length = halfLineWidth / tan(halfBendAngle);
    
    // Vertex to the left of the line
    vertexData[4 * i + 0] = vertices[thisVertex].x() - prevToCurRight.x() + bendDirection * length * prevToCur.x() + offset.x();
    vertexData[4 * i + 1] = vertices[thisVertex].y() - prevToCurRight.y() + bendDirection * length * prevToCur.y() + offset.y();
    
    // Vertex to the right of the line
    vertexData[4 * i + 2] = vertices[thisVertex].x() + prevToCurRight.x() - bendDirection * length * prevToCur.x() + offset.x();
    vertexData[4 * i + 3] = vertices[thisVertex].y() + prevToCurRight.y() - bendDirection * length * prevToCur.y() + offset.y();
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
      2,
      GetGLType<float>::value,
      2 * sizeof(float),
      0,
      f);
  
  // Draw lines.
  f->glDrawArrays(GL_TRIANGLE_STRIP, 0, numVertices);
  CHECK_OPENGL_NO_ERROR();
}

void RenderWindow::RenderSprites(std::vector<Texture*>* textures, const std::shared_ptr<SpriteShader>& shader, QOpenGLFunctions_3_2_Core* f) {
  shader->GetProgram()->UseProgram(f);
  
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
      
      void* data = f->glMapBufferRange(GL_ARRAY_BUFFER, 0, bufferSize, GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
      memcpy(data, texture->DrawCallBuffer().data(), bufferSize);
      f->glUnmapBuffer(GL_ARRAY_BUFFER);
      
      shader->UseProgramAndSetAttribPointers(f);  // TODO: We only need to set up the vertex attributes again after changing the GL_ARRAY_BUFFER buffer; we would not need to "use" the program again
      
      f->glDrawArrays(GL_POINTS, 0, bufferSize / vertexSize);
    }
    
    texture->DrawCallBuffer().clear();
  }
}

void RenderWindow::RenderShadows(double displayedServerTime, QOpenGLFunctions_3_2_Core* f) {
  auto& unitTypes = ClientUnitType::GetUnitTypes();
  
  std::vector<Texture*> textures;
  textures.reserve(64);
  
  float effectiveZoom = ComputeEffectiveZoom();
  
  for (auto& object : map->GetObjects()) {
    // TODO: Use virtual functions here to reduce duplicated code among buildings and units?
    
    if (object.second->isBuilding()) {
      ClientBuilding& building = *AsBuilding(object.second);
      if (!building.GetSprite().HasShadow()) {
        continue;
      }
      int maxViewCount = map->ComputeMaxViewCountForBuilding(&building);
      if (maxViewCount < 0) {
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
      ClientUnit& unit = *AsUnit(object.second);
      if (!unitTypes[static_cast<int>(unit.GetType())].GetAnimations(unit.GetCurrentAnimation()).front()->sprite.HasShadow()) {
        continue;
      }
      if (map->IsUnitInFogOfWar(&unit)) {
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
  spriteShader->GetProgram()->UseProgram(f);
  
  Timer preparationTimer("RenderBuildings() preparation");
  
  std::vector<Texture*> textures;
  textures.reserve(64);
  
  float effectiveZoom = ComputeEffectiveZoom();
  
  for (auto& object : map->GetObjects()) {
    if (!object.second->isBuilding()) {
      continue;
    }
    ClientBuilding& building = *AsBuilding(object.second);
    if (buildingsThatCauseOutlines != ClientBuildingType::GetBuildingTypes()[static_cast<int>(building.GetType())].DoesCauseOutlines()) {
      continue;
    }
    
    int maxViewCount = map->ComputeMaxViewCountForBuilding(&building);
    if (maxViewCount < 0) {
      continue;
    }
    u8 intensity = (maxViewCount > 0) ? 255 : 168;
    
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
          qRgb(intensity, intensity, intensity),
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
  spriteShader->GetProgram()->UseProgram(f);
  
  QPoint foundationBaseTile(-1, -1);
  bool canBePlacedHere = CanBuildingFoundationBePlacedHere(constructBuildingType, lastCursorPos, &foundationBaseTile);
  
  float effectiveZoom = ComputeEffectiveZoom();
  
  if (foundationBaseTile.x() >= 0 && foundationBaseTile.y() >= 0) {
    // Check whether any tile below the foundation is not in the black fog-of-war.
    // Only display the foundation in this case.
    QSize foundationSize = GetBuildingSize(constructBuildingType);
    int maxViewCount = -1;
    for (int y = foundationBaseTile.y(); y < foundationBaseTile.y() + foundationSize.width(); ++ y) {
      for (int x = foundationBaseTile.x(); x < foundationBaseTile.x() + foundationSize.height(); ++ x) {
        maxViewCount = std::max(maxViewCount, map->viewCountAt(x, y));
      }
    }
    
    if (maxViewCount >= 0) {
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
    ClientBuilding& building = *AsBuilding(object);
    
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
    
    RenderPath(effectiveZoom * 1.1f, qRgba(0, 0, 0, 255), outlineVertices, QPointF(0, effectiveZoom * 2), /*closed*/ true, f);
    RenderPath(effectiveZoom * 1.1f, color, outlineVertices, QPointF(0, 0), /*closed*/ true, f);
  } else if (object->isUnit()) {
    ClientUnit& unit = *AsUnit(object);
    
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
    
    RenderPath(effectiveZoom * 1.1f, qRgba(0, 0, 0, 255), outlineVertices, QPointF(0, effectiveZoom * 2), /*closed*/ true, f);
    RenderPath(effectiveZoom * 1.1f, color, outlineVertices, QPointF(0, 0), /*closed*/ true, f);
  }
}

void RenderWindow::RenderOutlines(double displayedServerTime, QOpenGLFunctions_3_2_Core* f) {
  auto& unitTypes = ClientUnitType::GetUnitTypes();
  
  outlineShader->GetProgram()->UseProgram(f);
  
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
      ClientBuilding& building = *AsBuilding(object.second);
      if (!building.GetSprite().HasOutline()) {
        continue;
      }
      
      int maxViewCount = map->ComputeMaxViewCountForBuilding(&building);
      if (maxViewCount < 0) {
        continue;
      } else if (maxViewCount == 0) {
        float intensity = 168 / 255.f;
        outlineColor = qRgb(intensity * qRed(outlineColor),
                            intensity * qGreen(outlineColor),
                            intensity * qBlue(outlineColor));
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
      ClientUnit& unit = *AsUnit(object.second);
      if (!unitTypes[static_cast<int>(unit.GetType())].GetAnimations(unit.GetCurrentAnimation()).front()->sprite.HasOutline()) {
        continue;
      }
      if (map->IsUnitInFogOfWar(&unit)) {
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
  spriteShader->GetProgram()->UseProgram(f);
  
  std::vector<Texture*> textures;
  textures.reserve(64);
  
  float effectiveZoom = ComputeEffectiveZoom();
  
  for (auto& object : map->GetObjects()) {
    if (!object.second->isUnit()) {
      continue;
    }
    ClientUnit& unit = *AsUnit(object.second);
    if (map->IsUnitInFogOfWar(&unit)) {
      continue;
    }
    
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
  spriteShader->GetProgram()->UseProgram(f);
  
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
  
  for (u32 id : selection) {
    auto it = map->GetObjects().find(id);
    if (it == map->GetObjects().end()) {
      continue;
    }
    ClientObject* object = it->second;
    
    // TODO: Use virtual functions here to reduce duplicated code among buildings and units?
    
    if (object->isBuilding()) {
      ClientBuilding& building = *AsBuilding(object);
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
        PrepareBufferObject(3 * sizeof(float), f);
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
    } else if (object->isUnit()) {
      ClientUnit& unit = *AsUnit(object);
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
        PrepareBufferObject(3 * sizeof(float), f);
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
  spriteShader->GetProgram()->UseProgram(f);
  
  std::vector<Texture*> textures;
  textures.reserve(64);
  
  float effectiveZoom = ComputeEffectiveZoom();
  
  for (auto& decal : decals) {
    int maxViewCount = -1;
    for (int y = decal->GetMinTileY(); y <= decal->GetMaxTileY(); ++ y) {
      for (int x = decal->GetMinTileX(); x <= decal->GetMaxTileX(); ++ x) {
        maxViewCount = std::max(maxViewCount, map->viewCountAt(x, y));
      }
    }
    if (maxViewCount <= 0) {
      // TODO: Decals in semi-black areas should be visible!
      //       They are currently not shown since the fog-of-war implementation
      //       is purely on the client side at the moment, so showing them would
      //       reveal information that the player should not know.
      continue;
    }
    
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
  shadowShader->GetProgram()->UseProgram(f);
  
  std::vector<Texture*> textures;
  textures.reserve(64);
  
  float effectiveZoom = ComputeEffectiveZoom();
  
  for (auto& decal : occludingDecals) {
    int maxViewCount = -1;
    for (int y = decal->GetMinTileY(); y <= decal->GetMaxTileY(); ++ y) {
      for (int x = decal->GetMinTileX(); x <= decal->GetMaxTileX(); ++ x) {
        maxViewCount = std::max(maxViewCount, map->viewCountAt(x, y));
      }
    }
    if (maxViewCount <= 0) {
      // TODO: Decals in semi-black areas should be visible!
      //       They are currently not shown since the fog-of-war implementation
      //       is purely on the client side at the moment, so showing them would
      //       reveal information that the player should not know.
      continue;
    }
    
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
  outlineShader->GetProgram()->UseProgram(f);
  
  std::vector<Texture*> textures;
  textures.reserve(64);
  
  float effectiveZoom = ComputeEffectiveZoom();
  
  for (auto& decal : occludingDecals) {
    int maxViewCount = -1;
    for (int y = decal->GetMinTileY(); y <= decal->GetMaxTileY(); ++ y) {
      for (int x = decal->GetMinTileX(); x <= decal->GetMaxTileX(); ++ x) {
        maxViewCount = std::max(maxViewCount, map->viewCountAt(x, y));
      }
    }
    if (maxViewCount <= 0) {
      // TODO: Decals in semi-black areas should be visible!
      //       They are currently not shown since the fog-of-war implementation
      //       is purely on the client side at the moment, so showing them would
      //       reveal information that the player should not know.
      continue;
    }
    
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

void RenderWindow::RenderGameUI(double displayedServerTime, double secondsSinceLastFrame, QOpenGLFunctions_3_2_Core* f) {
  RenderMenuPanel(f);
  
  RenderResourcePanel(f);
  
  RenderMinimap(secondsSinceLastFrame, f);
  
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
    fpsAndPingString = QObject::tr("%1 FPS | Ping: %2 ms").arg(roundedFPS).arg(static_cast<int>(1000 * filteredPing + 0.5f));
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
  int currentY = GetMinimapPanelTopLeft().y();
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
  
  auto renderSingleResource = [&](int index, TextureAndPointBuffer& resourceDisplay,
      TextDisplayAndPointBuffer& resourceTextDisplay, TextDisplayAndPointBuffer& villagersTextDisplay,
      int resourceCount, int villagerCount) {
    // Render one of the four resources
    RenderUIGraphic(
        topLeft.x() + uiScale * (17 + index * 200),
        topLeft.y() + uiScale * 16,
        uiScale * 83,
        uiScale * 83,
        qRgba(255, 255, 255, 255), resourceDisplay.pointBuffer,
        *resourceDisplay.texture,
        uiShader.get(), widgetWidth, widgetHeight, f);
    resourceTextDisplay.textDisplay->Render(
        georgiaFontSmaller,
        qRgba(255, 255, 255, 255),
        QString::number(resourceCount),
        QRect(topLeft.x() + uiScale * (17 + index * 200 + 83 + 16),
              topLeft.y() + uiScale * 16,
              uiScale * 82,
              uiScale * 83),
        Qt::AlignLeft | Qt::AlignVCenter,
        resourceTextDisplay.pointBuffer,
        uiShader.get(), widgetWidth, widgetHeight, f);
    villagersTextDisplay.textDisplay->Render(
        georgiaFontTiny,
        qRgba(255, 255, 255, 255),
        QString::number(villagerCount),
        QRect(topLeft.x() + uiScale * (17 + index * 200),
              topLeft.y() + uiScale * 16,
              uiScale * 79,
              uiScale * 83),
        Qt::AlignRight | Qt::AlignBottom,
        villagersTextDisplay.pointBuffer,
        uiShader.get(), widgetWidth, widgetHeight, f);
  };
  
  renderSingleResource(0, resourceWood, woodTextDisplay, woodVillagersTextDisplay, resources.wood(),
      gameController->GetUnitTypeCount(UnitType::MaleVillagerLumberjack) + gameController->GetUnitTypeCount(UnitType::FemaleVillagerLumberjack));
  renderSingleResource(1, resourceFood, foodTextDisplay, foodVillagersTextDisplay, resources.food(),
      gameController->GetUnitTypeCount(UnitType::MaleVillagerForager) + gameController->GetUnitTypeCount(UnitType::FemaleVillagerForager));
  renderSingleResource(2, resourceGold, goldTextDisplay, goldVillagersTextDisplay, resources.gold(),
      gameController->GetUnitTypeCount(UnitType::MaleVillagerGoldMiner) + gameController->GetUnitTypeCount(UnitType::FemaleVillagerGoldMiner));
  renderSingleResource(3, resourceStone, stoneTextDisplay, stoneVillagersTextDisplay, resources.stone(),
      gameController->GetUnitTypeCount(UnitType::MaleVillagerStoneMiner) + gameController->GetUnitTypeCount(UnitType::FemaleVillagerStoneMiner));
  
  RenderUIGraphic(
      topLeft.x() + uiScale * (17 + 4 * 200),
      topLeft.y() + uiScale * 16,
      uiScale * 83,
      uiScale * 83,
      qRgba(255, 255, 255, 255), pop.pointBuffer,
      *pop.texture,
      uiShader.get(), widgetWidth, widgetHeight, f);
  bool populationBlinking = false;
  if (gameController->IsPlayerHoused()) {
    if (housedStartTime < 0) {
      housedStartTime = lastDisplayedServerTime;
    }
    
    constexpr float kHousedBlinkInterval = 0.6f;
    if (fmod(lastDisplayedServerTime - housedStartTime, 2 * kHousedBlinkInterval) <= kHousedBlinkInterval) {
      populationBlinking = true;
      
      // Draw a white background for the population display.
      QRectF rect(
          topLeft.x() + uiScale * (17 + 4 * 200 + 83 + 16),
          topLeft.y() + uiScale * 2*18,
          uiScale * 2*60,
          uiScale * 2*22);
      
      f->glBindBuffer(GL_ARRAY_BUFFER, popWhiteBackgroundPointBuffer.buffer);
      f->glBufferData(GL_ARRAY_BUFFER, 8 * sizeof(float), nullptr, GL_STREAM_DRAW);
      
      uiSingleColorShader->GetProgram()->UseProgram(f);
      f->glUniform4f(uiSingleColorShader->GetColorLocation(), 1.f, 1.f, 1.f, 1.f);
      
      float* data = static_cast<float*>(f->glMapBufferRange(GL_ARRAY_BUFFER, 0, 8 * sizeof(float), GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT));
      data[0] = rect.x();
      data[1] = rect.y();
      data[2] = rect.right();
      data[3] = rect.y();
      data[4] = rect.x();
      data[5] = rect.bottom();
      data[6] = rect.right();
      data[7] = rect.bottom();
      f->glUnmapBuffer(GL_ARRAY_BUFFER);
      CHECK_OPENGL_NO_ERROR();
      uiSingleColorShader->GetProgram()->SetPositionAttribute(
          2,
          GetGLType<float>::value,
          2 * sizeof(float),
          0,
          f);
      
      f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      CHECK_OPENGL_NO_ERROR();
    }
  } else {
    housedStartTime = -1;
  }
  popTextDisplay.textDisplay->Render(
      georgiaFontSmaller,
      populationBlinking ? qRgba(0, 0, 0, 255) : qRgba(255, 255, 255, 255),
      tr("%1 / %2").arg(gameController->GetPopulationCount()).arg(gameController->GetAvailablePopulationSpace()),
      QRect(topLeft.x() + uiScale * (17 + 4 * 200 + 83 + 16),
            topLeft.y() + uiScale * 16,
            uiScale * 82,
            uiScale * 83),
      Qt::AlignLeft | Qt::AlignVCenter,
      popTextDisplay.pointBuffer,
      uiShader.get(), widgetWidth, widgetHeight, f);
  popVillagersTextDisplay.textDisplay->Render(
      georgiaFontTiny,
      qRgba(255, 255, 255, 255),
      QString::number(gameController->GetVillagerCount()),
      QRect(topLeft.x() + uiScale * (17 + 4 * 200),
            topLeft.y() + uiScale * 16,
            uiScale * 79,
            uiScale * 83),
      Qt::AlignRight | Qt::AlignBottom,
      popVillagersTextDisplay.pointBuffer,
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

QPointF RenderWindow::GetMinimapPanelTopLeft() {
  return QPointF(
      widgetWidth - uiScale * minimapPanel.texture->GetWidth(),
      widgetHeight - uiScale * minimapPanel.texture->GetHeight());
}

void RenderWindow::RenderMinimap(double secondsSinceLastFrame, QOpenGLFunctions_3_2_Core* f) {
  QPointF topLeft = GetMinimapPanelTopLeft();
  
  RenderUIGraphic(
      topLeft.x(),
      topLeft.y(),
      uiScale * minimapPanel.texture->GetWidth(),
      uiScale * minimapPanel.texture->GetHeight(),
      qRgba(255, 255, 255, 255), minimapPanel.pointBuffer,
      *minimapPanel.texture,
      uiShader.get(), widgetWidth, widgetHeight, f);
  
  // Update the minimap?
  timeSinceLastMinimapUpdate += secondsSinceLastFrame;
  if (timeSinceLastMinimapUpdate >= minimapUpdateInterval) {
    timeSinceLastMinimapUpdate = fmod(timeSinceLastMinimapUpdate, minimapUpdateInterval);
    
    minimap->Update(map.get(), playerColors, f);
  }
  
  // Render the minimap
  minimap->Render(topLeft, uiScale, minimapShader, f);
  
  // Render the frame showing the (approximate) bounds of the current view on the minimap
  QPointF projectedCoordAtScreenCenter = map->MapCoordToProjectedCoord(scroll) + scrollProjectedCoordOffset;
  QPointF screenCenterMapCoord;
  if (!map->ProjectedCoordToMapCoord(projectedCoordAtScreenCenter, &screenCenterMapCoord)) {
    screenCenterMapCoord = scroll;
  }
  
  float frameCenterX;
  float frameCenterY;
  minimap->MapCoordToScreen(screenCenterMapCoord.x(), screenCenterMapCoord.y(), topLeft, uiScale, map.get(), &frameCenterX, &frameCenterY);
  
  QPointF corners[4];
  minimap->GetMinimapCorners(topLeft, uiScale, corners);
  const QPointF& top = corners[0];
  const QPointF& right = corners[1];
  const QPointF& bottom = corners[2];
  const QPointF& left = corners[3];
  
  // NOTE: Assuming the same scaling for x and y (i.e., map width == map height).
  float minimapScaling = Length(top - left) / Length(map->MapCoordToProjectedCoord(QPointF(0, map->GetHeight())));
  
  float halfScreenWidthOnMinimap = minimapScaling * 0.5f * projectedCoordsViewRect.width();
  float halfScreenHeightOnMinimap = minimapScaling * 0.5f * projectedCoordsViewRect.height();
  
  std::vector<QPointF> vertices(2);
  QPointF topLeftCorner = QPointF(frameCenterX - halfScreenWidthOnMinimap, frameCenterY - halfScreenHeightOnMinimap);
  QPointF topRightCorner = QPointF(frameCenterX + halfScreenWidthOnMinimap, frameCenterY - halfScreenHeightOnMinimap);
  QPointF bottomLeftCorner = QPointF(frameCenterX - halfScreenWidthOnMinimap, frameCenterY + halfScreenHeightOnMinimap);
  QPointF bottomRightCorner = QPointF(frameCenterX + halfScreenWidthOnMinimap, frameCenterY + halfScreenHeightOnMinimap);
  QPointF shadowOffset = uiScale * QPointF(2, 2);
  
  QPointF leftToTop_right = RightVector(top - left);
  QPointF topToRight_right = RightVector(right - top);
  QPointF rightToBottom_right = RightVector(bottom - right);
  QPointF bottomToLeft_right = RightVector(left - bottom);
  
  auto clamp = [&](const QPointF& input, float xDir, float yDir) {
    QPointF result = input;
    
    // Clamp by top-left edge of minimap?
    if (Dot(leftToTop_right, result - left) < 0) {
      IntersectLines(result, QPointF(xDir, yDir), left, top - left, &result);
    }
    
    // Clamp by top-right edge of minimap?
    if (Dot(topToRight_right, result - top) < 0) {
      IntersectLines(result, QPointF(xDir, yDir), top, right - top, &result);
    }
    
    // Clamp by bottom-right edge of minimap?
    if (Dot(rightToBottom_right, result - right) < 0) {
      IntersectLines(result, QPointF(xDir, yDir), right, bottom - right, &result);
    }
    
    // Clamp by bottom-left edge of minimap?
    if (Dot(bottomToLeft_right, result - bottom) < 0) {
      IntersectLines(result, QPointF(xDir, yDir), bottom, left - bottom, &result);
    }
    
    return result;
  };
  
  // --- Shadow ---
  // Left edge
  if ((topLeftCorner + shadowOffset).x() >= left.x()) {
    vertices[0] = clamp(topLeftCorner + shadowOffset, 0, 1);
    vertices[1] = clamp(bottomLeftCorner + shadowOffset, 0, -1);
    RenderPath(uiScale * 2.f, qRgba(0, 0, 0, 255), vertices, QPointF(0, 0), /*closed*/ false, f);
  }
  
  // Right edge
  if ((topRightCorner + shadowOffset).x() <= right.x()) {
    vertices[0] = clamp(topRightCorner + shadowOffset, 0, 1);
    vertices[1] = clamp(bottomRightCorner + shadowOffset, 0, -1);
    RenderPath(uiScale * 2.f, qRgba(0, 0, 0, 255), vertices, QPointF(0, 0), /*closed*/ false, f);
  }
  
  // Top edge
  if ((topLeftCorner + shadowOffset).y() >= top.y()) {
    vertices[0] = clamp(topLeftCorner + shadowOffset, 1, 0);
    vertices[1] = clamp(topRightCorner + shadowOffset, -1, 0);
    RenderPath(uiScale * 2.f, qRgba(0, 0, 0, 255), vertices, QPointF(0, 0), /*closed*/ false, f);
  }
  
  // Bottom edge
  if ((bottomLeftCorner + shadowOffset).y() <= bottom.y()) {
    vertices[0] = clamp(bottomLeftCorner + shadowOffset, 1, 0);
    vertices[1] = clamp(bottomRightCorner + shadowOffset, -1, 0);
    RenderPath(uiScale * 2.f, qRgba(0, 0, 0, 255), vertices, QPointF(0, 0), /*closed*/ false, f);
  }
  
  // --- Frame ---
  // Left edge
  if (topLeftCorner.x() >= left.x()) {
    vertices[0] = clamp(topLeftCorner, 0, 1);
    vertices[1] = clamp(bottomLeftCorner, 0, -1);
    RenderPath(uiScale * 2.f, qRgba(255, 255, 255, 255), vertices, QPointF(0, 0), /*closed*/ false, f);
  }
  
  // Right edge
  if (topRightCorner.x() <= right.x()) {
    vertices[0] = clamp(topRightCorner, 0, 1);
    vertices[1] = clamp(bottomRightCorner, 0, -1);
    RenderPath(uiScale * 2.f, qRgba(255, 255, 255, 255), vertices, QPointF(0, 0), /*closed*/ false, f);
  }
  
  // Top edge
  if (topLeftCorner.y() >= top.y()) {
    vertices[0] = clamp(topLeftCorner, 1, 0);
    vertices[1] = clamp(topRightCorner, -1, 0);
    RenderPath(uiScale * 2.f, qRgba(255, 255, 255, 255), vertices, QPointF(0, 0), /*closed*/ false, f);
  }
  
  // Bottom edge
  if (bottomLeftCorner.y() <= bottom.y()) {
    vertices[0] = clamp(bottomLeftCorner, 1, 0);
    vertices[1] = clamp(bottomRightCorner, -1, 0);
    RenderPath(uiScale * 2.f, qRgba(255, 255, 255, 255), vertices, QPointF(0, 0), /*closed*/ false, f);
  }
}

QPointF RenderWindow::GetSelectionPanelTopLeft() {
  return QPointF(
      uiScale * 539,
      widgetHeight - uiScale * selectionPanel.texture->GetHeight());
}

void RenderWindow::RenderObjectIcon(const Texture* iconTexture, float x, float y, float size, int state, GLuint iconPointBuffer, GLuint overlayPointBuffer, QOpenGLFunctions_3_2_Core* f) {
  float iconInset = uiScale * 4;
  RenderUIGraphic(
      x + iconInset,
      y + iconInset,
      size - (size / (uiScale * 2 * 60.f)) * 2 * iconInset,
      size - (size / (uiScale * 2 * 60.f)) * 2 * iconInset,
      qRgba(255, 255, 255, 255),
      iconPointBuffer,
      *iconTexture,
      uiShader.get(), widgetWidth, widgetHeight, f);
  RenderUIGraphic(
      x,
      y,
      size,
      size,
      qRgba(255, 255, 255, 255),
      overlayPointBuffer,
      (state == 2) ? *iconOverlayActiveTexture : ((state == 1) ? *iconOverlayHoverTexture : *iconOverlayNormalTexture),
      uiShader.get(), widgetWidth, widgetHeight, f);
}

void RenderWindow::RenderSelectionPanel(QOpenGLFunctions_3_2_Core* f) {
  QPointF topLeft = GetSelectionPanelTopLeft();
  
  for (int i = 0; i < kMaxProductionQueueSize; ++ i) {
    productionQueueIconsSize[i] = -1;
  }
  
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
        QRect(topLeft.x() + uiScale * (2*32),
              topLeft.y() + uiScale * (50 + 2*25),
              uiScale * 2*172,
              uiScale * 2*16),
        Qt::AlignLeft | Qt::AlignTop,
        singleObjectNameDisplay.pointBuffer,
        uiShader.get(), widgetWidth, widgetHeight, f);
    
    // Display the object's HP
    if (singleSelectedObject->GetHP() > 0) {
      u32 maxHP;
      if (singleSelectedObject->isUnit()) {
        maxHP = GetUnitMaxHP(AsUnit(singleSelectedObject)->GetType());
      } else {
        CHECK(singleSelectedObject->isBuilding());
        maxHP = GetBuildingMaxHP(AsBuilding(singleSelectedObject)->GetType());
      }
      
      hpDisplay.textDisplay->Render(
          georgiaFontSmaller,
          qRgba(58, 29, 21, 255),
          QStringLiteral("%1 / %2").arg(singleSelectedObject->GetHP()).arg(maxHP),
          QRect(topLeft.x() + uiScale * (2*32),
                topLeft.y() + uiScale * (50 + 2*46 + 2*60),
                uiScale * 2*172,
                uiScale * 2*16),
          Qt::AlignLeft | Qt::AlignTop,
          hpDisplay.pointBuffer,
          uiShader.get(), widgetWidth, widgetHeight, f);
    }
    
    // Display unit / building details?
    if (singleSelectedObject->isUnit()) {
      ClientUnit* singleSelectedUnit = AsUnit(singleSelectedObject);
      
      if (IsVillager(singleSelectedUnit->GetType())) {
        // Display the villager's carried resources?
        if (singleSelectedUnit->GetCarriedResourceAmount() > 0) {
          carriedResourcesDisplay.textDisplay->Render(
              georgiaFontSmaller,
              qRgba(58, 29, 21, 255),
              QObject::tr("Carries %1 %2")
                  .arg(singleSelectedUnit->GetCarriedResourceAmount())
                  .arg(GetResourceName(singleSelectedUnit->GetCarriedResourceType())),
              QRect(topLeft.x() + uiScale * (2*32),
                    topLeft.y() + uiScale * (50 + 2*46 + 2*60 + 2*20),
                    uiScale * 2*172,
                    uiScale * 2*16),
              Qt::AlignLeft | Qt::AlignTop,
              carriedResourcesDisplay.pointBuffer,
              uiShader.get(), widgetWidth, widgetHeight, f);
        }
      }
    } else if (singleSelectedObject->isBuilding()) {
      ClientBuilding* singleSelectedBuilding = AsBuilding(singleSelectedObject);
      
      if (!singleSelectedBuilding->GetProductionQueue().empty()) {
        // Render the unit that is currently being produced.
        UnitType type = singleSelectedBuilding->GetProductionQueue().front();
        const Texture* iconTexture = ClientUnitType::GetUnitTypes()[static_cast<int>(type)].GetIconTexture();
        if (iconTexture) {
          productionQueueIconsTopLeft[0] = QPointF(
              topLeft.x() + uiScale * (2*32 + 2*155),
              topLeft.y() + uiScale * (50 + 2*46));
          productionQueueIconsSize[0] = uiScale * 2*35;
          int state = 0;
          if (lastMouseMoveEventPos.x() >= productionQueueIconsTopLeft[0].x() &&
              lastMouseMoveEventPos.y() >= productionQueueIconsTopLeft[0].y() &&
              lastMouseMoveEventPos.x() < productionQueueIconsTopLeft[0].x() + productionQueueIconsSize[0]  &&
              lastMouseMoveEventPos.y() < productionQueueIconsTopLeft[0].y() + productionQueueIconsSize[0]) {
            state = 1;
          }
          RenderObjectIcon(
              iconTexture,
              productionQueueIconsTopLeft[0].x(),
              productionQueueIconsTopLeft[0].y(),
              productionQueueIconsSize[0],
              (pressedProductionQueueItem == 0) ? 2 : state,
              productionQueuePointBuffers[0].buffer,
              productionQueueOverlayPointBuffers[0].buffer,
              f);
        }
        
        // Render progress text
        float floatProgress = singleSelectedBuilding->GetProductionProgress(lastDisplayedServerTime);
        int progress = static_cast<int>(floatProgress + 0.5f);
        productionProgressText.textDisplay->Render(
              georgiaFontLarger,
              qRgba(58, 29, 21, 255),
              QObject::tr("Creating (%1%)").arg(progress),
              QRect(topLeft.x() + uiScale * (2*32 + 2*155 + 2*35 + 2*6),
                    topLeft.y() + uiScale * (50 + 2*46),
                    uiScale * 2*200,
                    uiScale * 2*35),
              Qt::AlignLeft | Qt::AlignVCenter,
              productionProgressText.pointBuffer,
              uiShader.get(), widgetWidth, widgetHeight, f);
        
        // Render progress bar
        int progressBarMaxWidth = uiScale * 2 * 140;
        RenderUIGraphic(
            topLeft.x() + uiScale * (2*32 + 2*155),
            topLeft.y() + uiScale * (50 + 2*46 + 2*35 + 2*2),
            progressBarMaxWidth,
            uiScale * 2*10,
            qRgba(20, 20, 20, 255),
            productionProgressBarBackgroundPointBuffer.buffer,
            *productionProgressBar.texture,
            uiShader.get(), widgetWidth, widgetHeight, f);
        RenderUIGraphic(
            topLeft.x() + uiScale * (2*32 + 2*155),
            topLeft.y() + uiScale * (50 + 2*46 + 2*35 + 2*2),
            floatProgress / 100.f * progressBarMaxWidth,
            uiScale * 2*10,
            qRgba(255, 255, 255, 255),
            productionProgressBar.pointBuffer,
            *productionProgressBar.texture,
            uiShader.get(), widgetWidth, widgetHeight, f,
            floatProgress / 100.f, 1.f);
      }
      
      // Render the units that are queued behind the currently produced one.
      for (usize queueIndex = 1; queueIndex < singleSelectedBuilding->GetProductionQueue().size(); ++ queueIndex) {
        UnitType type = singleSelectedBuilding->GetProductionQueue()[queueIndex];
        const Texture* iconTexture = ClientUnitType::GetUnitTypes()[static_cast<int>(type)].GetIconTexture();
        if (iconTexture) {
          productionQueueIconsTopLeft[queueIndex] = QPointF(
              topLeft.x() + uiScale * (2*32 + 2*155 + 2*(queueIndex - 1)*35),
              topLeft.y() + uiScale * (50 + 2*46 + 2*49));
          productionQueueIconsSize[queueIndex] = uiScale * 2*35;
          int state = 0;
          if (lastMouseMoveEventPos.x() >= productionQueueIconsTopLeft[queueIndex].x() &&
              lastMouseMoveEventPos.y() >= productionQueueIconsTopLeft[queueIndex].y() &&
              lastMouseMoveEventPos.x() < productionQueueIconsTopLeft[queueIndex].x() + productionQueueIconsSize[queueIndex]  &&
              lastMouseMoveEventPos.y() < productionQueueIconsTopLeft[queueIndex].y() + productionQueueIconsSize[queueIndex]) {
            state = 1;
          }
          RenderObjectIcon(
              iconTexture,
              productionQueueIconsTopLeft[queueIndex].x(),
              productionQueueIconsTopLeft[queueIndex].y(),
              productionQueueIconsSize[queueIndex],
              (pressedProductionQueueItem == static_cast<int>(queueIndex)) ? 2 : state,
              productionQueuePointBuffers[queueIndex].buffer,
              productionQueueOverlayPointBuffers[queueIndex].buffer,
              f);
        }
      }
    }
    
    // Render icon of single selected object
    const Texture* iconTexture = singleSelectedObject->GetIconTexture();
    if (iconTexture) {
      RenderObjectIcon(
          iconTexture,
          topLeft.x() + uiScale * (2*32),
          topLeft.y() + uiScale * (50 + 2*46),
          uiScale * 2*60,
          0,
          selectionPanelIconPointBuffer.buffer,
          selectionPanelIconOverlayPointBuffer.buffer,
          f);
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
      
      if (commandButtons[row][col].GetType() == CommandButton::Type::Invisible) {
        continue; // skip invisible buttons
      }

      bool pressed =
          pressedCommandButtonRow == row &&
          pressedCommandButtonCol == col;
      bool mouseOver =
          lastCursorPos.x() >= buttonLeft &&
          lastCursorPos.y() >= buttonTop &&
          lastCursorPos.x() < buttonLeft + commandButtonSize &&
          lastCursorPos.y() < buttonTop + commandButtonSize;
      bool active = activeCommandButton == &commandButtons[row][col];
      
      CommandButton::State state = commandButtons[row][col].GetState(gameController.get());
      if (state == CommandButton::State::MaxLimitReached ||
          state == CommandButton::State::Researched ||
          state == CommandButton::State::Locked) {
        continue; // dont show
      }
      bool disabled = state != CommandButton::State::Valid;
      
      commandButtons[row][col].Render(
          buttonLeft,
          buttonTop,
          commandButtonSize,
          uiScale * 4,
          disabled ? *iconOverlayNormalExpensiveTexture :
              (pressed || active ? *iconOverlayActiveTexture :
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
  
  QPointF minimapPanelTopLeft = GetMinimapPanelTopLeft();
  if (minimapPanelOpaquenessMap.IsOpaque(factor * (x - minimapPanelTopLeft.x()), factor * (y - minimapPanelTopLeft.y()))) {
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
    if (grabMouse) UngrabMouse();
    setCursor(defaultCursor);
    menuButtonResign.SetEnabled(match->GetThisPlayer().state == Match::PlayerState::Playing);
  } else {
    if (grabMouse) GrabMouse();
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
      ClientBuilding& building = *AsBuilding(object.second);
      const ClientBuildingType& buildingType = buildingTypes[static_cast<int>(building.GetType())];
      
      int maxViewCount = map->ComputeMaxViewCountForBuilding(&building);
      if (maxViewCount < 0) {
        continue;
      }
      
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
      ClientUnit& unit = *AsUnit(object.second);
      if (map->IsUnitInFogOfWar(&unit)) {
        continue;
      }
      
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
      ClientUnit& unit = *AsUnit(object.second);
      
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
  selection.clear();
}

void RenderWindow::AddToSelection(u32 objectId) {
  selection.push_back(objectId);
}

void RenderWindow::AddToSelectionIfExists(u32 objectId) {
  auto objectIt = map->GetObjects().find(objectId);
  if (objectIt != map->GetObjects().end()) {
    selection.push_back(objectId);
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
  f->glClearColor(1.0f, 1.0f, 1.0f, 0.0f);
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
    
    QRgb shadowColor = ((qRed(playerColors[i]) + qGreen(playerColors[i]) + qBlue(playerColors[i])) / 3.f > 127) ? qRgb(0, 0, 0) : qRgb(255, 255, 255);
    for (int shadow = 0; shadow < 2; ++ shadow) {
      playerNames[i].textDisplay->Render(
          georgiaFont,
          (shadow == 0) ? shadowColor : playerColors[i],
          text,
          QRect((shadow == 0) ? (uiScale * 2) : 0,
                0.5f * widgetHeight - 0.5f * totalHeight + i * lineHeight + 0.5f * lineHeight + ((shadow == 0) ? (uiScale * 2) : 0),
                widgetWidth,
                lineHeight),
          Qt::AlignHCenter | Qt::AlignVCenter,
          (shadow == 0) ? playerNameShadowPointBuffers[i].buffer : playerNames[i].pointBuffer,
          uiShader.get(), widgetWidth, widgetHeight, f);
    }
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
  
  // Blend towards black before the game start.
  if (gameController->GetGameStartServerTimeSeconds() - connection->GetServerTimeToDisplayNow() < gameStartBlendToBlackTime) {
    float factor = std::max<float>(0, std::min<float>(1, 1.f - (gameController->GetGameStartServerTimeSeconds() - connection->GetServerTimeToDisplayNow()) / gameStartBlendToBlackTime));
    
    uiSingleColorFullscreenShader->GetProgram()->UseProgram(f);
    f->glUniform4f(uiSingleColorFullscreenShader->GetColorLocation(), 0, 0, 0, factor);
    
    f->glDrawArrays(GL_TRIANGLES, 0, 3);
    CHECK_OPENGL_NO_ERROR();
  }
  
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
      ClientUnit* unit = AsUnit(item.second);
      unit->UpdateGameState(displayedServerTime, map.get(), match.get());
    } else if (item.second->isBuilding()) {
      // TODO: Is this needed?
      // ClientBuilding* building = AsBuilding(item.second);
      // building->UpdateGameState(displayedServerTime);
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
  // TODO: Prevent "foundation scanning" in the semi-black fog of war.
  QRect foundationRect(foundationBaseTile, foundationSize);
  for (const auto& item : map->GetObjects()) {
    if (item.second->isBuilding()) {
      ClientBuilding* building = AsBuilding(item.second);
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
  
  // 2) Check whether the maximum elevation difference within the building space does not exceed 2,
  //    and whether any building tile is in the black fog of war.
  //    TODO: I made the first part of this criterion up; is that actually how the original game works?
  //    TODO: This part of the criterion must not apply to farms.
  int minElevation = std::numeric_limits<int>::max();
  int maxElevation = std::numeric_limits<int>::min();
  int minViewCount = std::numeric_limits<int>::max();
  for (int y = foundationBaseTile.y(); y < foundationBaseTile.y() + foundationSize.height(); ++ y) {
    for (int x = foundationBaseTile.x(); x < foundationBaseTile.x() + foundationSize.width(); ++ x) {
      int elevation = map->elevationAt(x, y);
      minElevation = std::min(minElevation, elevation);
      maxElevation = std::max(maxElevation, elevation);
      minViewCount = std::min(minViewCount, map->viewCountAt(x, y));
    }
  }
  
  if (maxElevation - minElevation > 2 ||
      minViewCount < 0) {
    return false;
  }
  
  return true;
}

void RenderWindow::PressCommandButton(CommandButton* button, bool shift) {

  CommandButton::State state = button->GetState(gameController.get());
  if (state != CommandButton::State::Valid) {
    
    ReportNonValidCommandButton(button, state);
    return;
  }
  // The state of the command button is Valid.
  
  // TODO: merge code from RenderWindow::PressCommandButton and CommandButton::Pressed
  button->Pressed(selection, gameController.get(), shift);
  
  // Handle building construction.
  if (button->GetType() == CommandButton::Type::ConstructBuilding) {
    activeCommandButton = button;
    constructBuildingType = button->GetBuildingConstructionType();
  }
  
  // "Action" buttons are handled here.
  if (button->GetType() == CommandButton::Type::Action) {

    if (constructBuildingType != BuildingType::NumBuildings) {
      // exit construction mode
      activeCommandButton = nullptr;
      constructBuildingType = BuildingType::NumBuildings;
    }

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
      break;
    }
  }
}

void RenderWindow::ReportNonValidCommandButton(CommandButton* button, CommandButton::State state) {

  if (state == CommandButton::State::CannotAfford) {
    QString name;
    ResourceAmount cost;
    if (button->GetType() == CommandButton::Type::ConstructBuilding) {
      name = GetBuildingName(button->GetBuildingConstructionType());
      cost = GetBuildingCost(button->GetBuildingConstructionType());
    } else if (button->GetType() == CommandButton::Type::ProduceUnit) {
      name = GetUnitName(button->GetUnitProductionType());
      cost = GetUnitCost(button->GetUnitProductionType());
    } else {
      assert(false); // TODO: implement all CommandButton::Type which can have State::CannotAfford
    }

    // TODO: move from the log to the gui message system
    LOG(INFO) << name.toStdString() << " is not affordable, missing: ";
    // TODO: extract to function 
    for (int i = 0; i < static_cast<int>(ResourceType::NumTypes); ++ i) {
      u32 available = gameController->GetLatestKnownResourceAmount().resources[i];
      u32 needed = cost.resources[i];
      if (available < needed) {
        LOG(INFO) << (needed - available) << " " << GetResourceName(static_cast<ResourceType>(i)).toStdString();
      }
    }
  } else if (state == CommandButton::State::MaxLimitReached) {
    QString name;
    if (button->GetType() == CommandButton::Type::ConstructBuilding) {
      name = GetBuildingName(button->GetBuildingConstructionType());
    } else {
      assert(false); // TODO: implement all CommandButton::Type which can have State::MaxLimitReached
    }

    // TODO: move from the log to the gui message system
    LOG(INFO) << "Max limit reached for " << name.toStdString();
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
      ClientBuilding* building = AsBuilding(object);
      
      if (building->GetPlayerIndex() == match->GetPlayerIndex()) {
        if (building->IsCompleted()) {
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
      ClientUnit* unit = AsUnit(object);
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

  commandButtons[2][0].SetBuilding(BuildingType::TownCenter, Qt::Key_Z);
  
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

void RenderWindow::DequeueProductionQueueItem(int queueIndex) {
  if (selection.size() != 1) {
    LOG(ERROR) << "Can only dequeue production queue items if only a single building is selected.";
    return;
  }
  
  int queueIndexFromBack = -1;
  for (int index = kMaxProductionQueueSize - 1; index >= 0; -- index) {
    if (productionQueueIconsSize[index] >= 0) {
      queueIndexFromBack = index - queueIndex;
      break;
    }
  }
  if (queueIndexFromBack < 0) {
    LOG(ERROR) << "Failed to determine the queue index from the back.";
    return;
  }
  
  connection->Write(CreateDequeueProductionQueueItemMessage(selection.front(), queueIndexFromBack));
}

void RenderWindow::JumpToNextTownCenter() {
  std::vector<std::pair<u32, ClientBuilding*>> townCenters;
  
  for (const auto& item : map->GetObjects()) {
    if (item.second->GetPlayerIndex() == match->GetPlayerIndex() &&
        item.second->isBuilding()) {
      ClientBuilding* building = AsBuilding(item.second);
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
    ClientBuilding* building = AsBuilding(object);
    scroll = building->GetCenterMapCoord();
    scrollProjectedCoordOffset = QPointF(0, 0);
    UpdateViewMatrix();
    UpdateCursor();
  } else if (object->isUnit()) {
    ClientUnit* unit = AsUnit(object);
    scroll = unit->GetMapCoord();
    scrollProjectedCoordOffset = QPointF(0, 0);
    UpdateViewMatrix();
    UpdateCursor();
  }
}

void RenderWindow::JumpToSelectedObjects() {
  QPointF projectedCoordSum(0, 0);
  int count = 0;
  
  for (u32 id : selection) {
    auto it = map->GetObjects().find(id);
    if (it != map->GetObjects().end()) {
      if (it->second->isBuilding()) {
        ClientBuilding* building = AsBuilding(it->second);
        projectedCoordSum += map->MapCoordToProjectedCoord(building->GetCenterMapCoord());
        ++ count;
      } else if (it->second->isUnit()) {
        ClientUnit* unit = AsUnit(it->second);
        projectedCoordSum += map->MapCoordToProjectedCoord(unit->GetMapCoord());
        ++ count;
      }
    }
  }
  
  if (count > 0) {
    QPointF projectedCoord = projectedCoordSum / count;
    
    map->ProjectedCoordToMapCoord(projectedCoord, &scroll);
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

void RenderWindow::DefineControlGroup(int controlGroupIndex) {
  controlGroups[controlGroupIndex] = selection;
}

void RenderWindow::SelectControlGroup(int controlGroupIndex) {
  ClearSelection();
  for (u32 id : controlGroups[controlGroupIndex]) {
    AddToSelectionIfExists(id);
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
  
  // Define the player colors.
  CreatePlayerColorPaletteTexture();
  
  // Create the resource loading thread.
  loadingThread = new LoadingThread(this, loadingContext, loadingSurface);
  loadingContext->moveToThread(loadingThread);
  connect(loadingThread, &LoadingThread::finished, this, &RenderWindow::LoadingFinished);
  
  isLoading = true;
  loadingStep = 0;
  maxLoadingStep = 62;
  loadingThread->start();
  
  // Create resources right now which are required for rendering the loading screen:
  
  // Load the UI shaders.
  uiShader.reset(new UIShader());
  uiSingleColorShader.reset(new UISingleColorShader());
  uiSingleColorFullscreenShader.reset(new UISingleColorFullscreenShader());
  minimapShader.reset(new MinimapShader());
  
  // Load the loading icon.
  loadingIcon.Load(GetModdedPath(graphicsSubPath.parent_path().parent_path() / "wpfg" / "resources" / "campaign" / "campaign_icon_2swords.png"));
  
  // Create the loading text display.
  playerNames.resize(match->GetPlayers().size());
  playerNameShadowPointBuffers.resize(playerNames.size());
  for (usize i = 0; i < playerNames.size(); ++ i) {
    playerNames[i].Initialize();
    playerNameShadowPointBuffers[i].Initialize();
  }
  
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
    
    // Remove any objects that have been deleted from the selection or
    // that are fully within the fog of war.
    usize outputIndex = 0;
    for (usize i = 0; i < selection.size(); ++ i) {
      auto it = map->GetObjects().find(selection[i]);
      
      // Drop if the object does not exist anymore.
      if (it == map->GetObjects().end()) {
        continue;
      }
      
      // Drop if it is an enemy object that is fully in the fog of war.
      if (it->second->GetPlayerIndex() != match->GetPlayerIndex()) {
        if (it->second->isUnit() && map->IsUnitInFogOfWar(AsUnit(it->second))) {
          continue;
        } else if (it->second->isBuilding() && map->IsBuildingInFogOfWar(AsBuilding(it->second))) {
          continue;
        }
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
  
  // Center the view on the selection if the space key is held.
  if (spaceHeld) {
    JumpToSelectedObjects();
  }
  
  gameStateUpdateTimer.Stop();
  Timer initialStatesAndClearTimer("paintGL() - initial state setting & clear");
  
  // Update scrolling and compute the view transformation.
  UpdateView(now, f);
  CHECK_OPENGL_NO_ERROR();
  
  // Set states for rendering.
  f->glDisable(GL_CULL_FACE);
  
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
  
  // Render the map terrain and ground decals.
  f->glBlendFuncSeparate(
      GL_ONE_MINUS_DST_ALPHA, GL_ZERO,  // blend with the shadows
      GL_ZERO, GL_ONE);  // keep the existing alpha (such that more objects can be rendered while applying the shadows)
  f->glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);  // reset to default
  
  CHECK_OPENGL_NO_ERROR();
  map->Render(viewMatrix, graphicsSubPath, f);
  mapTimer.Stop();
  
  Timer groundDecalTimer("paintGL() - ground decal rendering");
  // Set texture 1 for the sprite shader, which will remain there for the rest of the frame
  f->glActiveTexture(GL_TEXTURE0 + 1);
  f->glBindTexture(GL_TEXTURE_2D, playerColorsTexture->GetId());
  f->glActiveTexture(GL_TEXTURE0);
  
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
    
    RenderPath(1.1f, qRgba(0, 0, 0, 255), vertices, QPointF(2, 2), /*closed*/ true, f);
    RenderPath(1.1f, qRgba(255, 255, 255, 255), vertices, QPointF(0, 0), /*closed*/ true, f);
  }
  
  selectionBoxTimer.Stop();
  Timer uiTimer("paintGL() - UI rendering");
  
  // Render game UI.
  f->glEnable(GL_BLEND);
  
  // TODO: Would it be faster to render this at the start and then prevent rendering over the UI pixels,
  //       for example by setting the z-buffer such that no further pixel will be rendered there?
  CHECK_OPENGL_NO_ERROR();
  RenderGameUI(displayedServerTime, secondsSinceLastFrame, f);
  CHECK_OPENGL_NO_ERROR();
  
  uiTimer.Stop();
  
  // Blend from black after the game start.
  if (connection->GetServerTimeToDisplayNow() - gameController->GetGameStartServerTimeSeconds() < gameStartBlendToBlackTime) {
    f->glEnable(GL_BLEND);
    f->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    float factor = std::max<float>(0, std::min<float>(1, 1.f - (connection->GetServerTimeToDisplayNow() - gameController->GetGameStartServerTimeSeconds()) / gameStartBlendToBlackTime));
    
    uiSingleColorFullscreenShader->GetProgram()->UseProgram(f);
    f->glUniform4f(uiSingleColorFullscreenShader->GetColorLocation(), 0, 0, 0, factor);
    
    f->glDrawArrays(GL_TRIANGLES, 0, 3);
    CHECK_OPENGL_NO_ERROR();
  }
  
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
  
  // Handle clicks on the minimap.
  if (isUIClick) {
    float mapCoordX, mapCoordY;
    if (minimap->ScreenToMapCoord(event->pos().x(), event->pos().y(), GetMinimapPanelTopLeft(), uiScale, map.get(), &mapCoordX, &mapCoordY)) {
      if (event->button() == Qt::LeftButton) {
        scroll = QPointF(mapCoordX, mapCoordY);
        scrollProjectedCoordOffset = QPointF(0, 0);
        UpdateViewMatrix();
      } else if (event->button() == Qt::RightButton) {
        RightClickOnMap(false, QPoint(-1, -1), true, QPointF(mapCoordX, mapCoordY));
      }
      return;
    }
  }
  
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
    
    // Has a production queue item been pressed?
    for (int i = 0; i < kMaxProductionQueueSize; ++ i) {
      if (productionQueueIconsSize[i] <= 0) {
        continue;
      }
      if (event->pos().x() >= productionQueueIconsTopLeft[i].x() &&
          event->pos().y() >= productionQueueIconsTopLeft[i].y() &&
          event->pos().x() < productionQueueIconsTopLeft[i].x() + productionQueueIconsSize[i]  &&
          event->pos().y() < productionQueueIconsTopLeft[i].y() + productionQueueIconsSize[i]) {
        pressedProductionQueueItem = i;
        return;
      }
    }
    
    if (isUIClick) {
      menuButton.MousePress(event->pos());
      return;
    }
    
    // Place a building foundation?
    if (match->IsPlayerStillInGame() && constructBuildingType != BuildingType::NumBuildings) {
      ignoreLeftMouseRelease = true;
      
      CommandButton::State state = activeCommandButton->GetState(gameController.get());
      if (state != CommandButton::State::Valid) {
        ReportNonValidCommandButton(activeCommandButton, state);
      
        if (state == CommandButton::State::CannotAfford) {
          // remain in construction mode, player may want to wait until the construction is affordable
        } else {
          // exit construction mode 
          activeCommandButton = nullptr;
          constructBuildingType = BuildingType::NumBuildings;
        }
        return;
      }

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
              ClientUnit* unit = AsUnit(it->second);
              if (IsVillager(unit->GetType())) {
                selectedVillagerIds.push_back(it->first);
              }
            }
          }
        }
        
        connection->Write(CreatePlaceBuildingFoundationMessage(constructBuildingType, foundationBaseTile, selectedVillagerIds));
        
        bool shift = event->modifiers() & Qt::ShiftModifier;
        if (!shift) {
          // un-set constructBuildingType
          activeCommandButton = nullptr;
          constructBuildingType = BuildingType::NumBuildings;
        }
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

    if (constructBuildingType != BuildingType::NumBuildings) {
      // exit construction mode
      activeCommandButton = nullptr;
      constructBuildingType = BuildingType::NumBuildings;
      // return in order to not process the right click as a unit command
      return;
    }

    QPointF projectedCoord = ScreenCoordToProjectedCoord(event->x(), event->y());
    QPointF mapCoord;
    bool haveMapCoord = map->ProjectedCoordToMapCoord(projectedCoord, &mapCoord);
    
    RightClickOnMap(true, event->pos(), haveMapCoord, mapCoord);
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
    QMetaObject::invokeMethod(this, "HandleMouseMoveEvent", Qt::QueuedConnection);
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
  
  // Dragging on the minimap (and not on the actual map)?
  if (!dragging && (lastMouseMoveEventButtons & Qt::LeftButton)) {
    float mapCoordX, mapCoordY;
    if (minimap->ScreenToMapCoord(lastCursorPos.x(), lastCursorPos.y(), GetMinimapPanelTopLeft(), uiScale, map.get(), &mapCoordX, &mapCoordY)) {
      scroll = QPointF(mapCoordX, mapCoordY);
      scrollProjectedCoordOffset = QPointF(0, 0);
      UpdateViewMatrix();
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
  
  // Same for production queue icons.
  if (pressedProductionQueueItem >= 0 &&
      !(lastMouseMoveEventPos.x() >= productionQueueIconsTopLeft[pressedProductionQueueItem].x() &&
        lastMouseMoveEventPos.y() >= productionQueueIconsTopLeft[pressedProductionQueueItem].y() &&
        lastMouseMoveEventPos.x() < productionQueueIconsTopLeft[pressedProductionQueueItem].x() + productionQueueIconsSize[pressedProductionQueueItem]  &&
        lastMouseMoveEventPos.y() < productionQueueIconsTopLeft[pressedProductionQueueItem].y() + productionQueueIconsSize[pressedProductionQueueItem])) {
    pressedProductionQueueItem = -1;
  }
  
  // If hovering over the game area, possibly change the cursor to indicate possible interactions.
  UpdateCursor();
}

void RenderWindow::RightClickOnMap(bool haveScreenCoord, const QPoint& screenCoord, bool haveMapCoord, const QPointF& mapCoord) {
  if (!match->IsPlayerStillInGame()) {
    return;
  }
  
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
    if (haveScreenCoord) {
      u32 targetObjectId;
      if (GetObjectToSelectAt(screenCoord.x(), screenCoord.y(), &targetObjectId, &selection, false, true)) {
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
    }
    
    // Send the remaining selected units to the clicked map coordinate.
    if (haveMapCoord) {
      moveToMapCoord = mapCoord;
      
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

void RenderWindow::UpdateCursor() {
  QCursor* cursor = &defaultCursor;
  if (!IsUIAt(lastMouseMoveEventPos.x(), lastMouseMoveEventPos.y())) {
    u32 targetObjectId;
    if (GetObjectToSelectAt(lastMouseMoveEventPos.x(), lastMouseMoveEventPos.y(), &targetObjectId, &selection, false, true)) {
      ClientObject* targetObject = map->GetObjects().at(targetObjectId);
      for (u32 id : selection) {
        auto it = map->GetObjects().find(id);
        if (it != map->GetObjects().end()) {
          if (it->second->GetPlayerIndex() != match->GetPlayerIndex()) {
            continue;
          }
          
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

      bool shift = event->modifiers() & Qt::ShiftModifier;
      PressCommandButton(&commandButtons[pressedCommandButtonRow][pressedCommandButtonCol], shift);
      
      pressedCommandButtonRow = -1;
      pressedCommandButtonCol = -1;
      return;
    }
    
    if (pressedProductionQueueItem >= 0 &&
        match->IsPlayerStillInGame()) {
      DequeueProductionQueueItem(pressedProductionQueueItem);
      pressedProductionQueueItem = -1;
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
  if (!map->ProjectedCoordToMapCoord(newScreenCenterProjectedCoords, &scroll) && smoothZooming) {
    QPointF actualNewScreenCenterProjectedCoords = map->MapCoordToProjectedCoord(scroll);
    scrollProjectedCoordOffset = ScreenCoordToProjectedCoord(0.5f * widgetWidth, 0.5f * widgetHeight) - actualNewScreenCenterProjectedCoords;
    requiredProjectedCoordDifference = QPointF(0, 0);
  }
  
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
    if (!menuShown &&
        constructBuildingType != BuildingType::NumBuildings) {
      ShowDefaultCommandButtonsForSelection();
      activeCommandButton = nullptr;
      constructBuildingType = BuildingType::NumBuildings;
      return;
    }
    
    ShowMenu(!menuShown);
    return;
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
  } else if (event->key() == Qt::Key_Space) {
    spaceHeld = true;
  } else if (event->key() >= Qt::Key_0 && event->key() <= Qt::Key_9) {
    int controlGroupIndex = static_cast<int>(event->key()) - static_cast<int>(Qt::Key_0);
    if (event->modifiers() & Qt::ControlModifier) {
      DefineControlGroup(controlGroupIndex);
    } else {
      SelectControlGroup(controlGroupIndex);
    }
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
  } else if (event->key() == Qt::Key_Space) {
    spaceHeld = false;
    JumpToSelectedObjects();
  } else if (match->IsPlayerStillInGame()) {
    // Check whether a hotkey for a command button was released.
    for (int row = 0; row < kCommandButtonRows; ++ row) {
      for (int col = 0; col < kCommandButtonCols; ++ col) {
        if (commandButtons[row][col].GetHotkey() != Qt::Key_unknown &&
            commandButtons[row][col].GetHotkey() == event->key()) {
          PressCommandButton(&commandButtons[row][col], event->modifiers() & Qt::ShiftModifier);
          
          pressedCommandButtonRow = -1;
          pressedCommandButtonCol = -1;
          commandButtonPressedByHotkey = false;
          return;
        }
      }
    }
  }
}

void RenderWindow::focusInEvent(QFocusEvent* /*event*/) {
  if (grabMouse) {
    GrabMouse();
  }
}

void RenderWindow::focusOutEvent(QFocusEvent* /*event*/) {
  if (grabMouse) {
    UngrabMouse();
  }
}

void RenderWindow::resizeEvent(QResizeEvent* event) {
  QOpenGLWindow::resizeEvent(event);
  if (grabMouse) {
    GrabMouse();
  }
}

#include "render_window.moc"
