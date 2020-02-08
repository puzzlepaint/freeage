#include <gtest/gtest.h>
#include <QApplication>

#include "FreeAge/logging.h"
#include "FreeAge/map.h"

int main(int argc, char** argv) {
  // Initialize loguru
  loguru::g_preamble_date = false;
  loguru::g_preamble_thread = false;
  loguru::g_preamble_uptime = false;
  loguru::g_stderr_verbosity = 2;
  if (argc > 0) {
    loguru::init(argc, argv, /*verbosity_flag*/ nullptr);
  }
  
  // Initialize GoogleTest
  ::testing::InitGoogleTest(&argc, argv);
  
  // Initialize Qt
  QApplication qapp(argc, argv);
  
  // Run the tests
  return RUN_ALL_TESTS();
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

TEST(Map, DetermineInterpolationCoordinates) {
  QPointF interpolationCoords(0.4f, 0.6f);
  QPointF topLeft(0.1f, 0.2f);
  QPointF topRight(1.2f, 0.1f);
  QPointF bottomLeft(0.3f, 1.5f);
  QPointF bottomRight(2.4f, 2.6f);
  
  QPointF interpolated = InterpolateVectorBilinearly(
      interpolationCoords.x(),
      interpolationCoords.y(),
      topLeft,
      topRight,
      bottomLeft,
      bottomRight);
  
  QPointF interpolationCoords2 = DetermineInterpolationCoordinates(
      topLeft,
      topRight,
      bottomLeft,
      bottomRight,
      interpolated);
  
  EXPECT_NEAR(interpolationCoords.x(), interpolationCoords2.x(), 1e-3f);
  EXPECT_NEAR(interpolationCoords.y(), interpolationCoords2.y(), 1e-3f);
}

static void TestProjectedCoordToMapCoord(const Map& map) {
  constexpr bool kDebug = false;
  constexpr int kNumTests = 10;
  for (int test = 0; test < kNumTests; ++ test) {
    QPointF mapCoord(
        map.GetWidth() * ((rand() % 100000) / static_cast<float>(100000 - 1)),
        map.GetHeight() * ((rand() % 100000) / static_cast<float>(100000 - 1)));
    if (kDebug) {
      LOG(INFO) << "-- Test " << test << " --";
      LOG(INFO) << "mapCoord: " << mapCoord.x() << ", " << mapCoord.y();
    }
    QPointF projectedCoord = map.MapCoordToProjectedCoord(mapCoord);
    if (kDebug) {
      LOG(INFO) << "projectedCoord: " << projectedCoord.x() << ", " << projectedCoord.y();
    }
    QPointF mapCoord2;
    bool ok = map.ProjectedCoordToMapCoord(projectedCoord, &mapCoord2);
    EXPECT_TRUE(ok);
    if (ok) {
      EXPECT_NEAR(mapCoord.x(), mapCoord2.x(), 1e-3f);
      EXPECT_NEAR(mapCoord.y(), mapCoord2.y(), 1e-3f);
    }
  }
}

TEST(Map, CoordinateConversion_FlatMap) {
  srand(0);
  
  constexpr int kMapWidth = 15;
  constexpr int kMapHeight = 15;
  Map testMap(kMapWidth, kMapHeight);
  
  TestProjectedCoordToMapCoord(testMap);
}

TEST(Map, CoordinateConversion_HillyMap) {
  srand(0);
  
  constexpr int kMapWidth = 15;
  constexpr int kMapHeight = 15;
  Map testMap(kMapWidth, kMapHeight);
  testMap.GenerateRandomMap();
  
  TestProjectedCoordToMapCoord(testMap);
}
