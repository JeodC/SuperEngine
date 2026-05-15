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
//
// -----------------------------------------------------------------------

#include "systems/gl_loader.hpp"

#include "systems/sdl/graphics_backend.hpp"

#include "core/album.hpp"
#include "core/avdec/image_decoder.hpp"
#include "core/colour.hpp"
#include "log/domain_logger.hpp"
#include "systems/gl_utils.hpp"
#include "systems/glcanvas.hpp"
#include "systems/glrenderer.hpp"
#include "systems/gltexture.hpp"
#include "systems/screen_canvas.hpp"
#include "systems/sdl_surface.hpp"

#include <SDL.h>
#include <SDL_video.h>
#if !defined(__APPLE__) && !defined(_WIN32)
#include <SDL_image.h>
#include "../resources/48/rlvm_icon_48.xpm"
#endif

#include <fstream>
#include <string>
#include <vector>

#include <cstring>

using std::string_literals::operator""s;

static DomainLogger logger("SDLGraphicsBackend");

static std::string LoadFile(const std::filesystem::path& pth) {
  std::ifstream ifs(pth, std::ios::binary);
  if (!ifs) {
    logger(Severity::Error) << "Cannot open file: " << pth.string();
    return {};
  }

  return std::string(std::istreambuf_iterator<char>(ifs),
                     std::istreambuf_iterator<char>());
}

SDLGraphicsBackend::SDLGraphicsBackend()
    : window_(nullptr),
      gl_context_(nullptr),
      screen_contents_texture_(nullptr),
      screen_contents_texture_valid_(false) {}

void SDLGraphicsBackend::InitSystem(Size screen_size, bool is_fullscreen) {
  SDLSurface::screen_ = std::make_shared<ScreenCanvas>(screen_size);

  SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
#ifdef RLVM_USE_GLES2
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                      SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

  Uint32 window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
  if (is_fullscreen)
    window_flags |= SDL_WINDOW_FULLSCREEN;

  window_ = SDL_CreateWindow("rlvm", SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED, screen_size.width(),
                             screen_size.height(), window_flags);
  if (!window_)
    throw std::runtime_error("SDL_CreateWindow failed: "s + SDL_GetError());

  gl_context_ = SDL_GL_CreateContext(window_);
  if (!gl_context_)
    throw std::runtime_error("SDL_GL_CreateContext failed: "s + SDL_GetError());

  const char* missing = nullptr;
  if (!InitGLFunctions(&missing)) {
    throw std::runtime_error(
        std::string("Failed to load GL function: ") + (missing ? missing : "?"));
  }

  ShowGLErrors();

#if !defined(__APPLE__) && !defined(_WIN32)
  SDL_Surface* icon = IMG_ReadXPMFromArray(rlvm_icon_48);
  if (icon) {
    SDL_SetColorKey(icon, SDL_TRUE,
                    SDL_MapRGB(icon->format, 255, 255, 255));
    SDL_SetWindowIcon(window_, icon);
    SDL_FreeSurface(icon);
  }
#endif
}
void SDLGraphicsBackend::QuitSystem() {
  if (gl_context_) {
    SDL_GL_DeleteContext(gl_context_);
    gl_context_ = nullptr;
  }
  if (window_) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }
}

