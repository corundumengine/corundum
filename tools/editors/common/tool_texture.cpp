#include "common/tool_texture.hpp"
#include "tool_app.hpp"

namespace tools::common {

  ImTextureID tex_id(ToolApp &app, const ToolTexture &t) noexcept {
    return app.tex_id(t);
  }

  ToolTexture load_tool_texture(ToolApp &app, std::string_view path) {
    return app.load_texture(path);
  }

  ToolTexture make_checkerboard(ToolApp &app, unsigned w, unsigned h, unsigned check_size) {
    return app.make_checkerboard(w, h, check_size);
  }

  ToolTexture create_tool_texture(ToolApp &app, unsigned w, unsigned h, const uint32_t *pixels) {
    return app.create_texture(w, h, pixels);
  }

  void destroy_tool_texture(ToolApp &app, ToolTexture &tex) noexcept {
    app.destroy_texture(tex);
  }

} // namespace tools::common
