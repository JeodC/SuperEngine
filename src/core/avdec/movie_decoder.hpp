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

// Thin C++ wrapper around pl_mpeg (single-header MPEG-1 video + MP2 audio
// decoder, MIT-licensed). Used to play the .mpg files RealLive games ship
// in gamedata/mov/ via the movPlay / movPlayEx opcodes.
//
// Format profile (verified against an actual RealLive-era MPEG-1 file):
//   container:  MPEG-1 Program Stream
//   video:      mpeg1video, yuv420p, native game resolution, ~30 fps
//   audio:      mp2, 44100 Hz, stereo, s16p, ~224 kbps
//
// pl_mpeg handles A/V sync internally — we feed it wall-clock seconds via
// Step(), and it fires our registered callbacks for video frames and audio
// chunks at the correct times. No frame-skipping logic on our side.

#include "pl_mpeg.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

class MovieDecoder {
 public:
  // Invoked from inside Step() when pl_mpeg produces interleaved float
  // stereo audio pairs. Called on the engine main thread.
  using AudioCallback =
      std::function<void(const float* interleaved_pairs, int sample_pair_count)>;

  // Throws std::runtime_error if the file can't be opened or has no video.
  // The audio callback (if set) is invoked synchronously from Step() and
  // owns whatever buffering / format conversion the consumer needs.
  MovieDecoder(const std::filesystem::path& path, AudioCallback audio_cb = {});
  ~MovieDecoder();

  MovieDecoder(const MovieDecoder&) = delete;
  MovieDecoder& operator=(const MovieDecoder&) = delete;

  int Width() const { return width_; }
  int Height() const { return height_; }
  double Framerate() const { return framerate_; }
  bool HasAudio() const { return has_audio_; }
  int AudioSampleRate() const { return audio_sample_rate_; }

  // Advance the decoder by elapsed_seconds of wall time. pl_mpeg fires
  // its internal video/audio callbacks for any frames whose presentation
  // timestamp falls within this interval; we capture the most recent
  // video frame in rgba_buf_ and forward audio to the AudioCallback.
  void Step(double elapsed_seconds);

  // True after Step() has produced at least one new frame that hasn't
  // been consumed via Frame() / ConsumeFrame().
  bool HasNewFrame() const { return new_frame_; }
  // RGBA bytes, width*height*4. Valid as long as the decoder is alive.
  const std::vector<uint8_t>& Frame() const { return rgba_buf_; }
  void ConsumeFrame() { new_frame_ = false; }

  bool HasEnded() const;

 private:
  // C trampolines registered with pl_mpeg; both forward to the instance
  // pointer stored as pl_mpeg's user data.
  static void OnVideoFrame(plm_t* plm, plm_frame_t* frame, void* user);
  static void OnAudioSamples(plm_t* plm, plm_samples_t* samples, void* user);

  std::unique_ptr<plm_t, void (*)(plm_t*)> plm_;
  int width_ = 0;
  int height_ = 0;
  double framerate_ = 0.0;
  bool has_audio_ = false;
  int audio_sample_rate_ = 0;

  std::vector<uint8_t> rgba_buf_;
  bool new_frame_ = false;
  AudioCallback audio_cb_;
};
