// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <chrono>
#include <stddef.h>
#include <stdint.h>

// This file contains common definitions that should be available globally.

// Type definitions which are more concise and thus easier to read and write
// compared to the standard ones. int is used as-is.
typedef size_t usize;
typedef int64_t i64;
typedef uint64_t u64;
typedef int32_t i32;
typedef uint32_t u32;
typedef int16_t i16;
typedef uint16_t u16;
typedef int8_t i8;
typedef uint8_t u8;

/// Shorthands for the used time types
typedef std::chrono::steady_clock Clock;
typedef Clock::time_point TimePoint;
typedef std::chrono::duration<double, std::milli> MillisecondsDuration;
typedef std::chrono::duration<double> SecondsDuration;

/// Port used on the server.
static constexpr u16 serverPort = 49100;  // TODO: Make configurable

/// Factor on the game speed. 1.7 is the default for competitive games, 1.5 was used in the HD edition, 2.0 is used for Deathmatch.
static constexpr float gameSpeedFactor = 1.7f;

/// Frames per second for playing back sprite animations.
static constexpr float animationFramesPerSecond = gameSpeedFactor * 30;

/// Default map size for newly created server instances.
static constexpr int kDefaultMapSize = 75;

/// Player index for "gaia" objects (resources, animals, etc.)
static constexpr int kGaiaPlayerIndex = 255;

/// The maximum capacity of production queues in buildings.
static constexpr int kMaxProductionQueueSize = 11;

// The M_PI definition might be missing sometimes, so ensure that it is present.
#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif
