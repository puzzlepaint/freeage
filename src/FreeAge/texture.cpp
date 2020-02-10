#include "FreeAge/texture.h"

#include "FreeAge/opengl.h"

Texture::Texture() {
  textureId = -1;
}

Texture::~Texture() {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  f->glDeleteTextures(1, &textureId);
}

void Texture::Load(const QImage& image) {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  width = image.width();
  height = image.height();
  
  f->glGenTextures(1, &textureId);
  f->glBindTexture(GL_TEXTURE_2D, textureId);
  
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);  // TODO
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);  // TODO
  
  // TODO: Use this for 8-bit-per-pixel data
  // // OpenGL by default tries to read data in multiples of 4, if our data is
  // // only RGB or BGR and the width is not divible by 4 then we need to alert
  // // opengl
  // if((img->width % 4) != 0 && 
  //  (img->format == GL_RGB || 
  //   img->format == GL_BGR))
  // {
  //   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  // }
  
  // TODO: Don't assume RGBA format, check the QImage's format
  f->glTexImage2D(
      GL_TEXTURE_2D,
      0, GL_RGBA,
      image.width(), image.height(),
      0, GL_BGRA, GL_UNSIGNED_BYTE,
      image.scanLine(0));
  
  CHECK_OPENGL_NO_ERROR();
}
