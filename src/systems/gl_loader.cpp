// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 RLVM contributors
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// -----------------------------------------------------------------------

#include "systems/gl_loader.hpp"

#include <SDL.h>

extern "C" {

#ifdef _WIN32
PFNGLACTIVETEXTUREPROC glActiveTexture = nullptr;
PFNGLBLENDEQUATIONPROC glBlendEquation = nullptr;
#endif

PFNGLBINDBUFFERPROC glBindBuffer = nullptr;
PFNGLBUFFERDATAPROC glBufferData = nullptr;
PFNGLBUFFERSUBDATAPROC glBufferSubData = nullptr;
PFNGLGENBUFFERSPROC glGenBuffers = nullptr;

PFNGLATTACHSHADERPROC glAttachShader = nullptr;
PFNGLCOMPILESHADERPROC glCompileShader = nullptr;
PFNGLCREATEPROGRAMPROC glCreateProgram = nullptr;
PFNGLCREATESHADERPROC glCreateShader = nullptr;
PFNGLDELETEPROGRAMPROC glDeleteProgram = nullptr;
PFNGLDELETESHADERPROC glDeleteShader = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = nullptr;
PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog = nullptr;
PFNGLGETPROGRAMIVPROC glGetProgramiv = nullptr;
PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = nullptr;
PFNGLGETSHADERIVPROC glGetShaderiv = nullptr;
PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = nullptr;
PFNGLLINKPROGRAMPROC glLinkProgram = nullptr;
PFNGLSHADERSOURCEPROC glShaderSource = nullptr;
PFNGLUNIFORM1FPROC glUniform1f = nullptr;
PFNGLUNIFORM1IPROC glUniform1i = nullptr;
PFNGLUNIFORM3FPROC glUniform3f = nullptr;
PFNGLUNIFORM4FPROC glUniform4f = nullptr;
PFNGLUSEPROGRAMPROC glUseProgram = nullptr;
PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = nullptr;

PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer = nullptr;
PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus = nullptr;
PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers = nullptr;
PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D = nullptr;
PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers = nullptr;

PFNGLBINDVERTEXARRAYPROC glBindVertexArray = nullptr;
PFNGLGENVERTEXARRAYSPROC glGenVertexArrays = nullptr;

}  // extern "C"

namespace {

// Load `name` into the pointer `target`. Returns true on success.
template <typename FnPtr>
bool LoadOne(FnPtr& target, const char* name) {
  target = reinterpret_cast<FnPtr>(SDL_GL_GetProcAddress(name));
  return target != nullptr;
}

}  // namespace

#define LOAD(name)                  \
  do {                              \
    if (!LoadOne(name, #name)) {    \
      if (missing_function_out)     \
        *missing_function_out = #name; \
      return false;                 \
    }                               \
  } while (0)

bool InitGLFunctions(const char** missing_function_out) {
#ifdef _WIN32
  LOAD(glActiveTexture);
  LOAD(glBlendEquation);
#endif

  LOAD(glBindBuffer);
  LOAD(glBufferData);
  LOAD(glBufferSubData);
  LOAD(glGenBuffers);

  LOAD(glAttachShader);
  LOAD(glCompileShader);
  LOAD(glCreateProgram);
  LOAD(glCreateShader);
  LOAD(glDeleteProgram);
  LOAD(glDeleteShader);
  LOAD(glEnableVertexAttribArray);
  LOAD(glGetProgramInfoLog);
  LOAD(glGetProgramiv);
  LOAD(glGetShaderInfoLog);
  LOAD(glGetShaderiv);
  LOAD(glGetUniformLocation);
  LOAD(glLinkProgram);
  LOAD(glShaderSource);
  LOAD(glUniform1f);
  LOAD(glUniform1i);
  LOAD(glUniform3f);
  LOAD(glUniform4f);
  LOAD(glUseProgram);
  LOAD(glVertexAttribPointer);

  LOAD(glBindFramebuffer);
  LOAD(glCheckFramebufferStatus);
  LOAD(glDeleteFramebuffers);
  LOAD(glFramebufferTexture2D);
  LOAD(glGenFramebuffers);

  LOAD(glBindVertexArray);
  LOAD(glGenVertexArrays);

  return true;
}
