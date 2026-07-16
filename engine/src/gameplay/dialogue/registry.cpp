#include <corundum/gameplay/dialogue/loader.hpp>
#include <corundum/gameplay/dialogue/registry.hpp>

#include <print>

namespace corundum::gameplay::dialogue {

  int Registry::load_all(const std::filesystem::path &dir) {
    int loaded = 0;

    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
      std::println("[dialogue] no dialogue directory at '{}'", dir.string());
      return loaded;
    }

    for (const auto &entry : std::filesystem::directory_iterator(dir)) {
      if (entry.path().extension() != ".json")
        continue;

      auto result = load_graph(entry.path().string());
      if (!result) {
        std::println(stderr, "[dialogue] skipping '{}': {}", entry.path().filename().string(), result.error());
        continue;
      }

      const std::string id = result->graph_id;
      if (graphs_.contains(id))
        std::println(stderr, "[dialogue] duplicate graph id '{}' — '{}' is shadowed", id,
                     entry.path().filename().string());
      else {
        graphs_.emplace(id, std::move(*result));
        ++loaded;
      }
    }

    return loaded;
  }

  const Graph *Registry::find(std::string_view graph_id) const {
    const auto it = graphs_.find(graph_id);
    return it != graphs_.end() ? &it->second : nullptr;
  }

} // namespace corundum::gameplay::dialogue
