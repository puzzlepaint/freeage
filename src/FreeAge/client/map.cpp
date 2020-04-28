// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/client/map.hpp"

#include <mango/image/image.hpp>

#include "FreeAge/common/free_age.hpp"
#include "FreeAge/common/logging.hpp"
#include "FreeAge/client/mod_manager.hpp"
#include "FreeAge/client/opengl.hpp"

// TODO: Make this configurable
constexpr int kTileProjectedWidth = 96;
constexpr int kTileProjectedHeight = kTileProjectedWidth / 2;
// TODO: Might want to make this smaller than in the original game to give a better overview.
//       With the default, tile occupancy on hill sides can be very hard to see.
constexpr int kTileProjectedElevationDifference = kTileProjectedHeight / 2;

const float kTileDiagonalLength = 0.5f * sqrtf(kTileProjectedWidth * kTileProjectedWidth + kTileProjectedHeight * kTileProjectedHeight);

Map::Map(int width, int height)
    : width(width),
      height(height) {
  maxElevation = 7;  // TODO: Make configurable
  elevation = new int[(width + 1) * (height + 1)];
  viewCount = new int[width * height];
  
  // Initialize the elevation to "unknown" everywhere and the view count to zero.
  for (int y = 0; y <= height; ++ y) {
    for (int x = 0; x <= width; ++ x) {
      elevationAt(x, y) = -1;
      if (x < width && y < height) {
        viewCountAt(x, y) = -1;  // NOTE: Set this to 0 to make the map explored at the start
      }
    }
  }
  
  viewCountChangeMinX = 0;
  viewCountChangeMinY = 0;
  viewCountChangeMaxX = width - 1;
  viewCountChangeMaxY = height - 1;
}

Map::~Map() {
  UnloadRenderResources();
  
  delete[] elevation;
  delete[] viewCount;
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
  *mapCoord = QPointF(
      A00_inv * b0 + A01_inv * b1,
      A10_inv * b0 + A11_inv * b1);
  
  // Clamp the initial map coordinate to be within the map
  // (while keeping the projected x coordinate constant).
  constexpr float kClampMargin = 0.001f;
  // TODO: This did not work in all cases, there are cases where the result ends up outside of the map.
  // if (mapCoord->x() < 0) {
  //   // The coordinate is beyond the top-left map border. Move the coordinate down (in projected coordinates).
  //   *mapCoord += -mapCoord->x() * QPointF(1, -1);
  // }
  // if (mapCoord->y() >= height) {
  //   // The coordinate is beyond the top-right map border. Move the coordinate down (in projected coordinates).
  //   *mapCoord += -(mapCoord->y() - height + kClampMargin) * QPointF(1, -1);
  // }
  // if (mapCoord->y() < 0) {
  //   // The coordinate is beyond the bottom-left map border. Move the coordinate up (in projected coordinates)
  //   *mapCoord += -mapCoord->y() * QPointF(-1, 1);
  // }
  // if (mapCoord->x() >= width) {
  //   // The coordinate is beyond the bottom-right map border. Move the coordinate up (in projected coordinates)
  //   *mapCoord += -(mapCoord->x() - width + kClampMargin) * QPointF(-1, 1);
  // }
  
  // Safer approach: more straightforward clamping. The coordinate might not end up at the same projected x-coordinate,
  // but that does not concern us.
  *mapCoord = QPointF(
      std::max<double>(0, std::min<double>(width - kClampMargin, mapCoord->x())),
      std::max<double>(0, std::min<double>(height - kClampMargin, mapCoord->y())));
  
  // We use Gauss-Newton optimization (with coordinates clamped to the map) to do the search.
  // Note that we allow both coordinates to vary here, rather than constraining the movement
  // to be vertical, since this is easily possible, the performance difference should be completely
  // negligible, and it gives us a slightly more general implementation.
  bool converged = false;
  constexpr int kMaxNumIterations = 50;
  double lambda = 0;
  for (int iteration = 0; iteration < kMaxNumIterations; ++ iteration) {
    QPointF jacCol0;
    QPointF jacCol1;
    QPointF currentProjectedCoord = MapCoordToProjectedCoord(*mapCoord, &jacCol0, &jacCol1);
    QPointF residual = currentProjectedCoord - projectedCoord;
    double cost = residual.x() * residual.x() + residual.y() * residual.y();
    if (cost < 1e-8f) {
      converged = true;
      break;
    }
    
    bool foundAnUpdate = false;
    for (int lmIteration = 0; lmIteration < 8; ++ lmIteration) {
      // Compute update: - (H + lambda I)^(-1) b
      double H00 = jacCol0.x() * jacCol0.x() + jacCol0.y() * jacCol0.y() + lambda;
      double H01 = jacCol0.x() * jacCol1.x() + jacCol0.y() * jacCol1.y();  // = H10
      double H11 = jacCol1.x() * jacCol1.x() + jacCol1.y() * jacCol1.y() + lambda;
      
      double detH = H00 * H11 - H01 * H01;
      double detH_inv = 1.f / detH;
      double H00_inv = detH_inv * H11;
      double H01_inv = detH_inv * -H01;  // = H10_inv
      double H11_inv = detH_inv * H00;
      
      double b0 = - jacCol0.x() * residual.x() - jacCol0.y() * residual.y();
      double b1 = - jacCol1.x() * residual.x() - jacCol1.y() * residual.y();
      
      QPointF testMapCoord(
          mapCoord->x() + H00_inv * b0 + H01_inv * b1,
          mapCoord->y() + H01_inv * b0 + H11_inv * b1);
      // Clamp to map area.
      testMapCoord = QPointF(
          std::max<double>(0, std::min<double>(width - kClampMargin, testMapCoord.x())),
          std::max<double>(0, std::min<double>(height - kClampMargin, testMapCoord.y())));
      
      // Check if the update made progress.
      QPointF currentProjectedCoord = MapCoordToProjectedCoord(testMapCoord);
      QPointF testResidual = currentProjectedCoord - projectedCoord;
      double testCost = testResidual.x() * testResidual.x() + testResidual.y() * testResidual.y();
      if (testCost < cost) {
        *mapCoord = testMapCoord;
        lambda *= 0.5;
        foundAnUpdate = true;
        break;
      } else {
        if (lambda == 0) {
          lambda = 0.01f * 0.5f * (H00 + H11);
        } else {
          lambda *= 2;
        }
      }
    }
    
    if (!foundAnUpdate) {
      break;
    }
    
    // LOG(INFO) << "- currentMapCoordEstimate: " << mapCoord->x() << ", " << mapCoord->y();
  }
  // Debug output:
  // if (!converged) {
  //   QPointF currentProjectedCoord = MapCoordToProjectedCoord(*mapCoord);
  //   QPointF residual = currentProjectedCoord - projectedCoord;
  //   LOG(WARNING) << "NOT CONVERGED; residual.manhattanLength(): " << residual.manhattanLength();
  // }
  return converged;
}