void SDLGraphicsBackend::Resize(Size display_size, bool is_fullscreen) {
  if (auto fake_screen =
          std::dynamic_pointer_cast<ScreenCanvas>(SDLSurface::screen_)) {
    fake_screen->display_size_ = display_size;
  }

  if (window_) {
    SDL_SetWindowSize(window_, display_size.width(), display_size.height());
    SDL_SetWindowFullscreen(window_,
                            is_fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
  }

  screen_contents_texture_.reset();
  screen_contents_texture_valid_ = false;
}

// -----------------------------------------------------------------------

std::shared_ptr<Surface> SDLGraphicsBackend::CreateSurface(Size size) {
  if (size.width() <= 0 || size.height() <= 0)
    throw std::invalid_argument("Cannot create a surface of size " +
                                size.DebugString());
  return std::make_shared<SDLSurface>(size);
}

std::shared_ptr<Surface> SDLGraphicsBackend::CreateSurfaceBGRA(
    Size size,
    std::span<char> bgra,
    bool is_alpha_mask) {
  // Note to self: These describe the byte order IN THE RAW G00 DATA!
  // These should NOT be switched to native byte order.
  constexpr auto DefaultBpp = 32;
  constexpr auto DefaultAmask = 0xff000000;
  constexpr auto DefaultRmask = 0xff0000;
  constexpr auto DefaultGmask = 0xff00;
  constexpr auto DefaultBmask = 0xff;

  int amask = is_alpha_mask ? DefaultAmask : 0;
  SDL_Surface* tmp = SDL_CreateRGBSurfaceFrom(
      bgra.data(), size.width(), size.height(), DefaultBpp, size.width() * 4,
      DefaultRmask, DefaultGmask, DefaultBmask, amask);

  // SDL 2 dropped the SDL_SRCALPHA / SDL_SRCCOLORKEY / SDL_RLEACCELOK
  // surface flags (alpha/blend/colorkey are now per-attribute setters, not
  // bits OR'd into the surface). Under SDL 2 the convert is just a deep copy
  // in the same pixel format; any blend mode the caller wants is applied to
  // the returned surface separately.
  SDL_Surface* surf = SDL_ConvertSurface(tmp, tmp->format, 0);
  SDL_FreeSurface(tmp);

  return std::make_shared<Surface>(surf);
}

std::shared_ptr<Surface> SDLGraphicsBackend::LoadSurface(
    const std::filesystem::path& pth) {
  std::string raw = LoadFile(pth);
  ImageDecoder dec(raw);

  const auto width = dec.width;
  const auto height = dec.height;

  // do not free until SDL_FreeSurface() is called on the surface using it
  char* mem = dec.mem.data();
  bool is_mask = dec.ismask;
  if (is_mask) {
    int len = width * height;
    uint32_t* d = reinterpret_cast<uint32_t*>(mem);
    int i;
    for (i = 0; i < len; i++) {
      if ((*d & 0xff000000) != 0xff000000)
        break;
      d++;
    }
    if (i == len) {
      is_mask = false;
    }
  }

  std::shared_ptr<SDLSurface> s =
      CreateSurfaceBGRA(Size(width, height), dec.mem, is_mask);

  return std::make_shared<SDLSurface>(s->Release(),
                                      std::move(dec.region_table));
}

std::shared_ptr<Album> SDLGraphicsBackend::LoadAlbum(
    const std::filesystem::path& path) {
  std::string raw = LoadFile(path);
  ImageDecoder dec(raw);

  const auto width = dec.width;
  const auto height = dec.height;

  // do not free until SDL_FreeSurface() is called on the surface using it
  char* mem = dec.mem.data();
  bool is_mask = dec.ismask;
  if (is_mask) {
    int len = width * height;
    uint32_t* d = reinterpret_cast<uint32_t*>(mem);
    int i;
    for (i = 0; i < len; i++) {
      if ((*d & 0xff000000) != 0xff000000)
        break;
      d++;
    }
    if (i == len) {
      is_mask = false;
    }
  }

  std::shared_ptr<SDLSurface> s =
      CreateSurfaceBGRA(Size(width, height), dec.mem, is_mask);

  return std::make_shared<Album>(s, std::move(dec.region_table));
}

void SDLGraphicsBackend::SetWindowTitle(const std::string& title_utf8) {
  if (title_utf8 == current_window_title_)
    return;

  if (window_)
    SDL_SetWindowTitle(window_, title_utf8.c_str());
  current_window_title_ = title_utf8;
}

void SDLGraphicsBackend::ShowSystemCursor(bool /*show*/) {
  // The engine renders its own software cursor (see GetCurrentCursor /
  // draw_cursor in graphics_system.cpp). Showing the OS cursor on top
  // produces a visible duplicate when the game has a custom cursor. 
  // Hide unconditionally — the upstream ShouldUseCustomCursor() toggle 
  //still controls whether the SOFTWARE cursor is drawn.
  SDL_ShowCursor(SDL_DISABLE);
}

void SDLGraphicsBackend::RenderFrame(const RenderFrameConfig& config,
                                     const DrawCallback& draw_scene,
                                     const DrawCallback& draw_renderables,
                                     const DrawCallback& draw_cursor) {
  glRenderer renderer;
  renderer.SetUp();
  renderer.ClearBuffer(std::make_shared<ScreenCanvas>(config.screen_size),
                       RGBAColour(0, 0, 0, 255));
  ShowGLErrors();

  glViewport(0, 0, config.display_size.width(), config.display_size.height());

  // Shaders (glshaders.cpp) write gl_Position directly in NDC, so the
  // fixed-function projection/modelview stack is dead code on the modern
  // pipeline. Dropped along with the GLES migration — those calls aren't
  // available outside a compatibility profile anyway.

  if (draw_scene)
    draw_scene();
  if (draw_renderables)
    draw_renderables();

  if (config.manual_update_mode) {
    if (!screen_contents_texture_)
      screen_contents_texture_ =
          std::make_shared<glTexture>(config.display_size);

    glBindTexture(GL_TEXTURE_2D, screen_contents_texture_->GetID());
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0,
                        config.display_size.width(),
                        config.display_size.height());
    screen_contents_texture_valid_ = true;
  } else {
    screen_contents_texture_valid_ = false;
  }

  if (draw_cursor)
    draw_cursor();

  glFlush();
  SDL_GL_SwapWindow(window_);
  ShowGLErrors();
}

