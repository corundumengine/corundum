// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "render_item_editor.hpp"

#include <corundum/item/item.hpp>

#include <algorithm>
#include <cstring>
#include <format>
#include <imgui.h>

namespace tools::loom {

  namespace {

    /** @brief A blank item matching the document's category, with its payload set. */
    corundum::item::Item make_blank_item(corundum::item::ItemCategory category) {
      corundum::item::Item item;
      item.category = category;
      switch (category) {
        case corundum::item::ItemCategory::Weapon:
          item.weapon = corundum::item::WeaponData{};
          break;
        case corundum::item::ItemCategory::Apparel:
          item.apparel = corundum::item::ApparelData{};
          break;
        case corundum::item::ItemCategory::Potion:
          item.potion = corundum::item::PotionData{};
          break;
        case corundum::item::ItemCategory::Misc:
          break;
      }
      return item;
    }

    /** @brief Collect inline validation warnings for the batch. */
    std::vector<std::string> item_validation_warnings(const std::vector<corundum::item::Item> &items) {
      std::vector<std::string> warnings;
      for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].id.empty())
          warnings.push_back(std::format("Item {} has an empty id", i + 1));
        if (items[i].name.empty())
          warnings.push_back(std::format("Item '{}' has an empty name", items[i].id));
      }
      for (std::size_t i = 0; i < items.size(); ++i) {
        for (std::size_t j = i + 1; j < items.size(); ++j) {
          if (!items[i].id.empty() && items[i].id == items[j].id)
            warnings.push_back(std::format("Duplicate item id '{}'", items[i].id));
        }
      }
      return warnings;
    }

  } // namespace

  void render_item_editor(EditorState &state) {
    const auto avail = ImGui::GetContentRegionAvail();

    // ── Left panel: Item list ────────────────────────────────────────────
    ImGui::BeginChild("##item_list", {state.node_list_width_, avail.y}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);

    ImGui::Text("Items (%s)", corundum::item::to_string(state.item_category_).data());
    ImGui::Separator();

    const auto avail_h = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("##item_items", {0.f, avail_h - 40.f});

    for (int i = 0; i < static_cast<int>(state.item_doc_.size()); ++i) {
      const auto &item = state.item_doc_[i];
      const bool is_selected = (i == state.selected_item_);

      const std::string label = item.id.empty() ? "(unnamed)" : item.id;
      if (ImGui::Selectable(label.c_str(), is_selected, ImGuiSelectableFlags_AllowDoubleClick)) {
        state.selected_item_ = i;
      }
    }

    ImGui::EndChild(); // item_items

    if (ImGui::Button("+ Item", {state.node_list_width_ - 20.f, 0.f})) {
      state.push_undo_snapshot();
      state.item_doc_.push_back(make_blank_item(state.item_category_));
      state.selected_item_ = static_cast<int>(state.item_doc_.size()) - 1;
      state.dirty = true;
    }

    if (state.selected_item_ >= 0 && state.selected_item_ < static_cast<int>(state.item_doc_.size())) {
      if (ImGui::Button("Delete Item", {state.node_list_width_ - 20.f, 0.f})) {
        state.push_undo_snapshot();
        state.item_doc_.erase(state.item_doc_.begin() + state.selected_item_);
        state.selected_item_ = -1;
        state.dirty = true;
      }
    }

    ImGui::EndChild(); // item_list

    // ── Splitter ─────────────────────────────────────────────────────────
    ImGui::SameLine();
    ImGui::InvisibleButton("##splitter_item", {4.f, ImGui::GetContentRegionAvail().y});
    if (ImGui::IsItemHovered())
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    // ── Center panel: Item editor ────────────────────────────────────────
    ImGui::SameLine();
    const float editor_w = ImGui::GetContentRegionAvail().x;
    ImGui::BeginChild("##item_editor", {editor_w, avail.y});

    ImGui::Text("Item Editor");
    ImGui::Separator();

    // Category is fixed by the batch folder — read-only.
    ImGui::TextDisabled("Category: %s (fixed by folder)", corundum::item::to_string(state.item_category_).data());

    if (state.selected_item_ >= 0 && state.selected_item_ < static_cast<int>(state.item_doc_.size())) {
      auto &item = state.item_doc_[state.selected_item_];

      char id_buf[256];
      std::memset(id_buf, 0, sizeof(id_buf));
      std::memcpy(id_buf, item.id.c_str(), std::min(item.id.size(), sizeof(id_buf) - 1));
      ImGui::InputText("ID", id_buf, sizeof(id_buf));
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        state.push_undo_snapshot();
        item.id = std::string(id_buf);
        state.dirty = true;
      }

      char name_buf[256];
      std::memset(name_buf, 0, sizeof(name_buf));
      std::memcpy(name_buf, item.name.c_str(), std::min(item.name.size(), sizeof(name_buf) - 1));
      ImGui::InputText("Name", name_buf, sizeof(name_buf));
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        state.push_undo_snapshot();
        item.name = std::string(name_buf);
        state.dirty = true;
      }

      char desc_buf[1024];
      std::memset(desc_buf, 0, sizeof(desc_buf));
      std::memcpy(desc_buf, item.description.c_str(), std::min(item.description.size(), sizeof(desc_buf) - 1));
      ImGui::InputTextMultiline("Description", desc_buf, sizeof(desc_buf), {editor_w - 20.f, 60.f});
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        state.push_undo_snapshot();
        item.description = std::string(desc_buf);
        state.dirty = true;
      }

      char icon_buf[256];
      std::memset(icon_buf, 0, sizeof(icon_buf));
      std::memcpy(icon_buf, item.icon.c_str(), std::min(item.icon.size(), sizeof(icon_buf) - 1));
      ImGui::InputText("Icon", icon_buf, sizeof(icon_buf));
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        state.push_undo_snapshot();
        item.icon = std::string(icon_buf);
        state.dirty = true;
      }

      ImGui::SeparatorText("Payload");

      switch (state.item_category_) {
        case corundum::item::ItemCategory::Weapon: {
          int damage = item.weapon ? item.weapon->damage : 0;
          if (ImGui::InputInt("Damage", &damage)) {
            state.push_undo_snapshot();
            item.weapon = corundum::item::WeaponData{std::max(0, damage)};
            state.dirty = true;
          }
          break;
        }
        case corundum::item::ItemCategory::Apparel: {
          int defense = item.apparel ? item.apparel->defense : 0;
          if (ImGui::InputInt("Defense", &defense)) {
            state.push_undo_snapshot();
            item.apparel = corundum::item::ApparelData{std::max(0, defense), item.apparel ? item.apparel->slot : ""};
            state.dirty = true;
          }

          char slot_buf[128];
          std::memset(slot_buf, 0, sizeof(slot_buf));
          if (item.apparel)
            std::memcpy(slot_buf, item.apparel->slot.c_str(),
                        std::min(item.apparel->slot.size(), sizeof(slot_buf) - 1));
          ImGui::InputText("Slot", slot_buf, sizeof(slot_buf));
          if (ImGui::IsItemDeactivatedAfterEdit()) {
            state.push_undo_snapshot();
            item.apparel = corundum::item::ApparelData{item.apparel ? item.apparel->defense : 0, std::string(slot_buf)};
            state.dirty = true;
          }
          break;
        }
        case corundum::item::ItemCategory::Potion: {
          char effect_buf[128];
          std::memset(effect_buf, 0, sizeof(effect_buf));
          if (item.potion)
            std::memcpy(effect_buf, item.potion->effect.c_str(),
                        std::min(item.potion->effect.size(), sizeof(effect_buf) - 1));
          ImGui::InputText("Effect", effect_buf, sizeof(effect_buf));
          if (ImGui::IsItemDeactivatedAfterEdit()) {
            state.push_undo_snapshot();
            item.potion = corundum::item::PotionData{std::string(effect_buf), item.potion ? item.potion->magnitude : 0};
            state.dirty = true;
          }

          int magnitude = item.potion ? item.potion->magnitude : 0;
          if (ImGui::InputInt("Magnitude", &magnitude)) {
            state.push_undo_snapshot();
            item.potion = corundum::item::PotionData{item.potion ? item.potion->effect : "", magnitude};
            state.dirty = true;
          }
          break;
        }
        case corundum::item::ItemCategory::Misc:
          ImGui::TextDisabled("No payload for misc items.");
          break;
      }
    } else {
      ImGui::TextDisabled("Select an item from the list to edit its properties.");
    }

    // ── Validation section at bottom of editor ──
    ImGui::Separator();
    constexpr auto k_warning_col = ImVec4{1.f, 0.6f, 0.f, 1.f};
    for (const auto &msg : item_validation_warnings(state.item_doc_))
      ImGui::TextColored(k_warning_col, "  %s", msg.c_str());

    ImGui::EndChild(); // item_editor
  }

} // namespace tools::loom