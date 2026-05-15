// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2013 Elliot Glaysher
// Copyright (C) 2024 Serina Sakurai
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
// -----------------------------------------------------------------------

#include "systems/gl_loader.hpp"

#include "systems/gl_utils.hpp"
#include "systems/sdl/shaders.hpp"

#include <string>

static_assert(std::same_as<unsigned int, GLuint>);

static void checkCompileError(GLuint shader) {
  GLint success;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    GLchar msg[1024];
    glGetShaderInfoLog(shader, 1024, NULL, msg);
    throw std::runtime_error(std::string("Shader compile error: ") + msg);
  }
}

static void checkLinkError(GLuint program) {
  GLint success;
  glGetProgramiv(program, GL_LINK_STATUS, &success);
  if (!success) {
    GLchar msg[1024];
    glGetProgramInfoLog(program, 1024, NULL, msg);
    throw std::runtime_error(std::string("Shader linking error: ") + msg);
  }
}

glslProgram::glslProgram(std::string_view vertex_src,
                         std::string_view frag_src) {
  Build(vertex_src, frag_src, {});
}

glslProgram::glslProgram(std::string_view vertex_src,
                         std::string_view frag_src,
                         std::initializer_list<AttribBinding> attribs) {
  Build(vertex_src, frag_src, attribs);
}

void glslProgram::Build(std::string_view vertex_src,
                        std::string_view frag_src,
                        std::initializer_list<AttribBinding> attribs) {
  const char *vs = vertex_src.data(), *fs = frag_src.data();
  GLint vs_len = static_cast<GLint>(vertex_src.size());
  GLint fs_len = static_cast<GLint>(frag_src.size());

  auto vertex = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertex, 1, &vs, &vs_len);
  glCompileShader(vertex);
  checkCompileError(vertex);

  auto fragment = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragment, 1, &fs, &fs_len);
  glCompileShader(fragment);
  checkCompileError(fragment);

  id_ = glCreateProgram();
  glAttachShader(id_, vertex);
  glAttachShader(id_, fragment);

  // GLES 2.0 / GLSL ES 100 has no layout(location=N) qualifier — the
  // attribute->slot mapping must be established before linking.
  for (const auto& [loc, name] : attribs) {
    // string_view isn't guaranteed null-terminated; copy to std::string.
    std::string named(name);
    glBindAttribLocation(id_, loc, named.c_str());
  }

  glLinkProgram(id_);
  checkLinkError(id_);

  glDeleteShader(vertex);
  glDeleteShader(fragment);

  ShowGLErrors();
}

glslProgram::~glslProgram() {
  if (id_ != 0) {
    glDeleteProgram(id_);
  }
}

unsigned int glslProgram::UniformLocation(std::string_view name) {
  std::string named(name);
  auto location = glGetUniformLocation(id_, named.c_str());
  if (location == -1) {
    throw std::runtime_error("glslProgram: Uniform " + named + " not found.");
  }
  return location;
}

void glslProgram::SetUniform(std::string_view name, int value) {
  glUniform1i(UniformLocation(name), value);
}

void glslProgram::SetUniform(std::string_view name, float value) {
  glUniform1f(UniformLocation(name), value);
}

void glslProgram::SetUniform(std::string_view name,
                             float x,
                             float y,
                             float z,
                             float w) {
  glUniform4f(UniformLocation(name), x, y, z, w);
}

void glslProgram::SetUniform(std::string_view name, float x, float y, float z) {
  glUniform3f(UniformLocation(name), x, y, z);
}

std::shared_ptr<glslProgram> GetOpShader() {
#ifdef RLVM_USE_GLES2
  static constexpr std::string_view vertex_src = R"glsl(
#version 100

attribute vec2 aPos;
attribute float aOpacity;
attribute vec2 aTexCoord;

varying float Opacity;
varying vec2 TexCoord;

void main(){
  gl_Position = vec4(aPos, 0.0, 1.0);
  Opacity = aOpacity;
  TexCoord = aTexCoord;
}
)glsl";
  static constexpr std::string_view fragment_src = R"glsl(
#version 100
precision mediump float;

varying float Opacity;
varying vec2 TexCoord;

uniform sampler2D texture1;
uniform vec4 mask_color;

