// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "sokol_renderer.hpp"
#include "core/warn_log.hpp"
#include "font_atlas.hpp"
#include "render_backend.hpp"
#include "render_scale.hpp"
#include "sokol_texture_upload.hpp"

#include <corundum/core/utf8.hpp>
#include <corundum/platform/gpu_context.hpp>

#include <sokol_gfx.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace corundum::platform::glfw {

  namespace {

    /**
     * @brief Orthographic projection mapping the rect (@p l..@p r, @p top..@p bottom) onto the
     *        normalized device cube, in column-major (shader `mat4`) order.
     *
     * Y points down: @p top is the smaller (upper) coordinate and @p bottom the larger, so the
     * Y scale is negative and larger Y values map downward. Z collapses to -1 — the renderer is
     * 2D and orders draws itself, so there is no depth range.
     *
     * @param l,r     Horizontal bounds; @p l is the left edge.
     * @param bottom  Lower edge (the larger Y in this top-left-origin space).
     * @param top     Upper edge (the smaller Y).
     */
    std::array<float, 16> make_ortho(float l, float r, float bottom, float top) noexcept {
      const float scale_x = 2.f / (r - l);
      const float scale_y = 2.f / (top - bottom); // Negative: maps larger Y downward.
      const float translate_x = -(r + l) / (r - l);
      const float translate_y = -(top + bottom) / (top - bottom);

      // Columns, not rows — the shader consumes this column-major.
      return {
          {
              // Column 0: X scale.
              scale_x,
              0.f,
              0.f,
              0.f,

              // Column 1: Y scale.
              0.f,
              scale_y,
              0.f,
              0.f,

              // Column 2: depth collapse; every vertex lands at z = -1, so there is no depth test.
              0.f,
              0.f,
              -1.f,
              0.f,

              // Column 3: translation to the rect center, and homogeneous w.
              translate_x,
              translate_y,
              0.f,
              1.f,
          },
      };
    }

    struct Vertex {
      float x{};
      float y{};
      float u{};
      float v{};
      float r{};
      float g{};
      float b{};
      float a{};
    };

    constexpr Vertex make_vertex(float x, float y, float u, float v, float r, float g, float b, float a) noexcept {
      return Vertex{.x = x, .y = y, .u = u, .v = v, .r = r, .g = g, .b = b, .a = a};
    }

    constexpr int k_max_quads = 16384;
    constexpr int k_max_vertices = k_max_quads * 6;
    constexpr int k_vertex_buf_size = k_max_vertices * static_cast<int>(sizeof(Vertex));

    void emit_quad(std::vector<Vertex> &out, float px, float py, float pw, float ph, float u0, float v0, float u1,
                   float v1, float r, float g, float b, float a) {
      const std::array<Vertex, 6> verts{
          make_vertex(px, py, u0, v0, r, g, b, a),           make_vertex(px + pw, py, u1, v0, r, g, b, a),
          make_vertex(px + pw, py + ph, u1, v1, r, g, b, a), make_vertex(px, py, u0, v0, r, g, b, a),
          make_vertex(px + pw, py + ph, u1, v1, r, g, b, a), make_vertex(px, py + ph, u0, v1, r, g, b, a),
      };
      out.insert(out.end(), verts.begin(), verts.end());
    }

    void emit_quad_verts(std::vector<Vertex> &out, core::math::Vec2 v0, core::math::Vec2 v1, core::math::Vec2 v2,
                         core::math::Vec2 v3, float r, float g, float b, float a) {
      const std::array<Vertex, 6> verts{
          make_vertex(v0.x, v0.y, 0.f, 0.f, r, g, b, a), make_vertex(v1.x, v1.y, 1.f, 0.f, r, g, b, a),
          make_vertex(v2.x, v2.y, 1.f, 1.f, r, g, b, a), make_vertex(v0.x, v0.y, 0.f, 0.f, r, g, b, a),
          make_vertex(v2.x, v2.y, 1.f, 1.f, r, g, b, a), make_vertex(v3.x, v3.y, 0.f, 1.f, r, g, b, a),
      };
      out.insert(out.end(), verts.begin(), verts.end());
    }

    /// RGBA unpacked from an 8-bit-per-channel Colour into the 0..1 range the shader blends.
    struct ColourF {
      float r{};
      float g{};
      float b{};
      float a{};
    };

    ColourF unpack_colour(core::math::Colour colour) noexcept {
      constexpr float k_channel_max = 255.f;
      return {
          .r = static_cast<float>(colour.r) / k_channel_max,
          .g = static_cast<float>(colour.g) / k_channel_max,
          .b = static_cast<float>(colour.b) / k_channel_max,
          .a = static_cast<float>(colour.a) / k_channel_max,
      };
    }

  } // namespace

  namespace {
    class SokolRenderer final : public corundum::platform::Renderer {
    public:
      explicit SokolRenderer(corundum::platform::GpuContext &gpu_ctx);
      ~SokolRenderer() override;

      SokolRenderer(const SokolRenderer &) = delete;
      SokolRenderer &operator=(const SokolRenderer &) = delete;
      SokolRenderer(SokolRenderer &&) = delete;
      SokolRenderer &operator=(SokolRenderer &&) = delete;

      std::expected<uint32_t, std::string> load_texture(std::string_view path) override;
      std::expected<uint32_t, std::string> load_font(std::string_view path) override;
      [[nodiscard]] core::math::Vec2 texture_size(uint32_t texture_id) const override;
      void set_world_view(core::math::Vec2 top_left, core::math::Vec2 viewport_size, float zoom) override;
      void reset_screen_view() override;
      bool begin_frame(core::math::Colour clear_colour) override;
      void end_frame() override;
      void draw(const DrawSprite &cmd) override;
      void draw(const DrawText &cmd) override;
      void draw(const DrawRect &cmd) override;
      void draw(const DrawLine &cmd) override;
      float measure_text(uint32_t font_id, std::string_view text, uint32_t char_size) const override;

      corundum::platform::RendererStats stats() const override {
        return last_stats_;
      }

    private:
      struct LoadedTexture {
        std::string path;
        sg_image image{};
        sg_view view{};
        int width = 0;
        int height = 0;
      };

      struct BakedAtlas {
        BakedSize data;
        sg_image image{};
        sg_view view{};
      };

      [[nodiscard]] static uint64_t font_size_key(uint32_t font_id, uint32_t char_size) noexcept;
      [[nodiscard]] BakedAtlas *ensure_metrics(uint32_t font_id, uint32_t char_size) const;
      [[nodiscard]] BakedAtlas *ensure_uploaded(uint32_t font_id, uint32_t char_size);
      void destroy_gpu_resources();
      void ensure_gpu_resources();
      void rebuild_proj() noexcept;
      void update_screen_scale() noexcept;
      [[nodiscard]] bool has_quad_space();
      [[nodiscard]] bool begin_primitive(sg_view view);
      void add_to_batch(sg_view view);
      void flush_batch();
      void upload_and_draw_pending_batches();

      corundum::platform::GpuContext &gpu_ctx_;

      std::array<float, 16> proj_{};
      ScreenScale screen_scale_{};
      float cam_x_{0}, cam_y_{0}, vp_w_{0}, vp_h_{0}, zoom_{1.f};
      bool world_view_active_{false};
      bool pass_active_{false};
      bool gpu_resources_initialized_{false};
      bool gpu_init_failed_{false};

      sg_view batch_view_{};
      std::vector<Vertex> batch_vertices_;
      int batch_count_{0};
      int quad_count_{0};
      std::size_t batch_start_{0}; // index into batch_vertices_ where the open batch begins

      struct PendingBatch {
        std::array<float, 16> proj{};

        int vertex_count{0};

        int vertex_offset{0}; // byte offset into vertex_buf_

        sg_view view{};
      };

      std::vector<PendingBatch> pending_batches_;

      sg_shader pipeline_shader_{};
      sg_pipeline pipeline_{};
      sg_buffer vertex_buf_{};
      sg_sampler sampler_{};
      sg_bindings bindings_{};
      sg_image white_tex_{};
      sg_view white_view_{};

      std::vector<LoadedTexture> textures_;
      std::unordered_map<std::string, uint32_t> path_to_id_;

      std::unordered_map<std::string, uint32_t> font_path_to_id_;
      std::vector<std::unique_ptr<FontAtlas>> font_atlases_;

      mutable std::unordered_map<uint64_t, BakedAtlas> baked_atlases_;
      mutable uint64_t last_key_{~0ull};
      mutable BakedAtlas *last_atlas_{nullptr};
      // (font_id, char_size) pairs whose bake or GPU upload failed. Caching the failure
      // stops a broken font/size re-rasterising and re-logging on every frame.
      mutable std::unordered_set<uint64_t> failed_keys_;

      FT_Library ft_lib_{nullptr};

      corundum::platform::RendererStats last_stats_{};
      uint32_t draw_calls_this_frame_{0};
      uint32_t dropped_quads_this_frame_{0};
    };

    // LoadedTexture and BakedAtlas are aggregates with in-class initializers for the
    // sg_image/sg_view handle members — value-init (e.g. BakedAtlas b{}) leaves the
    // .id sentinel at 0 until sg_make_image populates it, which the `image.id == 0`
    // upload guard in ensure_uploaded() relies on.

    SokolRenderer::SokolRenderer(corundum::platform::GpuContext &gpu_ctx) : gpu_ctx_(gpu_ctx) {
      // The render-target scale is chosen per backend in update_screen_scale(); tools drive
      // GpuContext directly and keep its native default.

      // GPU resources (shader, pipeline, vertex buffer, sampler, white texture) are
      // created lazily on the first begin_frame() so that shader/pipeline compilation —
      // a costly step on some backends' cold start — no longer blocks make_engine()/create_platform().
      // The window can then appear before any shader work happens. Textures and fonts
      // loaded during initialize() call only sg_make_image / FreeType (which need the
      // sokol device from GpuContext, already set up) and never this pipeline.
      batch_vertices_.reserve(static_cast<std::size_t>(k_max_vertices));
      pending_batches_.reserve(64);

      // Reserve index 0 as an invalid-slot sentinel so a stray texture_id == 0 falls
      // through the existing range/id check without drawing texture 0 by accident.
      textures_.push_back({});

      // Reserve index 0 as the invalid font sentinel too, so a default-constructed
      // FontFamily (all zeros) can never collide with a real loaded font.
      font_atlases_.push_back(nullptr);

      if (FT_Init_FreeType(&ft_lib_) != 0) {
        ft_lib_ = nullptr;
        corundum::detail::warn_log("[sokol] FT_Init_FreeType failed");
      }

      update_screen_scale();
      rebuild_proj();
    }

    void SokolRenderer::ensure_gpu_resources() {
      if (gpu_resources_initialized_ || gpu_init_failed_)
        return;

      sg_shader_desc shdesc{};
      shdesc.vertex_func.source = render_backend::vertex_shader_source();
      shdesc.fragment_func.source = render_backend::fragment_shader_source();
      shdesc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
      shdesc.uniform_blocks[0].size = sizeof(float) * 16;
      shdesc.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
      shdesc.views[0].texture.image_type = SG_IMAGETYPE_2D;
      shdesc.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
      shdesc.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
      shdesc.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
      shdesc.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
      shdesc.texture_sampler_pairs[0].view_slot = 0;
      shdesc.texture_sampler_pairs[0].sampler_slot = 0;
      render_backend::configure_shader(shdesc);
      pipeline_shader_ = sg_make_shader(&shdesc);

      sg_pipeline_desc pdesc{};
      pdesc.shader = pipeline_shader_;
      pdesc.layout.attrs[0] = {.buffer_index = 0, .offset = 0, .format = SG_VERTEXFORMAT_FLOAT2};
      pdesc.layout.attrs[1] = {.buffer_index = 0, .offset = 0, .format = SG_VERTEXFORMAT_FLOAT2};
      pdesc.layout.attrs[2] = {.buffer_index = 0, .offset = 0, .format = SG_VERTEXFORMAT_FLOAT4};
      pdesc.colors[0].blend = {
          .enabled = true,
          .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
          .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
          .op_rgb = SG_BLENDOP_ADD,
          .src_factor_alpha = SG_BLENDFACTOR_ONE,
          .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
          .op_alpha = SG_BLENDOP_ADD,
      };
      pdesc.depth.pixel_format = SG_PIXELFORMAT_NONE;
      pdesc.depth.write_enabled = false;
      pipeline_ = sg_make_pipeline(&pdesc);

      // Write-transient: filled once per frame before any draws.
      sg_buffer_desc bdesc{};
      bdesc.size = k_vertex_buf_size;
      bdesc.usage.write_transient = true;
      vertex_buf_ = sg_make_buffer(&bdesc);

      const std::array<uint8_t, 4> white{255, 255, 255, 255};
      white_tex_ = make_rgba8_image(white, 1, 1, "white_1x1");
      white_view_ = make_texture_view(white_tex_, "white_1x1");

      sg_sampler_desc samdesc{};
      samdesc.min_filter = SG_FILTER_NEAREST;
      samdesc.mag_filter = SG_FILTER_NEAREST;
      samdesc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
      samdesc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
      sampler_ = sg_make_sampler(&samdesc);

      bindings_.vertex_buffers[0] = vertex_buf_;
      bindings_.views[0] = white_view_;
      bindings_.samplers[0] = sampler_;

      const bool ok = sg_query_shader_state(pipeline_shader_) == SG_RESOURCESTATE_VALID &&
                      sg_query_pipeline_state(pipeline_) == SG_RESOURCESTATE_VALID &&
                      sg_query_buffer_state(vertex_buf_) == SG_RESOURCESTATE_VALID &&
                      sg_query_image_state(white_tex_) == SG_RESOURCESTATE_VALID &&
                      sg_query_sampler_state(sampler_) == SG_RESOURCESTATE_VALID;
      if (!ok) {
        corundum::detail::warn_log("[sokol] renderer GPU init failed (shader={} pipeline={} vbuf={} tex={} sampler={})",
                                   int(sg_query_shader_state(pipeline_shader_)),
                                   int(sg_query_pipeline_state(pipeline_)), int(sg_query_buffer_state(vertex_buf_)),
                                   int(sg_query_image_state(white_tex_)), int(sg_query_sampler_state(sampler_)));
        gpu_init_failed_ = true;
        return;
      }

      rebuild_proj();
      gpu_resources_initialized_ = true;
    }

    uint64_t SokolRenderer::font_size_key(uint32_t font_id, uint32_t char_size) noexcept {
      return (static_cast<uint64_t>(font_id) << 32u) | static_cast<uint64_t>(char_size);
    }

    SokolRenderer::BakedAtlas *SokolRenderer::ensure_metrics(uint32_t font_id, uint32_t char_size) const {
      const uint64_t key = font_size_key(font_id, char_size);
      if (last_key_ == key && last_atlas_ != nullptr)
        return last_atlas_;
      if (failed_keys_.contains(key))
        return nullptr;
      auto it = baked_atlases_.find(key);
      if (it == baked_atlases_.end()) {
        if (font_id >= font_atlases_.size() || font_atlases_[font_id] == nullptr)
          return nullptr;
        const uint32_t physical_size = physical_font_size(char_size, screen_scale_.y);
        std::expected<BakedSize, std::string> baked = font_atlases_[font_id]->bake(physical_size);
        if (!baked) {
          corundum::detail::warn_log("[sokol] {}", baked.error());
          failed_keys_.insert(key);
          return nullptr;
        }
        BakedAtlas atlas{};
        atlas.data = std::move(*baked);
        it = baked_atlases_.emplace(key, std::move(atlas)).first;
      }
      last_key_ = key;
      last_atlas_ = &it->second;
      return last_atlas_;
    }

    SokolRenderer::BakedAtlas *SokolRenderer::ensure_uploaded(uint32_t font_id, uint32_t char_size) {
      const uint64_t key = font_size_key(font_id, char_size);
      if (failed_keys_.contains(key))
        return nullptr;

      BakedAtlas *atlas = ensure_metrics(font_id, char_size);
      if (atlas == nullptr)
        return nullptr;
      if (atlas->image.id != 0)
        return atlas;
      if (atlas->data.atlas_w <= 0 || atlas->data.atlas_h <= 0 || atlas->data.pixels.empty())
        return nullptr;

      atlas->image = make_rgba8_image(atlas->data.pixels, atlas->data.atlas_w, atlas->data.atlas_h, "font_atlas");
      if (atlas->image.id == 0) {
        // Keep the CPU pixels so a later frame can retry the upload, but stop retrying on
        // every draw call so a permanently failing device doesn't flood stderr.
        corundum::detail::warn_log("[sokol] font atlas upload failed (font={} size={})", font_id, char_size);
        failed_keys_.insert(key);
        return nullptr;
      }
      atlas->view = make_texture_view(atlas->image, "font_atlas");
      atlas->data.pixels.clear();
      atlas->data.pixels.shrink_to_fit();
      return atlas;
    }

    void SokolRenderer::update_screen_scale() noexcept {
      const auto [win_w, win_h] = gpu_ctx_.window_size();
      const auto [fb_w, fb_h] = gpu_ctx_.framebuffer_size();
      gpu_ctx_.set_render_scale(render_backend::render_scale(fb_w, fb_h, win_w, win_h));
      const auto [render_w, render_h] = gpu_ctx_.render_size();
      screen_scale_ = derive_screen_scale(render_w, render_h, win_w, win_h);
    }

    void SokolRenderer::rebuild_proj() noexcept {
      if (world_view_active_) {
        proj_ = make_ortho(cam_x_, cam_x_ + (vp_w_ / zoom_), cam_y_ + (vp_h_ / zoom_), cam_y_);
      } else {
        // Screen space spans the internal render target, matching the sokol swapchain built in
        // GpuContext::begin_default_pass(). draw() scales incoming logical-point positions by the
        // cached screen_scale_ so callers keep working in logical window points; only the emitted
        // geometry (and the baked font atlases, see ensure_metrics) is render-target-resolution.
        const auto [fb_w, fb_h] = gpu_ctx_.render_size();
        proj_ = make_ortho(0.f, static_cast<float>(fb_w), static_cast<float>(fb_h), 0.f);
      }
    }

    void SokolRenderer::flush_batch() {
      if (!pass_active_ || batch_count_ == 0)
        return;
      assert(static_cast<std::size_t>(6 * batch_count_) == batch_vertices_.size() - batch_start_);
      pending_batches_.push_back(PendingBatch{
          .proj = proj_,
          .vertex_count = 6 * batch_count_,
          .vertex_offset = static_cast<int>(batch_start_ * sizeof(Vertex)),
          .view = batch_view_,
      });
      batch_start_ = batch_vertices_.size();
      batch_count_ = 0;
    }

    /** @pre `pass_active_` is true; called once per frame, before `gpu_ctx_.end_frame()`.
     *  @note Issues the frame's single `sg_write_buffer_transient()` upload, then replays every
     *        recorded batch. Write-transient buffers may not be written after they are bound. */
    void SokolRenderer::upload_and_draw_pending_batches() {
      if (pending_batches_.empty())
        return;

      sg_write_buffer_desc wdesc{};
      wdesc.src.data = {.ptr = batch_vertices_.data(), .size = batch_vertices_.size() * sizeof(Vertex)};
      wdesc.dst.buffer = vertex_buf_;
      sg_write_buffer_transient(&wdesc);

      bindings_.vertex_buffers[0] = vertex_buf_;
      for (const PendingBatch &b : pending_batches_) {
        bindings_.vertex_buffer_offsets[0] = b.vertex_offset;
        bindings_.views[0] = b.view;
        sg_apply_bindings(&bindings_);

        const sg_range ub{.ptr = b.proj.data(), .size = sizeof(b.proj)};
        sg_apply_uniforms(0, &ub);

        sg_draw(0, b.vertex_count, 1);
        ++draw_calls_this_frame_;
      }
    }

    bool SokolRenderer::has_quad_space() {
      if (quad_count_ >= k_max_quads) {
        ++dropped_quads_this_frame_;
        return false;
      }
      return true;
    }

    bool SokolRenderer::begin_primitive(sg_view view) {
      if (!pass_active_ || !has_quad_space())
        return false;
      if (batch_count_ > 0 && batch_view_.id != view.id)
        flush_batch();
      return true;
    }

    void SokolRenderer::add_to_batch(sg_view view) {
      if (batch_count_ == 0) {
        batch_view_ = view;
      }
      ++batch_count_;
      ++quad_count_;
    }

    std::expected<uint32_t, std::string> SokolRenderer::load_texture(std::string_view path) {
      const std::string key{path};
      if (const auto it = path_to_id_.find(key); it != path_to_id_.end())
        return it->second;

      int width = 0;
      int height = 0;
      int channels = 0;
      stbi_uc *pixels = stbi_load(key.c_str(), &width, &height, &channels, 4);
      if (pixels == nullptr)
        return std::unexpected(std::string{"stb_image: "} + stbi_failure_reason());

      const auto image = make_rgba8_image(std::span<const uint8_t>{pixels, rgba8_byte_count(width, height)}, width,
                                          height, key.c_str());
      stbi_image_free(pixels);

      if (image.id == 0)
        return std::unexpected(std::string{"sokol: failed to create image for '"} + key + "'");

      const auto view = make_texture_view(image, key.c_str());
      if (view.id == 0) {
        sg_destroy_image(image);
        return std::unexpected(std::string{"sokol: failed to create texture view for '"} + key + "'");
      }

      const auto id = static_cast<uint32_t>(textures_.size());
      textures_.push_back({.path = key, .image = image, .view = view, .width = width, .height = height});
      path_to_id_[key] = id;
      return id;
    }

    core::math::Vec2 SokolRenderer::texture_size(uint32_t texture_id) const {
      if (texture_id < textures_.size()) {
        const LoadedTexture &texture = textures_[texture_id];
        if (texture.width > 0 && texture.height > 0)
          return {.x = static_cast<float>(texture.width), .y = static_cast<float>(texture.height)};
      }
      return {};
    }

    std::expected<uint32_t, std::string> SokolRenderer::load_font(std::string_view path) {
      const std::string key{path};
      if (const auto it = font_path_to_id_.find(key); it != font_path_to_id_.end())
        return it->second;

      if (ft_lib_ == nullptr)
        return std::unexpected(std::string{"FreeType: library unavailable for '"} + key + "'");

      std::unique_ptr<FontAtlas> atlas = std::make_unique<FontAtlas>();
      if (!atlas->load(ft_lib_, path))
        return std::unexpected(std::string{"FreeType: could not load '"} + key + "'");

      const auto id = static_cast<uint32_t>(font_atlases_.size());
      font_path_to_id_[key] = id;
      font_atlases_.push_back(std::move(atlas));
      return id;
    }

    void SokolRenderer::set_world_view(core::math::Vec2 top_left, core::math::Vec2 viewport_size, float zoom) {
      if (world_view_active_ && cam_x_ == top_left.x && cam_y_ == top_left.y && vp_w_ == viewport_size.x &&
          vp_h_ == viewport_size.y && zoom_ == zoom)
        return;
      flush_batch();
      cam_x_ = top_left.x;
      cam_y_ = top_left.y;
      vp_w_ = viewport_size.x;
      vp_h_ = viewport_size.y;
      zoom_ = zoom;
      world_view_active_ = true;
      rebuild_proj();
    }

    void SokolRenderer::reset_screen_view() {
      if (!world_view_active_)
        return;
      flush_batch();
      world_view_active_ = false;
      rebuild_proj();
    }

    bool SokolRenderer::begin_frame(core::math::Colour clear_colour) {
      ensure_gpu_resources();
      update_screen_scale();

      quad_count_ = 0;
      batch_count_ = 0;
      draw_calls_this_frame_ = 0;
      dropped_quads_this_frame_ = 0;
      batch_vertices_.clear();
      pending_batches_.clear();
      batch_start_ = 0;

      if (gpu_init_failed_)
        return false;

      if (!gpu_ctx_.begin_default_pass(clear_colour))
        return false;

      sg_apply_pipeline(pipeline_);
      pass_active_ = true;

      if (!world_view_active_)
        rebuild_proj();
      return true;
    }

    void SokolRenderer::end_frame() {
      if (!pass_active_)
        return;
      flush_batch();
      upload_and_draw_pending_batches();
      pass_active_ = false;
      gpu_ctx_.end_frame();

      if (dropped_quads_this_frame_ > 0)
        corundum::detail::warn_log("[sokol] dropped {} quads this frame (cap {})", dropped_quads_this_frame_,
                                   k_max_quads);
      last_stats_ = {
          .draw_calls = draw_calls_this_frame_,
          .quads = static_cast<uint32_t>(quad_count_),
          .dropped_quads = dropped_quads_this_frame_,
      };
    }

    void SokolRenderer::draw(const DrawSprite &cmd) {
      if (cmd.texture_id >= textures_.size())
        return;

      const LoadedTexture &tex = textures_[cmd.texture_id];
      if (tex.image.id == 0 || tex.width <= 0 || tex.height <= 0)
        return;

      const float tex_w = static_cast<float>(tex.width);
      const float tex_h = static_cast<float>(tex.height);

      float u0 = static_cast<float>(cmd.source.x) / tex_w;
      float v0 = static_cast<float>(cmd.source.y) / tex_h;
      float u1 = static_cast<float>(cmd.source.x + cmd.source.width) / tex_w;
      float v1 = static_cast<float>(cmd.source.y + cmd.source.height) / tex_h;

      if (cmd.flip_x)
        std::swap(u0, u1);
      if (cmd.flip_y)
        std::swap(v0, v1);

      if (!begin_primitive(tex.view))
        return;

      // Screen-space draws arrive in logical window points; scale to the physical framebuffer the
      // projection spans. World-space draws are already in world units and are scale-invariant.
      const float sx = world_view_active_ ? 1.f : screen_scale_.x;
      const float sy = world_view_active_ ? 1.f : screen_scale_.y;
      const float pw = static_cast<float>(cmd.source.width) * cmd.scale.x * sx;
      const float ph = static_cast<float>(cmd.source.height) * cmd.scale.y * sy;

      emit_quad(batch_vertices_, cmd.position.x * sx, cmd.position.y * sy, pw, ph, u0, v0, u1, v1, 1.f, 1.f, 1.f, 1.f);

      add_to_batch(tex.view);
    }

    /// Looks up a codepoint's glyph in `baked`: the dense Latin-1 table first
    /// (codepoints inside it that were never baked carry zeroed metrics, i.e. a
    /// zero-width skip), then a scan of the extended set. Returns nullptr only for
    /// codepoints in neither table (e.g. CJK, emoji) — same silent-skip behavior as
    /// today for genuinely unsupported characters.
    const GlyphInfo *find_glyph(const BakedSize &baked, uint32_t codepoint) {
      if (codepoint < k_latin1_count)
        return &baked.glyphs[codepoint];
      for (const ExtendedGlyph &extended : baked.extended_glyphs)
        if (extended.codepoint == codepoint)
          return &extended.info;
      return nullptr;
    }

    void SokolRenderer::draw(const DrawText &cmd) {
      if (!pass_active_ || cmd.text.empty())
        return;

      const BakedAtlas *baked = ensure_uploaded(cmd.font_id, cmd.char_size);
      if (baked == nullptr || baked->view.id == 0)
        return;

      const BakedSize &baked_data = baked->data;
      const float atlas_w = static_cast<float>(baked_data.atlas_w);
      const float atlas_h = static_cast<float>(baked_data.atlas_h);
      const ColourF colour = unpack_colour(cmd.colour);

      // Glyphs are baked at the physical font size (see ensure_metrics), so their pixel metrics are
      // converted back to logical units here — keeping the pen math below in the same units as
      // cmd.position/cmd.char_size — then re-expanded by (sx, sy) only when emitting quads, matching
      // DrawSprite/DrawRect/DrawLine.
      const float sx = world_view_active_ ? 1.f : screen_scale_.x;
      const float sy = world_view_active_ ? 1.f : screen_scale_.y;

      float pen_x = cmd.position.x;
      float pen_y = cmd.position.y;
      const float line_h = static_cast<float>(cmd.char_size);

      for (std::size_t i = 0; i < cmd.text.size();) {
        if (cmd.text[i] == '\n') {
          pen_x = cmd.position.x;
          pen_y += line_h;
          ++i;
          continue;
        }

        const uint32_t codepoint = corundum::core::decode_utf8(cmd.text, i);
        const GlyphInfo *g = find_glyph(baked_data, codepoint);
        if (g == nullptr)
          continue;
        const float advance = to_logical(static_cast<float>(g->advance_x), screen_scale_.x);
        if (g->width == 0) {
          pen_x += advance;
          continue;
        }

        if (!begin_primitive(baked->view))
          break;

        const float gw = to_logical(static_cast<float>(g->width), screen_scale_.x);
        const float gh = to_logical(static_cast<float>(g->height), screen_scale_.y);
        const float gx = pen_x + to_logical(static_cast<float>(g->bearing_x), screen_scale_.x);
        const float gy =
            pen_y + static_cast<float>(cmd.char_size) - to_logical(static_cast<float>(g->bearing_y), screen_scale_.y);
        const float u0 = static_cast<float>(g->atlas_x) / atlas_w;
        const float v0 = static_cast<float>(g->atlas_y) / atlas_h;
        const float u1 = static_cast<float>(g->atlas_x + g->width) / atlas_w;
        const float v1 = static_cast<float>(g->atlas_y + g->height) / atlas_h;

        // Screen-space glyphs snap to whole device pixels so their 1:1 bitmap stays crisp; world-space
        // text stays unsnapped, where rounding a zoom-scaled glyph makes it jitter as it moves.
        const float qx = world_view_active_ ? gx * sx : std::round(gx * sx);
        const float qy = world_view_active_ ? gy * sy : std::round(gy * sy);
        emit_quad(batch_vertices_, qx, qy, gw * sx, gh * sy, u0, v0, u1, v1, colour.r, colour.g, colour.b, colour.a);

        add_to_batch(baked->view);

        pen_x += advance;
      }
    }

    void SokolRenderer::draw(const DrawRect &cmd) {
      if (!begin_primitive(white_view_))
        return;
      const ColourF colour = unpack_colour(cmd.colour);
      const float sx = world_view_active_ ? 1.f : screen_scale_.x;
      const float sy = world_view_active_ ? 1.f : screen_scale_.y;

      emit_quad(batch_vertices_, cmd.position.x * sx, cmd.position.y * sy, cmd.size.x * sx, cmd.size.y * sy, 0.f, 0.f,
                1.f, 1.f, colour.r, colour.g, colour.b, colour.a);

      add_to_batch(white_view_);
    }

    void SokolRenderer::draw(const DrawLine &cmd) {
      const float sx = world_view_active_ ? 1.f : screen_scale_.x;
      const float sy = world_view_active_ ? 1.f : screen_scale_.y;

      const float sx0 = cmd.start.x * sx;
      const float sy0 = cmd.start.y * sy;
      const float sx1 = cmd.end.x * sx;
      const float sy1 = cmd.end.y * sy;
      const float dx = sx1 - sx0;
      const float dy = sy1 - sy0;
      const float len = std::sqrt((dx * dx) + (dy * dy));
      if (len < 0.0001f)
        return;

      if (!begin_primitive(white_view_))
        return;

      const ColourF colour = unpack_colour(cmd.colour);
      // Thickness is authored in logical points, so it tracks the mean physical scale.
      const float hw = cmd.thickness * 0.5f * (0.5f * (sx + sy));
      const float nx = dx / len;
      const float ny = dy / len;
      const float px = -ny * hw;
      const float py = nx * hw;

      emit_quad_verts(batch_vertices_, {.x = sx0 + px, .y = sy0 + py}, {.x = sx0 - px, .y = sy0 - py},
                      {.x = sx1 - px, .y = sy1 - py}, {.x = sx1 + px, .y = sy1 + py}, colour.r, colour.g, colour.b,
                      colour.a);

      add_to_batch(white_view_);
    }

    float SokolRenderer::measure_text(uint32_t font_id, std::string_view text, uint32_t char_size) const {
      const BakedAtlas *baked = ensure_metrics(font_id, char_size);
      if (baked == nullptr)
        return 0.f;

      // Advances are baked at the physical font size (see ensure_metrics); convert back to logical
      // window points so the result matches the logical char_size callers passed in, and report the
      // widest line, matching how draw(DrawText) lays multi-line text out.
      const BakedSize &baked_data = baked->data;
      float widest_line = 0.f;
      float line_width = 0.f;
      for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '\n') {
          widest_line = std::max(widest_line, line_width);
          line_width = 0.f;
          ++i;
          continue;
        }
        const uint32_t codepoint = corundum::core::decode_utf8(text, i);
        const GlyphInfo *g = find_glyph(baked_data, codepoint);
        if (g != nullptr)
          line_width += to_logical(static_cast<float>(g->advance_x), screen_scale_.x);
      }
      return std::max(widest_line, line_width);
    }

    void SokolRenderer::destroy_gpu_resources() {
      if (!sg_isvalid())
        return;

      for (const auto &entry : baked_atlases_) {
        sg_destroy_view(entry.second.view);
        sg_destroy_image(entry.second.image);
      }
      baked_atlases_.clear();

      for (const auto &texture : textures_) {
        sg_destroy_view(texture.view);
        sg_destroy_image(texture.image);
      }
      textures_.clear();

      sg_destroy_view(white_view_);
      sg_destroy_image(white_tex_);
      sg_destroy_pipeline(pipeline_);
      sg_destroy_shader(pipeline_shader_);
      sg_destroy_buffer(vertex_buf_);
      sg_destroy_sampler(sampler_);
    }

    SokolRenderer::~SokolRenderer() {
      destroy_gpu_resources();

      // Faces reference ft_lib_; clear them before freeing the library.
      font_atlases_.clear();
      if (ft_lib_ != nullptr)
        FT_Done_FreeType(ft_lib_);
    }

  } // namespace

  std::unique_ptr<corundum::platform::Renderer> make_sokol_renderer(corundum::platform::GpuContext &gpu_ctx) {
    return std::make_unique<SokolRenderer>(gpu_ctx);
  }

} // namespace corundum::platform::glfw
