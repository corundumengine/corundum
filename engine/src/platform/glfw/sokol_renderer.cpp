#include "sokol_renderer.hpp"
#include "font_atlas.hpp"

#include <corundum/platform/gpu_context.hpp>

#include <sokol_gfx.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <stb_image.h>

#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <memory>
#include <print>
#include <string>
#include <unordered_map>

namespace corundum::platform::glfw {

  namespace {

    // Column-major orthographic projection, Y-down (top-left origin).
    std::array<float, 16> make_ortho(float l, float r, float t, float b) noexcept {
      return {{
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
      }};
    }

    struct Vertex {
      float x, y;
      float u, v;
      float r, g, b, a;
    };

    inline constexpr int k_max_quads = 16384;
    inline constexpr int k_max_vertices = k_max_quads * 6;
    inline constexpr int k_vertex_buf_size = k_max_vertices * static_cast<int>(sizeof(Vertex));

    // ── Shader source ─────────────────────────────────────────────────────────

    static constexpr const char *k_vs_src = R"(
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

    static constexpr const char *k_fs_src = R"(
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

    sg_view make_texture_view(sg_image img) {
      sg_view_desc vdesc{};
      vdesc.texture.image = img;
      return sg_make_view(&vdesc);
    }

    sg_image make_rgba8_image(const uint8_t *pixels, int w, int h) {
      sg_image_desc desc{};
      desc.width = w;
      desc.height = h;
      desc.pixel_format = SG_PIXELFORMAT_RGBA8;
      desc.data.mip_levels[0] = {.ptr = pixels, .size = static_cast<std::size_t>(w * h * 4)};
      desc.label = "texture";
      return sg_make_image(&desc);
    }

    void emit_quad(std::vector<Vertex> &out, float px, float py, float pw, float ph, float u0, float v0, float u1,
                   float v1, float r, float g, float b, float a) {
      const std::array<Vertex, 6> verts{{
          {px, py, u0, v0, r, g, b, a},
          {px + pw, py, u1, v0, r, g, b, a},
          {px + pw, py + ph, u1, v1, r, g, b, a},
          {px, py, u0, v0, r, g, b, a},
          {px + pw, py + ph, u1, v1, r, g, b, a},
          {px, py + ph, u0, v1, r, g, b, a},
      }};
      out.insert(out.end(), verts.begin(), verts.end());
    }

    void emit_quad_verts(std::vector<Vertex> &out, core::math::Vec2 v0, core::math::Vec2 v1, core::math::Vec2 v2,
                         core::math::Vec2 v3, float r, float g, float b, float a) {
      const std::array<Vertex, 6> verts{{
          {v0.x, v0.y, 0.f, 0.f, r, g, b, a},
          {v1.x, v1.y, 1.f, 0.f, r, g, b, a},
          {v2.x, v2.y, 1.f, 1.f, r, g, b, a},
          {v0.x, v0.y, 0.f, 0.f, r, g, b, a},
          {v2.x, v2.y, 1.f, 1.f, r, g, b, a},
          {v3.x, v3.y, 0.f, 1.f, r, g, b, a},
      }};
      out.insert(out.end(), verts.begin(), verts.end());
    }

  } // namespace

  // ── SokolRenderer ───────────────────────────────────────────────────────────

  class SokolRenderer final : public corundum::platform::Renderer {
  public:
    explicit SokolRenderer(corundum::platform::GpuContext &gpu_ctx);
    ~SokolRenderer() override;

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
      sg_image image;
      sg_view view;
      int width = 0;
      int height = 0;
    };

    struct BakedAtlas {
      BakedSize data;
      sg_image image;
      sg_view view;
    };

    [[nodiscard]] uint64_t font_size_key(uint32_t font_id, uint32_t char_size) const noexcept;
    const BakedAtlas *ensure_metrics(uint32_t font_id, uint32_t char_size) const;
    const BakedAtlas *ensure_uploaded(uint32_t font_id, uint32_t char_size) const;
    void ensure_gpu_resources();
    void rebuild_proj() noexcept;
    [[nodiscard]] bool has_quad_space();
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
    mutable const BakedAtlas *last_atlas_{nullptr};

    FT_Library ft_lib_{nullptr};

