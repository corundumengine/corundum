#include <corundum/dialogue/action.hpp>
#include <corundum/dialogue/compiled_expr.hpp>
#include <corundum/dialogue/validate_refs.hpp>

#include <format>
#include <variant>

namespace corundum::dialogue {

  namespace {

    void check_action_quest_refs(const std::string &action_str, const std::string &scope, const quest::Registry &quests,
                                 const item::Registry *items, std::vector<std::string> &errors) {
      auto parsed = parse_action(action_str);
      if (!parsed)
        return;
      const auto *ev = std::get_if<EventAction>(&*parsed);
      if (!ev)
        return;

      if (ev->name == "quest_start" && !ev->args.empty()) {
        if (!quests.find(ev->args[0]))
          errors.push_back(std::format("{}: quest_start references unknown quest '{}'", scope, ev->args[0]));
      } else if (ev->name == "quest_advance" && ev->args.size() >= 2) {
        const auto *q = quests.find(ev->args[0]);
        if (!q)
          errors.push_back(std::format("{}: quest_advance references unknown quest '{}'", scope, ev->args[0]));
        else if (!q->find_stage(ev->args[1]))
          errors.push_back(
              std::format("{}: quest_advance references unknown stage '{}' in '{}'", scope, ev->args[1], ev->args[0]));
      } else if (items && ev->name == "give_item" && !ev->args.empty()) {
        if (!items->find(ev->args[0]))
          errors.push_back(std::format("{}: give_item references unknown item '{}'", scope, ev->args[0]));
      } else if (items && ev->name == "take_item" && !ev->args.empty()) {
        if (!items->find(ev->args[0]))
          errors.push_back(std::format("{}: take_item references unknown item '{}'", scope, ev->args[0]));
      }
    }

  } // namespace

  std::vector<std::string> validate_quest_refs(const Graph &graph, const quest::Registry &quests,
                                               const item::Registry *items) {
    std::vector<std::string> errors;

    for (const auto &node : graph.nodes) {
      for (const auto &action_str : node.actions)
        check_action_quest_refs(action_str, std::format("node '{}'", node.id), quests, items, errors);
      for (const auto &choice : node.choices)
        for (const auto &action_str : choice.actions)
          check_action_quest_refs(action_str, std::format("choice in node '{}'", node.id), quests, items, errors);
    }

    return errors;
  }

  std::vector<std::string> validate_condition_quest_refs(const Graph &graph, const quest::Registry &quests) {
    std::vector<std::string> errors;

    for (const auto &node : graph.nodes) {
      for (const auto &choice : node.choices) {
        if (!choice.condition)
          continue;

        const auto refs = choice.condition->refs();
        for (const auto &quest_id : refs.quest_ids) {
          if (quests.find(quest_id) == nullptr)
            errors.push_back(std::format("node '{}': condition references unknown quest '{}'", node.id, quest_id));
        }
        for (const auto &[quest_id, stage_name] : refs.quest_stages) {
          const auto *q = quests.find(quest_id);
          if (q != nullptr && q->find_stage(stage_name) == nullptr)
            errors.push_back(std::format("node '{}': quest_is_at references unknown stage '{}' in '{}'", node.id,
                                         stage_name, quest_id));
        }
      }
    }

    return errors;
  }

} // namespace corundum::dialogue
