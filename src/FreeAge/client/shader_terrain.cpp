#include "FreeAge/client/shader_terrain.hpp"

#include "FreeAge/common/logging.hpp"

TerrainShader::TerrainShader() {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  program.reset(new ShaderProgram());
  
  CHECK(program->AttachShader(
      "#version 330 core\n"
      "in vec2 in_position;\n"
      "in vec3 in_texcoord;\n"
      "uniform mat2 u_viewMatrix;\n"
      "out vec3 var_texcoord;\n"
      "void main() {\n"
      // TODO: Use sensible z value? Or disable z writing while rendering the terrain anyway
      "  gl_Position = vec4(u_viewMatrix[0][0] * in_position.x + u_viewMatrix[1][0], u_viewMatrix[0][1] * in_position.y + u_viewMatrix[1][1], 0.999, 1);\n"
      "  var_texcoord = in_texcoord;\n"
      "}\n",
      ShaderProgram::ShaderType::kVertexShader, f));
  
  CHECK(program->AttachShader(
      "#version 330 core\n"
      "layout(location = 0) out vec4 out_color;\n"
      "\n"
      "in vec3 var_texcoord;\n"
      "\n"
      "uniform sampler2D u_texture;\n"
      "\n"
      "void main() {\n"
      "  out_color = vec4(var_texcoord.z * texture(u_texture, var_texcoord.xy).rgb, 0);\n"
      "}\n",
      ShaderProgram::ShaderType::kFragmentShader, f));
  
  CHECK(program->LinkProgram(f));
  
  program->UseProgram(f);
  
  texture_location = program->GetUniformLocationOrAbort("u_texture", f);
  viewMatrix_location = program->GetUniformLocationOrAbort("u_viewMatrix", f);
}

TerrainShader::~TerrainShader() {
  program.reset();
}
