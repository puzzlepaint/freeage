// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include <gtest/gtest.h>
#include <QApplication>

#include "FreeAge/common/logging.hpp"
#include "FreeAge/client/map.hpp"

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
  
  for (int y = 0; y < testMap.GetHeight(); ++ y) {
    for (int x = 0; x < testMap.GetWidth(); ++ x) {
      testMap.elevationAt(x, y) = rand() % 2;
    }
  }
  
  TestProjectedCoordToMapCoord(testMap);
}
