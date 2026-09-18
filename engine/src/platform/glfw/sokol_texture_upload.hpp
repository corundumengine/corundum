// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <sokol_gfx.h>

#include <cstdint>
#include <span>

#include "texture_byte_size.hpp"

namespace corundum::platform::glfw {

  /** @brief Create an RGBA8 image from @p pixels, which are copied during the call.
   *
   * @pre @p width and @p height are positive; @p pixels covers at least `rgba8_byte_count(width, height)` bytes.
   * @param[in] label Debug label the backend reports for the image; read only during this call.
   * @return The image handle, or an invalid handle when the device rejected the upload. An invalid
   *         return owns no GPU resource — a rejected image has already been released here.
   * @note Sokol reports a rejected upload as a non-zero handle in the `FAILED` state, so the failure is
   *       folded into an invalid handle rather than left for every caller to query.
   */
  [[nodiscard]] inline sg_image make_rgba8_image(std::span<const uint8_t> pixels, int width, int height,
                                                 const char *label) noexcept {
    if (width <= 0 || height <= 0 || pixels.size() < rgba8_byte_count(width, height))
      return {};

    sg_image_desc desc{};
    desc.type = SG_IMAGETYPE_2D;
    desc.width = width;
    desc.height = height;
    desc.num_mipmaps = 1;
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.usage.immutable = true;
    desc.data.mip_levels[0] = {.ptr = pixels.data(), .size = rgba8_byte_count(width, height)};
    desc.label = label;

    const sg_image image = sg_make_image(&desc);
    if (sg_query_image_state(image) != SG_RESOURCESTATE_VALID) {
      sg_destroy_image(image);
      return {};
    }
    return image;
  }

  /** @brief Create the default texture view through which draws sample @p image.
   *
   * @pre @p image is valid; an invalid or failed image yields an invalid view without reaching the
   *      backend, which would otherwise log a validation error for a resource already known to be bad.
   * @param[in] label Debug label the backend reports for the view; read only during this call.
   * @return The view handle, or an invalid handle when the device rejected it; see @ref make_rgba8_image.
   */
  [[nodiscard]] inline sg_view make_texture_view(sg_image image, const char *label = nullptr) noexcept {
    if (sg_query_image_state(image) != SG_RESOURCESTATE_VALID)
      return {};

    sg_view_desc desc{};
    desc.texture.image = image;
    desc.label = label;

    const sg_view view = sg_make_view(&desc);
    if (sg_query_view_state(view) != SG_RESOURCESTATE_VALID) {
      sg_destroy_view(view);
      return {};
    }
    return view;
  }

} // namespace corundum::platform::glfw
