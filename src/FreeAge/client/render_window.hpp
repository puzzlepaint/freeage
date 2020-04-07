// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <chrono>
#include <memory>

#include <QOffscreenSurface>
#include <QOpenGLWindow>

#include "FreeAge/common/free_age.hpp"
#include "FreeAge/client/command_button.hpp"
#include "FreeAge/client/decal.hpp"
#include "FreeAge/client/map.hpp"
#include "FreeAge/client/match.hpp"
#include "FreeAge/client/opaqueness_map.hpp"
#include "FreeAge/client/shader_health_bar.hpp"
#include "FreeAge/client/shader_sprite.hpp"
#include "FreeAge/client/shader_ui.hpp"
#include "FreeAge/client/shader_ui_single_color.hpp"
#include "FreeAge/client/server_connection.hpp"
#include "FreeAge/client/sprite.hpp"
#include "FreeAge/client/text_display.hpp"
#include "FreeAge/client/texture.hpp"
#include "FreeAge/client/unit.hpp"

class GameController;
class LoadingThread;

class RenderWindow : public QOpenGLWindow {
 Q_OBJECT
 public:
  RenderWindow(
      const std::shared_ptr<Match>& match,
      const std::shared_ptr<GameController>& gameController,
      const std::shared_ptr<ServerConnection>& connection,
      float uiScale,
      int georgiaFontID,
      const Palettes& palettes,
      const std::filesystem::path& graphicsSubPath,
      const std::filesystem::path& cachePath,
      QWindow* parent = nullptr);
  ~RenderWindow();
  
  /// Called from the loading thread to load the game resources in the background.
  void LoadResources();
  
  /// Scrolls the given map coordinates by the given amount in projected coordinates.
  void Scroll(float x, float y, QPointF* mapCoord);
  
  /// Computes the current scroll, taking into account the currently pressed keys.
  QPointF GetCurrentScroll(const TimePoint& atTime, bool* scrollApplied);
  
  inline void SetScroll(const QPointF& value) { scroll = value; }
  
  inline void SetMap(const std::shared_ptr<Map>& map) { this->map = map; map->SetNeedsRenderResourcesUpdate(true); }
  
  inline void SetGameController(const std::shared_ptr<GameController>& gameController) { this->gameController = gameController; }
  
  inline void EnableBorderScrolling(bool enable) { borderScrollingEnabled = enable; }
  
  void AddDecal(Decal* decal);
  
 signals:
  void LoadingProgressUpdated(int progress);
  
 private slots:
  void SendLoadingProgress(int progress);
  void LoadingFinished();
  
  void HandleMouseMoveEvent();
  void UpdateCursor();
  
 protected:
  struct TextureAndPointBuffer {
    ~TextureAndPointBuffer();
    
    bool Load(const std::filesystem::path& path, QImage* qimage = nullptr, TextureManager::Loader loader = TextureManager::Loader::QImage);
    void Unload();
    
    GLuint pointBuffer;
    std::shared_ptr<Texture> texture;
  };
  
  struct TextDisplayAndPointBuffer {
    ~TextDisplayAndPointBuffer();
    
    void Initialize();
    void Destroy();
    
    GLuint pointBuffer;
    std::shared_ptr<TextDisplay> textDisplay;
  };
  
  struct PointBuffer {
    ~PointBuffer();
    
    void Initialize();
    void Destroy();
    
    GLuint buffer;
    bool initialized = false;
  };
  
  struct Button {
    void Load(const std::filesystem::path& defaultSubPath, const std::filesystem::path& hoverSubPath, const std::filesystem::path& activeSubPath, const std::filesystem::path& disabledSubPath);
    void Render(float x, float y, float width, float height, RenderWindow* renderWindow, QOpenGLFunctions_3_2_Core* f);
    void MouseMove(const QPoint& pos);
    void MousePress(const QPoint& pos);
    /// Returns true if the button was clicked.
    bool MouseRelease(const QPoint& pos);
    void SetEnabled(bool enabled);
    bool IsInButton(const QPoint& pos);
    void Destroy();
    
