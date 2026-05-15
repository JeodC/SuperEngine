// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2025 Serina Sakurai
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

#include "systems/gltexture.hpp"

#include "core/colour.hpp"
#include "systems/gl_utils.hpp"

#include "systems/gl_loader.hpp"

#include <bit>
#include <stdexcept>
#include <vector>

glTexture::glTexture(Size size, uint8_t* data) { Init(size, data); }

void glTexture::Init(Size size, uint8_t* data) {
  size_ = size;

  glGenTextures(1, &id_);
  glBindTexture(GL_TEXTURE_2D, id_);
  ShowGLErrors();
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size_.width(), size_.height(), 0,
               GL_RGBA, GL_UNSIGNED_BYTE, data);
  ShowGLErrors();
}

glTexture::~glTexture() { glDeleteTextures(1, &id_); }

unsigned int glTexture::GetID() const { return id_; }

Size glTexture::GetSize() const { return size_; }

void glTexture::Write(Rect region,
                      uint32_t format,
                      uint32_t type,
                      const void* data) {
  region = Flip_y(region);

  std::vector<uint8_t> converted;
  if (format != GL_RGBA || type != GL_UNSIGNED_BYTE) {
    const auto* src = static_cast<const uint8_t*>(data);
    const size_t pixels =
        static_cast<size_t>(region.width()) * region.height();
    converted.resize(pixels * 4);

    if (format == GL_BGRA &&
        (type == GL_UNSIGNED_BYTE ||
         (type == GL_UNSIGNED_INT_8_8_8_8_REV &&
          std::endian::native == std::endian::little))) {
      // Memory layout for both:
      //   - GL_BGRA + UNSIGNED_BYTE: B, G, R, A in memory by definition.
      //   - GL_BGRA + UNSIGNED_INT_8_8_8_8_REV on little-endian: the same
      //     B, G, R, A in memory (the 32-bit value, when stored in
      //     little-endian byte order, places A in the high byte / byte 3
      //     and B in the low byte / byte 0).
      // Conversion to RGBA byte order is just R↔B swap.
      for (size_t i = 0; i < pixels; ++i) {
        converted[4 * i + 0] = src[4 * i + 2];  // R <- B
        converted[4 * i + 1] = src[4 * i + 1];  // G
        converted[4 * i + 2] = src[4 * i + 0];  // B <- R
        converted[4 * i + 3] = src[4 * i + 3];  // A
      }
    } else if (format == GL_BGRA && type == GL_UNSIGNED_INT_8_8_8_8_REV) {
      // Big-endian fallback: the 32-bit value stores as A, R, G, B in
      // memory. Rotate per pixel.
      for (size_t i = 0; i < pixels; ++i) {
        converted[4 * i + 0] = src[4 * i + 1];  // R
        converted[4 * i + 1] = src[4 * i + 2];  // G
        converted[4 * i + 2] = src[4 * i + 3];  // B
        converted[4 * i + 3] = src[4 * i + 0];  // A
      }
    } else if (format == GL_RGB && type == GL_UNSIGNED_BYTE) {
      for (size_t i = 0; i < pixels; ++i) {
        converted[4 * i + 0] = src[3 * i + 0];
        converted[4 * i + 1] = src[3 * i + 1];
        converted[4 * i + 2] = src[3 * i + 2];
        converted[4 * i + 3] = 0xFF;
      }
    } else {
      throw std::runtime_error(
          "glTexture::Write: unsupported (format=" + std::to_string(format) +
          ", type=" + std::to_string(type) + ") combo. " +
          "Add a conversion path to gltexture.cpp.");
    }

    data = converted.data();
    format = GL_RGBA;
    type = GL_UNSIGNED_BYTE;
  }

  glBindTexture(GL_TEXTURE_2D, id_);
  glTexSubImage2D(GL_TEXTURE_2D, 0, region.x(), region.y(), region.width(),
                  region.height(), format, type, data);
  glBindTexture(GL_TEXTURE_2D, 0);
  ShowGLErrors();
}

void glTexture::Write(Rect region, std::vector<uint8_t> data) {
  data = Flip_y(region.size(), data.cbegin());
  Write(Flip_y(region), GL_RGBA, GL_UNSIGNED_BYTE, data.data());
}

std::vector<RGBAColour> glTexture::Dump(std::optional<Rect> in_region) {
  glFinish();

  const Rect region = Flip_y(in_region.value_or(Rect(Point(0, 0), size_)));
  std::vector<uint8_t> data(region.width() * region.height() * 4);

  GLint prev_fbo = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);

  GLuint tmp_fbo = 0;
  glGenFramebuffers(1, &tmp_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, tmp_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, id_, 0);
  glReadPixels(region.x(), region.y(), region.width(), region.height(),
               GL_RGBA, GL_UNSIGNED_BYTE, data.data());

  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));
  glDeleteFramebuffers(1, &tmp_fbo);

  data = Flip_y(region.size(), data.data());

  std::vector<RGBAColour> result(data.size() / 4);
  for (size_t i = 0; i < result.size(); ++i)
    result[i] = RGBAColour(data[i * 4], data[i * 4 + 1], data[i * 4 + 2],
                           data[i * 4 + 3]);
  return result;
}
