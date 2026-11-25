// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/dialogue/action.hpp>
#include <corundum/gameplay/dialogue/compiled_expr.hpp>
#include <corundum/gameplay/dialogue/registry.hpp>
#include <corundum/gameplay/dialogue/validate_refs.hpp>
#include <corundum/gameplay/item/registry.hpp>

#include <format>
#include <string>
#include <variant>
#include <vector>

namespace corundum::gameplay::dialogue {

namespace {

/** Report the unknown quest named by `quest_start(quest)`. */
void check_quest_start(const EventAction &event, const std::string &scope, const quest::Registry &quests,
                       std::vector<std::string> &errors) {
  if (event.args.empty())
    return;
  if (quests.find(event.args[0]) == nullptr)
    errors.push_back(std::format("{}: quest_start references unknown quest '{}'", scope, event.args[0]));
}

/** Report the unknown quest or stage named by `quest_advance(quest, stage)`. */
void check_quest_advance(const EventAction &event, const std::string &scope, const quest::Registry &quests,
                         std::vector<std::string> &errors) {
  if (event.args.size() < 2)
    return;
  const auto *quest = quests.find(event.args[0]);
  if (quest == nullptr)
    errors.push_back(std::format("{}: quest_advance references unknown quest '{}'", scope, event.args[0]));
  else if (quest->find_stage(event.args[1]) == nullptr)
    errors.push_back(
        std::format("{}: quest_advance references unknown stage '{}' in '{}'", scope, event.args[1], event.args[0]));
}

/** Report the unknown item named by `give_item(item, ...)` or `take_item(item, ...)`. */
void check_item_event(const EventAction &event, const std::string &scope, const item::Registry &items,
                      std::vector<std::string> &errors) {
  if (event.args.empty())
    return;
  if (items.find(event.args[0]) == nullptr)
    errors.push_back(std::format("{}: {} references unknown item '{}'", scope, event.name, event.args[0]));
}

/** Report the unknown graph or node named by `goto_graph(graph, node)`. */
void check_goto_graph(const EventAction &event, const std::string &scope, const Registry &graphs,
                      std::vector<std::string> &errors) {
  if (event.args.size() < 2)
    return;
  const auto *target = graphs.find(event.args[0]);
  if (target == nullptr)
    errors.push_back(std::format("{}: goto_graph references unknown graph '{}'", scope, event.args[0]));
  else if (target->find(event.args[1]) == nullptr)
    errors.push_back(
        std::format("{}: goto_graph references unknown node '{}' in '{}'", scope, event.args[1], event.args[0]));
}

/** Check the references of one action string; a null `items` or `graphs` skips that kind. */
void check_action_quest_refs(const std::string &action_str, const std::string &scope, const quest::Registry &quests,
                             const item::Registry *items, const Registry *graphs, std::vector<std::string> &errors) {
  auto parsed = parse_action(action_str);
  if (!parsed)
    return;
  const auto *event = std::get_if<EventAction>(&*parsed);
  if (event == nullptr)
    return;

  const bool is_item_event = event->name == "give_item" || event->name == "take_item";
  if (event->name == "quest_start")
    check_quest_start(*event, scope, quests, errors);
  else if (event->name == "quest_advance")
    check_quest_advance(*event, scope, quests, errors);
  else if (is_item_event && items != nullptr)
    check_item_event(*event, scope, *items, errors);
  else if (event->name == "goto_graph" && graphs != nullptr)
    check_goto_graph(*event, scope, *graphs, errors);
}

/** Report the unknown quests and stages named by one choice's condition. */
void check_condition_refs(const CompiledExpr &condition, const std::string &node_id, const quest::Registry &quests,
                          std::vector<std::string> &errors) {
  const auto refs = condition.refs();
  for (const auto &quest_id : refs.quest_ids) {
    if (quests.find(quest_id) == nullptr)
      errors.push_back(std::format("node '{}': condition references unknown quest '{}'", node_id, quest_id));
  }
  for (const auto &[quest_id, stage_name] : refs.quest_stages) {
    const auto *quest = quests.find(quest_id);
    if (quest != nullptr && quest->find_stage(stage_name) == nullptr)
      errors.push_back(
          std::format("node '{}': quest_is_at references unknown stage '{}' in '{}'", node_id, stage_name, quest_id));
  }
}

} // namespace

std::vector<std::string> validate_quest_refs(const Graph &graph, const quest::Registry &quests,
                                             const item::Registry *items, const Registry *graphs) {
  std::vector<std::string> errors;

  for (const auto &node : graph.nodes) {
    for (const auto &action_str : node.actions)
      check_action_quest_refs(action_str, std::format("node '{}'", node.id), quests, items, graphs, errors);
    for (const auto &choice : node.choices)
      for (const auto &action_str : choice.actions)
        check_action_quest_refs(action_str, std::format("choice in node '{}'", node.id), quests, items, graphs, errors);
  }

  return errors;
}

std::vector<std::string> validate_condition_quest_refs(const Graph &graph, const quest::Registry &quests) {
  std::vector<std::string> errors;

  for (const auto &node : graph.nodes) {
    for (const auto &choice : node.choices) {
      if (choice.condition)
        check_condition_refs(*choice.condition, node.id, quests, errors);
    }
  }

  return errors;
}

} // namespace corundum::gameplay::dialogue