void main(){
  vec4 textureColor = texture2D(texture1, TexCoord);
  vec3 blend_color = clamp((textureColor.rgb + mask_color.rgb*mask_color.a), 0.0, 1.0);
  gl_FragColor = vec4(blend_color, Opacity * textureColor.a);
}
)glsl";
#else
  static constexpr std::string_view vertex_src = R"glsl(
#version 330 core

layout (location = 0) in vec2 aPos;
layout (location = 1) in float aOpacity;
layout (location = 2) in vec2 aTexCoord;

out float Opacity;
out vec2 TexCoord;

void main(){
  gl_Position = vec4(aPos, 0.0, 1.0);
  Opacity = aOpacity;
  TexCoord = aTexCoord;
}
)glsl";
  static constexpr std::string_view fragment_src = R"glsl(
#version 330 core
in float Opacity;
in vec2 TexCoord;

uniform sampler2D texture1;
uniform vec4 mask_color;
out vec4 FragColor;

void main(){
  vec4 textureColor = texture(texture1, TexCoord);
  vec3 blend_color = clamp((textureColor.rgb + mask_color.rgb*mask_color.a), 0.0, 1.0);
  FragColor = vec4(blend_color, Opacity * textureColor.a);
}
)glsl";
#endif

  static auto shader = std::make_shared<glslProgram>(
      vertex_src, fragment_src,
      std::initializer_list<glslProgram::AttribBinding>{
          {0, "aPos"}, {1, "aOpacity"}, {2, "aTexCoord"}});
  return shader;
}

std::shared_ptr<glslProgram> GetColorMaskShader() {
#ifdef RLVM_USE_GLES2
  static constexpr std::string_view vertex_src = R"glsl(
#version 100

attribute vec2 aPos;
attribute vec2 aTexCoord0;
attribute vec2 aTexCoord1;

varying vec2 TexCoord0;
varying vec2 TexCoord1;

void main(){
  gl_Position = vec4(aPos, 0.0, 1.0);
  TexCoord0 = aTexCoord0;
  TexCoord1 = aTexCoord1;
}
)glsl";
  static constexpr std::string_view fragment_src = R"glsl(
#version 100
precision mediump float;

varying vec2 TexCoord0;
varying vec2 TexCoord1;

uniform sampler2D texture0;
uniform sampler2D texture1;
uniform vec4 color;

void main(){
  vec4 bg_color = texture2D(texture0, TexCoord0);
  vec4 mask_sample = texture2D(texture1, TexCoord1);

  float mask_strength = clamp(mask_sample.a * color.a, 0.0, 1.0);
  vec4 blended_color = bg_color - mask_strength + color * mask_strength;
  gl_FragColor = clamp(blended_color, 0.0, 1.0);
}
)glsl";
#else
  static constexpr std::string_view vertex_src = R"glsl(
#version 330 core

layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord0;
layout (location = 2) in vec2 aTexCoord1;

out vec2 TexCoord0;
out vec2 TexCoord1;

void main(){
  gl_Position = vec4(aPos, 0.0, 1.0);
  TexCoord0 = aTexCoord0;
  TexCoord1 = aTexCoord1;
}
)glsl";
  static constexpr std::string_view fragment_src = R"glsl(
#version 330 core

in vec2 TexCoord0;
in vec2 TexCoord1;

uniform sampler2D texture0;
uniform sampler2D texture1;
uniform vec4 color;
out vec4 FragColor;

void main(){
  vec4 bg_color = texture(texture0, TexCoord0);
  vec4 mask_sample = texture(texture1, TexCoord1);

  float mask_strength = clamp(mask_sample.a * color.a, 0.0, 1.0);
  vec4 blended_color = bg_color - mask_strength + color * mask_strength;
  FragColor = clamp(blended_color, 0.0, 1.0);
}
)glsl";
#endif

  static auto shader = std::make_shared<glslProgram>(
      vertex_src, fragment_src,
      std::initializer_list<glslProgram::AttribBinding>{
          {0, "aPos"}, {1, "aTexCoord0"}, {2, "aTexCoord1"}});
  return shader;
}

std::shared_ptr<glslProgram> GetObjectShader() {
#ifdef RLVM_USE_GLES2
  static constexpr std::string_view vertex_src = R"glsl(
#version 100

attribute vec2 aPos;
attribute vec2 aTexCoord;

varying vec2 TexCoord;

void main(){
  gl_Position = vec4(aPos, 0.0, 1.0);
  TexCoord = aTexCoord;
}
)glsl";
  static constexpr std::string_view fragment_src = R"glsl(
