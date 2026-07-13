#pragma once
#include <corundum/tool_host/tool_host.hpp>
#include <flat_map>
#include <functional>
#include <imgui.h>

namespace corundum::tool_host {

  /// A shortcut action bound to a chord.
  using ShortcutAction = std::function<void()>;

  /// Map of key chords to actions, checked by process_shortcuts() each frame.
  using ShortcutMap = std::flat_map<ImGuiKeyChord, ShortcutAction>;

  /// Process all shortcuts: iterate the map and fire actions on matching chords.
  inline void process_shortcuts(const ShortcutMap &map) {
    for (const auto &[chord, action] : map) {
      if (ImGui::IsKeyChordPressed(chord)) {
        action();
        break;
      }
    }
  }

  /// Add common editor shortcuts (quit on Esc/Ctrl+Q) to @p map.
  /// Tools extend this with their own tool-specific bindings.
  inline void add_default_shortcuts(ShortcutMap &map, ToolHost &host, bool &running) {
    map[ImGuiMod_Ctrl | ImGuiKey_Q] = [&running]() { running = false; };
    map[ImGuiKey_Escape] = [&host]() { host.request_close(); };
  }

} // namespace corundum::tool_host
