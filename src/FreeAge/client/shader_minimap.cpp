// Copyright 2020 The FreeAge authors
// This file is part of FreeAge, licensed under the new BSD license.
// See the COPYING file in the project root for the license text.

#include "FreeAge/client/shader_minimap.hpp"

#include "FreeAge/common/logging.hpp"
#include "FreeAge/client/opengl.hpp"

MinimapShader::MinimapShader() {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  program.reset(new ShaderProgram());
  
  CHECK(program->AttachShader(
      "#version 330 core\n"
      "in vec2 in_position;\n"
      "in vec2 in_texcoord;\n"
      "uniform mat2 u_viewMatrix;\n"
      "out vec2 texcoord;\n"
      "void main() {\n"
      "  gl_Position = vec4(u_viewMatrix[0][0] * in_position.x + u_viewMatrix[1][0], u_viewMatrix[0][1] * in_position.y + u_viewMatrix[1][1], 0, 1);\n"
      "  texcoord = in_texcoord;\n"
      "}\n",
      ShaderProgram::ShaderType::kVertexShader, f));
  
  CHECK(program->AttachShader(
      "#version 330 core\n"
      "layout(location = 0) out vec4 out_color;\n"
      "\n"
      "in vec2 texcoord;\n"
      "\n"
      "uniform sampler2D u_texture;\n"
      "\n"
      "void main() {\n"
      "  out_color = texture(u_texture, texcoord.xy);\n"
      "}\n",
      ShaderProgram::ShaderType::kFragmentShader, f));
  
  CHECK(program->LinkProgram(f));
  
  program->UseProgram(f);
  
  texture_location = program->GetUniformLocationOrAbort("u_texture", f);
  viewMatrix_location = program->GetUniformLocationOrAbort("u_viewMatrix", f);
}

MinimapShader::~MinimapShader() {
  program.reset();
}
