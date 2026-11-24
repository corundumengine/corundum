// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/gameplay/item/item.hpp>

#include <filesystem>
#include <flat_map>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::core {
class ContentSource;
}

namespace corundum::gameplay::item {

/** @brief Owns all loaded Item objects for the session.
 *
 * Keys are the "id" field from each JSON file (== Item::id).
 * Mirrors the dialogue::Registry pattern exactly.
 */
class Registry {
public:
  /**
   * @brief Load every item under a directory's per-category subfolders.
   *
   * Walks the known category folders (`weapons`, `apparel`, `potions`, `misc`)
   * beneath @p dir and loads each `*.json` batch file via Loader::load().
   * Files sitting directly under @p dir, unknown category folders, non-`.json`
   * files, and items failing validation are skipped with a stderr message (non-fatal).
   *
   * @param dir Root directory holding one subfolder per item category.
   * @return Number of items successfully loaded.
   */
  [[nodiscard]] int load_all(const core::ContentSource &content, const std::filesystem::path &dir);

  /**
   * @brief Look up an item by id.
   * @param id The item's machine-readable identifier.
   * @return Pointer to the Item, or nullptr if not found. O(log n).
   */
  [[nodiscard]] const Item *find(std::string_view id) const;

  /** @brief Number of loaded items. */
  [[nodiscard]] std::size_t size() const noexcept { return items_.size(); }

  /** @brief Register an item directly.
   *
   *  @param item The item to register (moved into the registry, keyed
   *               by id). Useful for tests.
   */
  void add(Item item) { items_.emplace(item.id, std::move(item)); }

  /** @brief Range-for support for iterating all loaded items (id, item) pairs. */
  [[nodiscard]] auto begin() const noexcept { return items_.begin(); }

  /** @brief Range-for support end sentinel. */
  [[nodiscard]] auto end() const noexcept { return items_.end(); }

private:
  /** @brief Add @p batch's items, skipping ids already loaded.
   *
   *  @param batch Items to add; each is moved into the registry unless its id is taken.
   *  @param file_name Batch file named in the skip warning.
   *  @return How many items were newly added.
   */
  int add_batch(std::vector<Item> &batch, const std::string &file_name);

  std::flat_map<std::string, Item, std::less<>> items_;
};

} // namespace corundum::gameplay::item
