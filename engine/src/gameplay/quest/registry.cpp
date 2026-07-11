#include <corundum/gameplay/quest/loader.hpp>
#include <corundum/gameplay/quest/registry.hpp>

#include <print>

namespace corundum::gameplay::quest {

  int Registry::load_all(const std::filesystem::path &dir) {
    int loaded = 0;

    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir))
      return loaded;

    for (const auto &entry : std::filesystem::directory_iterator(dir)) {
      if (entry.path().extension() != ".json")
        continue;

      auto result = load_quest(entry.path().string());
      if (!result) {
        std::println(stderr, "[quest] skipping '{}': {}", entry.path().filename().string(), result.error());
        continue;
      }

      const std::string id = result->quest_id;
      quests_.emplace(id, std::move(*result));
      ++loaded;
    }

    return loaded;
  }

  const Quest *Registry::find(std::string_view quest_id) const {
    const auto it = quests_.find(quest_id);
    return it != quests_.end() ? &it->second : nullptr;
  }

} // namespace corundum::gameplay::quest
