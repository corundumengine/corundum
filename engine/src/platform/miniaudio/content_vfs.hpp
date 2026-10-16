// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/content_source.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <miniaudio.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <cstddef>
#include <type_traits>

namespace corundum::platform::miniaudio {

  /// miniaudio VFS that opens every file through a core::ContentSource, so effects and streamed
  /// music load from whatever source the engine was given. Each open reads the whole file into
  /// memory once; reads and seeks then work on that buffer. miniaudio casts the ma_vfs* to
  /// ma_vfs_callbacks*, so callbacks must stay the first member.
  struct ContentVfs {
    ma_vfs_callbacks callbacks{};
    const core::ContentSource *content{};
  };
  static_assert(std::is_standard_layout_v<ContentVfs>, "miniaudio reinterprets ContentVfs* as ma_vfs_callbacks*");
  static_assert(offsetof(ContentVfs, callbacks) == 0, "callbacks must remain the first member");

  /// A ContentVfs whose callbacks are installed and which reads through @p content.
  [[nodiscard]] ContentVfs make_content_vfs(const core::ContentSource &content);

} // namespace corundum::platform::miniaudio
