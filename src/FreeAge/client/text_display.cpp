#include "FreeAge/client/text_display.hpp"

#include <cmath>

#include <QImage>
#include <QOpenGLFunctions_3_2_Core>
#include <QPainter>

TextDisplay::~TextDisplay() {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  if (textureInitialized) {
    f->glDeleteTextures(1, &textureId);
  }
}

void TextDisplay::Render(const QFont& font, const QRgb& color, const QString& text, const QRect& rect, int alignmentFlags, UIShader* uiShader, int widgetWidth, int widgetHeight, GLuint pointBuffer, QOpenGLFunctions_3_2_Core* f) {
  if (font != this->font ||
      color != this->color ||
      text != this->text ||
      alignmentFlags != this->alignmentFlags) {
    this->font = font;
    this->color = color;
    this->text = text;
    this->alignmentFlags = alignmentFlags;
    
    UpdateTexture(f);
  }
  
  // Render the texture.
  ShaderProgram* program = uiShader->GetProgram();
  program->UseProgram();
  program->SetUniform1i(uiShader->GetTextureLocation(), 0);  // use GL_TEXTURE0
  f->glBindTexture(GL_TEXTURE_2D, textureId);
  
  program->SetUniform2f(uiShader->GetTexTopLeftLocation(), 0, 0);
  program->SetUniform2f(uiShader->GetTexBottomRightLocation(), 1, 1);
  
  program->SetUniform2f(
      uiShader->GetSizeLocation(),
      2.f * textureWidth / static_cast<float>(widgetWidth),
      2.f * textureHeight / static_cast<float>(widgetHeight));
  
  float leftX;
  if (alignmentFlags & Qt::AlignLeft) {
    leftX = rect.x();
  } else if (alignmentFlags & Qt::AlignHCenter) {
    leftX = rect.x() + 0.5f * rect.width() - 0.5f * textureWidth;
  } else if (alignmentFlags & Qt::AlignRight) {
    leftX = rect.x() + rect.width() - textureWidth;
  } else {
    LOG(ERROR) << "Missing horizontal alignment for text rendering.";
    leftX = rect.x();
  }
  
  float topY;
  if (alignmentFlags & Qt::AlignTop) {
    topY = rect.y();
  } else if (alignmentFlags & Qt::AlignVCenter) {
    topY = rect.y() + 0.5f * rect.height() - 0.5f * textureHeight;
  } else if (alignmentFlags & Qt::AlignBottom) {
    topY = rect.y() + rect.height() - textureHeight;
  } else {
    LOG(ERROR) << "Missing vertical alignment for text rendering.";
    topY = rect.y();
  }
  
  bounds = QRect(leftX, topY, textureWidth, textureHeight);
  
  float data[] = {leftX, topY, 0.f};
  int elementSizeInBytes = 3 * sizeof(float);
  f->glBufferData(GL_ARRAY_BUFFER, 1 * elementSizeInBytes, data, GL_DYNAMIC_DRAW);
  program->SetPositionAttribute(
      3,
      GetGLType<float>::value,
      3 * sizeof(float),
      0);
  
  f->glDrawArrays(GL_POINTS, 0, 1);
  
  CHECK_OPENGL_NO_ERROR();
}

void TextDisplay::UpdateTexture(QOpenGLFunctions_3_2_Core* f) {
  // Compute the text size.
  // Note that we currently allocate a dummy image in order to get the correct font
  // metrics for drawing to QImages via a QPainter on that image. Is there a more efficient way to
  // get the correct font metrics?
  QImage dummyImage(1, 1, QImage::Format_RGBA8888);
  QPainter dummyPainter(&dummyImage);
  dummyPainter.setFont(font);
  QFontMetrics fontMetrics = dummyPainter.fontMetrics();
  QRect constrainRect(0, 0, 0, 0);
  QRect boundingRect = fontMetrics.boundingRect(constrainRect, alignmentFlags, text);
  textureWidth = std::ceil(boundingRect.width());
  textureHeight = std::ceil(boundingRect.height());
  dummyPainter.end();
  
  // Render the text into an image with that size.
  QImage textImage(textureWidth, textureHeight, QImage::Format_RGBA8888);
  textImage.fill(qRgba(0, 0, 0, 0));
  QPainter painter(&textImage);
  painter.setPen(color);
  painter.setFont(font);
  painter.fontMetrics();
  painter.drawText(textImage.rect(), alignmentFlags, text);
  painter.end();
  
  // Upload the rendered image to a texture.
  if (!textureInitialized) {
    f->glGenTextures(1, &textureId);
    f->glBindTexture(GL_TEXTURE_2D, textureId);
    
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    
    textureInitialized = true;
  } else {
    f->glBindTexture(GL_TEXTURE_2D, textureId);
  }
  
  f->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  f->glTexImage2D(
      GL_TEXTURE_2D,
      0, GL_RGBA,
      textImage.width(), textImage.height(),
      0, GL_BGRA, GL_UNSIGNED_BYTE,
      textImage.scanLine(0));
}
