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

#include "modules/module_sel.hpp"

#include <iterator>
#include <string>
#include <vector>

#include "core/gameexe.hpp"
#include "core/rlevent_listener.hpp"
#include "libreallive/parser.hpp"
#include "long_operations/button_object_select_long_operation.hpp"
#include "long_operations/select_long_operation.hpp"
#include "machine/rlmachine.hpp"
#include "machine/rloperation.hpp"
#include "machine/rloperation/rlop_store.hpp"
#include "object/drawer/parent.hpp"
#include "object/objdrawer.hpp"
#include "systems/base/graphics_object.hpp"
#include "systems/base/graphics_system.hpp"
#include "systems/base/system.hpp"
#include "systems/base/text_system.hpp"
#include "systems/base/text_window.hpp"
#include "systems/event_system.hpp"
#include "utilities/lazy_array.hpp"
#include "utilities/string_utilities.hpp"

using libreallive::CommandElement;
using libreallive::SelectElement;

namespace {

// TODO: All MarkSavepoint() function calls in this file won't work correctly as
// the IP has already move past the select instruction.
// For now, temporarily hack this, but fix it properly later.
inline void MarkSavepoint(RLMachine& machine) {
  machine.RevertIP();
  machine.MarkSavepoint();
  machine.AdvanceIP();
}

struct Sel_select : public RLOp_SpecialCase {
  void operator()(RLMachine& machine, const CommandElement& ce) override {
    if (machine.GetScenarioConfig().enable_selcom_savepoint)
      MarkSavepoint(machine);

    const SelectElement& element = dynamic_cast<const SelectElement&>(ce);
    machine.PushLongOperation(
        std::make_shared<NormalSelectLongOperation>(machine, element));
  }
};

struct Sel_select_s : public RLOp_SpecialCase {
  void operator()(RLMachine& machine, const CommandElement& ce) override {
    if (machine.GetScenarioConfig().enable_selcom_savepoint)
      MarkSavepoint(machine);

    const SelectElement& element = dynamic_cast<const SelectElement&>(ce);
    auto btn_select =
        std::make_shared<ButtonSelectLongOperation>(machine, element, 0);
    machine.PushLongOperation(btn_select);
    machine.GetSystem().graphics().AddRenderable(btn_select);
  }
};

struct ClearAndRestoreWindow : public LongOperation {
  int to_restore_;
  explicit ClearAndRestoreWindow(int in) : to_restore_(in) {}

