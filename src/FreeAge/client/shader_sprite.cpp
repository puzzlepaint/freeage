#include "FreeAge/client/shader_sprite.hpp"

#include "FreeAge/common/logging.hpp"
#include "FreeAge/client/opengl.hpp"

SpriteShader::SpriteShader(bool shadow, bool outline)
    : shadow(shadow),
      outline(outline) {
  QOpenGLFunctions_3_2_Core* f = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
  
  program.reset(new ShaderProgram());
  
  std::string vertexShaderSrc =
      "#version 330 core\n"
      "\n"
      "in vec3 in_position;\n"
      "in vec2 in_size;\n"
      "in vec2 in_tex_topleft;\n"
      "in vec2 in_tex_bottomright;\n";
  if (outline) {
    vertexShaderSrc +=
        "in vec3 in_playerColor;\n"
        "\n"
        "out vec3 var_playerColor;\n";
  } else if (!shadow && !outline) {
    vertexShaderSrc +=
        "in int in_playerIndex;\n"
        "in vec3 in_modulationColor;\n"
        "\n"
        "flat out int var_playerIndex;\n"
        "out vec3 var_modulationColor;\n";
  }
  vertexShaderSrc +=
      "out vec2 var_size;\n"
      "out vec2 var_tex_topleft;\n"
      "out vec2 var_tex_bottomright;\n"
      "\n"
      "uniform mat2 u_viewMatrix;\n"
      "void main() {\n"
      "  var_size = in_size;\n"
      "  var_tex_topleft = in_tex_topleft;\n"
      "  var_tex_bottomright = in_tex_bottomright;\n";
  if (outline) {
    vertexShaderSrc +=
        "var_playerColor = in_playerColor;\n";
  } else if (!shadow && !outline) {
    vertexShaderSrc +=
        "var_playerIndex = in_playerIndex;\n"
        "var_modulationColor = in_modulationColor;\n";
  }
  vertexShaderSrc +=
      "  gl_Position = vec4(u_viewMatrix[0][0] * in_position.x + u_viewMatrix[1][0], u_viewMatrix[0][1] * in_position.y + u_viewMatrix[1][1], in_position.z, 1);\n"
      "}\n";
  CHECK(program->AttachShader(vertexShaderSrc.c_str(), ShaderProgram::ShaderType::kVertexShader, f));
  
  std::string geometryShaderSrc =
      "#version 330 core\n"
      "#extension GL_EXT_geometry_shader : enable\n"
      "layout(points) in;\n"
      "layout(triangle_strip, max_vertices = 4) out;\n"
      "\n"
      "in vec2 var_size[];\n"
      "in vec2 var_tex_topleft[];\n"
      "in vec2 var_tex_bottomright[];\n";
  if (outline) {
    geometryShaderSrc +=
        "in vec3 var_playerColor[];\n"
        "\n"
        "out vec3 playerColor;\n";
  } else if (!shadow && !outline) {
    geometryShaderSrc +=
        "flat in int var_playerIndex[];\n"
        "in vec3 var_modulationColor[];\n"
        "\n"
        "flat out int playerIndex;\n"
        "out vec3 modulationColor;\n";
  }
  geometryShaderSrc +=
      "out vec2 texcoord;\n"
      "\n"
      "void main() {\n"
      "  gl_Position = vec4(gl_in[0].gl_Position.x, gl_in[0].gl_Position.y, gl_in[0].gl_Position.z, 1.0);\n"
      "  texcoord = vec2(var_tex_topleft[0].x, var_tex_topleft[0].y);\n";
  if (outline) {
    geometryShaderSrc +=
        "playerColor = var_playerColor[0];\n";
  } else if (!shadow && !outline) {
    geometryShaderSrc +=
        "playerIndex = var_playerIndex[0];\n"
        "modulationColor = var_modulationColor[0];\n";
  }
  geometryShaderSrc +=
      "  EmitVertex();\n"
      "  gl_Position = vec4(gl_in[0].gl_Position.x + var_size[0].x, gl_in[0].gl_Position.y, gl_in[0].gl_Position.z, 1.0);\n"
      "  texcoord = vec2(var_tex_bottomright[0].x, var_tex_topleft[0].y);\n"
      "  EmitVertex();\n"
      "  gl_Position = vec4(gl_in[0].gl_Position.x, gl_in[0].gl_Position.y - var_size[0].y, gl_in[0].gl_Position.z, 1.0);\n"
      "  texcoord = vec2(var_tex_topleft[0].x, var_tex_bottomright[0].y);\n"
      "  EmitVertex();\n"
      "  gl_Position = vec4(gl_in[0].gl_Position.x + var_size[0].x, gl_in[0].gl_Position.y - var_size[0].y, gl_in[0].gl_Position.z, 1.0);\n"
      "  texcoord = vec2(var_tex_bottomright[0].x, var_tex_bottomright[0].y);\n"
      "  EmitVertex();\n"
      "  \n"
      "  EndPrimitive();\n"
      "}\n";
  CHECK(program->AttachShader(geometryShaderSrc.c_str(), ShaderProgram::ShaderType::kGeometryShader, f));
  
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
        "in vec3 playerColor;\n"
        "\n"
        "uniform sampler2D u_texture;\n"
        "uniform vec2 u_textureSize;\n"
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
        "  out_color = vec4(playerColor.rgb, 1);\n"
        "}\n",
        ShaderProgram::ShaderType::kFragmentShader, f));
  } else {
    CHECK(program->AttachShader(
        "#version 330 core\n"
        "layout(location = 0) out vec4 out_color;\n"
        "\n"
        "in vec2 texcoord;\n"
        "flat in int playerIndex;\n"
        "in vec3 modulationColor;\n"
        "\n"
        "uniform sampler2D u_texture;\n"
        "uniform vec2 u_textureSize;\n"
        "uniform sampler2D u_playerColorsTexture;\n"
        "uniform vec2 u_playerColorsTextureSize;\n"
        "\n"
        "vec4 AdjustPlayerColor(vec4 value) {\n"
        "  int alpha = int(round(255 * value.a));"
        "  if (alpha == 254 || alpha == 252) {\n"
        "    // This is a player color pixel that is encoded as a palette index in the texture.\n"
        "    int palIndex = int(round(256 * value.r)) + 256 * int(round(256 * value.g));\n"
        "    return texture(u_playerColorsTexture, vec2((palIndex + 0.5) / u_playerColorsTextureSize.x, (playerIndex + 0.5) / u_playerColorsTextureSize.y));\n"
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
        "      vec4(modulationColor.rgb, 1) *\n"  // this is a component-wise multiplication
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
  size_location = f->glGetAttribLocation(program->program_name(), "in_size");
  CHECK_GE(size_location, 0);
  if (!shadow) {
    textureSize_location = program->GetUniformLocationOrAbort("u_textureSize", f);
    if (!outline) {
      playerColorsTexture_location = program->GetUniformLocationOrAbort("u_playerColorsTexture", f);
      playerColorsTextureSize_location = program->GetUniformLocationOrAbort("u_playerColorsTextureSize", f);
      playerIndex_location = f->glGetAttribLocation(program->program_name(), "in_playerIndex");
      CHECK_GE(playerIndex_location, 0);
      modulationColor_location = f->glGetAttribLocation(program->program_name(), "in_modulationColor");
      CHECK_GE(modulationColor_location, 0);
    }
  }
  if (outline) {
    playerColor_location = f->glGetAttribLocation(program->program_name(), "in_playerColor");
    CHECK_GE(playerColor_location, 0);
  }
  tex_topleft_location = f->glGetAttribLocation(program->program_name(), "in_tex_topleft");
  CHECK_GE(tex_topleft_location, 0);
  tex_bottomright_location = f->glGetAttribLocation(program->program_name(), "in_tex_bottomright");
  CHECK_GE(tex_bottomright_location, 0);
  
  if (outline) {
    vertexSize = (3 + 2 + 2 + 2 + 1) * sizeof(float);
  } else if (shadow) {
    vertexSize = (3 + 2 + 2 + 2) * sizeof(float);
  } else {
    vertexSize = (3 + 2 + 2 + 2 + 1 + 1) * sizeof(float);
  }
}

SpriteShader::~SpriteShader() {
  program.reset();
}

void SpriteShader::UseProgram(QOpenGLFunctions_3_2_Core* f) {
  program->UseProgram(f);
  
  usize offset = 0;
  
  program->SetPositionAttribute(3, GetGLType<float>::value, vertexSize, offset, f);
  offset += 3 * sizeof(float);
  
  f->glEnableVertexAttribArray(size_location);
  f->glVertexAttribPointer(size_location, 2, GetGLType<float>::value, GL_FALSE, vertexSize, reinterpret_cast<void*>(offset));
  offset += 2 * sizeof(float);
  
  f->glEnableVertexAttribArray(tex_topleft_location);
  f->glVertexAttribPointer(tex_topleft_location, 2, GetGLType<float>::value, GL_FALSE, vertexSize, reinterpret_cast<void*>(offset));
  offset += 2 * sizeof(float);
  
  f->glEnableVertexAttribArray(tex_bottomright_location);
  f->glVertexAttribPointer(tex_bottomright_location, 2, GetGLType<float>::value, GL_FALSE, vertexSize, reinterpret_cast<void*>(offset));
  offset += 2 * sizeof(float);
  
  if (outline) {
    f->glEnableVertexAttribArray(playerColor_location);
    f->glVertexAttribPointer(playerColor_location, 4, GetGLType<u8>::value, GL_TRUE, vertexSize, reinterpret_cast<void*>(offset));
    offset += 4;
  } else if (!outline && !shadow) {
    f->glEnableVertexAttribArray(modulationColor_location);
    f->glVertexAttribPointer(modulationColor_location, 4, GetGLType<u8>::value, GL_TRUE, vertexSize, reinterpret_cast<void*>(offset));
    offset += 4;
    
    f->glEnableVertexAttribArray(playerIndex_location);
    f->glVertexAttribPointer(playerIndex_location, 1, GetGLType<int>::value, GL_FALSE, vertexSize, reinterpret_cast<void*>(offset));
    offset += 1 * sizeof(float);  // same as GL_INT size
  }
  
  CHECK_OPENGL_NO_ERROR();
}
