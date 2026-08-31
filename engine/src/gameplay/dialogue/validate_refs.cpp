#include <corundum/gameplay/dialogue/action.hpp>
#include <corundum/gameplay/dialogue/validate_refs.hpp>

#include <cctype>
#include <format>
#include <variant>

namespace corundum::gameplay::dialogue {

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

    bool is_ident_start(char c) noexcept {
      return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
    }

    bool is_ident_char(char c) noexcept {
      return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    }

    bool is_quest_helper(const std::string &ident) noexcept {
      return ident == "quest_is_started" || ident == "quest_is_resolved" || ident == "quest_is_failed" ||
             ident == "quest_is_at";
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
        if (!choice.condition || choice.condition->empty())
          continue;
        const std::string &cond = *choice.condition;
        const std::size_t n = cond.size();
        std::size_t i = 0;

        while (i < n) {
          if (!is_ident_start(cond[i])) {
            ++i;
            continue;
          }

          const std::size_t ident_start = i;
          while (i < n && is_ident_char(cond[i]))
            ++i;
          const std::string ident = cond.substr(ident_start, i - ident_start);

          if (!is_quest_helper(ident))
            continue;

          std::size_t j = i;
          while (j < n && std::isspace(static_cast<unsigned char>(cond[j])) != 0)
            ++j;
          if (j >= n || cond[j] != '(')
            continue; // not a call — leave to the expression evaluator
          ++j;
          while (j < n && std::isspace(static_cast<unsigned char>(cond[j])) != 0)
            ++j;
          if (j >= n || !is_ident_start(cond[j]))
            continue; // malformed call — leave to the expression evaluator

          const std::size_t quest_start = j;
          while (j < n && is_ident_char(cond[j]))
            ++j;
          const std::string quest_id = cond.substr(quest_start, j - quest_start);

          const auto *q = quests.find(quest_id);
          if (!q) {
            errors.push_back(std::format("node '{}': {} references unknown quest '{}'", node.id, ident, quest_id));
            i = j;
            continue;
          }

          if (ident == "quest_is_at") {
            std::size_t k = j;
            while (k < n && std::isspace(static_cast<unsigned char>(cond[k])) != 0)
              ++k;
            if (k < n && cond[k] == ',') {
              ++k;
              while (k < n && std::isspace(static_cast<unsigned char>(cond[k])) != 0)
                ++k;
              if (k < n && is_ident_start(cond[k])) {
                const std::size_t stage_start = k;
                while (k < n && is_ident_char(cond[k]))
                  ++k;
                const std::string stage_name = cond.substr(stage_start, k - stage_start);
                if (!q->find_stage(stage_name))
                  errors.push_back(std::format("node '{}': quest_is_at references unknown stage '{}' in '{}'", node.id,
                                               stage_name, quest_id));
                j = k;
              }
            }
          }

          i = j;
        }
      }
    }

    return errors;
  }

} // namespace corundum::gameplay::dialogue
