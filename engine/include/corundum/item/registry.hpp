#pragma once

#include <corundum/item/item.hpp>

#include <filesystem>
#include <flat_map>
#include <string>
#include <string_view>

namespace corundum::item {

  /** @brief Owns all loaded Item objects for the session.
   *
   * Keys are the "id" field from each JSON file (== Item::id).
   * Mirrors the dialogue::Registry pattern exactly.
   */
  class Registry {
  public:
    /**
     * @brief Scan a directory for *.json files and load each as an Item.
     *
     * Bad files are skipped with a stderr message (non-fatal).
     *
     * @param dir Directory containing item JSON files.
     * @return Number of items successfully loaded.
     */
    [[nodiscard]] int load_all(const std::filesystem::path &dir);

    /**
     * @brief Look up an item by id.
     * @param id The item's machine-readable identifier.
     * @return Pointer to the Item, or nullptr if not found. O(log n).
     */
    [[nodiscard]] const Item *find(std::string_view id) const;

    /** @brief Number of loaded items. */
    [[nodiscard]] std::size_t size() const noexcept {
      return items_.size();
    }

    /** @brief Register an item directly.
     *
     *  @param item The item to register (moved into the registry, keyed
     *               by id). Useful for tests.
     */
    void add(Item item) {
      items_.emplace(item.id, std::move(item));
    }

    /** @brief Range-for support for iterating all loaded items (id, item) pairs. */
    [[nodiscard]] auto begin() const noexcept {
      return items_.begin();
    }

    /** @brief Range-for support end sentinel. */
    [[nodiscard]] auto end() const noexcept {
      return items_.end();
    }

  private:
    std::flat_map<std::string, Item, std::less<>> items_;
  };

} // namespace corundum::item
