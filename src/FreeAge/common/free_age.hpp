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

/// Player index for "gaia" objects (resources, animals, etc.)
static constexpr int kGaiaPlayerIndex = -1;