void Map::UpdateFieldOfView(float centerMapCoordX, float centerMapCoordY, float radius, int change) {
  // TODO: We could cache the patterns for small discrete radius values to potentially speed this up.
  
  float effectiveRadius = radius + 0.7f;  // TODO: Find out what gives equal results as in the original game
  float effectiveRadiusSquared = effectiveRadius * effectiveRadius;
  
  int minX = std::max<int>(0, centerMapCoordX - effectiveRadius);
  int minY = std::max<int>(0, centerMapCoordY - effectiveRadius);
  int maxX = std::min<int>(width - 1, centerMapCoordX + effectiveRadius);
  int maxY = std::min<int>(height - 1, centerMapCoordY + effectiveRadius);
  
  float centerMapCoordXMinusHalf = centerMapCoordX - 0.5f;
  float centerMapCoordYMinusHalf = centerMapCoordY - 0.5f;
  
  for (int y = minY; y <= maxY; ++ y) {
    int* row = viewCount + width * y;
    for (int x = minX; x <= maxX; ++ x) {
      float dx = x - centerMapCoordXMinusHalf;
      float dy = y - centerMapCoordYMinusHalf;
      
      float squaredDistance = dx * dx + dy * dy;
      if (squaredDistance <= effectiveRadiusSquared) {
        if (row[x] == -1) {
          // Uncover a newly seen map tile
          row[x] = 0;
        }
        
        row[x] += change;
      }
    }
  }
  
  ViewCountChanged(minX, minY, maxX, maxY);
}

void Map::Render(float* viewMatrix, const std::filesystem::path& graphicsSubPath, QOpenGLFunctions_3_2_Core* f) {
  if (needsRenderResourcesUpdate) {
    UpdateRenderResources(graphicsSubPath, f);
    needsRenderResourcesUpdate = false;
  }
  if (viewCountChangeMaxX >= viewCountChangeMinX &&
      viewCountChangeMaxY >= viewCountChangeMinY) {
    UpdateViewCountTexture(f);
  }
  
  ShaderProgram* terrainProgram = terrainShader->GetProgram();
  terrainProgram->UseProgram(f);
  
  f->glUniform1i(terrainShader->GetTextureLocation(), 0);  // use GL_TEXTURE0
  f->glBindTexture(GL_TEXTURE_2D, textureId);
  
  f->glUniform1i(terrainShader->GetViewTextureLocation(), 1);  // use GL_TEXTURE1
  f->glActiveTexture(GL_TEXTURE0 + 1);
  f->glBindTexture(GL_TEXTURE_2D, viewTextureId);
  
  terrainProgram->SetUniformMatrix2fv(terrainShader->GetViewMatrixLocation(), viewMatrix, true, f);
  f->glUniform2f(terrainShader->GetTexcoordToMapScalingLocation(), 10.f / width, 10.f / height);
  
  f->glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
  f->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
  
  terrainProgram->SetPositionAttribute(
      2,
      GetGLType<float>::value,
      5 * sizeof(float),
      0,
      f);
  terrainProgram->SetTexCoordAttribute(
      3,
      GetGLType<float>::value,
      5 * sizeof(float),
      2 * sizeof(float),
      f);
  
  f->glDrawElements(GL_TRIANGLES, width * height * 6, GL_UNSIGNED_INT, 0);
  CHECK_OPENGL_NO_ERROR();
}

