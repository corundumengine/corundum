// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "render_keys_panel.hpp"
#include "editor_state.hpp"
#include <corundum/dialogue/dialogue.hpp>
#include <corundum/quest/quest.hpp>

#include <corundum/dialogue/action.hpp>
#include <corundum/dialogue/compiled_expr.hpp>

#include <imgui.h>
#include <set>
#include <string>
#include <variant>

namespace tools::loom {

  namespace {

    void collect_action_lhs(const std::string &action_str, std::set<std::string> &keys) {
      auto parsed = corundum::dialogue::parse_action(action_str);
      if (!parsed)
        return;
      const auto *sa = std::get_if<corundum::dialogue::StateAction>(&*parsed);
      if (sa != nullptr)
        keys.insert(sa->var);
    }

    void collect_graph_keys(const corundum::dialogue::Graph &graph, std::set<std::string> &keys) {
      for (const auto &node : graph.nodes) {
        for (const auto &action_str : node.actions)
          collect_action_lhs(action_str, keys);
        for (const auto &choice : node.choices) {
          if (choice.condition) {
            const auto refs = choice.condition->refs();
            keys.insert(refs.idents.begin(), refs.idents.end());
          }
          for (const auto &action_str : choice.actions)
            collect_action_lhs(action_str, keys);
        }
      }
    }

    void collect_quest_keys(const corundum::quest::Quest &quest, std::set<std::string> &keys) {
      for (const auto &stage : quest.stages) {
        for (const auto &objective : stage.objectives) {
          if (objective.done_condition) {
            const auto refs = objective.done_condition->refs();
            keys.insert(refs.idents.begin(), refs.idents.end());
          }
        }
      }
    }

  } // namespace

  void render_keys_panel(const EditorState &state) {
    std::set<std::string> keys;
    if (state.doc_type_ == DocumentKind::Dialogue)
      collect_graph_keys(state.graph, keys);
    else if (state.doc_type_ == DocumentKind::Quest)
      collect_quest_keys(state.quest_doc_, keys);

    if (!ImGui::CollapsingHeader("Keys in File", ImGuiTreeNodeFlags_DefaultOpen))
      return;

    if (keys.empty()) {
      ImGui::TextDisabled("No keys referenced.");
      return;
    }

    constexpr auto k_local_col = ImVec4{0.4f, 0.9f, 0.5f, 1.f};
    for (const auto &key : keys) {
      if (key.starts_with("local."))
        ImGui::TextColored(k_local_col, "  %s (zone-scoped)", key.c_str());
      else
        ImGui::Text("  %s", key.c_str());
    }
  }

} // namespace tools::loom