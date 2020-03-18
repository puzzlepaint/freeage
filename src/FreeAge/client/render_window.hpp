#pragma once

#include <chrono>
#include <memory>

#include <QOffscreenSurface>
#include <QOpenGLWidget>

#include "FreeAge/client/unit.hpp"
#include "FreeAge/common/free_age.hpp"
#include "FreeAge/client/command_button.hpp"
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

class GameController;
class LoadingThread;

class RenderWindow : public QOpenGLWidget {
 Q_OBJECT
 public:
  RenderWindow(
      const std::shared_ptr<Match>& match,
      const std::shared_ptr<GameController>& gameController,
      const std::shared_ptr<ServerConnection>& connection,
      int georgiaFontID,
      const Palettes& palettes,
      const std::filesystem::path& graphicsPath,
      const std::filesystem::path& cachePath,
      QWidget* parent = nullptr);
  ~RenderWindow();
  
  /// Called from the loading thread to load the game resources in the background.
  void LoadResources();
  
  /// Scrolls the given map coordinates by the given amount in projected coordinates.
  void Scroll(float x, float y, QPointF* mapCoord);
  
  /// Computes the current scroll, taking into account the currently pressed keys.
  QPointF GetCurrentScroll(const TimePoint& atTime);
  
  inline void SetScroll(const QPointF& value) { scroll = value; }
  inline void SetMap(const std::shared_ptr<Map>& map) { this->map = map; map->SetNeedsRenderResourcesUpdate(true); }
  
  inline void SetGameController(const std::shared_ptr<GameController>& gameController) { this->gameController = gameController; }
  
 signals:
  void LoadingProgressUpdated(int progress);
  
 private slots:
  void SendLoadingProgress(int progress);
  void LoadingFinished();
  
 protected:
  void CreatePlayerColorPaletteTexture();
  
  void ComputePixelToOpenGLMatrix();
  void UpdateView(const TimePoint& now);
  /// Renders a closed path consisting of line segments between the given vertices.
  /// The last vertex will be connected to the first. The vertex coordinates are in screen (pixel) coordinates.
  /// The given offset is applied to each vertex.
  void RenderClosedPath(float halfLineWidth, const QRgb& color, const std::vector<QPointF>& vertices, const QPointF& offset);
  void RenderShadows(double displayedServerTime);
  void RenderBuildings(double displayedServerTime);
  void RenderBuildingFoundation(double displayedServerTime);
  void RenderSelectionGroundOutlines(double displayedServerTime);
  void RenderSelectionGroundOutline(QRgb color, ClientObject* object);
  void RenderOutlines(double displayedServerTime);
  void RenderUnits(double displayedServerTime);
  void RenderMoveToMarker(const TimePoint& now);
  void RenderHealthBars(double displayedServerTime);
  
  void RenderGameUI(double displayedServerTime);
  inline float GetUIScale() const { return 0.5f; }  // TODO: Make configurable
  QPointF GetResourcePanelTopLeft();
  void RenderResourcePanel(float uiScale);
  QPointF GetSelectionPanelTopLeft();
  void RenderSelectionPanel(float uiScale);
  QPointF GetCommandPanelTopLeft();
  void RenderCommandPanel(float uiScale);
  bool IsUIAt(int x, int y);
  
  /// Given screen-space coordinates (x, y), finds the next object to select at
  /// this point. If an object is found, true is returned, and the object's ID is
  /// returned in objectId.
  bool GetObjectToSelectAt(float x, float y, u32* objectId, std::vector<u32>* currentSelection);
  
  QPointF ScreenCoordToProjectedCoord(float x, float y);
  
  void ClearSelection();
  void AddToSelection(u32 objectId);
  /// This must be called after making changes to the selection.
  void SelectionChanged();
  /// Performs box selection. This already calls SelectionChanged() internally.
  void BoxSelection(const QPoint& p0, const QPoint& p1);
  
  void LetObjectGroundOutlineFlash(u32 objectId);
  
  void RenderLoadingScreen();
  
  /// Updates the game state to the given server time for which the state should be rendered.
  void UpdateGameState(double displayedServerTime);
  
  /// Checks whether a building foundation of the given type can be placed at the given
  /// cursor position in screen coordinates. If yes, true is returned and the base tile
  /// coordinates of the building are returned in baseTile.
  bool CanBuildingFoundationBePlacedHere(BuildingType type, const QPointF& cursorPos, QPoint* baseTile);
  
  void ShowDefaultCommandButtonsForSelection();
  void ShowEconomyBuildingCommandButtons();
  void ShowMilitaryBuildingCommandButtons();
  
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
  QPoint lastCursorPos = QPoint(0, 0);
  
  QPoint dragStartPos = QPoint(0, 0);
  bool possibleDragStart = false;
  bool dragging = false;
  
  bool ignoreLeftMouseRelease = false;
  
