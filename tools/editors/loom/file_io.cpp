// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "file_io.hpp"
#include "graph_layout.hpp"

#include <corundum/core/json_io.hpp>
#include <corundum/dialogue/loader.hpp>
#include <corundum/dialogue/serialize.hpp>
#include <corundum/item/loader.hpp>
#include <corundum/item/serialize.hpp>
#include <corundum/quest/loader.hpp>
#include <corundum/quest/serialize.hpp>

#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>

namespace tools::loom {

  using json = nlohmann::json;

  std::string default_doc_name(const EditorState &state) {
    if (!state.file_path.empty())
      return state.file_path.filename().string();
    if (state.doc_type_ == DocumentKind::Quest)
      return state.quest_doc_.quest_id + ".json";
    if (state.doc_type_ == DocumentKind::Item)
      return "items.json";
    return state.graph.graph_id + ".json";
  }

  // ── Dialogue save/load ─────────────────────────────────────────────────────

  std::expected<void, std::string> save_graph(const EditorState &state) {
    if (state.file_path.empty())
      return std::unexpected("No file path set. Use Save As.");
    return corundum::core::write_json(state.file_path, corundum::dialogue::serialize(state.graph));
  }

  std::expected<void, std::string> load_graph_file(EditorState &state, const std::string &path) {
    auto result = corundum::dialogue::load_graph(path);
    if (!result)
      return std::unexpected(result.error());

    state.doc_type_ = DocumentKind::Dialogue;
    state.graph = std::move(*result);
    state.quest_doc_ = {};
    state.file_path = path;
    state.selected_node = -1;
    state.selected_stage_ = -1;
    state.inspector_open = false;
    state.dirty = false;
    state.last_scroll_target_ = -1;
    state.undo_stack.clear();
    recompute_layout(state.graph, state.layout, state.graph_width_);
    return {};
  }

  // ── Quest save/load ────────────────────────────────────────────────────────

  std::expected<void, std::string> save_quest_file(const EditorState &state) {
    if (state.file_path.empty())
      return std::unexpected("No file path set. Use Save As.");
    return corundum::core::write_json(state.file_path, corundum::quest::serialize(state.quest_doc_));
  }

  std::expected<void, std::string> load_quest_file(EditorState &state, const std::string &path) {
    auto result = corundum::quest::load_quest(path);
    if (!result)
      return std::unexpected(result.error());

    state.doc_type_ = DocumentKind::Quest;
    state.quest_doc_ = std::move(*result);
    state.graph = {};
    state.file_path = path;
    state.selected_node = -1;
    state.selected_stage_ = -1;
    state.inspector_open = false;
    state.dirty = false;
    state.undo_stack.clear();
    return {};
  }

  // ── Item save/load ────────────────────────────────────────────────────────

  std::expected<void, std::string> save_item_file_doc(const EditorState &state) {
    if (state.file_path.empty())
      return std::unexpected("No file path set. Use Save As.");
    return corundum::core::write_json(state.file_path,
                                      corundum::item::serialize(state.item_doc_, state.item_category_));
  }

  std::expected<void, std::string> load_item_file_doc(EditorState &state, const std::string &path) {
    const auto category_name = std::filesystem::path(path).parent_path().filename().string();
    const auto category = corundum::item::category_from_dir_name(category_name);
    if (!category)
      return std::unexpected(
          std::format("'{}' is not an item category folder (expected weapons/apparel/potions/misc)", category_name));

    auto result = corundum::item::load_item_file(path, *category);
    if (!result)
      return std::unexpected(result.error());

    state.doc_type_ = DocumentKind::Item;
    state.item_doc_ = std::move(*result);
    state.item_category_ = *category;
    state.graph = {};
    state.quest_doc_ = {};
    state.file_path = path;
    state.selected_node = -1;
    state.selected_stage_ = -1;
    state.selected_item_ = -1;
    state.inspector_open = false;
    state.dirty = false;
    state.undo_stack.clear();
    return {};
  }

  // ── Generic dispatch ───────────────────────────────────────────────────────

  std::expected<void, std::string> load_file(EditorState &state, const std::string &path) {
    // Open and peek at top-level "type" to determine file type
    std::ifstream f(path);
    if (!f)
      return std::unexpected(std::format("Cannot open {}", path));
    json j;
    try {
      j = json::parse(f, nullptr, true, true);
    } catch (const json::parse_error &e) {
      return std::unexpected(std::format("JSON parse error in {}: {}", path, e.what()));
    }
    f.close();

    if (j.contains("type") && j["type"] == "quest")
      return load_quest_file(state, path);
    if (j.contains("items"))
      return load_item_file_doc(state, path);
    return load_graph_file(state, path);
  }

  std::expected<void, std::string> save_file(const EditorState &state) {
    if (state.doc_type_ == DocumentKind::Quest)
      return save_quest_file(state);
    if (state.doc_type_ == DocumentKind::Item)
      return save_item_file_doc(state);
    return save_graph(state);
  }

} // namespace tools::loom
