// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2007 Elliot Glaysher
// Copyright (C) 2026 RLVM contributors
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

#include "modules/module_mov.hpp"

#include "core/asset_scanner.hpp"
#include "core/rect.hpp"
#include "libreallive/elements/command.hpp"
#include "libreallive/expression.hpp"
#include "log/domain_logger.hpp"
#include "long_operations/play_movie_long_operation.hpp"
#include "machine/rlmachine.hpp"
#include "machine/rloperation.hpp"
#include "machine/rloperation/rlop_store.hpp"
#include "systems/base/system.hpp"

#include <filesystem>
#include <set>
#include <string>

// -----------------------------------------------------------------------

namespace {

static DomainLogger logger("Mov");

std::filesystem::path ResolveMovie(RLMachine& machine,
                                   const std::string& filename) {
  auto scanner = machine.GetSystem().GetAssetScanner();
  if (!scanner)
    return {};
  auto result = scanner->FindFile(filename, {"mpg", "mpeg"});
  if (!result.has_value())
    return {};
  return *result;
}

// Shared dispatch for every movPlay* variant. The RealLive bytecode for
// these opcodes varies by game and overload — we don't have authoritative
// signature documentation, so we extract whatever parameters are present
// (string filename + 0 or 4 ints for the rect) and ignore extras like the
// chroma-key colorkey on movPlayExC. Variants that need looping or
// background semantics still play once and block, since concurrent
// playback isn't implemented.
void DispatchMovPlay(RLMachine& machine,
                     const libreallive::CommandElement& f,
                     const char* op_name) {
  const auto& params = f.GetParsedParameters();
  if (params.empty()) {
    logger(Severity::Warn) << op_name << ": called with no parameters";
    return;
  }

  std::string filename;
  try {
    filename = params[0]->GetStringValue(machine);
  } catch (const std::exception& e) {
    logger(Severity::Warn) << op_name << ": cannot extract filename: "
                           << e.what();
    return;
  }

  Rect dest;
  if (params.size() >= 5) {
    try {
      const int x1 = params[1]->GetIntegerValue(machine);
      const int y1 = params[2]->GetIntegerValue(machine);
      const int x2 = params[3]->GetIntegerValue(machine);
      const int y2 = params[4]->GetIntegerValue(machine);
      dest = Rect::GRP(x1, y1, x2, y2);
    } catch (const std::exception& e) {
      logger(Severity::Warn) << op_name << ": cannot extract rect: "
                             << e.what();
      dest = Rect();
    }
  }

  auto path = ResolveMovie(machine, filename);
  if (path.empty()) {
    logger(Severity::Warn)
        << op_name << ": cannot find movie '" << filename << "'";
    return;
  }
  machine.PushLongOperation(
      std::make_shared<PlayMovieLongOperation>(machine, path, dest));
}

struct movPlay : public RLOp_SpecialCase {
  void operator()(RLMachine& machine,
                  const libreallive::CommandElement& f) override {
    DispatchMovPlay(machine, f, "movPlay");
  }
};

struct movPlayEx : public RLOp_SpecialCase {
  void operator()(RLMachine& machine,
                  const libreallive::CommandElement& f) override {
    DispatchMovPlay(machine, f, "movPlayEx");
  }
};

struct movLoop : public RLOp_SpecialCase {
  void operator()(RLMachine& machine,
                  const libreallive::CommandElement& f) override {
    DispatchMovPlay(machine, f, "movLoop");
  }
};

struct movPlayExC : public RLOp_SpecialCase {
  void operator()(RLMachine& machine,
                  const libreallive::CommandElement& f) override {
    DispatchMovPlay(machine, f, "movPlayExC");
  }
};

struct movWait : public RLOpcode<> {
  void operator()(RLMachine&) {}
};

struct movPlaying : public RLStoreOpcode<> {
  int operator()(RLMachine&) { return 0; }
};

struct movStop : public RLOpcode<> {
  void operator()(RLMachine&) {}
};

}  // namespace

// -----------------------------------------------------------------------

MovModule::MovModule() : RLModule("Mov", 1, 26) {
  AddOpcode(0, 0, "movPlay", new movPlay);
  AddOpcode(1, 0, "movPlayEx", new movPlayEx);
  AddOpcode(2, 0, "movLoop", new movLoop);
  AddOpcode(3, 0, "movWait", new movWait);
  AddOpcode(4, 0, "movPlaying", new movPlaying);
  AddOpcode(5, 0, "movStop", new movStop);
  AddOpcode(20, 0, "movPlayExC", new movPlayExC);
}
