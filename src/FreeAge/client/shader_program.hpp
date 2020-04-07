// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#pragma once

#include <QOpenGLFunctions_3_2_Core>

// Represents a shader program. At least a fragment and a vertex shader must be
// attached to a program to be complete. This class assumes some common
// attribute names in the shaders to simplify attribute handling:
// 
// in_position : Position input to the vertex shader.
// in_color    : Color input to the vertex shader.
// in_texcoord : Texture coordinate input to the vertex shader.
//
// A current OpenGL context is required for calling each member function except
// the constructor. This includes the destructor.
class ShaderProgram {
 public:
  enum class ShaderType {
    kVertexShader   = 1 << 0,
    kGeometryShader = 1 << 1,
    kFragmentShader = 1 << 2
  };
  
  // No-op constructor, no OpenGL context required.
  ShaderProgram();
  
  // Deletes the program. Attention: Requires a current OpenGL context for
  // this thread! You may need to explicitly delete this object at a point where
  // such a context still exists.
  ~ShaderProgram();
  
  // Attaches a shader to the program. Returns false if the shader does not
  // compile.
  bool AttachShader(const char* source_code, ShaderType type, QOpenGLFunctions_3_2_Core* f);
  
  // Links the program. Must be called after all shaders have been attached.
  // Returns true if successful.
  bool LinkProgram(QOpenGLFunctions_3_2_Core* f);
  
  // Makes this program the active program (calls glUseProgram()).
  void UseProgram(QOpenGLFunctions_3_2_Core* f) const;
  
  // Returns the location of the given uniform. If the uniform name does not
  // exist, returns -1.
  GLint GetUniformLocation(const char* name, QOpenGLFunctions_3_2_Core* f) const;
  
  // Same as GetUniformLocation(), but aborts the program if the uniform does
  // not exist.
  GLint GetUniformLocationOrAbort(const char* name, QOpenGLFunctions_3_2_Core* f) const;
  
  
  // Uniform setters.
  void SetUniformMatrix2fv(GLint location, float* values, bool valuesAreColumnMajor, QOpenGLFunctions_3_2_Core* f);
  
  
  // Attribute setters.
  void SetPositionAttribute(int component_count, GLenum component_type, GLsizei stride, std::size_t offset, QOpenGLFunctions_3_2_Core* f);
  void SetColorAttribute(int component_count, GLenum component_type, GLsizei stride, std::size_t offset, QOpenGLFunctions_3_2_Core* f);
  void SetTexCoordAttribute(int component_count, GLenum component_type, GLsizei stride, std::size_t offset, QOpenGLFunctions_3_2_Core* f);
  
  
  inline GLuint program_name() const { return program_; }
  
 private:
  // OpenGL name of the program. This is zero if the program has not been
  // successfully linked yet.
  GLuint program_;
  
  // OpenGL names of the shaders attached to the program. These are zero if not
  // attached.
  GLuint vertex_shader_;
  GLuint geometry_shader_;
  GLuint fragment_shader_;
  
  // Attribute locations. These are -1 if no attribute with the common name
  // exists.
  GLint position_attribute_location_;
  GLint color_attribute_location_;
  GLint texcoord_attribute_location_;
};