  bool operator()(RLMachine& machine) override {
    machine.GetSystem().text().HideAllTextWindows();
    machine.GetSystem().text().set_active_window(to_restore_);
    return true;
  }
};

struct Sel_select_w : public RLOp_SpecialCase {
  void operator()(RLMachine& machine, const CommandElement& ce) override {
    if (machine.GetScenarioConfig().enable_selcom_savepoint)
      MarkSavepoint(machine);

    const SelectElement& element = dynamic_cast<const SelectElement&>(ce);

    // Sometimes the RL bytecode will override DEFAULT_SEL_WINDOW.
    int window =
        machine.GetSystem().gameexe()("DEFAULT_SEL_WINDOW").Int().value_or(-1);
    libreallive::Expression window_exp = element.GetWindowExpression();
    int computed = window_exp->GetIntegerValue(machine);
    if (computed != -1)
      window = computed;

    // Restore the previous text state after the select operation completes.
    TextSystem& text = machine.GetSystem().text();
    int active_window = text.active_window();
    text.HideAllTextWindows();
    text.set_active_window(window);
    machine.PushLongOperation(
        std::make_shared<ClearAndRestoreWindow>(active_window));

    machine.PushLongOperation(
        std::make_shared<NormalSelectLongOperation>(machine, element));
  }
};

struct Sel_select_objbtn : public RLOpcode<IntConstant_T> {
  void operator()(RLMachine& machine, int group) {
    if (machine.GetScenarioConfig().enable_selcom_savepoint)
      MarkSavepoint(machine);

    machine.PushLongOperation(
        std::make_shared<ButtonObjectSelectLongOperation>(machine, group));
  }
};

struct Sel_select_objbtn_cancel_0 : public RLOpcode<IntConstant_T> {
  void operator()(RLMachine& machine, int group) {
    if (machine.GetScenarioConfig().enable_selcom_savepoint)
      MarkSavepoint(machine);

    auto obj =
        std::make_shared<ButtonObjectSelectLongOperation>(machine, group);
    obj->set_cancelable();
    machine.PushLongOperation(obj);
  }
};

struct Sel_select_objbtn_cancel_1
    : public RLOpcode<IntConstant_T, IntConstant_T> {
  void operator()(RLMachine& machine, int group, int se) {
    if (machine.GetScenarioConfig().enable_selcom_savepoint)
      MarkSavepoint(machine);

    auto obj =
        std::make_shared<ButtonObjectSelectLongOperation>(machine, group);
    obj->set_cancelable();
    machine.PushLongOperation(obj);
  }
};

struct Sel_select_objbtn_cancel_2 : public RLOpcode<> {
  void operator()(RLMachine& machine) {
    if (machine.GetScenarioConfig().enable_selcom_savepoint)
      MarkSavepoint(machine);

    auto& fg_objs = machine.GetSystem().graphics().GetForegroundObjects();
    int group = 0;
    for (GraphicsObject& obj : fg_objs)
      if (obj.Param().IsButton()) {
        group = obj.Param().GetButtonGroup();
        break;
      }

    auto obj =
        std::make_shared<ButtonObjectSelectLongOperation>(machine, group);
    obj->set_cancelable();
    machine.PushLongOperation(obj);
  }
};

// objbtn_init(group): register the active button group for the
// btnobjnow_{hit,push,decide} polling opcodes below. The script doesn't
// have a way to pass the group through the polling calls, so the engine
// remembers it on the System.
struct objbtn_init_0 : public RLOpcode<IntConstant_T> {
  void operator()(RLMachine& machine, int group) {
    machine.GetSystem().set_objbtn_polling_group(group);
  }
};

// No-arg variant: leave the group at whatever was previously set.
struct objbtn_init_1 : public RLOpcode<> {
  void operator()(RLMachine& machine) {}
};

namespace {

// Find the topmost button (in the polling group) currently under |cursor|.
// Scans foreground first, then background; returns the inner GraphicsObject
// (and its parent if any) so the caller can read button_number / etc.
//
// The look-up has to mirror what ButtonObjectSelectLongOperation does —
// scenes can place button objects directly on a layer, or as children of
// a parent obj (objOfChild). SS's options menu uses parent-with-children
// in the BG layer (scene 1002), so we have to handle both.
GraphicsObject* FindButtonAt(System& sys, int group, Point cursor) {
  GraphicsObject* hit = nullptr;
  auto try_obj = [&](GraphicsObject& obj, GraphicsObject* parent) {
    if (!obj.has_object_data())
      return;
    if (!obj.Param().IsButton())
      return;
    if (obj.Param().GetButtonGroup() != group)
      return;
    GraphicsObjectData& data = obj.GetObjectData();
    Rect r = data.DstRect(obj, parent);
    if (r.Contains(cursor))
      hit = &obj;
  };
  auto scan_layer = [&](LazyArray<GraphicsObject>& layer) {
    for (GraphicsObject& obj : layer) {
      try_obj(obj, nullptr);
      if (hit)
        return;
      if (obj.has_object_data()) {
        ParentGraphicsObjectData* parent =
            dynamic_cast<ParentGraphicsObjectData*>(&obj.GetObjectData());
        if (parent) {
          for (GraphicsObject& child : parent->objects()) {
            try_obj(child, &obj);
            if (hit)
              return;
          }
        }
      }
    }
  };
  GraphicsSystem& gs = sys.graphics();
  scan_layer(gs.GetForegroundObjects());
  if (!hit)
    scan_layer(gs.GetBackgroundObjects());
  return hit;
}

}  // namespace

// btnobjnow_hit: which button (button_number) is the cursor currently
// hovering over? -1 if none. No state change, just a query.
struct btnobjnow_hit : public RLStoreOpcode<> {
  int operator()(RLMachine& machine) override {
    System& sys = machine.GetSystem();
    Point cursor = sys.rlEvent().GetCursorPos();
    GraphicsObject* btn =
        FindButtonAt(sys, sys.objbtn_polling_group(), cursor);
    return btn ? btn->Param().GetButtonNumber() : -1;
  }
};

// btnobjnow_push: which button is the cursor currently HELD DOWN over
// with the left mouse button? -1 if none / not held. Used by the script
// to drive "pressed" visual feedback.
struct btnobjnow_push : public RLStoreOpcode<> {
  int operator()(RLMachine& machine) override {
    System& sys = machine.GetSystem();
    Point pos;
    int b1 = 0, b2 = 0;
    sys.rlEvent().GetCursorPos(pos, b1, b2);
    if (b1 != 1)  // 1 == currently pressed; 2 == press+release
      return -1;
    GraphicsObject* btn =
        FindButtonAt(sys, sys.objbtn_polling_group(), pos);
    return btn ? btn->Param().GetButtonNumber() : -1;
  }
};

// btnobjnow_decide: did the user just decide on a button (left-click
// release over a button) or cancel (right-click release)?
//   >=0  → button_number of the clicked button
//   -1   → user right-clicked (cancel)
//   -2   → no decision yet, keep polling
//
// Flushes the click state on a hit so the script's polling loop doesn't
// see the same click twice.
struct btnobjnow_decide : public RLStoreOpcode<> {
  int operator()(RLMachine& machine) override {
    System& sys = machine.GetSystem();
    Point pos;
    int b1 = 0, b2 = 0;
    sys.rlEvent().GetCursorPos(pos, b1, b2);

    if (b2 == 2) {  // right-click released → cancel
      sys.rlEvent().FlushMouseClicks();
      return -1;
    }
    if (b1 == 2) {  // left-click released → check for hit
      GraphicsObject* btn =
          FindButtonAt(sys, sys.objbtn_polling_group(), pos);
      sys.rlEvent().FlushMouseClicks();
      if (btn)
        return btn->Param().GetButtonNumber();
    }
    return -2;
  }
};
}  // namespace

SelModule::SelModule() : RLModule("Sel", 0, 2) {
  AddOpcode(0, 0, "select_w", new Sel_select_w);
  AddOpcode(1, 0, "select", new Sel_select);
  AddOpcode(2, 0, "select_s2", new Sel_select_s);
  AddOpcode(3, 0, "select_s", new Sel_select_s);
  AddOpcode(4, 0, "select_objbtn", new Sel_select_objbtn);
  AddOpcode(14, 0, "select_objbtn_cancel", new Sel_select_objbtn_cancel_0);
  AddOpcode(14, 1, "select_objbtn_cancel", new Sel_select_objbtn_cancel_1);
  AddOpcode(14, 2, "select_objbtn_cancel", new Sel_select_objbtn_cancel_2);

  AddOpcode(20, 0, "objbtn_init", new objbtn_init_0);
  AddOpcode(20, 1, "objbtn_init", new objbtn_init_1);

  AddOpcode(30, 0, "select_btnobjnow_hit", new btnobjnow_hit);
  AddOpcode(31, 0, "select_btnobjnow_push", new btnobjnow_push);
  AddOpcode(32, 0, "select_btnobjnow_decide", new btnobjnow_decide);
}