void Map::UnloadRenderResources() {
  if (hasTextureBeenLoaded) {
    QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
    f->glDeleteTextures(1, &textureId);
    hasTextureBeenLoaded = false;
  }
  if (haveGeometryBuffersBeenInitialized) {
    QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
    f->glDeleteBuffers(1, &vertexBuffer);
    f->glDeleteBuffers(1, &indexBuffer);
    haveGeometryBuffersBeenInitialized = false;
  }
}

void Map::AddObject(u32 objectId, ClientObject* object) {
  objects.insert(std::make_pair(objectId, object));
}

void Map::DeleteObject(u32 objectId) {
  auto it = objects.find(objectId);
  if (it == objects.end()) {
    LOG(ERROR) << "Cannot find to erase object id: " << objectId;
    return;
  }
  delete it->second;
  objects.erase(it);
}

bool Map::IsUnitInFogOfWar(ClientUnit* unit) {
  int tileX = std::max<int>(0, std::min<int>(width - 1, unit->GetMapCoord().x()));
  int tileY = std::max<int>(0, std::min<int>(height - 1, unit->GetMapCoord().y()));
  return viewCountAt(tileX, tileY) <= 0;
}

bool Map::IsBuildingInFogOfWar(ClientBuilding* building) {
  return ComputeMaxViewCountForBuilding(building) <= 0;
}

int Map::ComputeMaxViewCountForBuilding(ClientBuilding* building) {
  int maxViewCount = -1;
  const QPoint& baseTile = building->GetBaseTile();
  QSize size = building->GetStats().size;
  for (int y = 0; y < size.height(); ++ y) {
    for (int x = 0; x < size.width(); ++ x) {
      maxViewCount = std::max(maxViewCount, viewCountAt(baseTile.x() + x, baseTile.y() + y));
    }
  }
  return maxViewCount;
}