    PointBuffer pointBuffer;
    OpaquenessMap opaquenessMap;
    std::shared_ptr<Texture> defaultTexture;
    std::shared_ptr<Texture> hoverTexture;
    std::shared_ptr<Texture> activeTexture;
    std::shared_ptr<Texture> disabledTexture;
    
    float lastX = -1;
    float lastY = -1;
    float lastWidth = -1;
    float lastHeight = -1;
    
    int state = 0;  // 0: default, 1: hover, 2: active, 3: disabled
  };
  
  void CreatePlayerColorPaletteTexture();
  usize PrepareBufferObject(usize size, QOpenGLFunctions_3_2_Core* f);
  
  void ComputePixelToOpenGLMatrix(QOpenGLFunctions_3_2_Core* f);
  void UpdateViewMatrix();
  float ComputeEffectiveZoom();
  void UpdateView(const TimePoint& now, QOpenGLFunctions_3_2_Core* f);
  /// Renders a closed path consisting of line segments between the given vertices.
  /// The last vertex will be connected to the first. The vertex coordinates are in screen (pixel) coordinates.
  /// The given offset is applied to each vertex.
  void RenderClosedPath(float halfLineWidth, const QRgb& color, const std::vector<QPointF>& vertices, const QPointF& offset, QOpenGLFunctions_3_2_Core* f);
  void RenderSprites(std::vector<Texture*>* textures, const std::shared_ptr<SpriteShader>& shader, QOpenGLFunctions_3_2_Core* f);
  void RenderShadows(double displayedServerTime, QOpenGLFunctions_3_2_Core* f);
  void RenderBuildings(double displayedServerTime, bool buildingsThatCauseOutlines, QOpenGLFunctions_3_2_Core* f);
  void RenderBuildingFoundation(double displayedServerTime, QOpenGLFunctions_3_2_Core* f);
  void RenderSelectionGroundOutlines(QOpenGLFunctions_3_2_Core* f);
  void RenderSelectionGroundOutline(QRgb color, ClientObject* object, QOpenGLFunctions_3_2_Core* f);
  void RenderOutlines(double displayedServerTime, QOpenGLFunctions_3_2_Core* f);
  void RenderUnits(double displayedServerTime, QOpenGLFunctions_3_2_Core* f);
  void RenderMoveToMarker(const TimePoint& now, QOpenGLFunctions_3_2_Core* f);
  void RenderHealthBars(double displayedServerTime, QOpenGLFunctions_3_2_Core* f);
  void RenderGroundDecals(QOpenGLFunctions_3_2_Core* f);
  void RenderOccludingDecals(QOpenGLFunctions_3_2_Core* f);
  void RenderDecals(std::vector<Decal*>& decals, QOpenGLFunctions_3_2_Core* f);
  void RenderOccludingDecalShadows(QOpenGLFunctions_3_2_Core* f);
  void RenderOccludingDecalOutlines(QOpenGLFunctions_3_2_Core* f);
  
  
  void RenderGameUI(double displayedServerTime, QOpenGLFunctions_3_2_Core* f);
  QPointF GetMenuPanelTopLeft();
  void RenderMenuPanel(QOpenGLFunctions_3_2_Core* f);
  QPointF GetResourcePanelTopLeft();
  void RenderResourcePanel(QOpenGLFunctions_3_2_Core* f);
  QPointF GetSelectionPanelTopLeft();
  void RenderObjectIcon(const Texture* iconTexture, float x, float y, float size, GLuint iconPointBuffer, GLuint overlayPointBuffer, QOpenGLFunctions_3_2_Core* f);
  void RenderSelectionPanel(QOpenGLFunctions_3_2_Core* f);
  QPointF GetCommandPanelTopLeft();
  void RenderCommandPanel(QOpenGLFunctions_3_2_Core* f);
  void RenderMenu(QOpenGLFunctions_3_2_Core* f);
  bool IsUIAt(int x, int y);
  
  void ShowMenu(bool show);
  
  /// Given screen-space coordinates (x, y), finds the (next) object to select at
  /// this point. If an object is found, true is returned, and the object's ID is
  /// returned in objectId.
  ///
  /// If toggleThroughObjects is true, then repeated selections at the same location
  /// will toggle through all objects at this location.
  ///
  /// If selectSuitableTargetsOnly is true, then this function considers to return
  /// only objects that at least one of the currently selected objects could interact
  /// with.
  bool GetObjectToSelectAt(float x, float y, u32* objectId, std::vector<u32>* currentSelection, bool toggleThroughObjects, bool selectSuitableTargetsOnly);
  
