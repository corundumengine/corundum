// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "content_vfs.hpp"

#include <corundum/core/content_source.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace corundum::platform::miniaudio {
  namespace {

    /// One opened file: the whole contents in memory and a read cursor. Owned by miniaudio
    /// between onOpen and onClose.
    struct OpenFile {
      std::vector<std::uint8_t> bytes;
      std::size_t cursor{};
    };

    /// The ContentSource behind a callback's opaque ma_vfs pointer.
    [[nodiscard]] const corundum::core::ContentSource &source_of(void *p_vfs) {
      return *static_cast<ContentVfs *>(p_vfs)->content;
    }

    // The callback signatures are fixed by ma_vfs_callbacks; their raw pointers cannot be const.
    // NOLINTBEGIN(misc-const-correctness, readability-non-const-parameter)
    ma_result on_open(void *p_vfs, const char *file_path, ma_uint32 open_mode, void **p_file) {
      if ((open_mode & MA_OPEN_MODE_WRITE) != 0)
        return MA_ACCESS_DENIED;

      const corundum::core::ContentSource &content = source_of(p_vfs);
      try {
        std::expected<std::vector<std::uint8_t>, std::string> bytes = content.read(file_path);
        if (!bytes) {
          // A read failure isn't the same as a missing file; only report "missing" when it is.
          return content.contains(file_path) ? MA_IO_ERROR : MA_DOES_NOT_EXIST;
        }

        // The C API forces the opaque pointer: miniaudio owns the handle until onClose.
        auto file = std::make_unique<OpenFile>(OpenFile{.bytes = std::move(*bytes), .cursor = 0});
        *p_file = file.release();
        return MA_SUCCESS;
      } catch (const std::bad_alloc &) {
        return MA_OUT_OF_MEMORY;
      }
    }

    ma_result on_open_w(void *p_vfs, const wchar_t *file_path, ma_uint32 open_mode, void **p_file) {
      static_cast<void>(p_vfs);
      static_cast<void>(file_path);
      static_cast<void>(open_mode);
      static_cast<void>(p_file);
      return MA_NOT_IMPLEMENTED;
    }

    ma_result on_close(void *p_vfs, void *file_handle) {
      static_cast<void>(p_vfs);
      // Reclaims the pointer onOpen released to miniaudio; the unique_ptr frees it on return.
      const std::unique_ptr<OpenFile> file{static_cast<OpenFile *>(file_handle)};
      return MA_SUCCESS;
    }

    ma_result on_read(void *p_vfs, void *file_handle, void *p_dst, std::size_t size_in_bytes,
                      std::size_t *p_bytes_read) {
      static_cast<void>(p_vfs);
      auto *file = static_cast<OpenFile *>(file_handle);
      const std::size_t remaining = file->bytes.size() - file->cursor;
      const std::size_t to_copy = std::min(size_in_bytes, remaining);
      if (to_copy > 0)
        std::memcpy(p_dst, file->bytes.data() + file->cursor, to_copy);
      file->cursor += to_copy;
      *p_bytes_read = to_copy;
      if (to_copy == 0 && size_in_bytes > 0)
        return MA_AT_END;
      return MA_SUCCESS;
    }

    ma_result on_write(void *p_vfs, void *file_handle, const void *p_src, std::size_t size_in_bytes,
                       std::size_t *p_bytes_written) {
      static_cast<void>(p_vfs);
      static_cast<void>(file_handle);
      static_cast<void>(p_src);
      static_cast<void>(size_in_bytes);
      static_cast<void>(p_bytes_written);
      return MA_NOT_IMPLEMENTED;
    }

    ma_result on_seek(void *p_vfs, void *file_handle, ma_int64 offset, ma_seek_origin origin) {
      static_cast<void>(p_vfs);
      auto *file = static_cast<OpenFile *>(file_handle);
      const auto size = static_cast<ma_int64>(file->bytes.size());
      ma_int64 base = size;
      if (origin == ma_seek_origin_start)
        base = 0;
      else if (origin == ma_seek_origin_current)
        base = static_cast<ma_int64>(file->cursor);
      const ma_int64 target = base + offset;
      if (target < 0 || target > size)
        return MA_BAD_SEEK;
      file->cursor = static_cast<std::size_t>(target);
      return MA_SUCCESS;
    }

    ma_result on_tell(void *p_vfs, void *file_handle, ma_int64 *p_cursor) {
      static_cast<void>(p_vfs);
      *p_cursor = static_cast<ma_int64>(static_cast<OpenFile *>(file_handle)->cursor);
      return MA_SUCCESS;
    }

    ma_result on_info(void *p_vfs, void *file_handle, ma_file_info *p_info) {
      static_cast<void>(p_vfs);
      p_info->sizeInBytes = static_cast<OpenFile *>(file_handle)->bytes.size();
      return MA_SUCCESS;
    }

    // NOLINTEND(misc-const-correctness, readability-non-const-parameter)

  } // namespace

  ContentVfs make_content_vfs(const corundum::core::ContentSource &content) {
    ContentVfs vfs;
    vfs.content = &content;
    vfs.callbacks.onOpen = on_open;
    vfs.callbacks.onOpenW = on_open_w;
    vfs.callbacks.onClose = on_close;
    vfs.callbacks.onRead = on_read;
    vfs.callbacks.onWrite = on_write;
    vfs.callbacks.onSeek = on_seek;
    vfs.callbacks.onTell = on_tell;
    vfs.callbacks.onInfo = on_info;
    return vfs;
  }

} // namespace corundum::platform::miniaudio
