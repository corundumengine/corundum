#include <corundum/gameplay/quest/runner.hpp>

#include <format>

namespace corundum::gameplay::quest {

  std::expected<void, std::string> Runner::start(std::string_view quest_id) {
    const Quest *quest = registry_.find(quest_id);
    if (!quest)
      return std::unexpected(std::format("quest_start(\"{}\") references unknown quest", quest_id));
    corundum::gameplay::quest::start(*quest, flags_);
    return {};
  }

  std::expected<void, std::string> Runner::advance(std::string_view quest_id, std::string_view stage_name) {
    const Quest *quest = registry_.find(quest_id);
    if (!quest)
      return std::unexpected(
          std::format("quest_advance(\"{}\", \"{}\") references unknown quest", quest_id, stage_name));
    corundum::gameplay::quest::advance(*quest, stage_name, flags_);
    return {};
  }

} // namespace corundum::gameplay::quest
