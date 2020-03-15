#pragma once

#include <limits>

#include "FreeAge/common/free_age.hpp"

enum class ObjectType {
  Building = 0,
  Unit = 1
};

constexpr u32 kInvalidObjectId = std::numeric_limits<u32>::max();
