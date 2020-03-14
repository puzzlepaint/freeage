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
#include "FreeAge/client/shader_health_bar.hpp"
#include "FreeAge/client/shader_sprite.hpp"
#include "FreeAge/client/shader_ui.hpp"
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
  void RenderShadows(double displayedServerTime);
  void RenderBuildings(double displayedServerTime);
  void RenderOutlines(double displayedServerTime);
  void RenderUnits(double displayedServerTime);
  void RenderMoveToMarker(const TimePoint& now);
  void RenderHealthBars(double displayedServerTime);
  void RenderGameUI();
  
  /// Given screen-space coordinates (x, y), finds the next object to select at
  /// this point. If an object is found, true is returned, and the object's ID is
  /// returned in objectId.
  bool GetObjectToSelectAt(float x, float y, u32* objectId);
  
  QPointF ScreenCoordToProjectedCoord(float x, float y);
  
  void ClearSelection();
  void AddToSelection(u32 objectId);
  /// This must be called after making changes to the selection.
  void SelectionChanged();
  
  void RenderLoadingScreen();
  
  /// Updates the game state to the given server time for which the state should be rendered.
  void UpdateGameState(double displayedServerTime);
  
  virtual void initializeGL() override;
  virtual void paintGL() override;
  virtual void resizeGL(int width, int height) override;
  virtual void mousePressEvent(QMouseEvent* event) override;
  virtual void mouseMoveEvent(QMouseEvent* event) override;
  virtual void mouseReleaseEvent(QMouseEvent* event) override;
  virtual void wheelEvent(QWheelEvent* event) override;
  virtual void keyPressEvent(QKeyEvent* event) override;
  virtual void keyReleaseEvent(QKeyEvent* event) override;
  
  
  QPoint lastCursorPos = QPoint(0, 0);
  
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
  
  std::shared_ptr<Texture> commandPanelTexture;
  
  std::shared_ptr<Texture> selectionPanelTexture;
  std::shared_ptr<TextDisplay> singleObjectNameDisplay;
  
  std::shared_ptr<Texture> iconOverlayNormalTexture;
  std::shared_ptr<Texture> iconOverlayNormalExpensiveTexture;
  std::shared_ptr<Texture> iconOverlayHoverTexture;
  std::shared_ptr<Texture> iconOverlayActiveTexture;
  
  /// Command buttons (on the bottom-left). 3 rows of 5 buttons each. Buttons may be invisible.
  CommandButton commandButtons[kCommandButtonRows][kCommandButtonCols];
  int pressedCommandButtonRow = -1;
  int pressedCommandButtonCol = -1;
  
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
