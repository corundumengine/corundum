#include <corundum/platform/texture_cache.hpp>

#include <sokol_gfx.h>
#include <stb_image.h>

#include <print>
#include <vector>

namespace corundum::platform {

  namespace {

    sg_sampler make_sampler(WrapMode wrap) {
      sg_wrap sw = (wrap == WrapMode::repeat) ? SG_WRAP_REPEAT : SG_WRAP_CLAMP_TO_EDGE;
      sg_sampler_desc desc{};
      desc.min_filter = SG_FILTER_NEAREST;
      desc.mag_filter = SG_FILTER_NEAREST;
      desc.wrap_u = sw;
      desc.wrap_v = sw;
      desc.label = "texture_cache";
      return sg_make_sampler(&desc);
    }

    sg_view make_view(sg_image img) {
      sg_view_desc vdesc{};
      vdesc.texture.image = img;
      return sg_make_view(&vdesc);
    }

    sg_image make_image(const uint8_t *pixels, int w, int h) {
      sg_image_desc desc{};
      desc.width = w;
      desc.height = h;
      desc.pixel_format = SG_PIXELFORMAT_RGBA8;
      desc.data.mip_levels[0] = {.ptr = pixels, .size = static_cast<std::size_t>(w * h * 4)};
      desc.label = "texture_cache";
      return sg_make_image(&desc);
    }

  } // namespace

  struct TextureCache::Impl {
    struct Entry {
      sg_image image{};
      sg_sampler sampler{};
      sg_view view{};
      unsigned width{0};
      unsigned height{0};
      bool active{false};
    };

    std::vector<Entry> entries_;
  };

  TextureCache::TextureCache(GpuContext & /*ctx*/) : impl_{std::make_unique<Impl>()} {}

  TextureCache::~TextureCache() {
    for (auto &entry : impl_->entries_) {
      if (!entry.active)
        continue;
      if (entry.view.id != 0)
        sg_destroy_view(entry.view);
      if (entry.sampler.id != 0)
        sg_destroy_sampler(entry.sampler);
      if (entry.image.id != 0)
        sg_destroy_image(entry.image);
    }
  }

  std::expected<TextureInfo, std::string> TextureCache::load(std::string_view path) {
    const std::string path_str{path};

    int w{}, h{}, channels{};
    stbi_uc *pixels = stbi_load(path_str.c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (!pixels)
      return std::unexpected(std::string{"stb_image: "} + stbi_failure_reason());

    sg_image img = make_image(pixels, w, h);
    stbi_image_free(pixels);

    sg_sampler sampler = make_sampler(WrapMode::clamp);
    sg_view view = make_view(img);

    uint32_t slot = 0;
    const std::size_t count = impl_->entries_.size();
    for (std::size_t i = 0; i < count; ++i) {
      if (!impl_->entries_[i].active) {
        slot = static_cast<uint32_t>(i + 1);
        break;
      }
    }
    if (slot == 0) {
      impl_->entries_.emplace_back();
      slot = static_cast<uint32_t>(impl_->entries_.size());
    }

    auto &entry = impl_->entries_[slot - 1];
    entry.image = img;
    entry.sampler = sampler;
    entry.view = view;
    entry.width = static_cast<unsigned>(w);
    entry.height = static_cast<unsigned>(h);
    entry.active = true;

    return TextureInfo{slot, static_cast<unsigned>(w), static_cast<unsigned>(h)};
  }

  TextureInfo TextureCache::create(unsigned w, unsigned h, const uint32_t *rgba, WrapMode wrap) {
    sg_image img = make_image(reinterpret_cast<const uint8_t *>(rgba), static_cast<int>(w), static_cast<int>(h));
    sg_sampler sampler = make_sampler(wrap);
    sg_view view = make_view(img);

    uint32_t slot = 0;
    const std::size_t count = impl_->entries_.size();
    for (std::size_t i = 0; i < count; ++i) {
      if (!impl_->entries_[i].active) {
        slot = static_cast<uint32_t>(i + 1);
        break;
      }
    }
    if (slot == 0) {
      impl_->entries_.emplace_back();
      slot = static_cast<uint32_t>(impl_->entries_.size());
    }

    auto &entry = impl_->entries_[slot - 1];
    entry.image = img;
    entry.sampler = sampler;
    entry.view = view;
    entry.width = w;
    entry.height = h;
    entry.active = true;

    return TextureInfo{slot, w, h};
  }

  void TextureCache::destroy(uint32_t id) noexcept {
    if (id == 0 || id > impl_->entries_.size())
      return;
    auto &entry = impl_->entries_[id - 1];
    if (!entry.active)
      return;
    if (entry.view.id != 0)
      sg_destroy_view(entry.view);
    if (entry.sampler.id != 0)
      sg_destroy_sampler(entry.sampler);
    if (entry.image.id != 0)
      sg_destroy_image(entry.image);
    entry = {};
  }

  std::optional<TextureInfo> TextureCache::info(uint32_t id) const {
    if (id == 0 || id > impl_->entries_.size())
      return std::nullopt;
    const auto &entry = impl_->entries_[id - 1];
    if (!entry.active)
      return std::nullopt;
    return TextureInfo{id, entry.width, entry.height};
  }

  BackendTexture TextureCache::backend_handle(uint32_t id) const noexcept {
    if (id == 0 || id > impl_->entries_.size())
      return {0, 0};
    const auto &entry = impl_->entries_[id - 1];
    if (!entry.active)
      return {0, 0};
    return {static_cast<uint64_t>(entry.view.id), static_cast<uint64_t>(entry.sampler.id)};
  }

} // namespace corundum::platform
