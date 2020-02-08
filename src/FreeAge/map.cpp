#include "FreeAge/map.h"

#include <mango/image/image.hpp>

#include "FreeAge/free_age.h"
#include "FreeAge/logging.h"
#include "FreeAge/opengl.h"

// TODO: Make this configurable
constexpr int kTileProjectedWidth = 96;
constexpr int kTileProjectedHeight = kTileProjectedWidth / 2;
// TODO: Might want to make this smaller than in the original game to give a better overview.
//       With the default, tile occupancy on hill sides can be very hard to see.
constexpr int kTileProjectedElevationDifference = kTileProjectedHeight / 2;

Map::Map(int width, int height)
    : width(width),
      height(height) {
  maxElevation = 7;  // TODO: Make configurable
  elevation = new int[(width + 1) * (height + 1)];
  
  // Initialize the elevation to zero everywhere.
  for (int y = 0; y <= height; ++ y) {
    for (int x = 0; x <= width; ++ x) {
      elevationAt(x, y) = 0;
    }
  }
}

Map::~Map() {
  delete[] elevation;
}

QPointF Map::TileCornerToProjectedCoord(int cornerX, int cornerY) const {
  if (cornerX < 0 || cornerY < 0 ||
      cornerX > width || cornerY > height) {
    LOG(ERROR) << "Parameters are out-of-bounds: (" << cornerX << ", " << cornerY << ")";
    return QPointF(0, 0);
  }
  
  return cornerX * QPointF(kTileProjectedWidth / 2, kTileProjectedHeight / 2) +
         cornerY * QPointF(kTileProjectedWidth / 2, -kTileProjectedHeight / 2) +
         elevationAt(cornerX, cornerY) * QPointF(0, -kTileProjectedElevationDifference);
}

QPointF Map::MapCoordToProjectedCoord(const QPointF& mapCoord, QPointF* jacobianColumn0, QPointF* jacobianColumn1) const {
  int lowerX = std::max(0, std::min(width - 1, static_cast<int>(mapCoord.x())));
  int lowerY = std::max(0, std::min(height - 1, static_cast<int>(mapCoord.y())));
  
  QPointF left = TileCornerToProjectedCoord(lowerX, lowerY);
  QPointF bottom = TileCornerToProjectedCoord(lowerX + 1, lowerY);
  QPointF top = TileCornerToProjectedCoord(lowerX, lowerY + 1);
  QPointF right = TileCornerToProjectedCoord(lowerX + 1, lowerY + 1);
  
  float xDiff = mapCoord.x() - lowerX;
  float yDiff = mapCoord.y() - lowerY;
  
  if (jacobianColumn0) {
    // Left column of jacobian
    *jacobianColumn0 = - (1 - yDiff) * left +
                         (1 - yDiff) * bottom +
                       - (    yDiff) * top +
                         (    yDiff) * right;
  }
  if (jacobianColumn1) {
    // Right column of jacobian
    *jacobianColumn1 = - (1 - xDiff) * left +
                       - (    xDiff) * bottom +
                         (1 - xDiff) * top +
                         (    xDiff) * right;
  }
  
  return (1 - xDiff) * (1 - yDiff) * left +
         (    xDiff) * (1 - yDiff) * bottom +
         (1 - xDiff) * (    yDiff) * top +
         (    xDiff) * (    yDiff) * right;
}

static QPointF InterpolateVectorBilinearly(
    float factorX,
    float factorY,
    const QPointF& topLeft,
    const QPointF& topRight,
    const QPointF& bottomLeft,
    const QPointF& bottomRight) {
  return (1 - factorX) * (1 - factorY) * topLeft +
         (    factorX) * (1 - factorY) * topRight +
         (1 - factorX) * (    factorY) * bottomLeft +
         (    factorX) * (    factorY) * bottomRight;
}

