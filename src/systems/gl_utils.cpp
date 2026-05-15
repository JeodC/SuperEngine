// -----------------------------------------------------------------------
//
// Copyright (C) 2006, 2007 Elliot Glaysher
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
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
//
// -----------------------------------------------------------------------

#include "systems/gl_utils.hpp"

#include "systems/gl_loader.hpp"

#include <cstring>
#include <format>
#include <stdexcept>

std::string GetGLErrors(void) {
  GLenum error;
  std::string msg;

  // Drain the error queue
  while ((error = glGetError()) != GL_NO_ERROR) {
    if (!msg.empty()) msg += "; ";
    const char* name = nullptr;
    switch (error) {
      case GL_INVALID_ENUM:
        name = "GL_INVALID_ENUM (unacceptable enum value)";
        break;
      case GL_INVALID_VALUE:
        name = "GL_INVALID_VALUE (numeric arg out of range)";
        break;
      case GL_INVALID_OPERATION:
        name = "GL_INVALID_OPERATION (op not allowed in current state)";
        break;
      case GL_OUT_OF_MEMORY:
        name = "GL_OUT_OF_MEMORY";
        break;
      case GL_INVALID_FRAMEBUFFER_OPERATION:
        name = "GL_INVALID_FRAMEBUFFER_OPERATION (FBO incomplete)";
        break;
#ifdef GL_STACK_OVERFLOW
      case GL_STACK_OVERFLOW:
        name = "GL_STACK_OVERFLOW";
        break;
#endif
#ifdef GL_STACK_UNDERFLOW
      case GL_STACK_UNDERFLOW:
        name = "GL_STACK_UNDERFLOW";
        break;
#endif
      default:
        name = "unknown";
        break;
    }
    msg += std::format("0x{:04x} {}", static_cast<unsigned>(error), name);
  }
  return msg;
}

void ShowGLErrors(std::source_location loc) {
  auto error = GetGLErrors();
  if (!error.empty()) {
    // Strip any directory prefix from the source path for readability.
    std::string_view file = loc.file_name();
    if (auto pos = file.find_last_of("/\\"); pos != std::string_view::npos)
      file.remove_prefix(pos + 1);
    throw std::runtime_error(std::format("GL error at {}:{} in {}: {}", file,
                                         loc.line(), loc.function_name(),
                                         error));
  }
}

// -----------------------------------------------------------------------

bool IsNPOTSafe() {
  static bool initialized = false;
  static bool is_safe = false;
  if (!initialized) {
    initialized = true;
    const GLubyte* extensions_str = glGetString(GL_EXTENSIONS);
    if (extensions_str) {
      const char* extensions = reinterpret_cast<const char*>(extensions_str);
      is_safe =
          std::strstr(extensions, "GL_ARB_texture_non_power_of_two") != nullptr
          || std::strstr(extensions, "GL_OES_texture_npot") != nullptr;
    }
  }
  return is_safe;
}

int GetMaxTextureSize() {
  static GLint max_texture_size = 0;
  if (max_texture_size == 0) {
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    if (max_texture_size > 4096) {
      // Little Busters tries to page in 9 images, each 1,200 x 12,000. The AMD
      // drivers do *not* like dealing with those images as one texture, even
      // if it advertises that it can. Chopping those images doesn't fix the
      // memory consumption, but helps (slightly) with the allocation pause.
      max_texture_size = 4096;
    }
  }

  return max_texture_size;
}

int SafeSize(int i) {
  const GLint max_texture_size = GetMaxTextureSize();
  if (i > max_texture_size)
    return max_texture_size;

  if (IsNPOTSafe()) {
    return i;
  }

  for (int p = 0; p < 24; p++) {
    if (i <= (1 << p))
      return 1 << p;
  }

  return max_texture_size;
}
