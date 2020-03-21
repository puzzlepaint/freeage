#pragma once

// Use glog-like syntax:
#define LOGURU_REPLACE_GLOG 1

#define LOGURU_EXPORT __declspec(dllimport)
#include "loguru/loguru.hpp"
