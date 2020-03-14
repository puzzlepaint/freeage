#include "FreeAge/client/shader_ui_single_color.hpp"

#include "FreeAge/common/logging.hpp"
#include "FreeAge/client/opengl.hpp"

UISingleColorShader::UISingleColorShader() {
  program.reset(new ShaderProgram());
  
  CHECK(program->AttachShader(
      "#version 330 core\n"
      "in vec3 in_position;\n"
      "uniform mat2 u_viewMatrix;\n"
      "void main() {\n"
      "  gl_Position = vec4(u_viewMatrix[0][0] * in_position.x + u_viewMatrix[1][0], u_viewMatrix[0][1] * in_position.y + u_viewMatrix[1][1], in_position.z, 1);\n"
      "}\n",
      ShaderProgram::ShaderType::kVertexShader));
  
  CHECK(program->AttachShader(
      "#version 330 core\n"
      "layout(location = 0) out vec4 out_color;\n"
      "\n"
      "uniform vec4 u_color;\n"
      "\n"
      "void main() {\n"
      "  out_color = u_color;\n"
      "}\n",
      ShaderProgram::ShaderType::kFragmentShader));
  
  CHECK(program->LinkProgram());
  
  program->UseProgram();
  
  viewMatrix_location = program->GetUniformLocationOrAbort("u_viewMatrix");
  color_location = program->GetUniformLocationOrAbort("u_color");
}

UISingleColorShader::~UISingleColorShader() {
  program.reset();
}
