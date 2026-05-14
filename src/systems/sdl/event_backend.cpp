// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2025 Serina Sakurai
// Copyright (C) 2006 Elliot Glaysher
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

#include "systems/sdl/event_backend.hpp"

#include <SDL.h>
#include <SDL_events.h>

namespace {

inline MouseButton fromSDLButton(Uint8 sdlButton) {
  switch (sdlButton) {
    case SDL_BUTTON_LEFT:
      return MouseButton::LEFT;
    case SDL_BUTTON_RIGHT:
      return MouseButton::RIGHT;
    case SDL_BUTTON_MIDDLE:
      return MouseButton::MIDDLE;
    default:
      return MouseButton::NONE;
  }
}

// The KeyCode enum was lifted verbatim from SDL 1.2's SDLKey numeric values.
// SDL 2 renumbered all non-ASCII keys (arrows, modifiers, function keys, ...)
// to live above 0x40000000, so we translate at the boundary instead of
// renumbering the engine-facing enum (which would invalidate save data).
inline KeyCode fromSDLKey(SDL_Keycode k) {
  // ASCII range maps directly.
  if (k >= 0 && k < 0x80)
    return static_cast<KeyCode>(k);
  switch (k) {
    // Numeric keypad
    case SDLK_KP_0:        return static_cast<KeyCode>(256);
    case SDLK_KP_1:        return static_cast<KeyCode>(257);
    case SDLK_KP_2:        return static_cast<KeyCode>(258);
    case SDLK_KP_3:        return static_cast<KeyCode>(259);
    case SDLK_KP_4:        return static_cast<KeyCode>(260);
    case SDLK_KP_5:        return static_cast<KeyCode>(261);
    case SDLK_KP_6:        return static_cast<KeyCode>(262);
    case SDLK_KP_7:        return static_cast<KeyCode>(263);
    case SDLK_KP_8:        return static_cast<KeyCode>(264);
    case SDLK_KP_9:        return static_cast<KeyCode>(265);
    case SDLK_KP_PERIOD:   return static_cast<KeyCode>(266);
    case SDLK_KP_DIVIDE:   return static_cast<KeyCode>(267);
    case SDLK_KP_MULTIPLY: return static_cast<KeyCode>(268);
    case SDLK_KP_MINUS:    return static_cast<KeyCode>(269);
    case SDLK_KP_PLUS:     return static_cast<KeyCode>(270);
    case SDLK_KP_ENTER:    return static_cast<KeyCode>(271);
    case SDLK_KP_EQUALS:   return static_cast<KeyCode>(272);
    // Arrows + Home/End pad
    case SDLK_UP:          return static_cast<KeyCode>(273);
    case SDLK_DOWN:        return static_cast<KeyCode>(274);
    case SDLK_RIGHT:       return static_cast<KeyCode>(275);
    case SDLK_LEFT:        return static_cast<KeyCode>(276);
    case SDLK_INSERT:      return static_cast<KeyCode>(277);
    case SDLK_HOME:        return static_cast<KeyCode>(278);
    case SDLK_END:         return static_cast<KeyCode>(279);
    case SDLK_PAGEUP:      return static_cast<KeyCode>(280);
    case SDLK_PAGEDOWN:    return static_cast<KeyCode>(281);
    // Function keys
    case SDLK_F1:          return static_cast<KeyCode>(282);
    case SDLK_F2:          return static_cast<KeyCode>(283);
    case SDLK_F3:          return static_cast<KeyCode>(284);
    case SDLK_F4:          return static_cast<KeyCode>(285);
    case SDLK_F5:          return static_cast<KeyCode>(286);
    case SDLK_F6:          return static_cast<KeyCode>(287);
    case SDLK_F7:          return static_cast<KeyCode>(288);
    case SDLK_F8:          return static_cast<KeyCode>(289);
    case SDLK_F9:          return static_cast<KeyCode>(290);
    case SDLK_F10:         return static_cast<KeyCode>(291);
    case SDLK_F11:         return static_cast<KeyCode>(292);
    case SDLK_F12:         return static_cast<KeyCode>(293);
    case SDLK_F13:         return static_cast<KeyCode>(294);
    case SDLK_F14:         return static_cast<KeyCode>(295);
    case SDLK_F15:         return static_cast<KeyCode>(296);
    // Key state modifier keys. SDL 2 dropped meta in favor of GUI; map to the
    // SDL 1.2 META slot so any engine-side bindings keep working.
    case SDLK_NUMLOCKCLEAR: return static_cast<KeyCode>(300);
    case SDLK_CAPSLOCK:     return static_cast<KeyCode>(301);
    case SDLK_SCROLLLOCK:   return static_cast<KeyCode>(302);
    case SDLK_RSHIFT:       return static_cast<KeyCode>(303);
    case SDLK_LSHIFT:       return static_cast<KeyCode>(304);
    case SDLK_RCTRL:        return static_cast<KeyCode>(305);
    case SDLK_LCTRL:        return static_cast<KeyCode>(306);
    case SDLK_RALT:         return static_cast<KeyCode>(307);
    case SDLK_LALT:         return static_cast<KeyCode>(308);
    case SDLK_RGUI:         return static_cast<KeyCode>(309);  // RMETA
    case SDLK_LGUI:         return static_cast<KeyCode>(310);  // LMETA
    case SDLK_MODE:         return static_cast<KeyCode>(313);
    // Misc
    case SDLK_HELP:         return static_cast<KeyCode>(315);
    case SDLK_PRINTSCREEN:  return static_cast<KeyCode>(316);
    case SDLK_SYSREQ:       return static_cast<KeyCode>(317);
    case SDLK_PAUSE:        return KeyCode::PAUSE;  // SDL 1.2 PAUSE was 19
    case SDLK_MENU:         return static_cast<KeyCode>(319);
    case SDLK_POWER:        return static_cast<KeyCode>(320);
    case SDLK_UNDO:         return static_cast<KeyCode>(322);
    default:
      return KeyCode::UNKNOWN;
  }
}

Event translateSDLToEvent(const SDL_Event& sdlEvent) {
  switch (sdlEvent.type) {
    case SDL_QUIT:
      return Quit{};

    // SDL 2 collapsed VIDEOEXPOSE / VIDEORESIZE / ACTIVEEVENT into per-window
    // sub-events under SDL_WINDOWEVENT. We dispatch on .window.event.
    case SDL_WINDOWEVENT: {
      switch (sdlEvent.window.event) {
        case SDL_WINDOWEVENT_EXPOSED:
          return VideoExpose{};
        case SDL_WINDOWEVENT_RESIZED:
        case SDL_WINDOWEVENT_SIZE_CHANGED:
          return VideoResize{
              Size{sdlEvent.window.data1, sdlEvent.window.data2}};
        case SDL_WINDOWEVENT_FOCUS_GAINED:
          // Mirrors the SDL 1.2 SDL_APPINPUTFOCUS path (assume mouse inside).
          return Active{true};
        case SDL_WINDOWEVENT_ENTER:
          return Active{true};
        case SDL_WINDOWEVENT_LEAVE:
          return Active{false};
        default:
          return std::monostate{};
      }
    }

    case SDL_KEYDOWN: {
      KeyDown kd;
      kd.code = fromSDLKey(sdlEvent.key.keysym.sym);
      return kd;
    }
    case SDL_KEYUP: {
      KeyUp ku;
      ku.code = fromSDLKey(sdlEvent.key.keysym.sym);
      return ku;
    }

    case SDL_MOUSEBUTTONDOWN: {
      MouseDown md;
      md.button = fromSDLButton(sdlEvent.button.button);
      return md;
    }
    case SDL_MOUSEBUTTONUP: {
      MouseUp mu;
      mu.button = fromSDLButton(sdlEvent.button.button);
      return mu;
    }

    case SDL_MOUSEMOTION: {
      MouseMotion mm;
      mm.pos = {sdlEvent.motion.x, sdlEvent.motion.y};
      return mm;
    }

    // SDL 2 emits SDL_MOUSEWHEEL as its own event type instead of fake button
    // 4/5 presses. The SDL 1.2 path here had the wheel cases commented out,
    // so we keep the no-op behavior for now and translate to monostate.
    case SDL_MOUSEWHEEL:
      return std::monostate{};

    // Unhandled event type
    default:
      return std::monostate{};
  }
}

}  // namespace

SDLEventBackend::SDLEventBackend() = default;

std::shared_ptr<Event> SDLEventBackend::PollEvent() {
  SDL_Event event;
  if (SDL_PollEvent(&event))
    return std::make_shared<Event>(translateSDLToEvent(event));
  return std::make_shared<Event>(std::monostate());
}