#version 100
precision mediump float;

varying vec2 TexCoord;

uniform sampler2D image;
uniform vec4 colour;
uniform float mono;
uniform float invert;
uniform float light;
uniform vec3 tint;
uniform float alpha;

void tinter(in float pixel_val, in float tint_val, out float mixed) {
  if (tint_val > 0.0) {
    mixed = pixel_val + tint_val - (pixel_val * tint_val);
  } else if (tint_val < 0.0) {
    mixed = pixel_val * abs(tint_val);
  } else {
    mixed = pixel_val;
  }
}

void main() {
  vec4 pixel = texture2D(image, TexCoord);

  vec3 coloured = mix(pixel.rgb, colour.rgb, colour.a);
  pixel = vec4(coloured, pixel.a);

  if (mono > 0.0) {
    float gray = dot(pixel.rgb, vec3(0.299, 0.587, 0.114));
    vec3 mixed = mix(pixel.rgb, vec3(gray), mono);
    pixel.rgb = mixed;
  }

  if (invert > 0.0) {
    vec3 inverted = vec3(1.0) - pixel.rgb;
    vec3 mixed = mix(pixel.rgb, inverted, invert);
    pixel.rgb = mixed;
  }

  float out_r, out_g, out_b;
  tinter(pixel.r, light, out_r);
  tinter(pixel.g, light, out_g);
  tinter(pixel.b, light, out_b);
  pixel.rgb = vec3(out_r, out_g, out_b);

  tinter(pixel.r, tint.r, out_r);
  tinter(pixel.g, tint.g, out_g);
  tinter(pixel.b, tint.b, out_b);
  pixel.rgb = vec3(out_r, out_g, out_b);

  pixel.a *= alpha;
  gl_FragColor = pixel;
}
)glsl";
#else
  static constexpr std::string_view vertex_src = R"glsl(
#version 330 core

layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

void main(){
  gl_Position = vec4(aPos, 0.0, 1.0);
  TexCoord = aTexCoord;
}
)glsl";
  static constexpr std::string_view fragment_src = R"glsl(
#version 330 core
in vec2 TexCoord;

uniform sampler2D image;
uniform vec4 colour;
uniform float mono;
uniform float invert;
uniform float light;
uniform vec3 tint;
uniform float alpha;

out vec4 FragColor;

void tinter(in float pixel_val, in float tint_val, out float mixed) {
  if (tint_val > 0.0) {
    mixed = pixel_val + tint_val - (pixel_val * tint_val);
  } else if (tint_val < 0.0) {
    mixed = pixel_val * abs(tint_val);
  } else {
    mixed = pixel_val;
  }
}

void main() {
  vec4 pixel = texture(image, TexCoord);

  vec3 coloured = mix(pixel.rgb, colour.rgb, colour.a);
  pixel = vec4(coloured, pixel.a);

  if (mono > 0.0) {
    float gray = dot(pixel.rgb, vec3(0.299, 0.587, 0.114));
    vec3 mixed = mix(pixel.rgb, vec3(gray), mono);
    pixel.rgb = mixed;
  }

  if (invert > 0.0) {
    vec3 inverted = vec3(1.0) - pixel.rgb;
    vec3 mixed = mix(pixel.rgb, inverted, invert);
    pixel.rgb = mixed;
  }

  float out_r, out_g, out_b;
  tinter(pixel.r, light, out_r);
  tinter(pixel.g, light, out_g);
  tinter(pixel.b, light, out_b);
  pixel.rgb = vec3(out_r, out_g, out_b);

  tinter(pixel.r, tint.r, out_r);
  tinter(pixel.g, tint.g, out_g);
  tinter(pixel.b, tint.b, out_b);
  pixel.rgb = vec3(out_r, out_g, out_b);

  pixel.a *= alpha;
  FragColor = pixel;
}
)glsl";
#endif

  static auto shader = std::make_shared<glslProgram>(
      vertex_src, fragment_src,
      std::initializer_list<glslProgram::AttribBinding>{{0, "aPos"},
                                                        {1, "aTexCoord"}});
  return shader;
}
