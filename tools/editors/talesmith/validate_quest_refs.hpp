#pragma once

#include "editor_state.hpp"

#include <corundum/gameplay/dialogue/action.hpp>
#include <corundum/gameplay/quest/registry.hpp>

#include <format>
#include <string>
#include <vector>

namespace tools::talesmith {

  inline std::vector<std::string> validate_quest_refs(const EditorState &state) {
    std::vector<std::string> errors;
    if (!state.quests_loaded_)
      return errors;

    for (const auto &node : state.graph.nodes) {
      // Check node-level actions
      for (const auto &action_str : node.actions) {
        auto parsed = corundum::gameplay::dialogue::parse_action(action_str);
        if (!parsed)
          continue;
        if (auto *ev = std::get_if<corundum::gameplay::dialogue::EventAction>(&*parsed)) {
          if (ev->name == "quest_start" && !ev->args.empty()) {
            if (!state.quest_registry.find(ev->args[0]))
              errors.push_back(
                  std::format("Node '{}': quest_start references unknown quest '{}'", node.id, ev->args[0]));
          } else if (ev->name == "quest_advance" && ev->args.size() >= 2) {
            const auto *q = state.quest_registry.find(ev->args[0]);
            if (!q)
              errors.push_back(
                  std::format("Node '{}': quest_advance references unknown quest '{}'", node.id, ev->args[0]));
            else if (!q->find_stage(ev->args[1]))
              errors.push_back(std::format("Node '{}': quest_advance references unknown stage '{}' in '{}'", node.id,
                                           ev->args[1], ev->args[0]));
          }
        }
      }

      // Check choice-level actions
      for (const auto &ch : node.choices) {
        for (const auto &action_str : ch.actions) {
          auto parsed = corundum::gameplay::dialogue::parse_action(action_str);
          if (!parsed)
            continue;
          if (auto *ev = std::get_if<corundum::gameplay::dialogue::EventAction>(&*parsed)) {
            if (ev->name == "quest_start" && !ev->args.empty()) {
              if (!state.quest_registry.find(ev->args[0]))
                errors.push_back(std::format("Choice in node '{}': quest_start references unknown quest '{}'", node.id,
                                             ev->args[0]));
            } else if (ev->name == "quest_advance" && ev->args.size() >= 2) {
              const auto *q = state.quest_registry.find(ev->args[0]);
              if (!q)
                errors.push_back(std::format("Choice in node '{}': quest_advance references unknown quest '{}'",
                                             node.id, ev->args[0]));
              else if (!q->find_stage(ev->args[1]))
                errors.push_back(std::format("Choice in node '{}': quest_advance references unknown stage '{}' in '{}'",
                                             node.id, ev->args[1], ev->args[0]));
            }
          }
        }
      }
    }

    return errors;
  }

} // namespace tools::talesmith
