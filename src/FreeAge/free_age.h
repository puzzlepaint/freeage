#pragma once

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

// TODO: This is not a good place for this function, but it is only temporary anyway as the approach should be replaced:
// TODO: The hue offsetting approach to set the player color does not seem to work well:
//       Mixed color pixels are included in the player color pixels, which get wrong
//       colors when changed. Better approach: Load all the player color palettes into
//       a texture, write the palette index into the player color pixels, and look up the palette values.
inline float GetHueOffsetForPlayer(int playerIndex) {
  return (playerIndex == 0) ? 0 : 0.4f;
}
