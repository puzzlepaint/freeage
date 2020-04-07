// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

// Use glog-like syntax:
#define LOGURU_REPLACE_GLOG 1

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
  #define LOGURU_EXPORT __declspec(dllimport)
#endif
#include "loguru/loguru.hpp"
