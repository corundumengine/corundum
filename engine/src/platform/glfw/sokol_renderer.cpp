// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "sokol_renderer.hpp"
#include "font_atlas.hpp"
#include "sokol_texture_upload.hpp"

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
#include <print>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace corundum::platform::glfw {

  namespace {

    // Column-major orthographic projection, Y-down (top-left origin).
    std::array<float, 16> make_ortho(float l, float r, float t, float b) noexcept {
      return {
          {
              2.f / (r - l),
              0.f,
              0.f,
              0.f,
              0.f,
              2.f / (t - b),
              0.f,
              0.f,
              0.f,
              0.f,
              -1.f,
              0.f,
              -(r + l) / (r - l),
              -(t + b) / (t - b),
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

    // ── Shader source ─────────────────────────────────────────────────────────
    // MSL only: these are compiled by the Metal backend. A D3D11/GLCore port needs its
    // own source strings and a matching `glsl_uniforms` mapping for the projection block.

    constexpr const char *k_vs_src = R"(
#include <metal_stdlib>
using namespace metal;
struct Vertex {
    float2 position [[attribute(0)]];
    float2 texcoord [[attribute(1)]];
    float4 color    [[attribute(2)]];
};
struct Varyings {
    float4 clip_pos [[position]];
    float2 texcoord;
    float4 color;
};
struct UB { float4x4 proj; };
vertex Varyings vs_main(Vertex in [[stage_in]], constant UB& ub [[buffer(0)]]) {
    Varyings out;
    out.clip_pos = ub.proj * float4(in.position, 0.0, 1.0);
    out.texcoord = in.texcoord;
    out.color = in.color;
    return out;
}
)";

    constexpr const char *k_fs_src = R"(
#include <metal_stdlib>
using namespace metal;
struct Varyings {
    float4 clip_pos [[position]];
    float2 texcoord;
    float4 color;
};
fragment float4 fs_main(Varyings in [[stage_in]],
                        texture2d<float> tex [[texture(0)]],
                        sampler samp [[sampler(0)]]) {
    return tex.sample(samp, in.texcoord) * in.color;
}
)";

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

  // ── SokolRenderer ───────────────────────────────────────────────────────────

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
    [[nodiscard]] bool has_quad_space();
    [[nodiscard]] bool begin_primitive(sg_view view);
    void add_to_batch(sg_view view);
    void flush_batch();

    corundum::platform::GpuContext &gpu_ctx_;

    std::array<float, 16> proj_{};
    float cam_x_{0}, cam_y_{0}, vp_w_{0}, vp_h_{0}, zoom_{1.f};
    bool world_view_active_{false};
    bool pass_active_{false};
    bool gpu_resources_initialized_{false};
    bool gpu_init_failed_{false};

    // ── Batch state ──────────────────────────────────────────────────
    sg_view batch_view_{};
    std::vector<Vertex> batch_vertices_;
    int batch_count_{0};
    int quad_count_{0};

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
    // GPU resources (shader, pipeline, vertex buffer, sampler, white texture) are
    // created lazily on the first begin_frame() so that shader/pipeline compilation —
    // a costly Metal step on cold start — no longer blocks make_engine()/create_platform().
    // The window can then appear before any shader work happens. Textures and fonts
    // loaded during initialize() call only sg_make_image / FreeType (which need the
    // sokol device from GpuContext, already set up) and never this pipeline.
    batch_vertices_.reserve(static_cast<std::size_t>(k_max_vertices));

    // Reserve index 0 as an invalid-slot sentinel so a stray texture_id == 0 falls
    // through the existing range/id check without drawing texture 0 by accident.
    textures_.push_back({});

    if (FT_Init_FreeType(&ft_lib_) != 0) {
      ft_lib_ = nullptr;
      std::println(stderr, "[sokol] FT_Init_FreeType failed");
    }

    rebuild_proj();
  }

  void SokolRenderer::ensure_gpu_resources() {
    if (gpu_resources_initialized_ || gpu_init_failed_)
      return;

    // ── Shader ──────────────────────────────────────────────────────────────
    sg_shader_desc shdesc{};
    shdesc.vertex_func.source = k_vs_src;
    shdesc.fragment_func.source = k_fs_src;
    shdesc.vertex_func.entry = "vs_main";
    shdesc.fragment_func.entry = "fs_main";
    shdesc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shdesc.uniform_blocks[0].size = sizeof(float) * 16;
    shdesc.uniform_blocks[0].msl_buffer_n = 0;
    shdesc.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    shdesc.views[0].texture.image_type = SG_IMAGETYPE_2D;
    shdesc.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    shdesc.views[0].texture.msl_texture_n = 0;
    shdesc.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shdesc.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    shdesc.samplers[0].msl_sampler_n = 0;
    shdesc.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shdesc.texture_sampler_pairs[0].view_slot = 0;
    shdesc.texture_sampler_pairs[0].sampler_slot = 0;
    pipeline_shader_ = sg_make_shader(&shdesc);

    // ── Pipeline ────────────────────────────────────────────────────────────
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

    // ── Vertex buffer (stream, appended per draw call) ──────────────────────
    sg_buffer_desc bdesc{};
    bdesc.size = k_vertex_buf_size;
    bdesc.usage.stream_update = true;
    vertex_buf_ = sg_make_buffer(&bdesc);

    // ── 1x1 white RGBA texture for DrawRect ────────────────────────────────
    const std::array<uint8_t, 4> white{255, 255, 255, 255};
    white_tex_ = make_rgba8_image(white.data(), 1, 1, "texture");
    white_view_ = make_texture_view(white_tex_);

    // ── Sampler ────────────────────────────────────────────────────────────
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
      std::println(stderr, "[sokol] renderer GPU init failed (shader={} pipeline={} vbuf={} tex={} sampler={})",
                   int(sg_query_shader_state(pipeline_shader_)), int(sg_query_pipeline_state(pipeline_)),
                   int(sg_query_buffer_state(vertex_buf_)), int(sg_query_image_state(white_tex_)),
                   int(sg_query_sampler_state(sampler_)));
      gpu_init_failed_ = true;
      return;
    }

    rebuild_proj();
    gpu_resources_initialized_ = true;
  }

  // ── Private helpers ─────────────────────────────────────────────────────────

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
      std::expected<BakedSize, std::string> baked = font_atlases_[font_id]->bake(char_size);
      if (!baked) {
        std::println(stderr, "[sokol] {}", baked.error());
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

    atlas->image = make_rgba8_image(atlas->data.pixels.data(), atlas->data.atlas_w, atlas->data.atlas_h, "texture");
    if (atlas->image.id == 0) {
      // Keep the CPU pixels so a later frame can retry the upload, but stop retrying on
      // every draw call so a permanently failing device doesn't flood stderr.
      std::println(stderr, "[sokol] font atlas upload failed (font={} size={})", font_id, char_size);
      failed_keys_.insert(key);
      return nullptr;
    }
    atlas->view = make_texture_view(atlas->image);
    atlas->data.pixels.clear();
    atlas->data.pixels.shrink_to_fit();
    return atlas;
  }

  void SokolRenderer::rebuild_proj() noexcept {
    if (world_view_active_) {
      proj_ = make_ortho(cam_x_, cam_x_ + (vp_w_ / zoom_), cam_y_, cam_y_ + (vp_h_ / zoom_));
    } else {
      // Screen space spans the logical window size, not the physical framebuffer: the frame
      // is rendered at logical resolution and magnified to the swapchain by the content
      // scale. NEAREST sampling keeps the magnified pixels crisp for pixel art.
      auto [w, h] = gpu_ctx_.window_size();
      proj_ = make_ortho(0.f, static_cast<float>(w), 0.f, static_cast<float>(h));
    }
  }

  void SokolRenderer::flush_batch() {
    if (!pass_active_ || batch_count_ == 0)
      return;
    assert(static_cast<std::size_t>(6 * batch_count_) == batch_vertices_.size());
    bindings_.vertex_buffers[0] = vertex_buf_;
    const sg_range batch_range{.ptr = batch_vertices_.data(), .size = batch_vertices_.size() * sizeof(Vertex)};
    const int batch_offset = sg_append_buffer(vertex_buf_, &batch_range);
    bindings_.vertex_buffer_offsets[0] = batch_offset;
    bindings_.views[0] = batch_view_;
    sg_apply_bindings(&bindings_);

    const sg_range ub{.ptr = proj_.data(), .size = sizeof(proj_)};
    sg_apply_uniforms(0, &ub);

    sg_draw(0, 6 * batch_count_, 1);
    ++draw_calls_this_frame_;
    batch_vertices_.clear();
    batch_count_ = 0;
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

  // ── Renderer interface ──────────────────────────────────────────────────────

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

    const auto image = make_rgba8_image(pixels, width, height, "texture");
    stbi_image_free(pixels);

    if (sg_query_image_state(image) != SG_RESOURCESTATE_VALID)
      return std::unexpected(std::string{"sokol: failed to create image for '"} + key + "'");

    const auto view = make_texture_view(image);
    if (sg_query_view_state(view) != SG_RESOURCESTATE_VALID) {
      sg_destroy_image(image);
      return std::unexpected(std::string{"sokol: failed to create texture view for '"} + key + "'");
    }

    const auto id = static_cast<uint32_t>(textures_.size());
    textures_.push_back({.path = key, .image = image, .view = view, .width = width, .height = height});
    path_to_id_[key] = id;
    return id;
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

    quad_count_ = 0;
    batch_count_ = 0;
    draw_calls_this_frame_ = 0;
    dropped_quads_this_frame_ = 0;
    batch_vertices_.clear();

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
    pass_active_ = false;
    gpu_ctx_.end_frame();

    if (dropped_quads_this_frame_ > 0)
      std::println(stderr, "[sokol] dropped {} quads this frame (cap {})", dropped_quads_this_frame_, k_max_quads);
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

    const float pw = static_cast<float>(cmd.source.width) * cmd.scale.x;
    const float ph = static_cast<float>(cmd.source.height) * cmd.scale.y;

    emit_quad(batch_vertices_, cmd.position.x, cmd.position.y, pw, ph, u0, v0, u1, v1, 1.f, 1.f, 1.f, 1.f);

    add_to_batch(tex.view);
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

    float pen_x = cmd.position.x;
    float pen_y = cmd.position.y;
    const float line_h = static_cast<float>(cmd.char_size);

    for (const char ch : cmd.text) {
      if (ch == '\n') {
        pen_x = cmd.position.x;
        pen_y += line_h;
        continue;
      }

      const auto c = static_cast<unsigned char>(ch);
      if (c < 32 || c >= 128)
        continue;
      const GlyphInfo &g = baked_data.glyphs[c];
      if (g.width == 0) {
        pen_x += g.advance_x;
        continue;
      }

      if (!begin_primitive(baked->view))
        break;

      const float gx = pen_x + static_cast<float>(g.bearing_x);
      const float gy = pen_y + static_cast<float>(cmd.char_size) - static_cast<float>(g.bearing_y);
      const float gw = static_cast<float>(g.width);
      const float gh = static_cast<float>(g.height);
      const float u0 = static_cast<float>(g.atlas_x) / atlas_w;
      const float v0 = static_cast<float>(g.atlas_y) / atlas_h;
      const float u1 = static_cast<float>(g.atlas_x + g.width) / atlas_w;
      const float v1 = static_cast<float>(g.atlas_y + g.height) / atlas_h;

      emit_quad(batch_vertices_, gx, gy, gw, gh, u0, v0, u1, v1, colour.r, colour.g, colour.b, colour.a);

      add_to_batch(baked->view);

      pen_x += g.advance_x;
    }
  }

  void SokolRenderer::draw(const DrawRect &cmd) {
    if (!begin_primitive(white_view_))
      return;
    const ColourF colour = unpack_colour(cmd.colour);

    emit_quad(batch_vertices_, cmd.position.x, cmd.position.y, cmd.size.x, cmd.size.y, 0.f, 0.f, 1.f, 1.f, colour.r,
              colour.g, colour.b, colour.a);

    add_to_batch(white_view_);
  }

  void SokolRenderer::draw(const DrawLine &cmd) {
    const float dx = cmd.end.x - cmd.start.x;
    const float dy = cmd.end.y - cmd.start.y;
    const float len = std::sqrt((dx * dx) + (dy * dy));
    if (len < 0.0001f)
      return;

    if (!begin_primitive(white_view_))
      return;

    const ColourF colour = unpack_colour(cmd.colour);
    const float hw = cmd.thickness * 0.5f;
    const float nx = dx / len;
    const float ny = dy / len;
    const float px = -ny * hw;
    const float py = nx * hw;

    emit_quad_verts(batch_vertices_, {.x = cmd.start.x + px, .y = cmd.start.y + py},
                    {.x = cmd.start.x - px, .y = cmd.start.y - py}, {.x = cmd.end.x - px, .y = cmd.end.y - py},
                    {.x = cmd.end.x + px, .y = cmd.end.y + py}, colour.r, colour.g, colour.b, colour.a);

    add_to_batch(white_view_);
  }

  float SokolRenderer::measure_text(uint32_t font_id, std::string_view text, uint32_t char_size) const {
    const BakedAtlas *baked = ensure_metrics(font_id, char_size);
    if (baked == nullptr)
      return 0.f;

    // Report the widest line, matching how draw(DrawText) lays multi-line text out.
    const BakedSize &baked_data = baked->data;
    float widest_line = 0.f;
    float line_width = 0.f;
    for (const char ch : text) {
      if (ch == '\n') {
        widest_line = std::max(widest_line, line_width);
        line_width = 0.f;
        continue;
      }
      const auto c = static_cast<unsigned char>(ch);
      if (c >= 32 && c < 128)
        line_width += baked_data.glyphs[c].advance_x;
    }
    return std::max(widest_line, line_width);
  }

  // ── Factory ─────────────────────────────────────────────────────────────────

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

  std::unique_ptr<corundum::platform::Renderer> make_sokol_renderer(corundum::platform::GpuContext &gpu_ctx) {
    return std::make_unique<SokolRenderer>(gpu_ctx);
  }

} // namespace corundum::platform::glfw
