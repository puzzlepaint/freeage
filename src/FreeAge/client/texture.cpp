#include "FreeAge/client/texture.hpp"

#include <mango/image/image.hpp>

#include "FreeAge/client/opengl.hpp"

Texture::~Texture() {
  if (width != -1) {
    QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
    f->glDeleteTextures(1, &textureId);
  }
}

void Texture::Load(const QImage& image, int wrapMode, int magFilter, int minFilter) {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  width = image.width();
  height = image.height();
  
  f->glGenTextures(1, &textureId);
  f->glBindTexture(GL_TEXTURE_2D, textureId);
  
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapMode);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapMode);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
  
  // QImage scan lines are aligned to multiples of 4 bytes. Ensure that OpenGL reads this correctly.
  f->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  
  if (image.format() == QImage::Format_ARGB32) {
    f->glTexImage2D(
        GL_TEXTURE_2D,
        0, GL_RGBA,
        image.width(), image.height(),
        0, GL_BGRA, GL_UNSIGNED_BYTE,
        image.scanLine(0));
  } else if (image.format() == QImage::Format_Grayscale8) {
    f->glTexImage2D(
        GL_TEXTURE_2D,
        0, GL_RED,
        image.width(), image.height(),
        0, GL_RED, GL_UNSIGNED_BYTE,
        image.scanLine(0));
  } else {
    LOG(FATAL) << "Unsupported QImage format.";
  }
  
  CHECK_OPENGL_NO_ERROR();
}

bool Texture::Load(const std::filesystem::path& path, int wrapMode, int magFilter, int minFilter) {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  mango::Bitmap bitmap(path.string(), mango::Format(32, mango::Format::UNORM, mango::Format::BGRA, 8, 8, 8, 8));
  if (bitmap.width <= 0) {
    return false;
  }
  
  width = bitmap.width;
  height = bitmap.height;
  
  f->glGenTextures(1, &textureId);
  f->glBindTexture(GL_TEXTURE_2D, textureId);
  
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapMode);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapMode);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
  
  // QImage scan lines are aligned to multiples of 4 bytes. Ensure that OpenGL reads this correctly.
  f->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  
  f->glTexImage2D(
      GL_TEXTURE_2D,
      0, GL_RGBA,
      bitmap.width, bitmap.height,
      0, GL_BGRA, GL_UNSIGNED_BYTE,
      bitmap.address<u32>(0, 0));
  
  CHECK_OPENGL_NO_ERROR();
  return true;
}
