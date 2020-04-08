// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/client/shader_ui_single_color_fullscreen.hpp"

#include "FreeAge/common/logging.hpp"
#include "FreeAge/client/opengl.hpp"

UISingleColorFullscreenShader::UISingleColorFullscreenShader() {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  program.reset(new ShaderProgram());
  
  // This generates vertex positions directly in the vertex shader.
  CHECK(program->AttachShader(
      "#version 330 core\n"
      "\n"
      "void main() {\n"
      " vec2 vTexCoord = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
      " gl_Position = vec4( vTexCoord * 2.0 - 1.0, 0.0, 1.0);\n"
      "}\n",
      ShaderProgram::ShaderType::kVertexShader, f));
  
  CHECK(program->AttachShader(
      "#version 330 core\n"
      "layout(location = 0) out vec4 out_color;\n"
      "\n"
      "uniform vec4 u_color;\n"
      "\n"
      "void main() {\n"
      "  out_color = u_color;\n"
      "}\n",
      ShaderProgram::ShaderType::kFragmentShader, f));
  
  CHECK(program->LinkProgram(f));
  
  program->UseProgram(f);
  
  color_location = program->GetUniformLocationOrAbort("u_color", f);
}

UISingleColorFullscreenShader::~UISingleColorFullscreenShader() {
  program.reset();
}
