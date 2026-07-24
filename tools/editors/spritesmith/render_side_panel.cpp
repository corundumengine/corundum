#include "render_side_panel.hpp"
#include "layout.hpp"
#include <algorithm>
#include <array>
#include <corundum/resources/atlas_clips.hpp>
#include <corundum/resources/sprite.hpp>
#include <format>
#include <imgui.h>
#include <string>

namespace tools::sprite {

  namespace {

    using namespace corundum::resources;

    // ---------------------------------------------------------------------------
    // Shared helpers
    // ---------------------------------------------------------------------------

    void input_text_field(const char *label, std::string &value, EditorState &state, float width = 0.f) {
      std::array<char, 512> buf{};
      std::copy_n(value.c_str(), std::min(value.size(), buf.size() - 1), buf.data());
      if (width > 0.f)
        ImGui::SetNextItemWidth(width);
      if (ImGui::InputText(label, buf.data(), buf.size())) {
        value = buf.data();
        state.dirty = true;
      }
    }

    void record_button(bool &recording, const char *start_label, const char *stop_label) {
      if (recording) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.65f, 0.1f, 0.1f, 1.f});
        if (ImGui::Button(stop_label))
          recording = false;
        ImGui::PopStyleColor();
      } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.1f, 0.45f, 0.1f, 1.f});
        if (ImGui::Button(start_label))
          recording = true;
        ImGui::PopStyleColor();
      }
    }

    void frame_list(std::vector<FrameCoord> &frames, EditorState &state, const char *id) {
      const float list_h = std::min(120.f, static_cast<float>(frames.size()) * 20.f + 8.f);
      ImGui::BeginChild(id, {0.f, list_h}, ImGuiChildFlags_Borders);
      for (int i = 0; i < static_cast<int>(frames.size()); ++i) {
        ImGui::Text("%2d: (%d, %d)", i + 1, frames[i].col, frames[i].row);
        ImGui::SameLine();
        const std::string xlbl = std::format("X##fl{}_{}", id, i);
        if (ImGui::SmallButton(xlbl.c_str())) {
          frames.erase(frames.begin() + i);
          --i;
          state.dirty = true;
        }
      }
      if (frames.empty())
        ImGui::TextDisabled("(no frames)");
      ImGui::EndChild();
    }

    // ---------------------------------------------------------------------------
    // Sheet configuration (common)
    // ---------------------------------------------------------------------------

    // Atlas mode is a distinct, read-only-geometry sheet type; it can't toggle into a grid mode
    // (there's no grid to switch to), so it gets its own minimal config header.
    void render_atlas_sheet_config(const EditorState &state) {
      ImGui::SeparatorText("Atlas (read-only)");
      ImGui::TextDisabled("Image: %s", state.image_path.c_str());
      if (state.image_pixel_w > 0)
        ImGui::TextDisabled("Size: %d x %d px", state.image_pixel_w, state.image_pixel_h);
      ImGui::TextDisabled("Sprites: %d", static_cast<int>(state.atlas_sprites.size()));
      ImGui::TextDisabled("Source: %s", state.json_path.string().c_str());
      const auto sidecar = corundum::resources::atlas_clips_sidecar_path(state.json_path);
      ImGui::TextDisabled("Save target: %s", sidecar.filename().string().c_str());
    }

    void render_sheet_config(EditorState &state) {
      ImGui::SeparatorText("Sheet");

      // Mode toggle — Atlas is not offered here; it's a read-only geometry source, not something
      // a grid sheet converts into or out of.
      if (ImGui::RadioButton("Character", state.mode == SheetMode::Character)) {
        state.mode = SheetMode::Character;
        state.dirty = true;
      }
      ImGui::SameLine();
      if (ImGui::RadioButton("Sprite Sheet", state.mode == SheetMode::SpriteSheet)) {
        state.mode = SheetMode::SpriteSheet;
        state.dirty = true;
      }

      input_text_field("Sheet ID##sid", state.sheet_id, state);
      input_text_field("Image Path##img", state.image_path, state);

      {
        std::array<char, 512> pbuf{};
        const auto pstr = state.json_path.string();
        std::copy_n(pstr.c_str(), std::min(pstr.size(), pbuf.size() - 1), pbuf.data());
        if (ImGui::InputText("Save Path##sp", pbuf.data(), pbuf.size()))
          state.json_path = pbuf.data();
      }

      ImGui::SeparatorText("Grid");
      ImGui::SetNextItemWidth(80.f);
      if (ImGui::InputInt("Frame W##fw", &state.frame_width))
        state.frame_width = std::max(1, state.frame_width);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(80.f);
      if (ImGui::InputInt("Frame H##fh", &state.frame_height))
        state.frame_height = std::max(1, state.frame_height);

      ImGui::SetNextItemWidth(80.f);
      ImGui::InputInt("Offset X##ox", &state.offset_x);
      state.offset_x = std::max(0, state.offset_x);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(80.f);
      ImGui::InputInt("Offset Y##oy", &state.offset_y);
      state.offset_y = std::max(0, state.offset_y);

      ImGui::SetNextItemWidth(80.f);
      ImGui::InputInt("Spacing X##sx", &state.spacing_x);
      state.spacing_x = std::max(0, state.spacing_x);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(80.f);
      ImGui::InputInt("Spacing Y##sy", &state.spacing_y);
      state.spacing_y = std::max(0, state.spacing_y);

      if (state.image_pixel_w > 0) {
        ImGui::TextDisabled("Image: %d x %d px", state.image_pixel_w, state.image_pixel_h);
      }
    }

    // ---------------------------------------------------------------------------
    // Character mode: compass-grid animation selector
    // ---------------------------------------------------------------------------

    // 3×3 compass grid: [NW N NE / W _ E / SW S SE]
    // AnimId::COUNT is the sentinel for the blank center cell.
    struct CompassEntry {
      AnimId id;
      const char *label;
      const char *imgui_id;
    };

    constexpr std::array<CompassEntry, 9> k_compass{{
        {AnimId::NorthWest, "NW", "##d_nw"},
        {AnimId::North, "N", "##d_n"},
        {AnimId::NorthEast, "NE", "##d_ne"},
        {AnimId::West, "W", "##d_w"},
        {AnimId::COUNT, "", "##d_c"},
        {AnimId::East, "E", "##d_e"},
        {AnimId::SouthWest, "SW", "##d_sw"},
        {AnimId::South, "S", "##d_s"},
        {AnimId::SouthEast, "SE", "##d_se"},
    }};

    void render_anim_section(SpriteDefinition &sp, EditorState &state) {
      ImGui::SeparatorText("Animation");

      constexpr float btn_w = 46.f;
      constexpr float btn_h = 22.f;
      constexpr float gap = 4.f;
      const ImVec4 highlight{0.25f, 0.55f, 0.85f, 1.f};
      const ImVec4 populated{0.2f, 0.35f, 0.2f, 1.f};

      // 3×3 directional compass
      for (int i = 0; i < 9; ++i) {
        if (i % 3 != 0)
          ImGui::SameLine(0.f, gap);

        const CompassEntry &ce = k_compass[static_cast<std::size_t>(i)];
        if (ce.id == AnimId::COUNT) {
          ImGui::Dummy({btn_w, btn_h});
          continue;
        }

        const bool selected = (state.selected_anim == ce.id);
        const bool has_frames = !sp.anim_frames[static_cast<std::size_t>(ce.id)].empty();

        if (selected)
          ImGui::PushStyleColor(ImGuiCol_Button, highlight);
        else if (has_frames)
          ImGui::PushStyleColor(ImGuiCol_Button, populated);

        const std::string lbl = has_frames ? std::format("{}*", ce.label) : std::string{ce.label};
        const std::string full_id = lbl + ce.imgui_id;
        if (ImGui::Button(full_id.c_str(), {btn_w, btn_h})) {
          state.selected_anim = ce.id;
          state.anim_recording = false;
        }

        if (selected || has_frames)
          ImGui::PopStyleColor();
      }

      ImGui::Spacing();

      // Default animation — non-directional fallback or single-sheet sprites
      {
        const bool selected = (state.selected_anim == AnimId::Default);
        const bool has_frames = !sp.anim_frames[static_cast<std::size_t>(AnimId::Default)].empty();
        if (selected)
          ImGui::PushStyleColor(ImGuiCol_Button, highlight);
        else if (has_frames)
          ImGui::PushStyleColor(ImGuiCol_Button, populated);
        const char *lbl = has_frames ? "Default*##d_def" : "Default##d_def";
        if (ImGui::Button(lbl, {-1.f, btn_h})) {
          state.selected_anim = AnimId::Default;
          state.anim_recording = false;
        }
        if (selected || has_frames)
          ImGui::PopStyleColor();
      }

      ImGui::Spacing();

      const int ai = static_cast<int>(state.selected_anim);
      record_button(state.anim_recording, "● Record##ar", "■ Stop##ar");
      ImGui::SameLine();
      if (ImGui::Button("Clear##ac")) {
        sp.anim_frames[static_cast<std::size_t>(ai)].clear();
        state.dirty = true;
      }

      frame_list(sp.anim_frames[static_cast<std::size_t>(ai)], state, "##chr_frames");
    }

    // ---------------------------------------------------------------------------
    // Character mode
    // ---------------------------------------------------------------------------

    void render_character_section(EditorState &state) {
      ImGui::SeparatorText("Sprites");

      // Sprite list
      const float list_h = 110.f;
      ImGui::BeginChild("##sprlist", {0.f, list_h}, ImGuiChildFlags_Borders);
      for (int i = 0; i < static_cast<int>(state.sprites.size()); ++i) {
        ImGui::PushID(i);
        const bool sel = (state.selected_sprite == i);
        const char *display = state.sprites[i].name.empty() ? "(unnamed)" : state.sprites[i].name.c_str();
        if (ImGui::Selectable(display, sel)) {
          state.selected_sprite = i;
          state.anim_recording = false;
        }
        if (sel)
          ImGui::SetItemDefaultFocus();
        ImGui::PopID();
      }
      ImGui::EndChild();

      if (ImGui::Button("+ Add##sa")) {
        SpriteDefinition sp;
        sp.name = "sprite_" + std::to_string(state.sprites.size());
        state.sprites.push_back(std::move(sp));
        state.selected_sprite = static_cast<int>(state.sprites.size()) - 1;
        state.dirty = true;
      }
      ImGui::SameLine();
      ImGui::BeginDisabled(state.selected_sprite < 0);
      if (ImGui::Button("- Remove##sr") && state.selected_sprite >= 0) {
        state.sprites.erase(state.sprites.begin() + state.selected_sprite);
        state.selected_sprite = std::min(state.selected_sprite, static_cast<int>(state.sprites.size()) - 1);
        state.dirty = true;
      }
      ImGui::EndDisabled();

      if (state.selected_sprite < 0 || state.selected_sprite >= static_cast<int>(state.sprites.size()))
        return;

      auto &sp = state.sprites[static_cast<std::size_t>(state.selected_sprite)];

      ImGui::SeparatorText("Properties");
      {
        std::array<char, 512> name_buf{};
        std::copy_n(sp.name.c_str(), std::min(sp.name.size(), name_buf.size() - 1), name_buf.data());
        if (ImGui::InputText("Name##sn", name_buf.data(), name_buf.size()) && name_buf[0] != '\0') {
          sp.name = name_buf.data();
          state.dirty = true;
        }
      }

      ImGui::Text("Col/Row Span:");
      ImGui::SetNextItemWidth(70.f);
      if (ImGui::InputInt("##cs", &sp.col_span))
        sp.col_span = std::max(1, sp.col_span);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(70.f);
      if (ImGui::InputInt("##rs", &sp.row_span))
        sp.row_span = std::max(1, sp.row_span);

      if (ImGui::SliderFloat("Walk Offset##wo", &sp.walk_around_offset, 0.f, 1.f, "%.2f"))
        state.dirty = true;

      ImGui::Text("Collision (px):");
      ImGui::SetNextItemWidth(120.f);
      if (ImGui::InputInt("W##cw", &sp.collision_w)) {
        sp.collision_w = std::max(0, sp.collision_w);
        state.dirty = true;
      }
      ImGui::SameLine();
      ImGui::SetNextItemWidth(120.f);
      if (ImGui::InputInt("H##ch", &sp.collision_h)) {
        sp.collision_h = std::max(0, sp.collision_h);
        state.dirty = true;
      }
      ImGui::Checkbox("Show collision box##scb", &state.show_collision_box);

      render_anim_section(sp, state);
    }

    // ---------------------------------------------------------------------------
    // Sprite Sheet mode
    // ---------------------------------------------------------------------------

    void render_sprite_sheet_section(EditorState &state) {
      ImGui::SeparatorText("Grid Dimensions");
      ImGui::SetNextItemWidth(80.f);
      if (ImGui::InputInt("Columns##tc", &state.columns))
        state.columns = std::max(0, state.columns);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(80.f);
      if (ImGui::InputInt("Rows##tr", &state.rows))
        state.rows = std::max(0, state.rows);

      ImGui::SeparatorText("Animation Clips");
      ImGui::SetNextItemWidth(80.f);
      ImGui::InputInt("FPS##fps", &state.anim_fps);
      state.anim_fps = std::max(1, state.anim_fps);

      const float list_h = 90.f;
      ImGui::BeginChild("##cliplist", {0.f, list_h}, ImGuiChildFlags_Borders);
      for (int i = 0; i < static_cast<int>(state.anim_clips.size()); ++i) {
        const bool sel = (state.selected_clip == i);
        if (ImGui::Selectable(state.anim_clips[i].name.c_str(), sel)) {
          state.selected_clip = i;
          state.clip_recording = false;
        }
        if (sel)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndChild();

      if (ImGui::Button("+ Add##ca")) {
        AnimClip clip;
        clip.name = "clip_" + std::to_string(state.anim_clips.size());
        state.anim_clips.push_back(std::move(clip));
        state.selected_clip = static_cast<int>(state.anim_clips.size()) - 1;
        state.dirty = true;
      }
      ImGui::SameLine();
      ImGui::BeginDisabled(state.selected_clip < 0);
      if (ImGui::Button("- Remove##cr") && state.selected_clip >= 0) {
        state.anim_clips.erase(state.anim_clips.begin() + state.selected_clip);
        state.selected_clip = std::min(state.selected_clip, static_cast<int>(state.anim_clips.size()) - 1);
        state.dirty = true;
      }
      ImGui::EndDisabled();

      if (state.selected_clip < 0 || state.selected_clip >= static_cast<int>(state.anim_clips.size()))
        return;

      auto &clip = state.anim_clips[static_cast<std::size_t>(state.selected_clip)];
      input_text_field("Clip Name##cn", clip.name, state);

      record_button(state.clip_recording, "● Record##cr2", "■ Stop##cr2");
      ImGui::SameLine();
      if (ImGui::Button("Clear##cc")) {
        clip.frames.clear();
        state.dirty = true;
      }

      frame_list(clip.frames, state, "##ts_frames");
    }

    // ---------------------------------------------------------------------------
    // Atlas mode: read-only sprite list + sidecar-backed clips
    // ---------------------------------------------------------------------------

    void push_atlas_undo(EditorState &state) {
      state.atlas_undo.push({state.atlas_clips, state.selected_atlas_clip});
    }

    void atlas_frame_list(EditorState &state) {
      auto &frames = state.atlas_clips[static_cast<std::size_t>(state.selected_atlas_clip)].frames;
      const float list_h = std::min(120.f, static_cast<float>(frames.size()) * 20.f + 8.f);
      ImGui::BeginChild("##atlas_frames", {0.f, list_h}, ImGuiChildFlags_Borders);
      for (int i = 0; i < static_cast<int>(frames.size()); ++i) {
        const bool dangling = !state.atlas_name_to_index.contains(frames[static_cast<std::size_t>(i)]);
        if (dangling)
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.9f, 0.4f, 0.3f, 1.f});
        ImGui::Text("%2d: %s", i + 1, frames[static_cast<std::size_t>(i)].c_str());
        if (dangling) {
          ImGui::PopStyleColor();
          ImGui::SameLine();
          ImGui::TextDisabled("(missing)");
        }
        ImGui::SameLine();
        const std::string xlbl = std::format("X##atlasfl_{}", i);
        if (ImGui::SmallButton(xlbl.c_str())) {
          push_atlas_undo(state);
          frames.erase(frames.begin() + i);
          --i;
          state.dirty = true;
        }
      }
      if (frames.empty())
        ImGui::TextDisabled("(no frames)");
      ImGui::EndChild();
    }

    void render_atlas_section(EditorState &state) {
      ImGui::SeparatorText("Sprites");
      const float list_h = 110.f;
      ImGui::BeginChild("##atlas_sprlist", {0.f, list_h}, ImGuiChildFlags_Borders);
      for (int i = 0; i < static_cast<int>(state.atlas_sprites.size()); ++i) {
        ImGui::PushID(i);
        const bool sel = (state.selected_atlas_sprite == i);
        if (ImGui::Selectable(state.atlas_sprites[static_cast<std::size_t>(i)].name.c_str(), sel))
          state.selected_atlas_sprite = i;
        if (sel)
          ImGui::SetItemDefaultFocus();
        ImGui::PopID();
      }
      ImGui::EndChild();

      ImGui::SeparatorText("Animation Clips");

      const float clip_list_h = 90.f;
      ImGui::BeginChild("##atlas_cliplist", {0.f, clip_list_h}, ImGuiChildFlags_Borders);
      for (int i = 0; i < static_cast<int>(state.atlas_clips.size()); ++i) {
        const bool sel = (state.selected_atlas_clip == i);
        if (ImGui::Selectable(state.atlas_clips[static_cast<std::size_t>(i)].name.c_str(), sel)) {
          state.selected_atlas_clip = i;
          state.atlas_clip_recording = false;
        }
        if (sel)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndChild();

      if (ImGui::Button("+ Add##aca")) {
        push_atlas_undo(state);
        AtlasAnimClip clip;
        clip.name = "clip_" + std::to_string(state.atlas_clips.size());
        state.atlas_clips.push_back(std::move(clip));
        state.selected_atlas_clip = static_cast<int>(state.atlas_clips.size()) - 1;
        state.dirty = true;
      }
      ImGui::SameLine();
      ImGui::BeginDisabled(state.selected_atlas_clip < 0);
      if (ImGui::Button("- Remove##acr") && state.selected_atlas_clip >= 0) {
        push_atlas_undo(state);
        state.atlas_clips.erase(state.atlas_clips.begin() + state.selected_atlas_clip);
        state.selected_atlas_clip = std::min(state.selected_atlas_clip, static_cast<int>(state.atlas_clips.size()) - 1);
        state.dirty = true;
      }
      ImGui::EndDisabled();

      if (state.selected_atlas_clip < 0 || state.selected_atlas_clip >= static_cast<int>(state.atlas_clips.size()))
        return;

      auto &clip = state.atlas_clips[static_cast<std::size_t>(state.selected_atlas_clip)];

      {
        std::array<char, 512> name_buf{};
        std::copy_n(clip.name.c_str(), std::min(clip.name.size(), name_buf.size() - 1), name_buf.data());
        if (ImGui::InputText("Clip Name##acn", name_buf.data(), name_buf.size()) && name_buf[0] != '\0') {
          push_atlas_undo(state);
          clip.name = name_buf.data();
          state.dirty = true;
        }
      }

      ImGui::SetNextItemWidth(80.f);
      if (ImGui::InputInt("FPS##acfps", &clip.fps)) {
        clip.fps = std::max(1, clip.fps);
        state.dirty = true;
      }

      record_button(state.atlas_clip_recording, "● Record##acr2", "■ Stop##acr2");
      ImGui::SameLine();
      if (ImGui::Button("Clear##acc")) {
        push_atlas_undo(state);
        clip.frames.clear();
        state.dirty = true;
      }

      atlas_frame_list(state);
    }

  } // namespace

  // ---------------------------------------------------------------------------
  // Public entry point
  // ---------------------------------------------------------------------------

  void render_side_panel(EditorState &state, const corundum::tool_host::FontHandles & /*fonts*/,
                         const corundum::tool_host::ThemeColors & /*theme*/) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{8.f, 8.f});
    ImGui::SetNextWindowPos({static_cast<float>(CANVAS_W), 0.f});
    ImGui::SetNextWindowSize({static_cast<float>(PANEL_W), static_cast<float>(PANEL_H)});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{30.f / 255.f, 30.f / 255.f, 40.f / 255.f, 1.f});
    ImGui::Begin("##panel", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    if (state.mode == SheetMode::Atlas) {
      render_atlas_sheet_config(state);
      ImGui::Spacing();
      render_atlas_section(state);
    } else {
      render_sheet_config(state);
      ImGui::Spacing();
      if (state.mode == SheetMode::Character)
        render_character_section(state);
      else
        render_sprite_sheet_section(state);
    }

    ImGui::End();
  }

} // namespace tools::sprite
