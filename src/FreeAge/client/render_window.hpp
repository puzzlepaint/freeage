#pragma once

#include <chrono>
#include <memory>

#include <QOffscreenSurface>
#include <QOpenGLWidget>

#include "FreeAge/client/unit.hpp"
#include "FreeAge/common/free_age.hpp"
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
  
 signals:
  void LoadingProgressUpdated(int progress);
  
 private slots:
  void SendLoadingProgress(int progress);
  void LoadingFinished();
  
 protected:
  void CreatePlayerColorPaletteTexture();
  
  void ComputePixelToOpenGLMatrix();
  void UpdateView(const TimePoint& now);
  void RenderShadows(double elapsedSeconds);
  void RenderBuildings(double elapsedSeconds);
  void RenderOutlines(double elapsedSeconds);
  void RenderUnits(double elapsedSeconds);
  void RenderMoveToMarker(const TimePoint& now);
  void RenderHealthBars(double elapsedSeconds);
  
  /// Given screen-space coordinates (x, y), finds the next object to select at
  /// this point. If an object is found, true is returned, and the object's ID is
  /// returned in objectId.
  bool GetObjectToSelectAt(float x, float y, int* objectId);
  
  QPointF ScreenCoordToProjectedCoord(float x, float y);
  
  void ClearSelection();
  void AddToSelection(int objectId);
  
  void RenderLoadingScreen();
  
  virtual void initializeGL() override;
  virtual void paintGL() override;
  virtual void resizeGL(int width, int height) override;
  virtual void mousePressEvent(QMouseEvent* event) override;
  virtual void mouseMoveEvent(QMouseEvent* event) override;
  virtual void mouseReleaseEvent(QMouseEvent* event) override;
  virtual void wheelEvent(QWheelEvent* event) override;
  virtual void keyPressEvent(QKeyEvent* event) override;
  virtual void keyReleaseEvent(QKeyEvent* event) override;
  
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
  std::vector<int> selection;
  
  /// Current zoom factor. The default zoom is one, two would make everything twice as big, etc.
  float zoom;
  
  /// Game start time.
  TimePoint renderStartTime;
  
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
  
  // Resources.
  GLuint pointBuffer;
  
  std::shared_ptr<Texture> playerColorsTexture;
  int playerColorsTextureWidth;
  int playerColorsTextureHeight;
  std::vector<QRgb> playerColors;
  
  std::vector<ClientUnitType> unitTypes;
  std::vector<ClientBuildingType> buildingTypes;
  
  std::shared_ptr<SpriteAndTextures> moveToSprite;
  QPointF moveToMapCoord;
  TimePoint moveToTime;
  bool haveMoveTo = false;
  
  std::shared_ptr<Match> match;
  std::shared_ptr<GameController> gameController;
  std::shared_ptr<ServerConnection> connection;
  QFont georgiaFont;
  const Palettes& palettes;
  const std::filesystem::path& graphicsPath;
  const std::filesystem::path& cachePath;
};
