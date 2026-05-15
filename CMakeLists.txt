cmake_minimum_required(VERSION 3.18)

project(RLVM VERSION 0.14.7 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
option(RLVM_BUILD_TESTS "Build unit test program" OFF)
option(RLVM_FULLSTATIC "Build executables that are linked statically" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

if(RLVM_FULLSTATIC)
  set(BUILD_SHARED_LIBS OFF)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")
  set(CMAKE_FIND_LIBRARY_SUFFIXES .a .lib ${CMAKE_FIND_LIBRARY_SUFFIXES})
  set(CMAKE_LINK_SEARCH_START_STATIC ON)
  set(CMAKE_LINK_SEARCH_END_STATIC ON)

  message(WARNING "Building static targets. This is currently untested and not guaranteed to work.")
endif()

find_package(PkgConfig REQUIRED)
if(RLVM_FULLSTATIC)
  set(_PKG_CONFIG_ARGS "--static")
else()
  set(_PKG_CONFIG_ARGS "")
endif()

enable_testing()

# -------------------------------------------------------------------
# Find OpenGL Library
# -------------------------------------------------------------------
find_package(OpenGL REQUIRED)
if (OpenGL_FOUND)
  message(STATUS "OpenGL found: ${OpenGL_VERSION}")
  include_directories(${OpenGL_INCLUDE_DIRS})
else()
  message(FATAL_ERROR "OpenGL not found. Please install OpenGL.")
endif()

# -------------------------------------------------------------------
# Find SDL2 + satellites
# -------------------------------------------------------------------
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(SDL2_IMAGE REQUIRED SDL2_image)
pkg_check_modules(SDL2_MIXER REQUIRED SDL2_mixer)
pkg_check_modules(SDL2_TTF REQUIRED SDL2_ttf)
include_directories(
  ${SDL2_INCLUDE_DIRS}
  ${SDL2_IMAGE_INCLUDE_DIRS}
  ${SDL2_MIXER_INCLUDE_DIRS}
  ${SDL2_TTF_INCLUDE_DIRS}
)
link_directories(
  ${SDL2_LIBRARY_DIRS}
  ${SDL2_IMAGE_LIBRARY_DIRS}
  ${SDL2_MIXER_LIBRARY_DIRS}
  ${SDL2_TTF_LIBRARY_DIRS}
)

# -------------------------------------------------------------------
# Find Boost Libraries
# -------------------------------------------------------------------
set(BOOST_COMPONENTS program_options serialization iostreams filesystem log)
find_package(Boost REQUIRED COMPONENTS ${BOOST_COMPONENTS})

if (Boost_FOUND)
  message(STATUS "Boost found: ${Boost_VERSION}")
  include_directories(${Boost_INCLUDE_DIRS})
else()
  message(FATAL_ERROR "Boost libraries not found. Please install Boost.")
endif()

# -------------------------------------------------------------------
# Find Freetype2 Library
# -------------------------------------------------------------------
find_package(Freetype REQUIRED)

if (Freetype_FOUND)
  message(STATUS "Freetype2 found: ${FREETYPE_VERSION_STRING}")
  include_directories(${FREETYPE_INCLUDE_DIRS})
else()
  message(FATAL_ERROR "Freetype2 not found. Please install Freetype2 development libraries.")
endif()


# -------------------------------------------------------------------
# Find zlib Library
# -------------------------------------------------------------------
find_package(ZLIB REQUIRED)

if (ZLIB_FOUND)
  message(STATUS "zlib found: ${ZLIB_VERSION}")
  include_directories(${ZLIB_INCLUDE_DIRS})
else()
  message(FATAL_ERROR "zlib not found. Please install zlib development libraries.")
endif()

# -------------------------------------------------------------------
# Find JPEG Library
# -------------------------------------------------------------------
find_package(JPEG REQUIRED)

if (JPEG_FOUND)
  message(STATUS "libjpeg found")
  include_directories(${JPEG_INCLUDE_DIR})
else()
  message(FATAL_ERROR "libjpeg not found. Please install libjpeg (or libjpeg-turbo) development libraries.")
endif()

# -------------------------------------------------------------------
# Find Vorbis Library
# -------------------------------------------------------------------
find_package(VorbisFile REQUIRED)

if (VorbisFile_FOUND)
  message(STATUS "VorbisFile found")
  include_directories(${VorbisFile_INCLUDE_DIRS})
else()
  message(FATAL_ERROR "VorbisFile not found. Please install VorbisFile development libraries.")
endif()

# -------------------------------------------------------------------
# Find GTK 2.0 Library (optional)
# -------------------------------------------------------------------
find_package(GTK2 QUIET)

if (GTK2_FOUND)
  message(STATUS "GTK 2.0 found, version: ${GTK2_VERSION}")
  include_directories(${GTK2_INCLUDE_DIRS})
else()
  message(STATUS "GTK 2.0 not found. Continuing without GTK 2.0 support.")
endif()

# -------------------------------------------------------------------
# pl_mpeg — single-header MPEG-1 video + MP2 audio decoder for movPlay /
# movPlayEx (the .mpg files RealLive games ship in gamedata/mov/). Fetched
# via FetchContent at configure time so the source tree stays clean and we
# don't need to vendor 140 KB of upstream code as a committed file.
# -------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
  pl_mpeg
  GIT_REPOSITORY https://github.com/phoboslab/pl_mpeg.git
  # Pinned to the last commit before phoboslab/pl_mpeg#55 (seek/tell
  # callback support) and #56 (PLM_NO_STDIO). Those refactors moved the
  # plm_buffer_seek_callback / plm_buffer_tell_callback typedefs out of
  # the public header section, so any consumer compiling with modern GCC
  # (which errors on implicit-int and unknown-type-name) now fails to
  # build pl_mpeg.h. 6d4733ae is "Reset has_ended flag when rewinding",
  # 2025-01-24, the latest commit that compiles cleanly under GCC 13+
  # without -Wno-implicit-function-declaration noise.
  GIT_TAG        6d4733aed57e29d12344b926ca478c9d770badfb
)
FetchContent_MakeAvailable(pl_mpeg)

# pl_mpeg.h is "everything inside one header" — to get the implementation
# compiled, exactly one TU must define PL_MPEG_IMPLEMENTATION before the
# include. Generate that shim TU here so we don't need to commit a stub
# file to vendor/.
set(_pl_mpeg_impl "${CMAKE_BINARY_DIR}/pl_mpeg_impl.c")
file(WRITE "${_pl_mpeg_impl}"
  "#define PL_MPEG_IMPLEMENTATION\n"
  "#include \"pl_mpeg.h\"\n"
)
add_library(pl_mpeg STATIC "${_pl_mpeg_impl}")
target_include_directories(pl_mpeg PUBLIC "${pl_mpeg_SOURCE_DIR}")
# pl_mpeg uses libm
target_link_libraries(pl_mpeg PUBLIC m)

# -------------------------------------------------------------------
# Add Subdirectories to Build
# -------------------------------------------------------------------
include_directories(vendor)
add_subdirectory(vendor)
add_subdirectory(src)
if(RLVM_BUILD_TESTS)
  add_subdirectory(test)
endif()