  QPointF ScreenCoordToProjectedCoord(float x, float y);
  inline QPointF ScreenCoordToProjectedCoord(const QPointF& input) { return ScreenCoordToProjectedCoord(input.x(), input.y()); }
  QPointF ProjectedCoordToScreenCoord(float x, float y);
  inline QPointF ProjectedCoordToScreenCoord(const QPointF& input) { return ProjectedCoordToScreenCoord(input.x(), input.y()); }
  
  void ClearSelection();
  void AddToSelection(u32 objectId);
  void AddToSelectionIfExists(u32 objectId);
  /// This must be called after making changes to the selection.
  void SelectionChanged();
  /// Performs box selection. This already calls SelectionChanged() internally.
  void BoxSelection(const QPoint& p0, const QPoint& p1);
  
  void LetObjectFlash(u32 objectId);
  bool IsObjectFlashActive();
  
  void RenderLoadingScreen(QOpenGLFunctions_3_2_Core* f);
  
  /// Updates the game state to the given server time for which the state should be rendered.
  void UpdateGameState(double displayedServerTime);
  
  /// Checks whether a building foundation of the given type can be placed at the given
  /// cursor position in screen coordinates. If yes, true is returned and the base tile
  /// coordinates of the building are returned in baseTile.
  bool CanBuildingFoundationBePlacedHere(BuildingType type, const QPointF& cursorPos, QPoint* baseTile);
  
  void PressCommandButton(CommandButton* button);
  
  void ShowDefaultCommandButtonsForSelection();
  void ShowEconomyBuildingCommandButtons();
  void ShowMilitaryBuildingCommandButtons();
  
  void JumpToNextTownCenter();
  /// Selects the object and centers the view on it.
  void JumpToObject(u32 objectId, ClientObject* object);
  void JumpToSelectedObjects();
  
  void DeleteSelectedObjects();
  
  void DefineControlGroup(int controlGroupIndex);
  void SelectControlGroup(int controlGroupIndex);
  
  virtual void initializeGL() override;
  virtual void paintGL() override;
  virtual void resizeGL(int width, int height) override;
  virtual void mousePressEvent(QMouseEvent* event) override;
  virtual void mouseMoveEvent(QMouseEvent* event) override;
  virtual void mouseReleaseEvent(QMouseEvent* event) override;
  virtual void wheelEvent(QWheelEvent* event) override;
  virtual void keyPressEvent(QKeyEvent* event) override;
  virtual void keyReleaseEvent(QKeyEvent* event) override;
  
  
  /// Cursor handling.
  bool haveMouseMoveEvent = false;
  QPoint lastMouseMoveEventPos;
  Qt::MouseButtons lastMouseMoveEventButtons;
  
  QPoint lastCursorPos = QPoint(-1, -1);
  
  QPoint dragStartPos = QPoint(0, 0);
  bool possibleDragStart = false;
  bool dragging = false;
  
  bool ignoreLeftMouseRelease = false;
  
  QCursor defaultCursor;
  QCursor attackCursor;
  QCursor buildCursor;
  QCursor chopCursor;
  QCursor gatherCursor;
  QCursor mineGoldCursor;
  QCursor mineStoneCursor;
  
  /// Current map scroll position in map coordinates.
  /// The "scroll" map coordinate is visible at the center of the screen.
  QPointF scroll;
  
  /// An offset on the scroll position, in projected coordinates.
  /// This is used to implement smooth zooming.
  /// The offset step is gradually reduced to zero.
  QPointF scrollProjectedCoordOffset = QPointF(0, 0);
  
  bool scrollRightPressed = false;
  TimePoint scrollRightPressTime;
  
  bool scrollLeftPressed = false;
  TimePoint scrollLeftPressTime;
  
  bool scrollUpPressed = false;
  TimePoint scrollUpPressTime;
  
  bool scrollDownPressed = false;
  TimePoint scrollDownPressTime;
  
  bool borderScrollingEnabled = false;
  TimePoint lastScrollGetTime;
  
