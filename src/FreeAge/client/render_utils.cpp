// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/client/render_utils.hpp"

#include "FreeAge/client/mod_manager.hpp"
#include "FreeAge/client/render_window.hpp"

TextureAndPointBuffer::~TextureAndPointBuffer() {
  if (texture != nullptr) {
    LOG(ERROR) << "TextureAndPointBuffer object was destroyed without Unload() being called first.";
  }
}

bool TextureAndPointBuffer::Load(const std::filesystem::path& path, QImage* qimage, TextureManager::Loader loader) {
  if (texture != nullptr) {
    LOG(ERROR) << "Load() called on already initialized TextureAndPointBuffer";
    return false;
  }
  
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  // Load the texture.
  texture.reset(new Texture());
  if (loader == TextureManager::Loader::QImage) {
    QImage image(path.string().c_str());
    if (image.isNull()) {
      LOG(ERROR) << "Failed to load image: " << path.string();
      return false;
    }
    if (qimage) {
      *qimage = image;
    }
    texture->Load(image, GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR);
  } else {
    CHECK(qimage == nullptr);
    texture->Load(path, GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR);
  }
  
  // Initialize the point buffer.
  f->glGenBuffers(1, &pointBuffer);
  f->glBindBuffer(GL_ARRAY_BUFFER, pointBuffer);
  int elementSizeInBytes = 3 * sizeof(float);
  f->glBufferData(GL_ARRAY_BUFFER, 1 * elementSizeInBytes, nullptr, GL_STREAM_DRAW);
  
  return true;
}

void TextureAndPointBuffer::Unload() {
  if (texture) {
    QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
    
    f->glDeleteBuffers(1, &pointBuffer);
    texture.reset();
  }
}


TextDisplayAndPointBuffer::~TextDisplayAndPointBuffer() {
  if (textDisplay) {
    LOG(ERROR) << "TextDisplayAndPointBuffer object was destroyed without Destroy() being called first.";
  }
}

void TextDisplayAndPointBuffer::Initialize() {
  if (textDisplay) {
    LOG(ERROR) << "Initialize() called on already initialized TextDisplayAndPointBuffer";
    return;
  }
  
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  f->glGenBuffers(1, &pointBuffer);
  f->glBindBuffer(GL_ARRAY_BUFFER, pointBuffer);
  int elementSizeInBytes = 3 * sizeof(float);
  f->glBufferData(GL_ARRAY_BUFFER, 1 * elementSizeInBytes, nullptr, GL_STREAM_DRAW);
  
  textDisplay.reset(new TextDisplay());
}

void TextDisplayAndPointBuffer::Destroy() {
  if (textDisplay) {
    QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
    
    f->glDeleteBuffers(1, &pointBuffer);
    
    textDisplay.reset();
  }
}


PointBuffer::~PointBuffer() {
  if (initialized) {
    LOG(ERROR) << "PointBuffer object was destroyed without Destroy() being called first.";
  }
}

void PointBuffer::Initialize() {
  if (initialized) {
    LOG(ERROR) << "Initialize() called on already initialized PointBuffer";
    return;
  }
  
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  f->glGenBuffers(1, &buffer);
  f->glBindBuffer(GL_ARRAY_BUFFER, buffer);
  int elementSizeInBytes = 3 * sizeof(float);
  f->glBufferData(GL_ARRAY_BUFFER, 1 * elementSizeInBytes, nullptr, GL_STREAM_DRAW);
  
  initialized = true;
}

void PointBuffer::Destroy() {
  if (initialized) {
    QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
    
    f->glDeleteBuffers(1, &buffer);
    
    initialized = false;
  }
}


void Button::Load(const std::filesystem::path& defaultSubPath, const std::filesystem::path& hoverSubPath, const std::filesystem::path& activeSubPath, const std::filesystem::path& disabledSubPath) {
  pointBuffer.Initialize();
  
  defaultTexture.reset(new Texture());
  QImage image(GetModdedPathAsQString(defaultSubPath));
  opaquenessMap.Create(image);
  defaultTexture->Load(image, GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR);
  
  hoverTexture.reset(new Texture());
  hoverTexture->Load(QImage(GetModdedPathAsQString(hoverSubPath)), GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR);
  
  activeTexture.reset(new Texture());
  activeTexture->Load(QImage(GetModdedPathAsQString(activeSubPath)), GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR);
  
  if (!disabledSubPath.empty()) {
    disabledTexture.reset(new Texture());
    disabledTexture->Load(QImage(GetModdedPathAsQString(disabledSubPath)), GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR);
  }
}

void Button::Render(float x, float y, float width, float height, RenderWindow* renderWindow, QOpenGLFunctions_3_2_Core* f) {
  lastX = x;
  lastY = y;
  lastWidth = width;
  lastHeight = height;
  
  Texture* menuButtonTex = (state == 3) ? disabledTexture.get() : ((state == 2) ? activeTexture.get() : ((state == 1) ? hoverTexture.get() : defaultTexture.get()));
  RenderUIGraphic(
      x,
      y,
      width,
      height,
      qRgba(255, 255, 255, 255), pointBuffer.buffer,
      *menuButtonTex,
      renderWindow->uiShader.get(), renderWindow->widgetWidth, renderWindow->widgetHeight, f);
}

void Button::MouseMove(const QPoint& pos) {
  if (state == 3) {
    return;
  }
  
  if (IsInButton(pos)) {
    if (state == 0) {
      state = 1;
    }
  } else {
    state = 0;
  }
}

void Button::MousePress(const QPoint& pos) {
  if (state == 3) {
    return;
  }
  
  if (IsInButton(pos)) {
    state = 2;
  }
}

bool Button::MouseRelease(const QPoint& pos) {
  if (state == 3) {
    return false;
  }
  
  if (IsInButton(pos)) {
    bool clicked = state == 2;
    state = 1;
    return clicked;
  }
  
  state = 0;
  return false;
}

void Button::SetEnabled(bool enabled) {
  if (enabled) {
    if (state == 3) {
      state = 0;
    }
  } else {
    state = 3;
  }
}

bool Button::IsInButton(const QPoint& pos) {
  if (pos.x() >= lastX && pos.y() >= lastY &&
      pos.x() < lastX + lastWidth && pos.y() < lastY + lastHeight) {
    int ix = (pos.x() - lastX) * defaultTexture->GetWidth() / lastWidth;
    int iy = (pos.y() - lastY) * defaultTexture->GetHeight() / lastHeight;
    if (opaquenessMap.IsOpaque(ix, iy)) {
      return true;
    }
  }
  
  return false;
}

void Button::Destroy() {
  pointBuffer.Destroy();
  defaultTexture.reset();
  hoverTexture.reset();
  activeTexture.reset();
  disabledTexture.reset();
}
