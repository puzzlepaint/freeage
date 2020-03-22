#pragma once

// Use glog-like syntax:
#define LOGURU_REPLACE_GLOG 1

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
  #define LOGURU_EXPORT __declspec(dllimport)
#endif
#include "loguru/loguru.hpp"
