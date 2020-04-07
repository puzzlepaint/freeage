// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/client/shader_program.hpp"

#include <memory>

#include "FreeAge/common/logging.hpp"
#include "FreeAge/client/opengl.hpp"

ShaderProgram::ShaderProgram()
    : program_(0),
      vertex_shader_(0),
      geometry_shader_(0),
      fragment_shader_(0),
      position_attribute_location_(-1),
      color_attribute_location_(-1) {}

ShaderProgram::~ShaderProgram() {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  if (vertex_shader_ != 0) {
    f->glDetachShader(program_, vertex_shader_);
    f->glDeleteShader(vertex_shader_);
  }
  
  if (geometry_shader_ != 0) {
    f->glDetachShader(program_, geometry_shader_);
    f->glDeleteShader(geometry_shader_);
  }
  
  if (fragment_shader_ != 0) {
    f->glDetachShader(program_, fragment_shader_);
    f->glDeleteShader(fragment_shader_);
  }

  if (program_ != 0) {
    f->glDeleteProgram(program_);
  }
}

bool ShaderProgram::AttachShader(const char* source_code, ShaderType type, QOpenGLFunctions_3_2_Core* f) {
  CHECK(program_ == 0) << "Cannot attach a shader after linking the program.";
  
  GLenum shader_enum;
  GLuint* shader_name = nullptr;
  if (type == ShaderType::kVertexShader) {
    shader_enum = GL_VERTEX_SHADER;
    shader_name = &vertex_shader_;
  } else if (type == ShaderType::kGeometryShader) {
    shader_enum = GL_GEOMETRY_SHADER;
    shader_name = &geometry_shader_;
  } else if (type == ShaderType::kFragmentShader) {
    shader_enum = GL_FRAGMENT_SHADER;
    shader_name = &fragment_shader_;
  }
  CHECK(shader_name != nullptr) << "Unknown shader type.";
  
  *shader_name = f->glCreateShader(shader_enum);
  const GLchar* source_code_ptr =
      static_cast<const GLchar*>(source_code);
  f->glShaderSource(*shader_name, 1, &source_code_ptr, NULL);
  f->glCompileShader(*shader_name);

  GLint compiled;
  f->glGetShaderiv(*shader_name, GL_COMPILE_STATUS, &compiled);
  if (!compiled) {
    GLint length;
    f->glGetShaderiv(*shader_name, GL_INFO_LOG_LENGTH, &length);
    std::unique_ptr<GLchar[]> log(
        reinterpret_cast<GLchar*>(new uint8_t[length]));
    f->glGetShaderInfoLog(*shader_name, length, &length, log.get());
    LOG(ERROR) << "GL Shader Compilation Error: " << log.get();
    return false;
  }
  return true;
}

bool ShaderProgram::LinkProgram(QOpenGLFunctions_3_2_Core* f) {
  CHECK(program_ == 0) << "Program already linked.";
  
  program_ = f->glCreateProgram();
  if (fragment_shader_ != 0) {
    f->glAttachShader(program_, fragment_shader_);
  }
  if (geometry_shader_ != 0) {
    f->glAttachShader(program_, geometry_shader_);
  }
  if (vertex_shader_ != 0) {
    f->glAttachShader(program_, vertex_shader_);
  }
  f->glLinkProgram(program_);
  
  GLint linked;
  f->glGetProgramiv(program_, GL_LINK_STATUS, &linked);
  if (!linked) {
    GLint length;
    f->glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &length);
    std::unique_ptr<GLchar[]> log(
        reinterpret_cast<GLchar*>(new uint8_t[length]));
    f->glGetProgramInfoLog(program_, length, &length, log.get());
    LOG(ERROR) << "GL Program Linker Error: " << log.get();
    return false;
  }
  
  // Get attributes.
  position_attribute_location_ = f->glGetAttribLocation(program_, "in_position");
  color_attribute_location_ = f->glGetAttribLocation(program_, "in_color");
  texcoord_attribute_location_ = f->glGetAttribLocation(program_, "in_texcoord");
  return true;
}

void ShaderProgram::UseProgram(QOpenGLFunctions_3_2_Core* f) const {
  f->glUseProgram(program_);
}

GLint ShaderProgram::GetUniformLocation(const char* name, QOpenGLFunctions_3_2_Core* f) const {
  return f->glGetUniformLocation(program_, name);
}

GLint ShaderProgram::GetUniformLocationOrAbort(const char* name, QOpenGLFunctions_3_2_Core* f) const {
  GLint result = f->glGetUniformLocation(program_, name);
  if (result == -1) {
    LOG(WARNING) << "Uniform does not exist (might have been optimized out by the compiler): " << name;
  }
  return result;
}

void ShaderProgram::SetUniformMatrix2fv(GLint location, float* values, bool valuesAreColumnMajor, QOpenGLFunctions_3_2_Core* f) {
  f->glUniformMatrix2fv(location, 1, valuesAreColumnMajor ? GL_FALSE : GL_TRUE, values);
}

void ShaderProgram::SetPositionAttribute(int component_count, GLenum component_type, GLsizei stride, std::size_t offset, QOpenGLFunctions_3_2_Core* f) {
  // CHECK(position_attribute_location_ != -1) << "SetPositionAttribute() called, but no attribute \"in_position\" found.";
  if (position_attribute_location_ == -1) {
    // Allow using an object with positions with a material that ignores the positions.
    return;
  }
  
  f->glEnableVertexAttribArray(position_attribute_location_);
  f->glVertexAttribPointer(
      position_attribute_location_,
      component_count,
      component_type,
      GL_FALSE,  // Whether fixed-point data values should be normalized.
      stride,
      reinterpret_cast<void*>(offset));
  CHECK_OPENGL_NO_ERROR();
}

void ShaderProgram::SetColorAttribute(int component_count, GLenum component_type, GLsizei stride, std::size_t offset, QOpenGLFunctions_3_2_Core* f) {
  // CHECK(color_attribute_location_ != -1) << "SetColorAttribute() called, but no attribute \"in_color\" found.";
  if (color_attribute_location_ == -1) {
    // Allow using an object with colors with a material that ignores the colors.
    return;
  }
  
  f->glEnableVertexAttribArray(color_attribute_location_);
  f->glVertexAttribPointer(
      color_attribute_location_,
      component_count,
      component_type,
      GL_TRUE,  // Whether fixed-point data values should be normalized.
      stride,
      reinterpret_cast<void*>(offset));
  CHECK_OPENGL_NO_ERROR();
}

void ShaderProgram::SetTexCoordAttribute(int component_count, GLenum component_type, GLsizei stride, std::size_t offset, QOpenGLFunctions_3_2_Core* f) {
  // CHECK(position_attribute_location_ != -1) << "SetTexCoordAttribute() called, but no attribute \"in_position\" found.";
  if (texcoord_attribute_location_ == -1) {
    // Allow using an object with positions with a material that ignores the positions.
    return;
  }
  
  f->glEnableVertexAttribArray(texcoord_attribute_location_);
  f->glVertexAttribPointer(
      texcoord_attribute_location_,
      component_count,
      component_type,
      GL_FALSE,  // Whether fixed-point data values should be normalized.
      stride,
      reinterpret_cast<void*>(offset));
  CHECK_OPENGL_NO_ERROR();
}