/// Determines the coordinates that were used for bilinear interpolation
/// of the topLeft .. bottomRight vectors to obtain interpolatedPoint.
QPointF DetermineInterpolationCoordinates(
    const QPointF& topLeft,
    const QPointF& topRight,
    const QPointF& bottomLeft,
    const QPointF& bottomRight,
    const QPointF& interpolatedPoint) {
  // Approach 1:
  // The vertical line with line parameter t, depending on the x interpolation factor x, is:
  //       ((1 - x) * topLeft + x * topRight) +
  // t * ( ((1 - x) * bottomLeft + x * bottomRight) -
  //       ((1 - x) * topLeft + x * topRight) )
  // We intersect this line with interpolatedPoint to get two equations with two unknowns.
  // Solving this yiels the x interpolation factor.
  // The y interpolation factor can be determined analogously.
  // This seems like a lot of work.
  // TODO: Implement such a more direct approach that does not rely on iteration. Some derivation:
  // 
  // interpolatedPoint = ((1 - x) * topLeft + x * topRight) + t * ( ((1 - x) * bottomLeft + x * bottomRight) - ((1 - x) * topLeft + x * topRight) )
  // interpolatedPoint = (1 - x) * topLeft + x * topRight + t * ( ((1 - x) * bottomLeft + x * bottomRight) - ((1 - x) * topLeft + x * topRight) )
  // 
  // (interpolatedPoint - ((1 - x) * topLeft + x * topRight)) / ( ((1 - x) * bottomLeft + x * bottomRight) - ((1 - x) * topLeft + x * topRight) ) = t
  // 
  // (interpolatedPoint_x - ((1 - x) * topLeft_x + x * topRight_x)) / ( ((1 - x) * bottomLeft_x + x * bottomRight_x) - ((1 - x) * topLeft_x + x * topRight_x) ) = t
  // 
  // 
  // // Set in into other equation:
  // 
  // interpolatedPoint_y = (1 - x) * topLeft_y + x * topRight_y + ( ((1 - x) * bottomLeft_y + x * bottomRight_y) - ((1 - x) * topLeft_y + x * topRight_y) ) * (interpolatedPoint_x - ((1 - x) * topLeft_x + x * topRight_x)) / ( ((1 - x) * bottomLeft_x + x * bottomRight_x) - ((1 - x) * topLeft_x + x * topRight_x) )
  // ( ((1 - x) * bottomLeft_x + x * bottomRight_x) - ((1 - x) * topLeft_x + x * topRight_x) ) * ((1 - x) * topLeft_y + x * topRight_y) + ( ((1 - x) * bottomLeft_y + x * bottomRight_y) - ((1 - x) * topLeft_y + x * topRight_y) ) * (interpolatedPoint_x - ((1 - x) * topLeft_x + x * topRight_x)) - ( ((1 - x) * bottomLeft_x + x * bottomRight_x) - ((1 - x) * topLeft_x + x * topRight_x) ) * interpolatedPoint_y
  // 
  // 
  // pkg load symbolic;
  // syms x interpolatedPoint_x interpolatedPoint_y topLeft_x topLeft_y topRight_x topRight_y bottomLeft_x bottomLeft_y bottomRight_x bottomRight_y;
  // ccode( expand( ( ((1 - x) * bottomLeft_x + x * bottomRight_x) - ((1 - x) * topLeft_x + x * topRight_x) ) * ((1 - x) * topLeft_y + x * topRight_y) + ( ((1 - x) * bottomLeft_y + x * bottomRight_y) - ((1 - x) * topLeft_y + x * topRight_y) ) * (interpolatedPoint_x - ((1 - x) * topLeft_x + x * topRight_x)) - ( ((1 - x) * bottomLeft_x + x * bottomRight_x) - ((1 - x) * topLeft_x + x * topRight_x) ) * interpolatedPoint_y ) )
  // 
  // // Yields:
  // 
  // bottomLeft_x*interpolatedPoint_y*x - bottomLeft_x*interpolatedPoint_y + bottomLeft_x*topLeft_y*pow(x, 2) - 2*bottomLeft_x*topLeft_y*x + bottomLeft_x*topLeft_y - bottomLeft_x*topRight_y*pow(x, 2) + bottomLeft_x*topRight_y*x - bottomLeft_y*interpolatedPoint_x*x + bottomLeft_y*interpolatedPoint_x - bottomLeft_y*topLeft_x*pow(x, 2) + 2*bottomLeft_y*topLeft_x*x - bottomLeft_y*topLeft_x + bottomLeft_y*topRight_x*pow(x, 2) - bottomLeft_y*topRight_x*x - bottomRight_x*interpolatedPoint_y*x - bottomRight_x*topLeft_y*pow(x, 2) + bottomRight_x*topLeft_y*x +bottomRight_x*topRight_y*pow(x, 2) + bottomRight_y*interpolatedPoint_x*x + bottomRight_y*topLeft_x*pow(x, 2) - bottomRight_y*topLeft_x*x - bottomRight_y*topRight_x*pow(x, 2) + interpolatedPoint_x*topLeft_y*x - interpolatedPoint_x*topLeft_y - interpolatedPoint_x*topRight_y*x - interpolatedPoint_y*topLeft_x*x + interpolatedPoint_y*topLeft_x + interpolatedPoint_y*topRight_x*x
  // 
  // // Grouping terms:
  // 
  // a         : - bottomLeft_x*interpolatedPoint_y + bottomLeft_x*topLeft_y + bottomLeft_y*interpolatedPoint_x - bottomLeft_y*topLeft_x - interpolatedPoint_x*topLeft_y + interpolatedPoint_y*topLeft_x
  // + b * x   : bottomLeft_x*interpolatedPoint_y - 2*bottomLeft_x*topLeft_y + bottomLeft_x*topRight_y - bottomLeft_y*interpolatedPoint_x + 2*bottomLeft_y*topLeft_x - bottomLeft_y*topRight_x - bottomRight_x*interpolatedPoint_y + bottomRight_x*topLeft_y + bottomRight_y*interpolatedPoint_x - bottomRight_y*topLeft_x + interpolatedPoint_x*topLeft_y - interpolatedPoint_x*topRight_y - interpolatedPoint_y*topLeft_x + interpolatedPoint_y*topRight_x
  // + c * x^2 : bottomLeft_x*topLeft_y - bottomLeft_x*topRight_y - bottomLeft_y*topLeft_x + bottomLeft_y*topRight_x - bottomRight_x*topLeft_y + bottomRight_x*topRight_y + bottomRight_y*topLeft_x - bottomRight_y*topRight_x
  // 
  // // --> solve quadratic equation
  
  // Approach 2: Initialize the solution somehow, do Gauss-Newton.
  // This seems much easier.
  // The residual is: Interpolate(solution) - interpolatedPoint.
  QPointF solution = QPointF(0.5f, 0.5f);
  
  bool converged = false;
  for (int i = 0; i < 10; ++ i) {
    const QPointF currentInterpolation = InterpolateVectorBilinearly(solution.x(), solution.y(), topLeft, topRight, bottomLeft, bottomRight);
    QPointF residual = currentInterpolation - interpolatedPoint;
    if (residual.manhattanLength() < 1e-5f) {
      converged = true;
      break;
    }
    
    // Left column of jacobian (Interpolate(solution) derivative by x)
    QPointF jacCol0 = - (1 - solution.y()) * topLeft +
                        (1 - solution.y()) * topRight +
                      - (    solution.y()) * bottomLeft +
                        (    solution.y()) * bottomRight;
    
    // Right column of jacobian (Interpolate(solution) derivative by y)
    QPointF jacCol1 = - (1 - solution.x()) * topLeft +
                      - (    solution.x()) * topRight +
                        (1 - solution.x()) * bottomLeft +
                        (    solution.x()) * bottomRight;
    
    // Compute Gauss-Newton update: - H^(-1) b
    float H00 = jacCol0.x() * jacCol0.x() + jacCol0.y() * jacCol0.y();
    float H01 = jacCol0.x() * jacCol1.x() + jacCol0.y() * jacCol1.y();  // = H10
    float H11 = jacCol1.x() * jacCol1.x() + jacCol1.y() * jacCol1.y();
    
    float detH = H00 * H11 - H01 * H01;
    float detH_inv = 1.f / detH;
    float H00_inv = detH_inv * H11;
    float H01_inv = detH_inv * -H01;  // = H10_inv
    float H11_inv = detH_inv * H00;
    
    float b0 = - jacCol0.x() * residual.x() - jacCol0.y() * residual.y();
    float b1 = - jacCol1.x() * residual.x() - jacCol1.y() * residual.y();
    
    float update0 = H00_inv * b0 + H01_inv * b1;
    float update1 = H01_inv * b0 + H11_inv * b1;
    
    // Since we start at 0, the update equals the result (after a single iteration).
    solution += QPointF(update0, update1);
    
    // LOG(INFO) << "- current solution: " << solution.x() << ", " << solution.y();
  }
  
  if (!converged) {
    LOG(ERROR) << "Not converged.";
    LOG(ERROR) << "  topLeft: " << topLeft.x() << ", " << topLeft.y();
    LOG(ERROR) << "  topRight: " << topRight.x() << ", " << topRight.y();
    LOG(ERROR) << "  bottomLeft: " << bottomLeft.x() << ", " << bottomLeft.y();
    LOG(ERROR) << "  bottomRight: " << bottomRight.x() << ", " << bottomRight.y();
    LOG(ERROR) << "  interpolatedPoint: " << interpolatedPoint.x() << ", " << interpolatedPoint.y();
    const QPointF currentInterpolation = InterpolateVectorBilinearly(solution.x(), solution.y(), topLeft, topRight, bottomLeft, bottomRight);
    LOG(ERROR) << "  currentInterpolation: " << currentInterpolation.x() << ", " << currentInterpolation.y();
  }
  
  return solution;
}

