#pragma once

#include "editor_state.hpp"

#include <corundum/dialogue/validate_refs.hpp>

#include <string>
#include <vector>

namespace tools::talesmith {

  /// Cross-checks quest_start/quest_advance references (node and choice-edge actions,
  /// plus quest_is_* helpers in choice conditions) against the loaded quest registry.
  /// Delegates to corundum::dialogue's shared validator — see validate_refs.hpp.
  inline std::vector<std::string> validate_quest_refs(const EditorState &state) {
    if (!state.quests_loaded_)
      return {};

    auto errors = corundum::dialogue::validate_quest_refs(state.graph, state.quest_registry);
    auto condition_errors = corundum::dialogue::validate_condition_quest_refs(state.graph, state.quest_registry);
    errors.insert(errors.end(), std::make_move_iterator(condition_errors.begin()),
                  std::make_move_iterator(condition_errors.end()));
    return errors;
  }

} // namespace tools::talesmith
