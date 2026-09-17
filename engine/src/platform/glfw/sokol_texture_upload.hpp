// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <sokol_gfx.h>

#include <cstddef>
#include <cstdint>

namespace corundum::platform::glfw {

  /** @brief Create an RGBA8 image from @p pixels, which are copied during the call.
   *
   * @param[in] width   Image width in pixels.
   * @param[in] height  Image height in pixels.
   * @param[in] label   Debug label the backend reports for the image.
   * @return The image handle; invalid if the device rejected it.
   */
  inline sg_image make_rgba8_image(const uint8_t *pixels, int width, int height, const char *label) {
    sg_image_desc desc{};
    desc.width = width;
    desc.height = height;
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.data.mip_levels[0] = {.ptr = pixels, .size = static_cast<std::size_t>(width * height * 4)};
    desc.label = label;
    return sg_make_image(&desc);
  }

  /** @brief Create the default texture view through which draws sample @p image. */
  inline sg_view make_texture_view(sg_image image) {
    sg_view_desc desc{};
    desc.texture.image = image;
    return sg_make_view(&desc);
  }

} // namespace corundum::platform::glfw
