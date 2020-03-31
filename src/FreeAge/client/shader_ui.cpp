#include "FreeAge/client/shader_ui.hpp"

#include "FreeAge/common/logging.hpp"
#include "FreeAge/client/opengl.hpp"

UIShader::UIShader() {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  program.reset(new ShaderProgram());
  
  CHECK(program->AttachShader(
      "#version 330 core\n"
      "in vec3 in_position;\n"
      "uniform mat2 u_viewMatrix;\n"
      "void main() {\n"
      "  gl_Position = vec4(u_viewMatrix[0][0] * in_position.x + u_viewMatrix[1][0], u_viewMatrix[0][1] * in_position.y + u_viewMatrix[1][1], in_position.z, 1);\n"
      "}\n",
      ShaderProgram::ShaderType::kVertexShader, f));
  
  CHECK(program->AttachShader(
      "#version 330 core\n"
      "#extension GL_EXT_geometry_shader : enable\n"
      "layout(points) in;\n"
      "layout(triangle_strip, max_vertices = 4) out;\n"
      "\n"
      "uniform vec2 u_size;\n"
      "uniform vec2 u_tex_topleft;\n"
      "uniform vec2 u_tex_bottomright;\n"
      "\n"
      "out vec2 texcoord;\n"
      "\n"
      "void main() {\n"
      "  gl_Position = vec4(gl_in[0].gl_Position.x, gl_in[0].gl_Position.y, gl_in[0].gl_Position.z, 1.0);\n"
      "  texcoord = vec2(u_tex_topleft.x, u_tex_topleft.y);\n"
      "  EmitVertex();\n"
      "  gl_Position = vec4(gl_in[0].gl_Position.x + u_size.x, gl_in[0].gl_Position.y, gl_in[0].gl_Position.z, 1.0);\n"
      "  texcoord = vec2(u_tex_bottomright.x, u_tex_topleft.y);\n"
      "  EmitVertex();\n"
      "  gl_Position = vec4(gl_in[0].gl_Position.x, gl_in[0].gl_Position.y - u_size.y, gl_in[0].gl_Position.z, 1.0);\n"
      "  texcoord = vec2(u_tex_topleft.x, u_tex_bottomright.y);\n"
      "  EmitVertex();\n"
      "  gl_Position = vec4(gl_in[0].gl_Position.x + u_size.x, gl_in[0].gl_Position.y - u_size.y, gl_in[0].gl_Position.z, 1.0);\n"
      "  texcoord = vec2(u_tex_bottomright.x, u_tex_bottomright.y);\n"
      "  EmitVertex();\n"
      "  \n"
      "  EndPrimitive();\n"
      "}\n",
      ShaderProgram::ShaderType::kGeometryShader, f));
  
  CHECK(program->AttachShader(
      "#version 330 core\n"
      "layout(location = 0) out vec4 out_color;\n"
      "\n"
      "in vec2 texcoord;\n"
      "\n"
      "uniform sampler2D u_texture;\n"
      "uniform vec4 u_modulationColor;\n"
      "\n"
      "void main() {\n"
      "  out_color = u_modulationColor * texture(u_texture, texcoord.xy);\n"
      "}\n",
      ShaderProgram::ShaderType::kFragmentShader, f));
  
  CHECK(program->LinkProgram(f));
  
  program->UseProgram(f);
  
  texture_location = program->GetUniformLocationOrAbort("u_texture", f);
  viewMatrix_location = program->GetUniformLocationOrAbort("u_viewMatrix", f);
  size_location = program->GetUniformLocationOrAbort("u_size", f);
  tex_topleft_location = program->GetUniformLocationOrAbort("u_tex_topleft", f);
  tex_bottomright_location = program->GetUniformLocationOrAbort("u_tex_bottomright", f);
  modulationColor_location = program->GetUniformLocationOrAbort("u_modulationColor", f);
}

UIShader::~UIShader() {
  program.reset();
}


void RenderUIGraphic(float x, float y, float width, float height, QRgb modulationColor, GLuint pointBuffer, const Texture& texture, UIShader* uiShader, int widgetWidth, int widgetHeight, QOpenGLFunctions_3_2_Core* f) {
  ShaderProgram* program = uiShader->GetProgram();
  program->UseProgram(f);
  f->glUniform1i(uiShader->GetTextureLocation(), 0);  // use GL_TEXTURE0
  f->glBindTexture(GL_TEXTURE_2D, texture.GetId());
  
  f->glUniform2f(uiShader->GetTexTopLeftLocation(), 0, 0);
  f->glUniform2f(uiShader->GetTexBottomRightLocation(), 1, 1);
  
  f->glUniform2f(
      uiShader->GetSizeLocation(),
      2.f * width / static_cast<float>(widgetWidth),
      2.f * height / static_cast<float>(widgetHeight));
  
  f->glUniform4f(uiShader->GetModulationColorLocation(), qRed(modulationColor) / 255.f, qGreen(modulationColor) / 255.f, qBlue(modulationColor) / 255.f, qAlpha(modulationColor) / 255.f);
  
  int elementSizeInBytes = 3 * sizeof(float);
  f->glBindBuffer(GL_ARRAY_BUFFER, pointBuffer);
  float* data = static_cast<float*>(f->glMapBufferRange(GL_ARRAY_BUFFER, 0, elementSizeInBytes, GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT));
  data[0] = x;
  data[1] = y;
  data[2] = 0.f;
  f->glUnmapBuffer(GL_ARRAY_BUFFER);
  
  program->SetPositionAttribute(
      3,
      GetGLType<float>::value,
      3 * sizeof(float),
      0,
      f);
  
  f->glDrawArrays(GL_POINTS, 0, 1);
  
  CHECK_OPENGL_NO_ERROR();
}