  /// Current map scroll position in map coordinates.
  /// The "scroll" map coordinate is visible at the center of the screen.
  QPointF scroll;
  
  bool scrollRightPressed = false;
  TimePoint scrollRightPressTime;
  
  bool scrollLeftPressed = false;
  TimePoint scrollLeftPressTime;
  
  bool scrollUpPressed = false;
  TimePoint scrollUpPressTime;
  
  bool scrollDownPressed = false;
  TimePoint scrollDownPressTime;
  
  static constexpr const float scrollDistancePerSecond = 2000;  // TODO: Make configurable
  
  /// Map data.
  std::shared_ptr<Map> map;
  
  /// IDs of selected objects (units or buildings).
  std::vector<u32> selection;
  
  /// Current zoom factor. The default zoom is one, two would make everything twice as big, etc.
  float zoom;
  
  /// Game start time.
  TimePoint renderStartTime;
  
  double lastDisplayedServerTime = -1;
  
  /// Cached widget size.
  int widgetWidth;
  int widgetHeight;
  
  float viewMatrix[4];  // column-major
  QRectF projectedCoordsViewRect;
  
  // Shaders.
  std::shared_ptr<UIShader> uiShader;
  std::shared_ptr<UISingleColorShader> uiSingleColorShader;
  std::shared_ptr<SpriteShader> spriteShader;
  std::shared_ptr<SpriteShader> shadowShader;
  std::shared_ptr<SpriteShader> outlineShader;
  std::shared_ptr<HealthBarShader> healthBarShader;
  
  // Loading thread.
  QOffscreenSurface* loadingSurface;
  LoadingThread* loadingThread;
  
  std::atomic<int> loadingStep;
  int maxLoadingStep;
  
  // Loading screen.
  bool isLoading;
  
  std::shared_ptr<Texture> loadingIcon;
  std::shared_ptr<TextDisplay> loadingTextDisplay;
  
  // Game UI.
  std::shared_ptr<Texture> resourcePanelTexture;
  OpaquenessMap resourcePanelOpaquenessMap;
  std::shared_ptr<Texture> resourceWoodTexture;
  std::shared_ptr<TextDisplay> woodTextDisplay;
  std::shared_ptr<Texture> resourceFoodTexture;
  std::shared_ptr<TextDisplay> foodTextDisplay;
  std::shared_ptr<Texture> resourceGoldTexture;
  std::shared_ptr<TextDisplay> goldTextDisplay;
  std::shared_ptr<Texture> resourceStoneTexture;
  std::shared_ptr<TextDisplay> stoneTextDisplay;
  std::shared_ptr<Texture> popTexture;
  std::shared_ptr<TextDisplay> popTextDisplay;
  std::shared_ptr<Texture> idleVillagerDisabledTexture;
  std::shared_ptr<Texture> currentAgeShieldTexture;
  std::shared_ptr<TextDisplay> currentAgeTextDisplay;
  
  std::shared_ptr<TextDisplay> gameTimeDisplay;
  std::shared_ptr<TextDisplay> pingDisplay;
  
  std::shared_ptr<Texture> commandPanelTexture;
  OpaquenessMap commandPanelOpaquenessMap;
  std::shared_ptr<Texture> buildEconomyBuildingsTexture;
  std::shared_ptr<Texture> buildMilitaryBuildingsTexture;
  std::shared_ptr<Texture> toggleBuildingsCategoryTexture;
  std::shared_ptr<Texture> quitTexture;
  
  std::shared_ptr<Texture> selectionPanelTexture;
  OpaquenessMap selectionPanelOpaquenessMap;
  std::shared_ptr<TextDisplay> singleObjectNameDisplay;
  std::shared_ptr<TextDisplay> hpDisplay;
  std::shared_ptr<TextDisplay> carriedResourcesDisplay;
  
  std::shared_ptr<Texture> iconOverlayNormalTexture;
  std::shared_ptr<Texture> iconOverlayNormalExpensiveTexture;
  std::shared_ptr<Texture> iconOverlayHoverTexture;
  std::shared_ptr<Texture> iconOverlayActiveTexture;
  
  /// Command buttons (on the bottom-left). 3 rows of 5 buttons each. Buttons may be invisible.
  CommandButton commandButtons[kCommandButtonRows][kCommandButtonCols];
  int pressedCommandButtonRow = -1;
  int pressedCommandButtonCol = -1;
  
  bool showingEconomyBuildingCommandButtons = false;
  /// The type of building that the user is about to place a foundation for.
  /// If not constructing a building, this is set to BuildingType::NumBuildings.
  BuildingType constructBuildingType = BuildingType::NumBuildings;
  
  // Resources.
  GLuint pointBuffer;
  
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
  const Palettes& palettes;
  const std::filesystem::path& graphicsPath;
  const std::filesystem::path& cachePath;
};
