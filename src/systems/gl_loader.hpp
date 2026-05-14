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

#pragma once

#include <SDL_opengl.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
extern PFNGLACTIVETEXTUREPROC glActiveTexture;
extern PFNGLBLENDEQUATIONPROC glBlendEquation;
#endif

// GL 1.5 — buffer objects
extern PFNGLBINDBUFFERPROC glBindBuffer;
extern PFNGLBUFFERDATAPROC glBufferData;
extern PFNGLBUFFERSUBDATAPROC glBufferSubData;
extern PFNGLGENBUFFERSPROC glGenBuffers;

// GL 2.0 — shaders + vertex attribs
extern PFNGLATTACHSHADERPROC glAttachShader;
extern PFNGLCOMPILESHADERPROC glCompileShader;
extern PFNGLCREATEPROGRAMPROC glCreateProgram;
extern PFNGLCREATESHADERPROC glCreateShader;
extern PFNGLDELETEPROGRAMPROC glDeleteProgram;
extern PFNGLDELETESHADERPROC glDeleteShader;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
extern PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog;
extern PFNGLGETPROGRAMIVPROC glGetProgramiv;
extern PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog;
extern PFNGLGETSHADERIVPROC glGetShaderiv;
extern PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
extern PFNGLLINKPROGRAMPROC glLinkProgram;
extern PFNGLSHADERSOURCEPROC glShaderSource;
extern PFNGLUNIFORM1FPROC glUniform1f;
extern PFNGLUNIFORM1IPROC glUniform1i;
extern PFNGLUNIFORM3FPROC glUniform3f;
extern PFNGLUNIFORM4FPROC glUniform4f;
extern PFNGLUSEPROGRAMPROC glUseProgram;
extern PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;

// GL 3.0 — framebuffer objects (ARB_framebuffer_object promoted to core)
extern PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer;
extern PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus;
extern PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers;
extern PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D;
extern PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers;

// GL 3.0 — vertex array objects
extern PFNGLBINDVERTEXARRAYPROC glBindVertexArray;
extern PFNGLGENVERTEXARRAYSPROC glGenVertexArrays;

#ifdef __cplusplus
}
#endif

// Resolve every function pointer above via SDL_GL_GetProcAddress.
// Returns true on success, false if any required entry point is missing
// (in which case missing_function_out, if non-null, is set to the first
// failing name). A current GL context must exist when this is called.
bool InitGLFunctions(const char** missing_function_out = nullptr);
