#include <corundum/gameplay/ui/nine_patch.hpp>

namespace corundum::gameplay::ui {

  void NinePatchBorder::render(platform::Renderer &r, float x, float y, float w, float h) const {
    if (texture_id == 0)
      return;

    const float cw = static_cast<float>(tile_w);
    const float ch = static_cast<float>(tile_h);

    // Source rects derived from the uniform 3×3 tile grid.
    auto src = [&](int col, int row) -> core::math::IntRect { return {col * tile_w, row * tile_h, tile_w, tile_h}; };

    auto emit = [&](core::math::IntRect s, float px, float py, float sx = 1.f, float sy = 1.f) {
      r.draw(platform::DrawSprite{texture_id, {px, py}, s, {sx, sy}});
    };

    // Corners — drawn at natural tile size.
    emit(src(0, 0), x, y);
    emit(src(2, 0), x + w - cw, y);
    emit(src(0, 2), x, y + h - ch);
    emit(src(2, 2), x + w - cw, y + h - ch);

    // Horizontal edges — stretch X to fill inner width.
    const float inner_w = w - cw * 2.f;
    const float sx_h = inner_w / static_cast<float>(tile_w);
    emit(src(1, 0), x + cw, y, sx_h, 1.f);
    emit(src(1, 2), x + cw, y + h - ch, sx_h, 1.f);

    // Vertical edges — stretch Y to fill inner height.
    const float inner_h = h - ch * 2.f;
    const float sy_v = inner_h / static_cast<float>(tile_h);
    emit(src(0, 1), x, y + ch, 1.f, sy_v);
    emit(src(2, 1), x + w - cw, y + ch, 1.f, sy_v);
  }

} // namespace corundum::gameplay::ui
