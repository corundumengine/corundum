#include "sokol_renderer.hpp"
#include "font_atlas.hpp"

#include <sokol_gfx.h>

#ifdef SOKOL_METAL
#include "glfw_window_metal.h"
#endif

// Always needed: glfwGetWindowSize / glfwGetFramebufferSize / glfwSwapBuffers.
#include <GLFW/glfw3.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <array>
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

#ifdef SOKOL_METAL
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
#else
    static constexpr const char *k_vs_src = R"(
#version 330 core
layout(location=0) in vec2 position;
layout(location=1) in vec2 texcoord;
layout(location=2) in vec4 color;
out vec2 v_texcoord;
out vec4 v_color;
uniform mat4 proj;
void main() {
    gl_Position = proj * vec4(position, 0.0, 1.0);
    v_texcoord = texcoord;
    v_color = color;
}
)";

    static constexpr const char *k_fs_src = R"(
#version 330 core
in vec2 v_texcoord;
in vec4 v_color;
out vec4 frag_color;
uniform sampler2D tex;
void main() {
    frag_color = texture(tex, v_texcoord) * v_color;
}
)";
#endif

    // ── Helpers ───────────────────────────────────────────────────────────────

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

    // Emit a textured quad and return the byte offset for sg_bindings.vertex_buffer_offsets.
    int emit_quad(sg_buffer vbuf, float px, float py, float pw, float ph, float u0, float v0, float u1, float v1,
                  float r, float g, float b, float a) {
      const std::array<Vertex, 6> verts{{
          {px, py, u0, v0, r, g, b, a},
          {px + pw, py, u1, v0, r, g, b, a},
          {px + pw, py + ph, u1, v1, r, g, b, a},
          {px, py, u0, v0, r, g, b, a},
          {px + pw, py + ph, u1, v1, r, g, b, a},
          {px, py + ph, u0, v1, r, g, b, a},
      }};
      const sg_range data{.ptr = verts.data(), .size = sizeof(verts)};
      return sg_append_buffer(vbuf, &data);
    }

    // Emit two triangles from four arbitrary vertices (v0-v1-v2, v0-v2-v3).
    int emit_quad_verts(sg_buffer vbuf, core::math::Vec2 v0, core::math::Vec2 v1, core::math::Vec2 v2,
                        core::math::Vec2 v3, float r, float g, float b, float a) {
      const std::array<Vertex, 6> verts{{
          {v0.x, v0.y, 0.f, 0.f, r, g, b, a},
          {v1.x, v1.y, 1.f, 0.f, r, g, b, a},
          {v2.x, v2.y, 1.f, 1.f, r, g, b, a},
          {v0.x, v0.y, 0.f, 0.f, r, g, b, a},
          {v2.x, v2.y, 1.f, 1.f, r, g, b, a},
          {v3.x, v3.y, 0.f, 1.f, r, g, b, a},
      }};
      const sg_range data{.ptr = verts.data(), .size = sizeof(verts)};
      return sg_append_buffer(vbuf, &data);
    }

  } // namespace

  // ── Impl ──────────────────────────────────────────────────────────────────────

  struct Impl {
    ::GLFWwindow *window{nullptr};
    int win_w{1920};
    int win_h{1080};

#ifdef SOKOL_METAL
    void *metal_layer{nullptr};
#endif

    std::array<float, 16> proj{};
    float cam_x{0}, cam_y{0}, vp_w{0}, vp_h{0};
    bool world_view_active{false};

    bool pass_active{false};

    sg_shader pipeline_shader{}; // kept alive; pipeline holds a reference validated each frame
    sg_pipeline pipeline{};
    sg_buffer vertex_buf{};
    sg_sampler sampler{};
    sg_bindings bindings{};
    sg_image white_tex{};
    sg_view white_view{};

    struct LoadedTexture {
      std::string path;
      sg_image image;
      sg_view view;
    };

    std::vector<LoadedTexture> textures_; // index = texture_id (0 = invalid)
    std::unordered_map<std::string, uint32_t> path_to_id;

    std::unordered_map<std::string, uint32_t> font_path_to_id;
    std::vector<std::unique_ptr<FontAtlas>> font_atlases;

    struct BakedAtlas {
      BakedSize data;
      sg_image image;
      sg_view view;
    };

    std::unordered_map<uint64_t, BakedAtlas> baked_atlases;

    [[nodiscard]] uint64_t font_size_key(uint32_t font_id, uint32_t char_size) const noexcept {
      return (static_cast<uint64_t>(font_id) << 32) | char_size;
    }

    void ensure_baked(uint32_t font_id, uint32_t char_size) {
      const uint64_t key = font_size_key(font_id, char_size);
      if (baked_atlases.contains(key))
        return;
      if (font_id >= font_atlases.size() || !font_atlases[font_id])
        return;
      BakedAtlas baked;
      baked.data = font_atlases[font_id]->bake(char_size);
      baked.image = make_rgba8_image(baked.data.pixels.data(), baked.data.atlas_w, baked.data.atlas_h);
      baked.view = make_texture_view(baked.image);
      baked_atlases[key] = std::move(baked);
    }

    void rebuild_proj() noexcept {
      if (world_view_active) {
        proj = make_ortho(cam_x, cam_x + vp_w, cam_y, cam_y + vp_h);
      } else {
        proj = make_ortho(0.f, static_cast<float>(win_w), 0.f, static_cast<float>(win_h));
      }
    }

    void apply_draw(sg_view view, int byte_offset) {
      bindings.vertex_buffers[0] = vertex_buf;
      bindings.vertex_buffer_offsets[0] = byte_offset;
      bindings.views[0] = view;
      sg_apply_bindings(&bindings);

      const sg_range ub{.ptr = proj.data(), .size = sizeof(proj)};
      sg_apply_uniforms(0, &ub);

      sg_draw(0, 6, 1);
    }
  };

  // ── Static free functions (former SokolRenderer methods) ─────────────────────

  static std::expected<uint32_t, std::string> sokol_load_texture(void *state, std::string_view path) {
    auto *impl = static_cast<Impl *>(state);
    const std::string key{path};
    if (const auto it = impl->path_to_id.find(key); it != impl->path_to_id.end())
      return it->second;

    int w{}, h{}, channels{};
    stbi_uc *pixels = stbi_load(key.c_str(), &w, &h, &channels, 4);
    if (!pixels)
      return std::unexpected(std::string{"stb_image: "} + stbi_failure_reason());

    sg_image img = make_rgba8_image(pixels, w, h);
    sg_view view = make_texture_view(img);
    stbi_image_free(pixels);

    const uint32_t id = static_cast<uint32_t>(impl->textures_.size());
    impl->path_to_id[std::string{path}] = id;
    impl->textures_.push_back({std::string{path}, img, view});
    return id;
  }

  static std::expected<uint32_t, std::string> sokol_load_font(void *state, std::string_view path) {
    auto *impl = static_cast<Impl *>(state);
    const std::string key{path};
    if (const auto it = impl->font_path_to_id.find(key); it != impl->font_path_to_id.end())
      return it->second;

    auto atlas = std::make_unique<FontAtlas>();
    if (!atlas->load(path))
      return std::unexpected(std::string{"FreeType: could not load '"} + key + "'");

    const uint32_t id = static_cast<uint32_t>(impl->font_atlases.size());
    impl->font_path_to_id[key] = id;
    impl->font_atlases.push_back(std::move(atlas));
    return id;
  }

  static void sokol_set_world_view(void *state, corundum::core::math::Vec2 top_left,
                                   corundum::core::math::Vec2 viewport_size) {
    auto *impl = static_cast<Impl *>(state);
    impl->cam_x = top_left.x;
    impl->cam_y = top_left.y;
    impl->vp_w = viewport_size.x;
    impl->vp_h = viewport_size.y;
    impl->world_view_active = true;
    impl->rebuild_proj();
  }

  static void sokol_reset_screen_view(void *state) {
    auto *impl = static_cast<Impl *>(state);
    impl->world_view_active = false;
    impl->rebuild_proj();
  }

  static void sokol_begin_frame(void *state, corundum::core::math::Colour clear_colour) {
    auto *impl = static_cast<Impl *>(state);
    // Logical size drives the projection (game coordinates are in logical pixels).
    // Physical size drives the swapchain (Metal drawable must match the CAMetalLayer
    // pixel dimensions, which equal the framebuffer size on Retina displays).
    int log_w{}, log_h{};
    glfwGetWindowSize(impl->window, &log_w, &log_h);
    if (log_w != impl->win_w || log_h != impl->win_h) {
      impl->win_w = log_w;
      impl->win_h = log_h;
      if (!impl->world_view_active)
        impl->rebuild_proj();
    }

    int fb_w{}, fb_h{};
    glfwGetFramebufferSize(impl->window, &fb_w, &fb_h);

    sg_pass_action action{};
    action.colors[0].load_action = SG_LOADACTION_CLEAR;
    action.colors[0].clear_value = {
        clear_colour.r / 255.f,
        clear_colour.g / 255.f,
        clear_colour.b / 255.f,
        clear_colour.a / 255.f,
    };

    sg_swapchain swapchain{};
    swapchain.width = fb_w;
    swapchain.height = fb_h;
    swapchain.sample_count = 1;
    swapchain.depth_format = SG_PIXELFORMAT_NONE;

#ifdef SOKOL_METAL
    swapchain.color_format = SG_PIXELFORMAT_BGRA8;
    swapchain.metal.current_drawable = metal_next_drawable(impl->metal_layer);
    if (!swapchain.metal.current_drawable)
      return; // no drawable ready; skip this frame
#else
    swapchain.color_format = SG_PIXELFORMAT_RGBA8;
#endif

    sg_pass pass{};
    pass.action = action;
    pass.swapchain = swapchain;
    sg_begin_pass(&pass);
    sg_apply_pipeline(impl->pipeline);
    impl->pass_active = true;
  }

  static void sokol_end_frame(void *state) {
    auto *impl = static_cast<Impl *>(state);
    if (!impl->pass_active)
      return;
    impl->pass_active = false;
    sg_end_pass();
    sg_commit();
#ifndef SOKOL_METAL
    glfwSwapBuffers(impl->window);
#endif
  }

  static void sokol_draw_sprite(void *state, const corundum::platform::DrawSprite &cmd) {
    auto *impl = static_cast<Impl *>(state);
    if (cmd.texture_id >= impl->textures_.size())
      return;

    const Impl::LoadedTexture &tex = impl->textures_[cmd.texture_id];
    if (tex.image.id == 0)
      return;

    const sg_image_desc info = sg_query_image_desc(tex.image);
    if (info.width == 0)
      return;

    const float tex_w = static_cast<float>(info.width);
    const float tex_h = static_cast<float>(info.height);

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

    const int offset =
        emit_quad(impl->vertex_buf, cmd.position.x, cmd.position.y, pw, ph, u0, v0, u1, v1, 1.f, 1.f, 1.f, 1.f);
    impl->apply_draw(tex.view, offset);
  }

  static void sokol_draw_text(void *state, const corundum::platform::DrawText &cmd) {
    auto *impl = static_cast<Impl *>(state);
    if (cmd.text.empty())
      return;

    impl->ensure_baked(cmd.font_id, cmd.char_size);
    const uint64_t key = impl->font_size_key(cmd.font_id, cmd.char_size);

    const auto it = impl->baked_atlases.find(key);
    if (it == impl->baked_atlases.end())
      return;

    const Impl::BakedAtlas &baked = it->second;
    const BakedSize &baked_data = baked.data;
    const float atlas_w = static_cast<float>(baked_data.atlas_w);
    const float atlas_h = static_cast<float>(baked_data.atlas_h);
    const float cr = cmd.colour.r / 255.f;
    const float cg = cmd.colour.g / 255.f;
    const float cb = cmd.colour.b / 255.f;
    const float ca = cmd.colour.a / 255.f;

    float pen_x = cmd.position.x;
    const float pen_y = cmd.position.y;

    for (const char ch : cmd.text) {
      const auto c = static_cast<unsigned char>(ch);
      if (c < 32 || c >= 128)
        continue;
      const GlyphInfo &g = baked_data.glyphs[c];
      if (g.width == 0) {
        pen_x += g.advance_x;
        continue;
      }

      const float gx = pen_x + static_cast<float>(g.bearing_x);
      const float gy = pen_y + static_cast<float>(cmd.char_size) - static_cast<float>(g.bearing_y);
      const float gw = static_cast<float>(g.width);
      const float gh = static_cast<float>(g.height);
      const float u0 = static_cast<float>(g.atlas_x) / atlas_w;
      const float v0 = static_cast<float>(g.atlas_y) / atlas_h;
      const float u1 = static_cast<float>(g.atlas_x + g.width) / atlas_w;
      const float v1 = static_cast<float>(g.atlas_y + g.height) / atlas_h;

      const int offset = emit_quad(impl->vertex_buf, gx, gy, gw, gh, u0, v0, u1, v1, cr, cg, cb, ca);
      impl->apply_draw(baked.view, offset);

      pen_x += g.advance_x;
    }
  }

  static void sokol_draw_rect(void *state, const corundum::platform::DrawRect &cmd) {
    auto *impl = static_cast<Impl *>(state);
    const float cr = cmd.colour.r / 255.f;
    const float cg = cmd.colour.g / 255.f;
    const float cb = cmd.colour.b / 255.f;
    const float ca = cmd.colour.a / 255.f;
    const int offset = emit_quad(impl->vertex_buf, cmd.position.x, cmd.position.y, cmd.size.x, cmd.size.y, 0.f, 0.f,
                                 1.f, 1.f, cr, cg, cb, ca);
    impl->apply_draw(impl->white_view, offset);
  }

  static void sokol_draw_line(void *state, const corundum::platform::DrawLine &cmd) {
    auto *impl = static_cast<Impl *>(state);
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
    const int offset =
        emit_quad_verts(impl->vertex_buf,
                        {cmd.start.x + px, cmd.start.y + py},
                        {cmd.start.x - px, cmd.start.y - py},
                        {cmd.end.x - px, cmd.end.y - py},
                        {cmd.end.x + px, cmd.end.y + py}, cr, cg, cb, ca);
    impl->apply_draw(impl->white_view, offset);
  }

  static float sokol_measure_text(const void *state, uint32_t font_id, std::string_view text, uint32_t char_size) {
    auto *impl = const_cast<Impl *>(static_cast<const Impl *>(state));
    impl->ensure_baked(font_id, char_size);
    const uint64_t key = impl->font_size_key(font_id, char_size);
    const auto it = impl->baked_atlases.find(key);
    if (it == impl->baked_atlases.end())
      return 0.f;

    const BakedSize &baked = it->second.data;
    float width = 0.f;
    for (const char ch : text) {
      const auto c = static_cast<unsigned char>(ch);
      if (c >= 32 && c < 128)
        width += baked.glyphs[c].advance_x;
    }
    return width;
  }

  // ── Factory ───────────────────────────────────────────────────────────────────

  corundum::platform::Renderer make_sokol_renderer(::GLFWwindow *window) {
    auto *impl = new Impl{};
    impl->window = window;

    int log_w{}, log_h{};
    glfwGetWindowSize(window, &log_w, &log_h);
    impl->win_w = log_w;
    impl->win_h = log_h;

#ifdef SOKOL_METAL
    // GLFWWindow already attached the CAMetalLayer; retrieve without re-creating it.
    impl->metal_layer = metal_get_layer(window);
#endif

    sg_desc sdesc{};
#ifdef SOKOL_METAL
    sdesc.environment.metal.device = metal_device(impl->metal_layer);
#else
    sdesc.environment.gl.default_framebuffer = 0;
#endif
    sdesc.logger.func = [](const char *tag, uint32_t level, uint32_t item_id, const char *msg, uint32_t line,
                           const char *file, void *) {
      if (level < 2) // 0=panic, 1=error
        std::println(stderr, "[{}] item={} ({}:{}) {}", tag ? tag : "sg", item_id, file ? file : "?", line,
                     msg ? msg : "");
    };
    sg_setup(&sdesc);

    // ── Shader ────────────────────────────────────────────────────────────────
    sg_shader_desc shdesc{};
    shdesc.vertex_func.source = k_vs_src;
    shdesc.fragment_func.source = k_fs_src;
#ifdef SOKOL_METAL
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
#else
    shdesc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shdesc.uniform_blocks[0].size = sizeof(float) * 16;
    shdesc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "proj";
    shdesc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;
    shdesc.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    shdesc.views[0].texture.image_type = SG_IMAGETYPE_2D;
    shdesc.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    shdesc.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shdesc.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    shdesc.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shdesc.texture_sampler_pairs[0].view_slot = 0;
    shdesc.texture_sampler_pairs[0].sampler_slot = 0;
    shdesc.texture_sampler_pairs[0].glsl_name = "tex";
#endif
    impl->pipeline_shader = sg_make_shader(&shdesc);

    // ── Pipeline ──────────────────────────────────────────────────────────────
    sg_pipeline_desc pdesc{};
    pdesc.shader = impl->pipeline_shader;
    pdesc.layout.attrs[0] = {.format = SG_VERTEXFORMAT_FLOAT2}; // position
    pdesc.layout.attrs[1] = {.format = SG_VERTEXFORMAT_FLOAT2}; // texcoord
    pdesc.layout.attrs[2] = {.format = SG_VERTEXFORMAT_FLOAT4}; // color
    pdesc.colors[0].blend = {
        .enabled = true,
        .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
        .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        .src_factor_alpha = SG_BLENDFACTOR_ONE,
        .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
    };
    pdesc.depth.pixel_format = SG_PIXELFORMAT_NONE; // swapchain has no depth attachment
    pdesc.depth.write_enabled = false;
    impl->pipeline = sg_make_pipeline(&pdesc);
    // pipeline_shader is kept alive in Impl; sg_shutdown() will destroy all resources.

    // ── Vertex buffer (stream, appended per draw call) ─────────────────────────
    sg_buffer_desc bdesc{};
    bdesc.size = k_vertex_buf_size;
    bdesc.usage.stream_update = true;
    impl->vertex_buf = sg_make_buffer(&bdesc);

    // ── 1×1 white RGBA texture for DrawRect ───────────────────────────────────
    const uint8_t white[4] = {255, 255, 255, 255};
    impl->white_tex = make_rgba8_image(white, 1, 1);
    impl->white_view = make_texture_view(impl->white_tex);

    // ── Sampler ───────────────────────────────────────────────────────────────
    sg_sampler_desc samdesc{};
    samdesc.min_filter = SG_FILTER_NEAREST;
    samdesc.mag_filter = SG_FILTER_NEAREST;
    impl->sampler = sg_make_sampler(&samdesc);

    impl->bindings.vertex_buffers[0] = impl->vertex_buf;
    impl->bindings.views[0] = impl->white_view;
    impl->bindings.samplers[0] = impl->sampler;

    impl->rebuild_proj();

    corundum::platform::Renderer r{};
    r.state = impl;
    r._destroy = [](void *s) {
      auto *i = static_cast<Impl *>(s);
      if (sg_isvalid())
        sg_shutdown();
      delete i;
    };
    r._load_texture = sokol_load_texture;
    r._load_font = sokol_load_font;
    r._set_world_view = sokol_set_world_view;
    r._reset_screen_view = sokol_reset_screen_view;
    r._begin_frame = sokol_begin_frame;
    r._end_frame = sokol_end_frame;
    r._draw_sprite = sokol_draw_sprite;
    r._draw_text = sokol_draw_text;
    r._draw_rect = sokol_draw_rect;
    r._draw_line = sokol_draw_line;
    r._measure_text = sokol_measure_text;
    return r;
  }

} // namespace corundum::platform::glfw
