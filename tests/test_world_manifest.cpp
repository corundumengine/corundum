#include <doctest/doctest.h>

#include <corundum/gameplay/world/tilemap/world_manifest.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

  void write_file(const fs::path &p, std::string_view content) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << content;
  }

  fs::path temp_dir(std::string_view tag) {
    const auto p = fs::temp_directory_path() / "crpg_test_manifest" / tag;
    fs::create_directories(p);
    return p;
  }

  // 16×8 grid, 128 tiles per chunk
  constexpr std::string_view MANIFEST_JSON = R"({
    "chunk_size": 128,
    "chunks_wide": 16,
    "chunks_tall": 8,
    "scale_factor": 1.0,
    "chunks": []
  })";

} // namespace

TEST_CASE("load_world_manifest parses valid JSON") {
  const auto dir = temp_dir("load");
  const auto path = dir / "manifest.json";
  write_file(path, MANIFEST_JSON);

  auto manifest_result = corundum::gameplay::world::tilemap::load_world_manifest(path);
  REQUIRE(manifest_result.has_value());
  const auto &m = *manifest_result;
  CHECK(m.chunk_size == 128);
  CHECK(m.chunks_wide == 16);
  CHECK(m.chunks_tall == 8);
  CHECK(m.base_dir == dir);
}

TEST_CASE("load_world_manifest returns error on missing file") {
  auto result = corundum::gameplay::world::tilemap::load_world_manifest("/nonexistent/path/manifest.json");
  CHECK(!result.has_value());
}

TEST_CASE("load_world_manifest returns error on missing field") {
  const auto dir = temp_dir("missing");
  const auto path = dir / "manifest.json";
  write_file(path, R"({"chunk_size": 128, "chunks_wide": 16})"); // missing chunks_tall
  auto result = corundum::gameplay::world::tilemap::load_world_manifest(path);
  CHECK(!result.has_value());
}

TEST_CASE("WorldManifest::chunk_path") {
  const auto dir = temp_dir("chunk_path");
  const auto path = dir / "manifest.json";
  write_file(path, MANIFEST_JSON);
  auto manifest_result = corundum::gameplay::world::tilemap::load_world_manifest(path);
  REQUIRE(manifest_result.has_value());
  const auto &m = *manifest_result;

  CHECK(m.chunk_path({0, 0}) == dir / "chunk_0_0.json");
  CHECK(m.chunk_path({3, 5}) == dir / "chunk_3_5.json");
  CHECK(m.chunk_path({15, 7}) == dir / "chunk_15_7.json");
}

TEST_CASE("WorldManifest::in_bounds") {
  const auto dir = temp_dir("in_bounds");
  write_file(dir / "manifest.json", MANIFEST_JSON);
  auto manifest_result = corundum::gameplay::world::tilemap::load_world_manifest(dir / "manifest.json");
  REQUIRE(manifest_result.has_value());
  const auto &m = *manifest_result;

  CHECK(m.in_bounds({0, 0}));
  CHECK(m.in_bounds({15, 7}));
  CHECK(m.in_bounds({8, 4}));
  CHECK_FALSE(m.in_bounds({-1, 0}));
  CHECK_FALSE(m.in_bounds({0, -1}));
  CHECK_FALSE(m.in_bounds({16, 0}));
  CHECK_FALSE(m.in_bounds({0, 8}));
}

TEST_CASE("chunk_at_cart — origin maps to (0,0)") {
  using namespace corundum::gameplay::world::tilemap;
  const auto dir = temp_dir("chunk_at_cart");
  write_file(dir / "manifest.json", MANIFEST_JSON);
  auto manifest_result = load_world_manifest(dir / "manifest.json");
  REQUIRE(manifest_result.has_value());
  const auto &m = *manifest_result;

  // 128 tiles × 16 px × 2.0 scale = 4096 px per chunk
  // Position (0,0) → chunk (0,0)
  CHECK(chunk_at_cart(0.f, 0.f, m, 16, 2.f) == ChunkCoord{0, 0});
}

TEST_CASE("chunk_at_cart — center of world") {
  using namespace corundum::gameplay::world::tilemap;
  const auto dir = temp_dir("chunk_at_cart_center");
  write_file(dir / "manifest.json", MANIFEST_JSON);
  auto manifest_result = load_world_manifest(dir / "manifest.json");
  REQUIRE(manifest_result.has_value());
  const auto &m = *manifest_result;

  // Center = (8 * 4096, 4 * 4096) = (32768, 16384)
  // That's the origin of chunk (8,4)
  CHECK(chunk_at_cart(32768.f, 16384.f, m, 16, 2.f) == ChunkCoord{8, 4});
}

