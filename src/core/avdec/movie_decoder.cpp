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

#include "core/avdec/movie_decoder.hpp"

#include "pl_mpeg.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace {

void plm_deleter(plm_t* p) {
  if (p) plm_destroy(p);
}

}  // namespace

MovieDecoder::MovieDecoder(const std::filesystem::path& path,
                           AudioCallback audio_cb)
    : plm_(nullptr, plm_deleter), audio_cb_(std::move(audio_cb)) {
  plm_t* raw = plm_create_with_filename(path.string().c_str());
  if (!raw)
    throw std::runtime_error("MovieDecoder: failed to open " + path.string());
  plm_.reset(raw);

  if (!plm_get_video_enabled(plm_.get()))
    throw std::runtime_error("MovieDecoder: no video stream in " +
                             path.string());

  width_ = plm_get_width(plm_.get());
  height_ = plm_get_height(plm_.get());
  framerate_ = plm_get_framerate(plm_.get());
  rgba_buf_.assign(static_cast<size_t>(width_) * height_ * 4, 0);
  for (size_t i = 3; i < rgba_buf_.size(); i += 4)
    rgba_buf_[i] = 255;

  has_audio_ = plm_get_audio_enabled(plm_.get());
  if (has_audio_)
    audio_sample_rate_ = plm_get_samplerate(plm_.get());

  // pl_mpeg doesn't loop on its own once we register callbacks via the
  // time-driven path — and we don't want it to: the long op handles the
  // single-shot lifetime.
  plm_set_loop(plm_.get(), false);

  // Register the C trampolines. pl_mpeg's user-data pointer is set
  // separately via plm_set_*_callback's third argument.
  plm_set_video_decode_callback(plm_.get(), &MovieDecoder::OnVideoFrame, this);
  if (has_audio_ && audio_cb_) {
    plm_set_audio_decode_callback(plm_.get(), &MovieDecoder::OnAudioSamples,
                                  this);
    // Audio decoder defaults to stream 0 (the first MP2 track), which is
    // what every RealLive .mpg uses.
  } else {
    // Save CPU on the audio path if no consumer wants samples.
    plm_set_audio_enabled(plm_.get(), false);
    has_audio_ = false;
  }

  // Lead pl_mpeg's audio output by ~one video-frame's worth so the audio
  // callback has something to hand to SDL2_mixer the first time it fires.
  plm_set_audio_lead_time(plm_.get(),
                          framerate_ > 0 ? 1.0 / framerate_ : 0.04);
}

MovieDecoder::~MovieDecoder() = default;

void MovieDecoder::Step(double elapsed_seconds) {
  if (elapsed_seconds <= 0)
    return;
  // pl_mpeg's plm_decode advances the internal clock by elapsed_seconds,
  // dispatching to the registered video/audio callbacks for any frames
  // whose presentation time falls within the interval. A/V sync is
  // entirely pl_mpeg's responsibility — we just supply wall time.
  plm_decode(plm_.get(), elapsed_seconds);
}

bool MovieDecoder::HasEnded() const { return plm_has_ended(plm_.get()); }

void MovieDecoder::OnVideoFrame(plm_t* /*plm*/,
                                plm_frame_t* frame,
                                void* user) {
  auto* self = static_cast<MovieDecoder*>(user);
  const int row_bytes = self->width_ * 4;
  const int h = self->height_;
  plm_frame_to_rgba(frame, self->rgba_buf_.data(), row_bytes);
  uint8_t* base = self->rgba_buf_.data();
  for (int y = 0; y < h / 2; ++y) {
    uint8_t* a = base + y * row_bytes;
    uint8_t* b = base + (h - 1 - y) * row_bytes;
    std::swap_ranges(a, a + row_bytes, b);
  }
  self->new_frame_ = true;
}

void MovieDecoder::OnAudioSamples(plm_t* /*plm*/,
                                  plm_samples_t* samples,
                                  void* user) {
  auto* self = static_cast<MovieDecoder*>(user);
  // samples->count is per-channel sample count; samples->interleaved
  // holds count*2 float pairs. Forward straight to the consumer.
  if (self->audio_cb_)
    self->audio_cb_(samples->interleaved, static_cast<int>(samples->count));
}
