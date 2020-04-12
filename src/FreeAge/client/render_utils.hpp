// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <filesystem>

#include "FreeAge/client/opaqueness_map.hpp"
#include "FreeAge/client/opengl.hpp"
#include "FreeAge/client/text_display.hpp"
#include "FreeAge/client/texture.hpp"

class RenderWindow;

struct TextureAndPointBuffer {
  ~TextureAndPointBuffer();
  
  bool Load(const std::filesystem::path& path, QImage* qimage = nullptr, TextureManager::Loader loader = TextureManager::Loader::QImage);
  void Unload();
  
  GLuint pointBuffer;
  std::shared_ptr<Texture> texture;
};

struct TextDisplayAndPointBuffer {
  ~TextDisplayAndPointBuffer();
  
  void Initialize();
  void Destroy();
  
  GLuint pointBuffer;
  std::shared_ptr<TextDisplay> textDisplay;
};

struct PointBuffer {
  ~PointBuffer();
  
  void Initialize();
  void Destroy();
  
  GLuint buffer;
  bool initialized = false;
};

struct Button {
  void Load(const std::filesystem::path& defaultSubPath, const std::filesystem::path& hoverSubPath, const std::filesystem::path& activeSubPath, const std::filesystem::path& disabledSubPath);
  void Render(float x, float y, float width, float height, RenderWindow* renderWindow, QOpenGLFunctions_3_2_Core* f);
  void MouseMove(const QPoint& pos);
  void MousePress(const QPoint& pos);
  /// Returns true if the button was clicked.
  bool MouseRelease(const QPoint& pos);
  void SetEnabled(bool enabled);
  bool IsInButton(const QPoint& pos);
  void Destroy();
  
  PointBuffer pointBuffer;
  OpaquenessMap opaquenessMap;
  std::shared_ptr<Texture> defaultTexture;
  std::shared_ptr<Texture> hoverTexture;
  std::shared_ptr<Texture> activeTexture;
  std::shared_ptr<Texture> disabledTexture;
  
  float lastX = -1;
  float lastY = -1;
  float lastWidth = -1;
  float lastHeight = -1;
  
  int state = 0;  // 0: default, 1: hover, 2: active, 3: disabled
};
