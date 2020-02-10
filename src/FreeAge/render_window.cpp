#include "FreeAge/render_window.h"

#include <math.h>

#include <QKeyEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_2_Core>
#include <QTimer>

#include "FreeAge/logging.h"
#include "FreeAge/opengl.h"
#include "FreeAge/sprite.h"
#include "FreeAge/sprite_atlas.h"

RenderWindow::RenderWindow(const Palettes& palettes, const std::filesystem::path& graphicsPath, QWidget* parent)
    : QOpenGLWidget(parent),
      palettes(palettes),
      graphicsPath(graphicsPath) {
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
  if (map) {
    delete map;
  }
  buildingTextures.clear();
  buildingSprites.clear();
  doneCurrent();
}

void RenderWindow::SetMap(Map* map) {
  this->map = map;
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

void RenderWindow::initializeGL() {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  CHECK_OPENGL_NO_ERROR();
  
  // Create a vertex array object (VAO).
  // TODO: Handle this properly.
  GLuint vao;
  f->glGenVertexArrays(1, &vao);
  f->glBindVertexArray(vao);
  CHECK_OPENGL_NO_ERROR();
  
  // Create shaders.
  spriteShader.reset(new SpriteShader());
  
  // Load sprites.
  int numBuildingTypes = static_cast<int>(BuildingType::NumBuildings);
  buildingSprites.resize(numBuildingTypes);
  buildingTextures.resize(numBuildingTypes);
  for (int buildingType = 0; buildingType < static_cast<int>(BuildingType::NumBuildings); ++ buildingType) {
    Sprite& sprite = buildingSprites[buildingType];
    
    if (!sprite.LoadFromFile((graphicsPath / GetBuildingFilename(static_cast<BuildingType>(buildingType)).toStdString()).c_str(), palettes)) {
      std::cout << "Failed to load sprite. Exiting.\n";
      exit(1);  // TODO: Exit gracefully
    }
    
    // Create a sprite atlas texture containing all frames of the SMX animation.
    // TODO: This generally takes a LOT of memory. We probably want to do a dense packing of the images using
    //       non-rectangular geometry to save some more space.
    SpriteAtlas atlas;
    atlas.AddSprite(&sprite);
    
    int textureSize = 2048;
    int largestTooSmallSize = -1;
    int smallestAcceptableSize = -1;
    for (int attempt = 0; attempt < 8; ++ attempt) {
      if (!atlas.BuildAtlas(textureSize, textureSize, nullptr)) {
        // The size is too small.
        // LOG(INFO) << "Size " << textureSize << " is too small.";
        largestTooSmallSize = textureSize;
        if (smallestAcceptableSize >= 0) {
          textureSize = (largestTooSmallSize + smallestAcceptableSize) / 2;
        } else {
          textureSize = 2 * largestTooSmallSize;
        }
      } else {
        // The size is large enough.
        // LOG(INFO) << "Size " << textureSize << " is okay.";
        smallestAcceptableSize = textureSize;
        if (smallestAcceptableSize >= 0) {
          textureSize = (largestTooSmallSize + smallestAcceptableSize) / 2;
        } else {
          textureSize = smallestAcceptableSize / 2;
        }
      }
    }
    if (smallestAcceptableSize <= 0) {
      std::cout << "Unable to find a texture size which all animation frames can be packed into. Exiting.";
      exit(1);  // TODO: Exit gracefully
    }
    
    LOG(INFO) << "Atlas for " << GetBuildingFilename(static_cast<BuildingType>(buildingType)).toStdString() << " uses size: " << smallestAcceptableSize;
    QImage atlasImage;
    if (!atlas.BuildAtlas(smallestAcceptableSize, smallestAcceptableSize, &atlasImage)) {
      LOG(ERROR) << "Unexpected error while building an atlas image.";
      exit(1);  // TODO: Exit gracefully
    }
    
    // Transfer the atlasImage to the GPU
    buildingTextures[buildingType].Load(atlasImage);
  }
  
  if (map) {
    map->LoadRenderResources();
  }
  
  // Create a buffer containing a single point for sprite rendering
  f->glGenBuffers(1, &pointBuffer);
  f->glBindBuffer(GL_ARRAY_BUFFER, pointBuffer);
  int elementSizeInBytes = 3 * sizeof(float);
  float data[] = {
      0.f, 0.f, 0};
  f->glBufferData(GL_ARRAY_BUFFER, 1 * elementSizeInBytes, data, GL_DYNAMIC_DRAW);
  CHECK_OPENGL_NO_ERROR();
  
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
  
  float viewMatrix[4];  // column-major
  viewMatrix[0] = scalingX;
  viewMatrix[1] = scalingY;
  viewMatrix[2] = -scalingX * projectedCoordAtScreenCenter.x();
  viewMatrix[3] = -scalingY * projectedCoordAtScreenCenter.y();
  
  // Apply the view transformation to all shaders.
  // TODO: Use a uniform buffer object for that.
  spriteShader->GetProgram()->UseProgram();
  spriteShader->GetProgram()->setUniformMatrix2fv(spriteShader->GetViewMatrixLocation(), viewMatrix);
  
  // Set states for rendering.
  // f->glEnable(GL_MULTISAMPLE);
  // f->glEnable(GL_DEPTH_TEST);
  f->glDisable(GL_CULL_FACE);
  
  // Clear background.
  f->glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
  f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  
  CHECK_OPENGL_NO_ERROR();
  
  // Draw the map.
  map->Render(
      viewMatrix,
      buildingSprites,
      buildingTextures,
      spriteShader.get(),
      pointBuffer,
      zoom,
      widgetWidth,
      widgetHeight,
      elapsedSeconds);
}

void RenderWindow::resizeGL(int width, int height) {
  widgetWidth = width;
  widgetHeight = height;
}

void RenderWindow::mousePressEvent(QMouseEvent* /*event*/) {
  // TODO
}

void RenderWindow::mouseMoveEvent(QMouseEvent* /*event*/) {
  // TODO
  // TODO: Possibly manually batch these events together, since we disabled event batching globally.
}

void RenderWindow::mouseReleaseEvent(QMouseEvent* /*event*/) {
  // TODO
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
