#include "FreeAge/client/shader_health_bar.hpp"

#include "FreeAge/common/logging.hpp"

HealthBarShader::HealthBarShader() {
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
      "#extension GL_EXT_geometry_shader : enable\n"
      "layout(points) in;\n"
      "layout(triangle_strip, max_vertices = 4) out;\n"
      "\n"
      "uniform vec2 u_size;\n"
      "\n"
      "out vec2 texcoord;\n"
      "\n"
      "void main() {\n"
      "  gl_Position = vec4(gl_in[0].gl_Position.x, gl_in[0].gl_Position.y, gl_in[0].gl_Position.z, 1.0);\n"
      "  texcoord = vec2(0, 0);\n"
      "  EmitVertex();\n"
      "  gl_Position = vec4(gl_in[0].gl_Position.x + u_size.x, gl_in[0].gl_Position.y, gl_in[0].gl_Position.z, 1.0);\n"
      "  texcoord = vec2(1, 0);\n"
      "  EmitVertex();\n"
      "  gl_Position = vec4(gl_in[0].gl_Position.x, gl_in[0].gl_Position.y - u_size.y, gl_in[0].gl_Position.z, 1.0);\n"
      "  texcoord = vec2(0, 1);\n"
      "  EmitVertex();\n"
      "  gl_Position = vec4(gl_in[0].gl_Position.x + u_size.x, gl_in[0].gl_Position.y - u_size.y, gl_in[0].gl_Position.z, 1.0);\n"
      "  texcoord = vec2(1, 1);\n"
      "  EmitVertex();\n"
      "  \n"
      "  EndPrimitive();\n"
      "}\n",
      ShaderProgram::ShaderType::kGeometryShader));
  
  CHECK(program->AttachShader(
      "#version 330 core\n"
      "layout(location = 0) out vec4 out_color;\n"
      "\n"
      "in vec2 texcoord;\n"
      "\n"
      "uniform vec3 u_playerColor;\n"
      "uniform float u_fillAmount;\n"
      "\n"
      "void main() {\n"
      "  const float borderX = 1.0 / 30.0;\n"
      "  const float borderY = 1.0 / 3.0;\n"
      "  bool leftBorder = texcoord.x < borderX;\n"
      "  bool rightBorder = texcoord.x > 1.0 - borderX;\n"
      "  bool topBorder = texcoord.y < borderY;\n"
      "  bool bottomBorder = texcoord.y > 1.0 - borderY;\n"
      "  if ((leftBorder && bottomBorder) || (topBorder && rightBorder)) {\n"
      "    discard;\n"
      "  } else if (rightBorder || bottomBorder) {\n"
      "    out_color = vec4(0, 0, 0, 0);\n"
      "  } else {\n"
      "    out_color = vec4(((texcoord.x - 0) / (1 - 1 * borderX) < u_fillAmount) ? u_playerColor : vec3(0, 0, 0), 1);\n"
      "  }\n"
      "}\n",
      ShaderProgram::ShaderType::kFragmentShader));
  
  CHECK(program->LinkProgram());
  
  program->UseProgram();
  
  viewMatrix_location = program->GetUniformLocationOrAbort("u_viewMatrix");
  size_location = program->GetUniformLocationOrAbort("u_size");
  playerColor_location = program->GetUniformLocationOrAbort("u_playerColor");
  fillAmount_location = program->GetUniformLocationOrAbort("u_fillAmount");
}

HealthBarShader::~HealthBarShader() {
  program.reset();
}
