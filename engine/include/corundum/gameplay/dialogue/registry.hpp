#pragma once

#include <corundum/gameplay/dialogue/dialogue.hpp>

#include <filesystem>
#include <flat_map>
#include <string>
#include <string_view>

namespace corundum::gameplay::dialogue {

  /// Owns all loaded DialogueGraphs for the session.
  /// Keys are the "id" field from each JSON file (== Graph::graph_id).
  class Registry {
  public:
    /// Scans `dir` for *.json files and loads each as a Graph.
    /// Bad files are skipped with a stderr message (non-fatal).
    /// @return Number of graphs successfully loaded.
    [[nodiscard]] int load_all(const std::filesystem::path &dir);

    /// Returns a pointer to the graph with the given id, or nullptr.
    [[nodiscard]] const Graph *find(std::string_view graph_id) const;

    [[nodiscard]] std::size_t size() const noexcept {
      return graphs_.size();
    }

  private:
    std::flat_map<std::string, Graph, std::less<>> graphs_;
  };

} // namespace corundum::gameplay::dialogue
