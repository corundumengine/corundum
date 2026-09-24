// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "actions.hpp"
#include "file_io.hpp"
#include <corundum/dialogue/dialogue.hpp>
#include <corundum/toolkit/widgets/file_browser.hpp>
#include <filesystem>
#include <format>
#include <utility>

namespace tools::loom {

  void action_save_as(EditorState &state) {
    const auto start = state.file_path.empty() ? std::filesystem::current_path() : state.file_path.parent_path();
    corundum::toolkit::widgets::open_save_browser(state.popups.save_browser, "Save As", start, {{"JSON", {"json"}}},
                                                  default_doc_name(state));
  }

  void action_open(EditorState &state) {
    const auto start = state.file_path.empty() ? std::filesystem::current_path() : state.file_path.parent_path();
    corundum::toolkit::widgets::open_file_browser(state.popups.open_browser, "Open", start, {{"JSON", {"json"}}});
  }

  void action_save(EditorState &state, corundum::toolkit::host::ToolHost &host) {
    if (state.file_path.empty()) {
      action_save_as(state);
      return;
    }

    if (state.doc_type_ == DocumentKind::Dialogue) {
      auto errors = corundum::dialogue::validate_graph(state.graph);
      if (!errors.empty()) {
        state.validation_errors_ = std::move(errors);
        state.show_validation_modal_ = true;
        return;
      }
    }

    auto result = save_file(state);
    if (result) {
      state.dirty = false;
      host.set_title(app_title(state));
    } else {
      state.toast.show(std::format("[Loom] Save error: {}", result.error()));
    }
  }

} // namespace tools::loom
