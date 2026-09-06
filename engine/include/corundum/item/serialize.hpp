#pragma once
#include <corundum/item/item.hpp>

#include <nlohmann/json.hpp>
#include <span>

namespace corundum::item {

  /**
   * @brief Serialize a batch of items to the engine's item batch JSON format.
   *
   * Mirrors `load_item_file`: emits `{ "schema_version": k_item_schema_version,
   * "items": [ ... ] }`. The category is folder-derived and therefore omitted
   * from each element (the loader injects it), so the element's payload is
   * emitted for @p category exactly as the loader would read it back.
   *
   * @param items    The items to write, typically all sharing @p category.
   * @param category The category of the folder this batch lives in.
   * @return A JSON object suitable for write_json().
   */
  [[nodiscard]] nlohmann::json serialize_item_file(std::span<const Item> items, ItemCategory category);

} // namespace corundum::item