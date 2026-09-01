#include <doctest/doctest.h>

#include <corundum/item/loader.hpp>
#include <corundum/item/registry.hpp>

#include <filesystem>
#include <fstream>

namespace item = corundum::item;

TEST_CASE("item loader: valid JSON produces correct Item struct") {
  const auto tmp = std::filesystem::temp_directory_path() / "item_test_valid.json";
  {
    std::ofstream f(tmp);
    f << R"({"id":"sword","name":"Iron Sword","description":"A sturdy blade.","icon":"sword_icon"})";
  }

  const auto result = item::load_item(tmp);
  REQUIRE(result.has_value());
  CHECK(result->id == "sword");
  CHECK(result->name == "Iron Sword");
  CHECK(result->description == "A sturdy blade.");
  CHECK(result->icon == "sword_icon");

  std::filesystem::remove(tmp);
}

TEST_CASE("item loader: missing id defaults to filename stem") {
  const auto tmp = std::filesystem::temp_directory_path() / "salt.json";
  {
    std::ofstream f(tmp);
    f << R"({"name":"Salt"})";
  }

  const auto result = item::load_item(tmp);
  REQUIRE(result.has_value());
  CHECK(result->id == "salt");
  CHECK(result->name == "Salt");
  CHECK(result->description.empty());
  CHECK(result->icon.empty());

  std::filesystem::remove(tmp);
}

TEST_CASE("item loader: missing name fails schema validation") {
  const auto tmp = std::filesystem::temp_directory_path() / "item_test_no_name.json";
  {
    std::ofstream f(tmp);
    f << R"({"id":"x"})";
  }

  const auto result = item::load_item(tmp);
  CHECK_FALSE(result.has_value());

  std::filesystem::remove(tmp);
}

TEST_CASE("item loader: malformed JSON returns error") {
  const auto tmp = std::filesystem::temp_directory_path() / "item_test_bad_json.json";
  {
    std::ofstream f(tmp);
    f << "not valid json";
  }

  const auto result = item::load_item(tmp);
  CHECK_FALSE(result.has_value());
  CHECK(result.error().find("malformed item JSON") != std::string::npos);

  std::filesystem::remove(tmp);
}

TEST_CASE("registry load_all loads valid files, skips bad ones") {
  const auto tmp_dir = std::filesystem::temp_directory_path() / "item_test_registry";
  std::filesystem::create_directories(tmp_dir);

  {
    std::ofstream f(tmp_dir / "sword.json");
    f << R"({"name":"Iron Sword","description":"A sturdy blade."})";
  }
  {
    std::ofstream f(tmp_dir / "bad.json");
    f << R"({})"; // missing required "name"
  }
  {
    std::ofstream f(tmp_dir / "notes.txt");
    f << "not an item";
  }

  item::Registry reg;
  const int loaded = reg.load_all(tmp_dir);

  CHECK(loaded == 1);
  CHECK(reg.size() == 1);

  const auto *sword = reg.find("sword");
  REQUIRE(sword != nullptr);
  CHECK(sword->name == "Iron Sword");

  CHECK(reg.find("missing") == nullptr);

  std::filesystem::remove_all(tmp_dir);
}