void Map::UpdateRenderResources(const std::filesystem::path& graphicsSubPath, QOpenGLFunctions_3_2_Core* f) {
  // Load texture
  if (!hasTextureBeenLoaded) {
    mango::Bitmap textureBitmap(
        GetModdedPath(graphicsSubPath.parent_path().parent_path() / "terrain" / "textures" / "2x" / "g_gr2.dds").string(),
        mango::Format(32, mango::Format::UNORM, mango::Format::BGRA, 8, 8, 8, 8));
    
    f->glGenTextures(1, &textureId);
    f->glBindTexture(GL_TEXTURE_2D, textureId);
    
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    
    f->glTexImage2D(
        GL_TEXTURE_2D,
        0, GL_RGBA,
        textureBitmap.width, textureBitmap.height,
        0, GL_BGRA, GL_UNSIGNED_BYTE,
        textureBitmap.address<u32>(0, 0));
    f->glGenerateMipmap(GL_TEXTURE_2D);
    
    CHECK_OPENGL_NO_ERROR();
    hasTextureBeenLoaded = true;
  }
  
  // Build geometry buffer
  if (haveGeometryBuffersBeenInitialized) {
    f->glDeleteBuffers(1, &vertexBuffer);
    f->glDeleteBuffers(1, &indexBuffer);
  }
  
  int elementSizeInBytes = 5 * sizeof(float);
  u8* data = new u8[(width + 1) * (height + 1) * elementSizeInBytes];
  u8* ptr = data;
  for (int y = 0; y <= height; ++ y) {
    for (int x = 0; x <= width; ++ x) {
      QPointF projectedCoord = TileCornerToProjectedCoord(x, y);
      
      // Estimate the vertex normal
      // TODO: This is quite messy, it would be nice to have a proper 3D vector class for this.
      float elevationHere = elevationAt(x, y);
      float topLeftHeight = (kTileProjectedElevationDifference / kTileDiagonalLength) * (elevationAt(std::max(0, x - 1), y) - elevationHere);
      float bottomRightHeight = (kTileProjectedElevationDifference / kTileDiagonalLength) * (elevationAt(std::min(width - 1, x + 1), y) - elevationHere);
      float bottomLeftHeight = (kTileProjectedElevationDifference / kTileDiagonalLength) * (elevationAt(x, std::max(0, y - 1)) - elevationHere);
      float topRightHeight = (kTileProjectedElevationDifference / kTileDiagonalLength) * (elevationAt(x, std::min(height - 1, y + 1)) - elevationHere);
      
      float normalX = topLeftHeight - bottomRightHeight;
      float normalY = bottomLeftHeight - topRightHeight;
      
      float normalLength = sqrtf(normalX * normalX + normalY * normalY + 1 * 1);
      normalX /= normalLength;
      normalY /= normalLength;
      float normalZ = 1 / normalLength;
      
      const float lightingDirectionX = 0.3f / sqrtf(0.3f * 0.3f + 0 * 0 + 0.8f * 0.8f);
      const float lightingDirectionY = 0.f;
      const float lightingDirectionZ = 0.8f / sqrtf(0.3f * 0.3f + 0 * 0 + 0.8f * 0.8f);
      
      float dot = normalX * lightingDirectionX + normalY * lightingDirectionY + normalZ * lightingDirectionZ;
      
      // Scale such that upright terrain gets a lighting factor of one
      float lightingFactor = dot / lightingDirectionZ;
      
      // Position
      *reinterpret_cast<float*>(ptr) = projectedCoord.x();
      ptr += sizeof(float);
      *reinterpret_cast<float*>(ptr) = projectedCoord.y();
      ptr += sizeof(float);
      
      // Texture coordinate
      *reinterpret_cast<float*>(ptr) = 0.1f * x;
      ptr += sizeof(float);
      *reinterpret_cast<float*>(ptr) = 0.1f * y;
      ptr += sizeof(float);
      
      // Darkening factor for map lighting
      // NOTE: This is passed on as part of the texture coordinates (for convenience)
      *reinterpret_cast<float*>(ptr) = lightingFactor;
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
      int horizontalDiff = std::abs(elevationAt(x, y) - elevationAt(x + 1, y + 1));
      int verticalDiff = std::abs(elevationAt(x + 1, y) - elevationAt(x, y + 1));
      
      // The special case was needed to make the elevation difference visible at all, since in this case,
      // the left, upper, and right vertex are all at the same y-coordinate in projected coordinates.
      bool specialCase = (horizontalDiff == 0) && ((elevationAt(x + 1, y) - elevationAt(x, y + 1)) == 1);
      if (horizontalDiff < verticalDiff && !specialCase) {
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
  
  haveGeometryBuffersBeenInitialized = true;
  
  terrainShader.reset(new TerrainShader());
  
  // TODO: Un-load the render resources again on destruction
}

void Map::UpdateViewCountTexture(QOpenGLFunctions_3_2_Core* f) {
  if (!haveViewTexture) {
    f->glGenTextures(1, &viewTextureId);
    f->glBindTexture(GL_TEXTURE_2D, viewTextureId);
    
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    
    f->glTexImage2D(
        GL_TEXTURE_2D,
        0, GL_RED,
        width, height,
        0, GL_RED, GL_UNSIGNED_BYTE,
        nullptr);
    
    CHECK_OPENGL_NO_ERROR();
    haveViewTexture = true;
  }
  
  int changeWidth = viewCountChangeMaxX - viewCountChangeMinX + 1;
  int changeHeight = viewCountChangeMaxY - viewCountChangeMinY + 1;
  
  u8* textureData = new u8[changeWidth * changeHeight];
  for (int y = 0; y < changeHeight; ++ y) {
    const int* viewCountRow = viewCount + (viewCountChangeMinY + y) * width;
    u8* textureDataRow = textureData + y * changeWidth;
    for (int x = 0; x < changeWidth; ++ x) {
      int viewCountValue = viewCountRow[viewCountChangeMinX + x];
      
      textureDataRow[x] = (viewCountValue == 0) ? 168 : ((viewCountValue > 0) ? 255 : 0);
    }
  }
  
  f->glBindTexture(GL_TEXTURE_2D, viewTextureId);
  f->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  f->glTexSubImage2D(
      GL_TEXTURE_2D,
    	0,
    	viewCountChangeMinX,
    	viewCountChangeMinY,
    	changeWidth,
    	changeHeight,
    	GL_RED,
    	GL_UNSIGNED_BYTE,
    	textureData);
  CHECK_OPENGL_NO_ERROR();
  
  delete[] textureData;
  
  viewCountChangeMinX = std::numeric_limits<int>::max();
  viewCountChangeMinY = std::numeric_limits<int>::max();
  viewCountChangeMaxX = -1;
  viewCountChangeMaxY = -1;
}
