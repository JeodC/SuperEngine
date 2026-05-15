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

#include <filesystem>
#include <memory>
#include <mutex>

#include "core/rect.hpp"
#include "machine/long_operation.hpp"

struct _SDL_AudioStream;
typedef struct _SDL_AudioStream SDL_AudioStream;

class MovieDecoder;
class RLMachine;
class glTexture;

// LongOperation that plays back an MPEG-1 video file (the .mpg files
// RealLive games ship in gamedata/mov/) into a target rect on screen.
// pl_mpeg drives A/V sync internally — we just feed it wall-clock deltas
// each tick and it dispatches video frames + audio chunks at the right
// times. Returns when the movie ends, the user clicks / hits a key, or
// the engine enters fast-forward mode.
class PlayMovieLongOperation : public LongOperation {
 public:
  // |dest| of empty/zero size means "fullscreen at the engine's screen
  // resolution" (movPlay). Otherwise plays into the given rect (movPlayEx).
  PlayMovieLongOperation(RLMachine& machine,
                         const std::filesystem::path& mpeg_path,
                         const Rect& dest);
  ~PlayMovieLongOperation() override;

  // EventListener
  void OnEvent(std::shared_ptr<Event> event) override;

  // LongOperation
  bool operator()(RLMachine& machine) override;

 private:
  // Audio thread callback installed via Mix_HookMusic. Drains the audio
  // stream under audio_mutex_ and writes whatever PCM format Mix_QuerySpec
  // reported into |stream|.
  static void AudioCallback(void* userdata, unsigned char* stream, int len);

  // Called synchronously from MovieDecoder::Step on the engine main thread
  // when pl_mpeg decodes a new audio chunk. Pushes the float pairs into
  // audio_stream_ (which handles format/rate conversion) under the mutex.
  void OnDecodedAudio(const float* interleaved_pairs, int sample_pair_count);

  RLMachine& machine_;
  Rect dest_;

  std::unique_ptr<MovieDecoder> decoder_;
  std::shared_ptr<glTexture> texture_;

  std::mutex audio_mutex_;
  SDL_AudioStream* audio_stream_ = nullptr;
  bool audio_active_ = false;

  unsigned int last_step_ms_ = 0;
  bool finished_ = false;
};
