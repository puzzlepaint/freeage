#pragma once

#include <QOpenGLFunctions>

#include "FreeAge/free_age.h"
#include "FreeAge/logging.h"

std::string GetGLErrorName(GLenum error_code);

std::string GetGLErrorDescription(GLenum error_code);

// This uses a macro such that LOG(ERROR) picks up the correct file and line
// number.
#define CHECK_OPENGL_NO_ERROR() \
  do { \
    GLenum error = f->glGetError(); \
    if (error == GL_NO_ERROR) {  \
      break; \
    } \
    LOG(ERROR) << "OpenGL Error: " << GetGLErrorName(error) << " (" << error << "), description:" << std::endl << GetGLErrorDescription(error); \
  } while (true)

// Traits type allowing to get the OpenGL enum value for a given type as:
// GetGLType<Type>::value. For example, GetGLType<int>::value resoves to GL_INT.
// Not applicable for: GL_HALF_FLOAT, GL_FIXED, GL_INT_2_10_10_10_REV,
// GL_UNSIGNED_INT_2_10_10_10_REV, GL_UNSIGNED_INT_10F_11F_11F_REV.
template <typename T> struct GetGLType {
  // Default value for unknown type.
  static const GLenum value = GL_FLOAT;
};

template<> struct GetGLType<i8> {
  static const GLenum value = GL_BYTE;
};

template<> struct GetGLType<u8> {
  static const GLenum value = GL_UNSIGNED_BYTE;
};

template<> struct GetGLType<i16> {
  static const GLenum value = GL_SHORT;
};

template<> struct GetGLType<u16> {
  static const GLenum value = GL_UNSIGNED_SHORT;
};

template<> struct GetGLType<int> {
  static const GLenum value = GL_INT;
};

template<> struct GetGLType<u32> {
  static const GLenum value = GL_UNSIGNED_INT;
};

template<> struct GetGLType<float> {
  static const GLenum value = GL_FLOAT;
};

template<> struct GetGLType<double> {
  static const GLenum value = GL_DOUBLE;
};
