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

#include "long_operations/play_movie_long_operation.hpp"

#include "core/avdec/movie_decoder.hpp"
#include "core/event.hpp"
#include "log/domain_logger.hpp"
#include "machine/rlmachine.hpp"
#include "systems/base/graphics_system.hpp"
#include "systems/base/system.hpp"
#include "systems/event_system.hpp"
#include "systems/gl_loader.hpp"
#include "systems/glrenderer.hpp"
#include "systems/gltexture.hpp"
#include "systems/screen_canvas.hpp"
#include "systems/sdl/sound_implementor.hpp"
#include "systems/sdl_surface.hpp"

#include <SDL.h>
#include <SDL_mixer.h>

#include <cstring>
#include <variant>

static DomainLogger logger("PlayMovieLongOperation");

PlayMovieLongOperation::PlayMovieLongOperation(
    RLMachine& machine,
    const std::filesystem::path& mpeg_path,
    const Rect& dest)
    : machine_(machine), dest_(dest) {
  try {
    decoder_ = std::make_unique<MovieDecoder>(
        mpeg_path,
        [this](const float* p, int n) { OnDecodedAudio(p, n); });
  } catch (const std::exception& e) {
    logger(Severity::Error)
        << "Failed to open " << mpeg_path.string() << ": " << e.what();
    finished_ = true;
    return;
  }

  if (dest_.size().width() <= 0 || dest_.size().height() <= 0)
    dest_ = Rect(Point(0, 0), machine.GetSystem().graphics().screen_size());

  texture_ = std::make_shared<glTexture>(
      Size(decoder_->Width(), decoder_->Height()), nullptr);

  last_step_ms_ = machine.GetSystem().event().GetTicks();

  if (decoder_->HasAudio()) {
    int dst_freq = 44100;
    Uint16 dst_format = AUDIO_S16SYS;
    int dst_channels = 2;
    if (Mix_QuerySpec(&dst_freq, &dst_format, &dst_channels) == 0) {
      logger(Severity::Warn)
          << "Mix_QuerySpec returned 0; movie audio disabled";
    } else {
      audio_stream_ = SDL_NewAudioStream(
          AUDIO_F32SYS, 2, decoder_->AudioSampleRate(),
          dst_format, static_cast<Uint8>(dst_channels), dst_freq);
      if (!audio_stream_) {
        logger(Severity::Warn) << "SDL_NewAudioStream failed: " << SDL_GetError()
                               << "; movie audio disabled";
      } else {
        SDL_LockAudio();
        Mix_HookMusic(&PlayMovieLongOperation::AudioCallback, this);
        SDL_UnlockAudio();
        audio_active_ = true;
      }
    }
  }
  // Take ownership of the frame from ExecuteGraphicsSystem so the engine
  // doesn't redraw the game scene over us every main-loop tick. The
  // destructor restores normal updates.
  machine.GetSystem().graphics().set_is_responsible_for_update(false);
}

PlayMovieLongOperation::~PlayMovieLongOperation() {
  if (audio_active_) {
    // Hold the SDL audio mutex while swapping the music hook back so any
    // in-flight callback finishes before |this| (and audio_mutex_) goes
    // out of scope.
    SDL_LockAudio();
    SDLSoundImpl::RestoreBgmHook();
    SDL_UnlockAudio();
  }
  if (audio_stream_)
    SDL_FreeAudioStream(audio_stream_);
  machine_.GetSystem().graphics().set_is_responsible_for_update(true);
}

void PlayMovieLongOperation::OnEvent(std::shared_ptr<Event> event) {
  if (!event)
    return;
  std::visit(
      [this](auto&& e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, KeyDown> ||
                      std::is_same_v<T, MouseDown>) {
          finished_ = true;
        }
      },
      *event);
}

bool PlayMovieLongOperation::operator()(RLMachine& machine) {
  if (finished_)
    return true;
  if (machine.GetSystem().ShouldFastForward()) {
    finished_ = true;
    return true;
  }
  if (!decoder_ || !texture_ || !SDLSurface::screen_) {
    logger(Severity::Error) << "movie playback aborted: decoder="
                            << (decoder_ ? "ok" : "null")
                            << " texture=" << (texture_ ? "ok" : "null")
                            << " screen="
                            << (SDLSurface::screen_ ? "ok" : "null");
    return true;
  }
  // Re-assert each tick — Effect's destructor flips this back to true and
  // we may be running concurrently with effects queued before or after.
  machine.GetSystem().graphics().set_is_responsible_for_update(false);

  const unsigned int now = machine.GetSystem().event().GetTicks();
  double elapsed_s = (now - last_step_ms_) / 1000.0;
  // Stale last_step_ms_ on the first tick (set in the ctor) could ask
  // pl_mpeg to decode the entire movie in one Step(); clamp.
  if (elapsed_s < 0 || elapsed_s > 0.25)
    elapsed_s = 1.0 / 30.0;
  last_step_ms_ = now;

  decoder_->Step(elapsed_s);

  if (decoder_->HasNewFrame()) {
    texture_->Write(Rect(Point(0, 0),
                         Size(decoder_->Width(), decoder_->Height())),
                    GL_RGBA, GL_UNSIGNED_BYTE, decoder_->Frame().data());
    decoder_->ConsumeFrame();
  }

  GraphicsSystem& graphics = machine.GetSystem().graphics();
  graphics.RenderCustomFrame([&]() {
    glRenderer renderer;
    renderer.Render(
        {texture_, Rect(Point(0, 0),
                        Size(decoder_->Width(), decoder_->Height()))},
        {SDLSurface::screen_, dest_});
  });

  if (decoder_->HasEnded()) {
    finished_ = true;
    return true;
  }
  return false;
}

void PlayMovieLongOperation::OnDecodedAudio(const float* pairs, int n) {
  if (!audio_stream_)
    return;
  std::lock_guard<std::mutex> lock(audio_mutex_);
  SDL_AudioStreamPut(audio_stream_, pairs, n * 2 * sizeof(float));
}

void PlayMovieLongOperation::AudioCallback(void* userdata,
                                           unsigned char* stream,
                                           int len) {
  auto* self = static_cast<PlayMovieLongOperation*>(userdata);
  if (!self || !self->audio_stream_) {
    std::memset(stream, 0, len);
    return;
  }
  std::lock_guard<std::mutex> lock(self->audio_mutex_);
  const int got = SDL_AudioStreamGet(self->audio_stream_, stream, len);
  if (got < 0) {
    std::memset(stream, 0, len);
    return;
  }
  if (got < len)
    std::memset(stream + got, 0, len - got);
}