    corundum::platform::RendererStats last_stats_{};
    uint32_t draw_calls_this_frame_{0};
    uint32_t dropped_quads_this_frame_{0};
  };

  // (LoadedTexture and BakedAtlas are POD aggregates; no explicit ctors needed.)

  SokolRenderer::SokolRenderer(corundum::platform::GpuContext &gpu_ctx) : gpu_ctx_(gpu_ctx) {
    // GPU resources (shader, pipeline, vertex buffer, sampler, white texture) are
    // created lazily on the first begin_frame() so that shader/pipeline compilation —
    // a costly Metal step on cold start — no longer blocks make_engine()/create_platform().
    // The window can then appear before any shader work happens. Textures and fonts
    // loaded during initialize() call only sg_make_image / FreeType (which need the
    // sokol device from GpuContext, already set up) and never this pipeline.
    batch_vertices_.reserve(k_max_quads * 6);

    // Reserve index 0 as an invalid-slot sentinel so a stray texture_id == 0 falls
    // through the existing range/id check without drawing texture 0 by accident.
    textures_.push_back({});

    if (FT_Init_FreeType(&ft_lib_) != 0)
      std::println(stderr, "[sokol] FT_Init_FreeType failed");

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
    pdesc.layout.attrs[0] = {.format = SG_VERTEXFORMAT_FLOAT2};
    pdesc.layout.attrs[1] = {.format = SG_VERTEXFORMAT_FLOAT2};
    pdesc.layout.attrs[2] = {.format = SG_VERTEXFORMAT_FLOAT4};
    pdesc.colors[0].blend = {
        .enabled = true,
        .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
        .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        .src_factor_alpha = SG_BLENDFACTOR_ONE,
        .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
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
    const uint8_t white[4] = {255, 255, 255, 255};
    white_tex_ = make_rgba8_image(white, 1, 1);
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

  uint64_t SokolRenderer::font_size_key(uint32_t font_id, uint32_t char_size) const noexcept {
    return (static_cast<uint64_t>(font_id) << 32) | char_size;
  }

  const SokolRenderer::BakedAtlas *SokolRenderer::ensure_metrics(uint32_t font_id, uint32_t char_size) const {
    const uint64_t key = font_size_key(font_id, char_size);
    if (last_key_ == key && last_atlas_)
      return last_atlas_;
    auto it = baked_atlases_.find(key);
    if (it == baked_atlases_.end()) {
      if (font_id >= font_atlases_.size() || !font_atlases_[font_id])
        return nullptr;
      BakedAtlas b;
      b.data = font_atlases_[font_id]->bake(char_size);
      it = baked_atlases_.emplace(key, std::move(b)).first;
    }
    last_key_ = key;
    last_atlas_ = &it->second;
    return last_atlas_;
  }

  const SokolRenderer::BakedAtlas *SokolRenderer::ensure_uploaded(uint32_t font_id, uint32_t char_size) const {
    const BakedAtlas *a = ensure_metrics(font_id, char_size);
    if (!a)
      return nullptr;
    if (a->image.id == 0) {
      BakedAtlas &m = const_cast<BakedAtlas &>(*a);
      m.image = make_rgba8_image(m.data.pixels.data(), m.data.atlas_w, m.data.atlas_h);
      m.view = make_texture_view(m.image);
      m.data.pixels.clear();
      m.data.pixels.shrink_to_fit();
    }
    return a;
  }

  void SokolRenderer::rebuild_proj() noexcept {
    if (world_view_active_) {
      proj_ = make_ortho(cam_x_, cam_x_ + vp_w_ / zoom_, cam_y_, cam_y_ + vp_h_ / zoom_);
    } else {
      auto [w, h] = gpu_ctx_.window_size();
      proj_ = make_ortho(0.f, static_cast<float>(w), 0.f, static_cast<float>(h));
    }
  }

  void SokolRenderer::flush_batch() {
    if (batch_count_ == 0)
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

    int w{}, h{}, channels{};
    stbi_uc *pixels = stbi_load(key.c_str(), &w, &h, &channels, 4);
    if (!pixels)
      return std::unexpected(std::string{"stb_image: "} + stbi_failure_reason());

    sg_image img = make_rgba8_image(pixels, w, h);
    sg_view view = make_texture_view(img);
    stbi_image_free(pixels);

    const uint32_t id = static_cast<uint32_t>(textures_.size());
    path_to_id_[std::string{path}] = id;
    textures_.push_back({std::string{path}, img, view, w, h});
    return id;
  }

  std::expected<uint32_t, std::string> SokolRenderer::load_font(std::string_view path) {
    const std::string key{path};
    if (const auto it = font_path_to_id_.find(key); it != font_path_to_id_.end())
      return it->second;

    std::unique_ptr<FontAtlas> atlas = std::make_unique<FontAtlas>();
    if (!atlas->load(ft_lib_, path))
      return std::unexpected(std::string{"FreeType: could not load '"} + key + "'");

    const uint32_t id = static_cast<uint32_t>(font_atlases_.size());
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
    last_stats_ = {draw_calls_this_frame_, static_cast<uint32_t>(quad_count_), dropped_quads_this_frame_};
  }

  void SokolRenderer::draw(const DrawSprite &cmd) {
    if (!pass_active_)
      return;
    if (!has_quad_space())
      return;
    if (cmd.texture_id >= textures_.size())
      return;

    const LoadedTexture &tex = textures_[cmd.texture_id];
    if (tex.image.id == 0)
      return;

    if (tex.width == 0)
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

    const float pw = static_cast<float>(cmd.source.width) * cmd.scale.x;
    const float ph = static_cast<float>(cmd.source.height) * cmd.scale.y;

    if (batch_count_ > 0 && batch_view_.id != tex.view.id)
      flush_batch();

    emit_quad(batch_vertices_, cmd.position.x, cmd.position.y, pw, ph, u0, v0, u1, v1, 1.f, 1.f, 1.f, 1.f);

    add_to_batch(tex.view);
  }

  void SokolRenderer::draw(const DrawText &cmd) {
    if (!pass_active_)
      return;
    if (cmd.text.empty())
      return;

    const BakedAtlas *baked = ensure_uploaded(cmd.font_id, cmd.char_size);
    if (!baked)
      return;

    const BakedSize &baked_data = baked->data;
    const float atlas_w = static_cast<float>(baked_data.atlas_w);
    const float atlas_h = static_cast<float>(baked_data.atlas_h);
    const float cr = cmd.colour.r / 255.f;
    const float cg = cmd.colour.g / 255.f;
    const float cb = cmd.colour.b / 255.f;
    const float ca = cmd.colour.a / 255.f;

    float pen_x = cmd.position.x;
    float pen_y = cmd.position.y;
    const float line_h = static_cast<float>(cmd.char_size);

    for (const char ch : cmd.text) {
      if (!has_quad_space())
        break;

      if (ch == '\n') {
        pen_x = cmd.position.x;
        pen_y += line_h;
        continue;
      }

      const unsigned char c = static_cast<unsigned char>(ch);
      if (c < 32 || c >= 128)
        continue;
      const GlyphInfo &g = baked_data.glyphs[c];
      if (g.width == 0) {
        pen_x += g.advance_x;
        continue;
      }

      if (batch_count_ > 0 && batch_view_.id != baked->view.id)
        flush_batch();

      const float gx = pen_x + static_cast<float>(g.bearing_x);
      const float gy = pen_y + static_cast<float>(cmd.char_size) - static_cast<float>(g.bearing_y);
      const float gw = static_cast<float>(g.width);
      const float gh = static_cast<float>(g.height);
      const float u0 = static_cast<float>(g.atlas_x) / atlas_w;
      const float v0 = static_cast<float>(g.atlas_y) / atlas_h;
      const float u1 = static_cast<float>(g.atlas_x + g.width) / atlas_w;
      const float v1 = static_cast<float>(g.atlas_y + g.height) / atlas_h;

      emit_quad(batch_vertices_, gx, gy, gw, gh, u0, v0, u1, v1, cr, cg, cb, ca);

      add_to_batch(baked->view);

      pen_x += g.advance_x;
    }
  }

  void SokolRenderer::draw(const DrawRect &cmd) {
    if (!pass_active_)
      return;
    if (!has_quad_space())
      return;
    const float cr = cmd.colour.r / 255.f;
    const float cg = cmd.colour.g / 255.f;
    const float cb = cmd.colour.b / 255.f;
    const float ca = cmd.colour.a / 255.f;

    if (batch_count_ > 0 && batch_view_.id != white_view_.id)
      flush_batch();

    emit_quad(batch_vertices_, cmd.position.x, cmd.position.y, cmd.size.x, cmd.size.y, 0.f, 0.f, 1.f, 1.f, cr, cg, cb,
              ca);

    add_to_batch(white_view_);
  }

  void SokolRenderer::draw(const DrawLine &cmd) {
    if (!pass_active_)
      return;
    if (!has_quad_space())
      return;
    const float cr = cmd.colour.r / 255.f;
    const float cg = cmd.colour.g / 255.f;
    const float cb = cmd.colour.b / 255.f;
    const float ca = cmd.colour.a / 255.f;
    const float hw = cmd.thickness * 0.5f;
    const float dx = cmd.end.x - cmd.start.x;
    const float dy = cmd.end.y - cmd.start.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.0001f)
      return;
    const float nx = dx / len;
    const float ny = dy / len;
    const float px = -ny * hw;
    const float py = nx * hw;

    if (batch_count_ > 0 && batch_view_.id != white_view_.id)
      flush_batch();

    emit_quad_verts(batch_vertices_, {cmd.start.x + px, cmd.start.y + py}, {cmd.start.x - px, cmd.start.y - py},
                    {cmd.end.x - px, cmd.end.y - py}, {cmd.end.x + px, cmd.end.y + py}, cr, cg, cb, ca);

    add_to_batch(white_view_);
  }

  float SokolRenderer::measure_text(uint32_t font_id, std::string_view text, uint32_t char_size) const {
    const BakedAtlas *baked = ensure_metrics(font_id, char_size);
    if (!baked)
      return 0.f;

    const BakedSize &baked_data = baked->data;
    float width = 0.f;
    for (const char ch : text) {
      const unsigned char c = static_cast<unsigned char>(ch);
      if (c >= 32 && c < 128)
        width += baked_data.glyphs[c].advance_x;
    }
    return width;
  }

  // ── Factory ─────────────────────────────────────────────────────────────────

  SokolRenderer::~SokolRenderer() {
    // Faces reference ft_lib_; clear them before freeing the library.
    font_atlases_.clear();
    if (ft_lib_)
      FT_Done_FreeType(ft_lib_);
  }

  std::unique_ptr<corundum::platform::Renderer> make_sokol_renderer(corundum::platform::GpuContext &gpu_ctx) {
    return std::make_unique<SokolRenderer>(gpu_ctx);
  }

} // namespace corundum::platform::glfw
