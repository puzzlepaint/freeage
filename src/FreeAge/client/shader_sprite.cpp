#include "FreeAge/client/shader_sprite.hpp"

#include "FreeAge/common/logging.hpp"
#include "FreeAge/client/opengl.hpp"

SpriteShader::SpriteShader(bool shadow, bool outline) {
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
  
  if (shadow) {
    CHECK(program->AttachShader(
        "#version 330 core\n"
        "layout(location = 0) out vec4 out_color;\n"
        "\n"
        "in vec2 texcoord;\n"
        "\n"
        "uniform sampler2D u_texture;\n"
        "\n"
        "void main() {\n"
        "  out_color = vec4(0, 0, 0, 1.5 * texture(u_texture, texcoord.xy).r);\n"  // TODO: Magic factor 1.5 makes it look nicer (darker shadows)
        "}\n",
        ShaderProgram::ShaderType::kFragmentShader, f));
  } else if (outline) {
    CHECK(program->AttachShader(
        "#version 330 core\n"
        "layout(location = 0) out vec4 out_color;\n"
        "\n"
        "in vec2 texcoord;\n"
        "\n"
        "uniform sampler2D u_texture;\n"
        "uniform vec2 u_textureSize;\n"
        "uniform vec3 u_playerColor;\n"
        "\n"
        "float GetOutlineAlpha(vec4 value) {\n"
        "  int alpha = int(round(255 * value.a));"
        "  if (alpha == 253 || alpha == 252 || alpha == 1) {\n"
        "    // This is an outline pixel.\n"
        "    return 1.0;\n"
        "  } else {\n"
        "    // This is not an outline pixel.\n"
        "    return 0.0;\n"
        "  }\n"
        "}\n"
        "\n"
        "void main() {\n"
        "  vec2 pixelTexcoord = vec2(u_textureSize.x * texcoord.x, u_textureSize.y * texcoord.y);\n"
        "  float ix = floor(pixelTexcoord.x - 0.5);\n"
        "  float iy = floor(pixelTexcoord.y - 0.5);\n"
        "  float fx = pixelTexcoord.x - 0.5 - ix;\n"
        "  float fy = pixelTexcoord.y - 0.5 - iy;\n"
        "  \n"
        "  vec4 value = texture(u_texture, vec2((ix + 0.5) / u_textureSize.x, (iy + 0.5) / u_textureSize.y));\n"
        "  float topLeftAlpha = GetOutlineAlpha(value);\n"
        "  value = texture(u_texture, vec2((ix + 1.5) / u_textureSize.x, (iy + 0.5) / u_textureSize.y));\n"
        "  float topRightAlpha = GetOutlineAlpha(value);\n"
        "  value = texture(u_texture, vec2((ix + 0.5) / u_textureSize.x, (iy + 1.5) / u_textureSize.y));\n"
        "  float bottomLeftAlpha = GetOutlineAlpha(value);\n"
        "  value = texture(u_texture, vec2((ix + 1.5) / u_textureSize.x, (iy + 1.5) / u_textureSize.y));\n"
        "  float bottomRightAlpha = GetOutlineAlpha(value);\n"
        "  \n"
        "  float outAlpha =\n"
        "      (1 - fx) * (1 - fy) * topLeftAlpha +\n"
        "      (    fx) * (1 - fy) * topRightAlpha +\n"
        "      (1 - fx) * (    fy) * bottomLeftAlpha +\n"
        "      (    fx) * (    fy) * bottomRightAlpha;\n"
        "  \n"
        "  if (outAlpha < 0.5) {\n"
        "    discard;\n"
        "  }\n"
        "  out_color = vec4(u_playerColor.rgb, 1);\n"
        "}\n",
        ShaderProgram::ShaderType::kFragmentShader, f));
  } else {
    CHECK(program->AttachShader(
        "#version 330 core\n"
        "layout(location = 0) out vec4 out_color;\n"
        "\n"
        "in vec2 texcoord;\n"
        "\n"
        "uniform sampler2D u_texture;\n"
        "uniform sampler2D u_playerColorsTexture;\n"
        "uniform vec2 u_textureSize;\n"
        "uniform vec2 u_playerColorsTextureSize;\n"
        "uniform int u_playerIndex;\n"
        "uniform vec4 u_modulationColor;\n"
        "\n"
        "vec4 AdjustPlayerColor(vec4 value) {\n"
        "  int alpha = int(round(255 * value.a));"
        "  if (alpha == 254 || alpha == 252) {\n"
        "    // This is a player color pixel that is encoded as a palette index in the texture.\n"
        "    int palIndex = int(round(256 * value.r)) + 256 * int(round(256 * value.g));\n"
        "    return texture(u_playerColorsTexture, vec2((palIndex + 0.5) / u_playerColorsTextureSize.x, (u_playerIndex + 0.5) / u_playerColorsTextureSize.y));\n"
        "  } else {\n"
        "    return value;\n"
        "  }\n"
        "}\n"
        "\n"
        "void main() {\n"
        "  vec2 pixelTexcoord = vec2(u_textureSize.x * texcoord.x, u_textureSize.y * texcoord.y);\n"
        "  float ix = floor(pixelTexcoord.x - 0.5);\n"
        "  float iy = floor(pixelTexcoord.y - 0.5);\n"
        "  float fx = pixelTexcoord.x - 0.5 - ix;\n"
        "  float fy = pixelTexcoord.y - 0.5 - iy;\n"
        "  \n"
        "  vec4 topLeft = texture(u_texture, vec2((ix + 0.5) / u_textureSize.x, (iy + 0.5) / u_textureSize.y));\n"
        "  topLeft = AdjustPlayerColor(topLeft);\n"
        "  vec4 topRight = texture(u_texture, vec2((ix + 1.5) / u_textureSize.x, (iy + 0.5) / u_textureSize.y));\n"
        "  topRight = AdjustPlayerColor(topRight);\n"
        "  vec4 bottomLeft = texture(u_texture, vec2((ix + 0.5) / u_textureSize.x, (iy + 1.5) / u_textureSize.y));\n"
        "  bottomLeft = AdjustPlayerColor(bottomLeft);\n"
        "  vec4 bottomRight = texture(u_texture, vec2((ix + 1.5) / u_textureSize.x, (iy + 1.5) / u_textureSize.y));\n"
        "  bottomRight = AdjustPlayerColor(bottomRight);\n"
        "  \n"
        "  out_color =\n"
        "      u_modulationColor *\n"  // this is a component-wise multiplication
        "      mix(mix(topLeft, topRight, fx),\n"
        "          mix(bottomLeft, bottomRight, fx),\n"
        "          fy);\n"
        "  \n"
        "  if (out_color.a < 0.5) {\n"
        "    discard;\n"
        "  }\n"
        "  out_color.a = 1;\n"  // TODO: Instead of setting a to 1 here, disable blending?
        "}\n",
        ShaderProgram::ShaderType::kFragmentShader, f));
  }
  
  CHECK(program->LinkProgram(f));
  
  program->UseProgram(f);
  
  texture_location = program->GetUniformLocationOrAbort("u_texture", f);
  viewMatrix_location = program->GetUniformLocationOrAbort("u_viewMatrix", f);
  size_location = program->GetUniformLocationOrAbort("u_size", f);
  if (!shadow) {
    textureSize_location = program->GetUniformLocationOrAbort("u_textureSize", f);
    if (!outline) {
      playerColorsTextureSize_location = program->GetUniformLocationOrAbort("u_playerColorsTextureSize", f);
      playerIndex_location = program->GetUniformLocationOrAbort("u_playerIndex", f);
      playerColorsTexture_location = program->GetUniformLocationOrAbort("u_playerColorsTexture", f);
      modulationColor_location = program->GetUniformLocationOrAbort("u_modulationColor", f);
    }
  }
  if (outline) {
    playerColor_location = program->GetUniformLocationOrAbort("u_playerColor", f);
  }
  tex_topleft_location = program->GetUniformLocationOrAbort("u_tex_topleft", f);
  tex_bottomright_location = program->GetUniformLocationOrAbort("u_tex_bottomright", f);
}

SpriteShader::~SpriteShader() {
  program.reset();
}

void SpriteShader::UseProgram(QOpenGLFunctions_3_2_Core* f) {
  program->UseProgram(f);
  program->SetPositionAttribute(
      3,
      GetGLType<float>::value,
      3 * sizeof(float),
      0,
      f);
}
