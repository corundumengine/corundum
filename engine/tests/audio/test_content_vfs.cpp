// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "platform/miniaudio/content_vfs.hpp"

#include "memory_content_source.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <string>

namespace {

  using corundum::platform::miniaudio::ContentVfs;
  using corundum::platform::miniaudio::make_content_vfs;

} // namespace

TEST_CASE("ContentVfs: reads a whole file and then reports end of file") {
  corundum::test::MemoryContentSource content;
  content.add("file.txt", "hello");
  ContentVfs vfs = make_content_vfs(content);

  void *file = nullptr;
  REQUIRE(ma_vfs_open(&vfs, "file.txt", MA_OPEN_MODE_READ, &file) == MA_SUCCESS);

  ma_file_info info{};
  REQUIRE(ma_vfs_info(&vfs, file, &info) == MA_SUCCESS);
  CHECK(info.sizeInBytes == 5);

  std::array<char, 8> buffer{};
  std::size_t read = 0;
  REQUIRE(ma_vfs_read(&vfs, file, buffer.data(), 5, &read) == MA_SUCCESS);
  CHECK(read == 5);
  CHECK(std::string{buffer.data(), read} == "hello");

  REQUIRE(ma_vfs_read(&vfs, file, buffer.data(), 5, &read) == MA_AT_END);
  CHECK(read == 0);
  CHECK(ma_vfs_close(&vfs, file) == MA_SUCCESS);
}

TEST_CASE("ContentVfs: seek from start, current and end moves the cursor") {
  corundum::test::MemoryContentSource content;
  content.add("file.txt", "hello");
  ContentVfs vfs = make_content_vfs(content);

  void *file = nullptr;
  REQUIRE(ma_vfs_open(&vfs, "file.txt", MA_OPEN_MODE_READ, &file) == MA_SUCCESS);

  ma_int64 cursor = 0;
  REQUIRE(ma_vfs_seek(&vfs, file, 1, ma_seek_origin_start) == MA_SUCCESS);
  REQUIRE(ma_vfs_tell(&vfs, file, &cursor) == MA_SUCCESS);
  CHECK(cursor == 1);

  REQUIRE(ma_vfs_seek(&vfs, file, 2, ma_seek_origin_current) == MA_SUCCESS);
  REQUIRE(ma_vfs_tell(&vfs, file, &cursor) == MA_SUCCESS);
  CHECK(cursor == 3);

  REQUIRE(ma_vfs_seek(&vfs, file, -2, ma_seek_origin_end) == MA_SUCCESS);
  REQUIRE(ma_vfs_tell(&vfs, file, &cursor) == MA_SUCCESS);
  CHECK(cursor == 3);

  std::array<char, 8> buffer{};
  std::size_t read = 0;
  REQUIRE(ma_vfs_read(&vfs, file, buffer.data(), 5, &read) == MA_SUCCESS);
  CHECK(std::string{buffer.data(), read} == "lo");
  CHECK(ma_vfs_close(&vfs, file) == MA_SUCCESS);
}

TEST_CASE("ContentVfs: seek past the end is rejected") {
  corundum::test::MemoryContentSource content;
  content.add("file.txt", "hello");
  ContentVfs vfs = make_content_vfs(content);

  void *file = nullptr;
  REQUIRE(ma_vfs_open(&vfs, "file.txt", MA_OPEN_MODE_READ, &file) == MA_SUCCESS);

  CHECK(ma_vfs_seek(&vfs, file, 6, ma_seek_origin_start) == MA_BAD_SEEK);
  CHECK(ma_vfs_seek(&vfs, file, 1, ma_seek_origin_end) == MA_BAD_SEEK);
  CHECK(ma_vfs_seek(&vfs, file, -1, ma_seek_origin_start) == MA_BAD_SEEK);
  CHECK(ma_vfs_close(&vfs, file) == MA_SUCCESS);
}

TEST_CASE("ContentVfs: opening a missing file reports MA_DOES_NOT_EXIST") {
  const corundum::test::MemoryContentSource content;
  ContentVfs vfs = make_content_vfs(content);

  void *file = nullptr;
  CHECK(ma_vfs_open(&vfs, "nope.txt", MA_OPEN_MODE_READ, &file) == MA_DOES_NOT_EXIST);
}

TEST_CASE("ContentVfs: opening for write is refused") {
  corundum::test::MemoryContentSource content;
  content.add("file.txt", "hello");
  ContentVfs vfs = make_content_vfs(content);

  void *file = nullptr;
  CHECK(ma_vfs_open(&vfs, "file.txt", MA_OPEN_MODE_WRITE, &file) == MA_ACCESS_DENIED);
}