TEST_CASE("chunk_at_cart — clamped at world edge") {
  using namespace corundum::gameplay::world::tilemap;
  const auto dir = temp_dir("chunk_at_cart_clamp");
  write_file(dir / "manifest.json", MANIFEST_JSON);
  auto manifest_result = load_world_manifest(dir / "manifest.json");
  REQUIRE(manifest_result.has_value());
  const auto &m = *manifest_result;

  // Past the right/bottom edge → clamped to last chunk (15,7)
  CHECK(chunk_at_cart(999999.f, 999999.f, m, 16, 2.f) == ChunkCoord{15, 7});
  // Negative → clamped to (0,0)
  CHECK(chunk_at_cart(-1.f, -1.f, m, 16, 2.f) == ChunkCoord{0, 0});
}

TEST_CASE("chunk_origin_px — (0,0) at world origin") {
  using namespace corundum::gameplay::world::tilemap;
  const auto dir = temp_dir("origin_px");
  write_file(dir / "manifest.json", MANIFEST_JSON);
  auto manifest_result = load_world_manifest(dir / "manifest.json");
  REQUIRE(manifest_result.has_value());
  const auto &m = *manifest_result;

  auto [ox, oy] = chunk_origin_px({0, 0}, m, 16, 2.f);
  CHECK(ox == doctest::Approx(0.f));
  CHECK(oy == doctest::Approx(0.f));
}

TEST_CASE("chunk_origin_px — (1,0) offset") {
  using namespace corundum::gameplay::world::tilemap;
  const auto dir = temp_dir("origin_1_0");
  write_file(dir / "manifest.json", MANIFEST_JSON);
  auto manifest_result = load_world_manifest(dir / "manifest.json");
  REQUIRE(manifest_result.has_value());
  const auto &m = *manifest_result;

  // 128 * 16 * 2 = 4096
  auto [ox, oy] = chunk_origin_px({1, 0}, m, 16, 2.f);
  CHECK(ox == doctest::Approx(4096.f));
  CHECK(oy == doctest::Approx(0.f));
}

TEST_CASE("world_bounds_iso for 16×8 manifest") {
  using namespace corundum::gameplay::world::tilemap;
  const auto dir = temp_dir("world_bounds");
  write_file(dir / "manifest.json", MANIFEST_JSON);
  auto manifest_result = load_world_manifest(dir / "manifest.json");
  REQUIRE(manifest_result.has_value());
  const auto &m = *manifest_result;

  // 16*128 + 8*128 = 3072 tiles total; (3072 - 1) * half_tw * 2
  // half_tw = 16 * 2.f * 0.5f = 16; half_th is half of that (classic 2:1 diamond ratio).
  const float half_tw = 16.f * 2.f * 0.5f;
  const float half_th = half_tw * 0.5f;
  auto [w, h] = world_bounds_iso(m, half_tw, half_th);
  // (tiles_wide + tiles_tall - 1) * half_tw * 2 = (2048 + 1024 - 1) * 16 * 2 = 3071 * 32
  const float steps = static_cast<float>(16 * 128 + 8 * 128 - 1);
  CHECK(w == doctest::Approx(steps * half_tw * 2.f));
  CHECK(h == doctest::Approx(steps * half_th * 2.f));
  CHECK(w == doctest::Approx(h * 2.f)); // half_th = half_tw / 2, so height is half of width
}

TEST_CASE("active_chunk_coords — center of world, radius=1") {
  using namespace corundum::gameplay::world::tilemap;
  const auto dir = temp_dir("active_center");
  write_file(dir / "manifest.json", MANIFEST_JSON);
  auto manifest_result = load_world_manifest(dir / "manifest.json");
  REQUIRE(manifest_result.has_value());
  const auto &m = *manifest_result;

  const auto coords = active_chunk_coords({8, 4}, 1, m);
  CHECK(coords.size() == 9);
}

TEST_CASE("active_chunk_coords — top-left corner, radius=1 clamped") {
  using namespace corundum::gameplay::world::tilemap;
  const auto dir = temp_dir("active_corner");
  write_file(dir / "manifest.json", MANIFEST_JSON);
  auto manifest_result = load_world_manifest(dir / "manifest.json");
  REQUIRE(manifest_result.has_value());
  const auto &m = *manifest_result;

  const auto coords = active_chunk_coords({0, 0}, 1, m);
  CHECK(coords.size() == 4); // only bottom-right quadrant of 3×3 is in bounds
}

TEST_CASE("active_chunk_coords — bottom-right corner clamped") {
  using namespace corundum::gameplay::world::tilemap;
  const auto dir = temp_dir("active_br");
  write_file(dir / "manifest.json", MANIFEST_JSON);
  auto manifest_result = load_world_manifest(dir / "manifest.json");
  REQUIRE(manifest_result.has_value());
  const auto &m = *manifest_result;

  const auto coords = active_chunk_coords({15, 7}, 1, m);
  CHECK(coords.size() == 4);
}