void SDLGraphicsBackend::RedrawLastFrame(const RenderFrameConfig& config,
                                         const DrawCallback& draw_cursor) {
  if (!config.manual_update_mode || !screen_contents_texture_valid_ ||
      !screen_contents_texture_)
    return;

  glRenderer renderer;
  renderer.Render(
      {screen_contents_texture_, Rect(Point(0, 0), config.display_size)},
      {std::make_shared<ScreenCanvas>(config.screen_size),
       Rect(Point(0, 0), config.screen_size)});

  if (draw_cursor)
    draw_cursor();

  SDL_GL_SwapWindow(window_);
  ShowGLErrors();
}

std::shared_ptr<Surface> SDLGraphicsBackend::RenderToSurface(
    const RenderFrameConfig& config,
    const DrawCallback& draw_scene) {
  auto canvas = std::make_shared<glCanvas>(
      config.screen_size, config.display_size, config.screen_origin);
  canvas->Use();

  const std::shared_ptr<glFrameBuffer> original_screen = SDLSurface::screen_;
  SDLSurface::screen_ = canvas->GetBuffer();
  if (draw_scene)
    draw_scene();
  SDLSurface::screen_ = original_screen;

  std::shared_ptr<glTexture> texture = canvas->GetBuffer()->GetTexture();
  const int width = config.screen_size.width();
  const int height = config.screen_size.height();

  std::vector<GLubyte> buf(width * height * 4);
  GLint prev_fbo = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
  GLuint tmp_fbo = 0;
  glGenFramebuffers(1, &tmp_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, tmp_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, texture->GetID(), 0);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));
  glDeleteFramebuffers(1, &tmp_fbo);

  SDL_Surface* surface =
      SDL_CreateRGBSurface(0, width, height, 32, 0xFF000000,
                           0x00FF0000, 0x0000FF00, 0x000000FF);
  if (!surface)
    throw std::runtime_error("SDL_CreateRGBSurface failed");

  for (int y = 0; y < height; ++y) {
    void* dst = static_cast<uint8_t*>(surface->pixels) + y * surface->pitch;
    const void* src = buf.data() + (height - y - 1) * width * 4;
    std::memcpy(dst, src, width * 4);
  }

  return std::make_shared<SDLSurface>(surface);
}