  static constexpr const float scrollDistancePerSecond = 2000;  // TODO: Make configurable
  
  /// Map data.
  std::shared_ptr<Map> map;
  
  /// IDs of selected objects (units or buildings).
  std::vector<u32> selection;
  
  /// Decals on the ground.
  std::vector<Decal*> groundDecals;
  
  /// Decals that may occlude other sprites.
  std::vector<Decal*> occludingDecals;
  
  /// Current zoom factor. The default zoom is one, two would make everything twice as big, etc.
  float zoom;
  
  /// An offset on the zoom factor step. This is used to implement smooth zooming.
  /// The offset step is gradually reduced to zero.
  float remainingZoomStepOffset = 0;
  
  /// Whether to use smooth zooming or step-wise zooming. TODO: Make configurable.
  bool smoothZooming = true;
  
  /// Game start time.
  TimePoint renderStartTime;
  
  bool haveLastFrameTime = false;
  TimePoint lastFrameTime;
  
  /// The server time displayed in the last rendering iteration.
  /// Do not set this to something negative as this might make the code try to access negative frame numbers of animations.
  double lastDisplayedServerTime = 0;
  
  /// Cached widget size.
  int widgetWidth;
  int widgetHeight;
  
  float viewMatrix[4];  // column-major
  QRectF projectedCoordsViewRect;
  
  // Shaders.
  std::shared_ptr<ColorDilationShader> colorDilationShader;
  std::shared_ptr<UIShader> uiShader;
  std::shared_ptr<UISingleColorShader> uiSingleColorShader;
  std::shared_ptr<SpriteShader> spriteShader;
  std::shared_ptr<SpriteShader> shadowShader;
  std::shared_ptr<SpriteShader> outlineShader;
  std::shared_ptr<HealthBarShader> healthBarShader;
  
  // FPS computation.
  TimePoint fpsMeasuringFrameStartTime;
  int framesAfterFPSMeasuringStartTime = -1;
  int roundedFPS = -1;
  
  // Loading thread.
  QOffscreenSurface* loadingSurface;
  LoadingThread* loadingThread;
  
  std::atomic<int> loadingStep;
  int maxLoadingStep;
  
  // Loading screen.
  bool isLoading;
  
  TextureAndPointBuffer loadingIcon;
  std::vector<TextDisplayAndPointBuffer> playerNames;
  std::vector<PointBuffer> playerNameShadowPointBuffers;
  
  // Menu.
  bool menuShown = false;
  TextureAndPointBuffer menuDialog;
  TextDisplayAndPointBuffer menuTextDisplay;
  Button menuButtonExit;
  TextDisplayAndPointBuffer menuButtonExitText;
  Button menuButtonResign;
  TextDisplayAndPointBuffer menuButtonResignText;
  Button menuButtonCancel;
  TextDisplayAndPointBuffer menuButtonCancelText;
  
  // Game UI.
  float uiScale;
  
  TextDisplayAndPointBuffer gameEndTextDisplay;
  PointBuffer gameEndTextDisplayShadowPointBuffer;
  
  TextureAndPointBuffer menuPanel;
  OpaquenessMap menuPanelOpaquenessMap;
  Button menuButton;
  PointBuffer objectivesButtonPointBuffer;
  std::shared_ptr<Texture> objectivesButtonDisabledTexture;
  PointBuffer chatButtonPointBuffer;
  std::shared_ptr<Texture> chatButtonDisabledTexture;
  PointBuffer diplomacyButtonPointBuffer;
  std::shared_ptr<Texture> diplomacyButtonDisabledTexture;
  PointBuffer settingsButtonPointBuffer;
  std::shared_ptr<Texture> settingsButtonDisabledTexture;
  
  TextureAndPointBuffer resourcePanel;
  OpaquenessMap resourcePanelOpaquenessMap;
  TextureAndPointBuffer resourceWood;
  TextDisplayAndPointBuffer woodTextDisplay;
  TextureAndPointBuffer resourceFood;
  TextDisplayAndPointBuffer foodTextDisplay;
  TextureAndPointBuffer resourceGold;
  TextDisplayAndPointBuffer goldTextDisplay;
  TextureAndPointBuffer resourceStone;
  TextDisplayAndPointBuffer stoneTextDisplay;
  TextureAndPointBuffer pop;
  TextDisplayAndPointBuffer popTextDisplay;
  double housedStartTime = -1;
  PointBuffer popWhiteBackgroundPointBuffer;
  TextureAndPointBuffer idleVillagerDisabled;
  TextureAndPointBuffer currentAgeShield;
  TextDisplayAndPointBuffer currentAgeTextDisplay;
  