bool Map::ProjectedCoordToMapCoord(const QPointF& projectedCoord, QPointF* mapCoord) const {
  // This is a bit more difficult than MapCoordToProjectedCoord() since we do not know the
  // elevation beforehand. Thus, we use the following strategy: Assume that the elevation is
  // constant, compute the map coord under this assumption, then go up or
  // down until we hit the actual map coord.
  int assumedElevation = maxElevation / 2;
  
  // Get the map coordinates that would result in projectedCoord given that the map was
  // flat, with an elevation of assumedElevation everywhere.
  // To do this, we solve this for x and y:
  //   originTileAtAssumedElevCoord + x * plusXDirection + y * plusYDirection = projectedCoord
  // As a matrix equation "A * x = b", this reads:
  //   (plusXDirection.x plusYDirection.x) * (x) = (projectedCoord.x - originTileAtAssumedElevCoord.x)
  //   (plusXDirection.y plusYDirection.y)   (y)   (projectedCoord.y - originTileAtAssumedElevCoord.y)
  QPointF originTileAtAssumedElevCoord =
      assumedElevation * QPointF(0, -kTileProjectedElevationDifference);
  QPointF plusXDirection = QPointF(kTileProjectedWidth / 2, kTileProjectedHeight / 2);
  QPointF plusYDirection = QPointF(kTileProjectedWidth / 2, -kTileProjectedHeight / 2);
  
  // Build matrix A.
  float A00 = plusXDirection.x();
  float A01 = plusYDirection.x();
  float A10 = plusXDirection.y();
  float A11 = plusYDirection.y();
  
  // Invert A.
  float detH = A00 * A11 - A01 * A10;
  float detH_inv = 1.f / detH;
  float A00_inv = detH_inv * A11;
  float A01_inv = detH_inv * -A01;
  float A10_inv = detH_inv * -A10;
  float A11_inv = detH_inv * A00;
  
  // Build vector b.
  float b0 = projectedCoord.x() - originTileAtAssumedElevCoord.x();
  float b1 = projectedCoord.y() - originTileAtAssumedElevCoord.y();
  
  // Compute the solution.
  QPointF currentMapCoordEstimate = QPointF(
      A00_inv * b0 + A01_inv * b1,
      A10_inv * b0 + A11_inv * b1);
  
  // Clamp the initial map coordinate to be within the map
  // (while keeping the projected x coordinate constant).
  constexpr float kClampMargin = 0.001f;
  if (currentMapCoordEstimate.x() < 0) {
    // The coordinate is beyond the top-left map border. Move the coordinate down (in projected coordinates).
    currentMapCoordEstimate += -currentMapCoordEstimate.x() * QPointF(1, -1);
  }
  if (currentMapCoordEstimate.y() >= height) {
    // The coordinate is beyond the top-right map border. Move the coordinate down (in projected coordinates).
    currentMapCoordEstimate += -(currentMapCoordEstimate.y() - height + kClampMargin) * QPointF(1, -1);
  }
  if (currentMapCoordEstimate.y() < 0) {
    // The coordinate is beyond the bottom-left map border. Move the coordinate up (in projected coordinates)
    currentMapCoordEstimate += -currentMapCoordEstimate.y() * QPointF(-1, 1);
  }
  if (currentMapCoordEstimate.x() >= width) {
    // The coordinate is beyond the bottom-right map border. Move the coordinate up (in projected coordinates)
    currentMapCoordEstimate += -(currentMapCoordEstimate.x() - width + kClampMargin) * QPointF(-1, 1);
  }
  
  // We use Gauss-Newton optimization (with coordinates clamped to the map) to do the search.
  // Note that we allow both coordinates to vary here, rather than constraining the movement
  // to be vertical, since this is easily possible, the performance difference should be completely
  // negligible, and it gives us a slightly more general implementation.
  // TODO: Use Levenberg-Marquardt as it is safer
  bool converged = false;
  constexpr int kMaxNumIterations = 50;
  for (int iteration = 0; iteration < kMaxNumIterations; ++ iteration) {
    QPointF jacCol0;
    QPointF jacCol1;
    QPointF currentProjectedCoord = MapCoordToProjectedCoord(currentMapCoordEstimate, &jacCol0, &jacCol1);
    QPointF residual = currentProjectedCoord - projectedCoord;
    if (residual.manhattanLength() < 1e-5f) {
      converged = true;
      break;
    }
    
    // Compute Gauss-Newton update: - H^(-1) b
    float H00 = jacCol0.x() * jacCol0.x() + jacCol0.y() * jacCol0.y();
    float H01 = jacCol0.x() * jacCol1.x() + jacCol0.y() * jacCol1.y();  // = H10
    float H11 = jacCol1.x() * jacCol1.x() + jacCol1.y() * jacCol1.y();
    
    float detH = H00 * H11 - H01 * H01;
    float detH_inv = 1.f / detH;
    float H00_inv = detH_inv * H11;
    float H01_inv = detH_inv * -H01;  // = H10_inv
    float H11_inv = detH_inv * H00;
    
    float b0 = - jacCol0.x() * residual.x() - jacCol0.y() * residual.y();
    float b1 = - jacCol1.x() * residual.x() - jacCol1.y() * residual.y();
    
    float update0 = H00_inv * b0 + H01_inv * b1;
    float update1 = H01_inv * b0 + H11_inv * b1;
    currentMapCoordEstimate += QPointF(update0, update1);
    // Clamp to map area.
    currentMapCoordEstimate = QPointF(
        std::max<float>(0, std::min<float>(width - kClampMargin, currentMapCoordEstimate.x())),
        std::max<float>(0, std::min<float>(height - kClampMargin, currentMapCoordEstimate.y())));
    
    // LOG(INFO) << "- currentMapCoordEstimate: " << currentMapCoordEstimate.x() << ", " << currentMapCoordEstimate.y();
  }
  if (converged) {
    *mapCoord = currentMapCoordEstimate;
    return true;
  } else {
    return false;
  }
  
  return false;
}


