#include "FreeAge/client/shader_color_dilation.hpp"

#include "FreeAge/common/logging.hpp"
#include "FreeAge/client/opengl.hpp"

ColorDilationShader::ColorDilationShader() {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  program.reset(new ShaderProgram());
  
  // This generates vertex positions directly in the vertex shader.
  CHECK(program->AttachShader(
      "#version 330 core\n"
      "\n"
      "out vec2 vTexCoord;\n"
      "\n"
      "void main() {\n"
      " vTexCoord = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
      " gl_Position = vec4( vTexCoord * 2.0 - 1.0, 0.0, 1.0);\n"
      "}\n",
      ShaderProgram::ShaderType::kVertexShader, f));
  
  CHECK(program->AttachShader(
      "#version 330 core\n"
      "layout(location = 0) out vec4 out_color;\n"
      "\n"
      "in vec2 vTexCoord;\n"
      "\n"
      "uniform sampler2D u_texture;\n"
      "uniform vec2 u_pixelStep;\n"
      "\n"
      "void main() {\n"
      "  out_color = texture(u_texture, vTexCoord.xy);\n"
      "  if (int(round(255 * out_color.w)) <= 1) {\n"
      "    vec4 sum = vec4(0, 0, 0, 0);\n"
      "    vec4 v;"
      "    \n"
      "    v = texture(u_texture, vTexCoord.xy -                          u_pixelStep); if (int(round(255 * v.a)) > 1) { sum += vec4(v.xyz, 1); }\n"
      "    v = texture(u_texture, vTexCoord.xy + vec2(             0, -u_pixelStep.y)); if (int(round(255 * v.a)) > 1) { sum += vec4(v.xyz, 1); }\n"
      "    v = texture(u_texture, vTexCoord.xy + vec2( u_pixelStep.x, -u_pixelStep.y)); if (int(round(255 * v.a)) > 1) { sum += vec4(v.xyz, 1); }\n"
      "    v = texture(u_texture, vTexCoord.xy + vec2( u_pixelStep.x,              0)); if (int(round(255 * v.a)) > 1) { sum += vec4(v.xyz, 1); }\n"
      "    v = texture(u_texture, vTexCoord.xy +                          u_pixelStep); if (int(round(255 * v.a)) > 1) { sum += vec4(v.xyz, 1); }\n"
      "    v = texture(u_texture, vTexCoord.xy + vec2(             0,  u_pixelStep.y)); if (int(round(255 * v.a)) > 1) { sum += vec4(v.xyz, 1); }\n"
      "    v = texture(u_texture, vTexCoord.xy + vec2(-u_pixelStep.x,  u_pixelStep.y)); if (int(round(255 * v.a)) > 1) { sum += vec4(v.xyz, 1); }\n"
      "    v = texture(u_texture, vTexCoord.xy + vec2(-u_pixelStep.x,              0));  if (int(round(255 * v.a)) > 1) { sum += vec4(v.xyz, 1); }\n"
      "    \n"
      "    if (sum.w > 0) {\n"
      "      out_color = vec4(sum.xyz / sum.w, out_color.w);\n"
      "    }\n"
      "  }\n"
      "}\n",
      ShaderProgram::ShaderType::kFragmentShader, f));
  
  CHECK(program->LinkProgram(f));
  
  program->UseProgram(f);
  
  texture_location = program->GetUniformLocationOrAbort("u_texture", f);
  pixelStep_location = program->GetUniformLocationOrAbort("u_pixelStep", f);
}

ColorDilationShader::~ColorDilationShader() {
  program.reset();
}