  TextDisplayAndPointBuffer gameTimeDisplay;
  PointBuffer gameTimeDisplayShadowPointBuffer;
  TextDisplayAndPointBuffer fpsAndPingDisplay;
  PointBuffer fpsAndPingDisplayShadowPointBuffer;
  
  TextureAndPointBuffer commandPanel;
  OpaquenessMap commandPanelOpaquenessMap;
  TextureAndPointBuffer buildEconomyBuildings;
  TextureAndPointBuffer buildMilitaryBuildings;
  TextureAndPointBuffer toggleBuildingsCategory;
  TextureAndPointBuffer quit;
  
  TextureAndPointBuffer selectionPanel;
  OpaquenessMap selectionPanelOpaquenessMap;
  TextDisplayAndPointBuffer singleObjectNameDisplay;
  TextDisplayAndPointBuffer hpDisplay;
  TextDisplayAndPointBuffer carriedResourcesDisplay;
  PointBuffer selectionPanelIconPointBuffer;
  PointBuffer selectionPanelIconOverlayPointBuffer;
  PointBuffer productionQueuePointBuffers[kMaxProductionQueueSize];
  PointBuffer productionQueueOverlayPointBuffers[kMaxProductionQueueSize];
  TextDisplayAndPointBuffer productionProgressText;
  TextureAndPointBuffer productionProgressBar;
  PointBuffer productionProgressBarBackgroundPointBuffer;
  
  std::shared_ptr<Texture> iconOverlayNormalTexture;
  std::shared_ptr<Texture> iconOverlayNormalExpensiveTexture;
  std::shared_ptr<Texture> iconOverlayHoverTexture;
  std::shared_ptr<Texture> iconOverlayActiveTexture;
  
  /// Command buttons (on the bottom-left). 3 rows of 5 buttons each. Buttons may be invisible.
  CommandButton commandButtons[kCommandButtonRows][kCommandButtonCols];
  int pressedCommandButtonRow = -1;
  int pressedCommandButtonCol = -1;
  bool commandButtonPressedByHotkey = false;
  
  bool showingEconomyBuildingCommandButtons = false;
  /// The type of building that the user is about to place a foundation for.
  /// If not constructing a building, this is set to BuildingType::NumBuildings.
  BuildingType constructBuildingType = BuildingType::NumBuildings;
  
  // Control groups.
  static constexpr int kNumControlGroups = 10;
  std::vector<u32> controlGroups[kNumControlGroups];
  
  bool spaceHeld = false;
  
  // Resources.
  bool haveSyncObject = false;
  GLsync syncObject;
  
  GLuint pointBuffer;
  
  // Generic list of vertex buffer objects
  struct BufferObject {
    usize size;
    GLuint name;
  };
  std::vector<BufferObject> bufferObjects;
  usize nextBufferObject = 0;
  
  std::shared_ptr<Texture> playerColorsTexture;
  int playerColorsTextureWidth;
  int playerColorsTextureHeight;
  std::vector<QRgb> playerColors;
  
  std::shared_ptr<SpriteAndTextures> moveToSprite;
  QPointF moveToMapCoord;
  TimePoint moveToTime;
  bool haveMoveTo = false;
  
  u32 flashingObjectId = kInvalidObjectId;
  double flashingObjectStartTime;
  
  std::shared_ptr<Match> match;
  std::shared_ptr<GameController> gameController;
  std::shared_ptr<ServerConnection> connection;
  QFont georgiaFont;
  QFont georgiaFontSmaller;
  QFont georgiaFontLarger;
  QFont georgiaFontLargerStrikeOut;
  QFont georgiaFontHuge;
  const Palettes& palettes;
  const std::filesystem::path& graphicsSubPath;
  const std::filesystem::path& cachePath;
};