void Map::GenerateRandomMap() {
  constexpr int numHills = 40;  // TODO: Make configurable
  for (int hill = 0; hill < numHills; ++ hill) {
    int tileX = rand() % width;
    int tileY = rand() % height;
    int elevationValue = rand() % maxElevation;
    PlaceElevation(tileX, tileY, elevationValue);
  }
}

void Map::PlaceElevation(int tileX, int tileY, int elevationValue) {
  int currentMinElev = elevationValue;
  int currentMaxElev = elevationValue;
  
  int minX = tileX;
  int minY = tileY;
  int maxX = tileX + 1;
  int maxY = tileY + 1;
  
  while (true) {
    // Set the current ring.
    bool anyChangeDone = false;
    
    for (int x = std::max(0, minX); x <= std::min(width, maxX); ++ x) {
      if (minY >= 0) {
        int& elev = elevationAt(x, minY);
        int newElev = std::min(currentMaxElev, std::max(currentMinElev, elev));
        anyChangeDone |= newElev != elev;
        elev = newElev;
      }
      if (maxY <= height) {
        int& elev = elevationAt(x, maxY);
        int newElev = std::min(currentMaxElev, std::max(currentMinElev, elev));
        anyChangeDone |= newElev != elev;
        elev = newElev;
      }
    }
    for (int y = std::max(0, minY + 1); y <= std::min(height, maxY - 1); ++ y) {
      if (minX >= 0) {
        int& elev = elevationAt(minX, y);
        int newElev = std::min(currentMaxElev, std::max(currentMinElev, elev));
        anyChangeDone |= newElev != elev;
        elev = newElev;
      }
      if (maxX <= width) {
        int& elev = elevationAt(maxX, y);
        int newElev = std::min(currentMaxElev, std::max(currentMinElev, elev));
        anyChangeDone |= newElev != elev;
        elev = newElev;
      }
    }
    
    if (!anyChangeDone) {
      break;
    }
    
    // Go to the next ring.
    -- currentMinElev;
    ++ currentMaxElev;
    if (currentMinElev <= 0 && currentMaxElev >= maxElevation) {
      break;
    }
    
    -- minX;
    -- minY;
    ++ maxX;
    ++ maxY;
  }
}


void Map::LoadRenderResources() {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  // Load texture
  const char* texturePath = "/home/thomas/.local/share/Steam/steamapps/compatdata/813780/pfx/drive_c/users/steamuser/Games/Age of Empires 2 DE/76561197995377131/mods/subscribed/812_Zetnus Improved Grid Mod/resources/_common/terrain/textures/2x/g_gr2.dds";
  mango::Bitmap textureBitmap(texturePath, mango::Format(32, mango::Format::UNORM, mango::Format::BGRA, 8, 8, 8, 8));
  
  f->glGenTextures(1, &textureId);
  f->glBindTexture(GL_TEXTURE_2D, textureId);
  
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);  // TODO
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);  // TODO
  
  f->glTexImage2D(
      GL_TEXTURE_2D,
      0, GL_RGBA,
      textureBitmap.width, textureBitmap.height,
      0, GL_BGRA, GL_UNSIGNED_BYTE,
      textureBitmap.address<u32>(0, 0));
  
  CHECK_OPENGL_NO_ERROR();
  
  // Build geometry buffer
  int elementSizeInBytes = 4 * sizeof(float);
  u8* data = new u8[(width + 1) * (height + 1) * elementSizeInBytes];
  u8* ptr = data;
  for (int y = 0; y <= height; ++ y) {
    for (int x = 0; x <= width; ++ x) {
      QPointF projectedCoord = TileCornerToProjectedCoord(x, y);
      *reinterpret_cast<float*>(ptr) = projectedCoord.x();
      ptr += sizeof(float);
      *reinterpret_cast<float*>(ptr) = projectedCoord.y();
      ptr += sizeof(float);
      *reinterpret_cast<float*>(ptr) = 0.1f * x;
      ptr += sizeof(float);
      *reinterpret_cast<float*>(ptr) = 0.1f * y;
      ptr += sizeof(float);
    }
  }
  f->glGenBuffers(1, &vertexBuffer);
  f->glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
  f->glBufferData(GL_ARRAY_BUFFER, (width + 1) * (height + 1) * elementSizeInBytes, data, GL_STATIC_DRAW);
  delete[] data;
  CHECK_OPENGL_NO_ERROR();
  
  // Build index buffer
  u32* indexData = new u32[width * height * 6];
  u32* indexPtr = indexData;
  for (int y = 0; y < height; ++ y) {
    for (int x = 0; x < width; ++ x) {
      int diff1 = std::abs(elevationAt(x, y) - elevationAt(x + 1, y + 1));
      int diff2 = std::abs(elevationAt(x + 1, y) - elevationAt(x, y + 1));
      
      if (diff1 < diff2) {
        *indexPtr++ = (x + 0) + (width + 1) * (y + 0);
        *indexPtr++ = (x + 1) + (width + 1) * (y + 1);
        *indexPtr++ = (x + 0) + (width + 1) * (y + 1);
        
        *indexPtr++ = (x + 0) + (width + 1) * (y + 0);
        *indexPtr++ = (x + 1) + (width + 1) * (y + 0);
        *indexPtr++ = (x + 1) + (width + 1) * (y + 1);
      } else {
        *indexPtr++ = (x + 0) + (width + 1) * (y + 0);
        *indexPtr++ = (x + 1) + (width + 1) * (y + 0);
        *indexPtr++ = (x + 0) + (width + 1) * (y + 1);
        
        *indexPtr++ = (x + 1) + (width + 1) * (y + 0);
        *indexPtr++ = (x + 1) + (width + 1) * (y + 1);
        *indexPtr++ = (x + 0) + (width + 1) * (y + 1);
      }
    }
  }
  f->glGenBuffers(1, &indexBuffer);
  f->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
  f->glBufferData(GL_ELEMENT_ARRAY_BUFFER, width * height * 6 * sizeof(u32), indexData, GL_STATIC_DRAW);
  delete[] indexData;
  CHECK_OPENGL_NO_ERROR();
  
  // Create shader program
  program.reset(new ShaderProgram());
  
  CHECK(program->AttachShader(
      "#version 330 core\n"
      "in vec2 in_position;\n"
      "in vec2 in_texcoord;\n"
      "uniform vec2 u_scaling;\n"
      "uniform vec2 u_translation;\n"
      "out vec2 var_texcoord;\n"
      "void main() {\n"
      // TODO: Use sensible z value? Or disable z writing while rendering the terrain anyway
      "  gl_Position = vec4(u_scaling.x * in_position.x + u_translation.x, u_scaling.y * in_position.y + u_translation.y, 0.999, 1);\n"
      "  var_texcoord = in_texcoord;\n"
      "}\n",
      ShaderProgram::ShaderType::kVertexShader));
  
  CHECK(program->AttachShader(
      "#version 330 core\n"
      "layout(location = 0) out vec4 out_color;\n"
      "\n"
      "in vec2 var_texcoord;\n"
      "\n"
      "uniform sampler2D u_texture;\n"
      "\n"
      "void main() {\n"
      "  out_color = texture(u_texture, var_texcoord.xy);\n"
      "}\n",
      ShaderProgram::ShaderType::kFragmentShader));
  
  CHECK(program->LinkProgram());
  
  program->UseProgram();
  
  program_u_texture_location = program->GetUniformLocationOrAbort("u_texture");
  program_u_scaling_location = program->GetUniformLocationOrAbort("u_scaling");
  program_u_translation_location = program->GetUniformLocationOrAbort("u_translation");
  
  // TODO: Un-load the render resources again on destruction
}

void Map::Render(int screenWidth, int screenHeight) {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  program->UseProgram();
  
  // f->glEnable(GL_BLEND);
  // f->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  
  program->SetUniform1i(program_u_texture_location, 0);  // use GL_TEXTURE0
  f->glBindTexture(GL_TEXTURE_2D, textureId);
  
  program->SetUniform2f(program_u_scaling_location, 2.f / screenWidth, -2.f / screenHeight);
  program->SetUniform2f(program_u_translation_location, -1.f, 1.f - /* TODO */ 1.f);
  
  f->glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
  program->SetPositionAttribute(
      2,
      GetGLType<float>::value,
      4 * sizeof(float),
      0);
  
  f->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
  program->SetTexCoordAttribute(
      2,
      GetGLType<float>::value,
      4 * sizeof(float),
      2 * sizeof(float));
  
  f->glDrawElements(GL_TRIANGLES, width * height * 6, GL_UNSIGNED_INT, 0);
  
  CHECK_OPENGL_NO_ERROR();
}
