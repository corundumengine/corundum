// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/json_io.hpp>
#include <corundum/core/json_schema.hpp>
#include <corundum/core/schema_version.hpp>
#include <corundum/dialogue/compiled_expr.hpp>
#include <corundum/quest/loader.hpp>
#include <corundum/quest/quest.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdio>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <nlohmann/json.hpp>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using nlohmann::json;

namespace corundum::quest {

  namespace {

    struct LoadError : std::runtime_error {
      using std::runtime_error::runtime_error;
    };

    constexpr std::string_view k_ctx = "quest";
    constexpr std::string_view k_asset_label = "Quest";

    /// Migrates a quest JSON object in place from @p from_version up to
    /// k_quest_schema_version, returning the version reached. No migrations exist yet —
    /// schema_version 1 is both the legacy (absent-field) format and the current format
    /// — so this returns @p from_version unchanged. Future steps advance @p from_version
    /// and are never edited once shipped; leaving this stub unchanged after a version
    /// bump fails loudly (the caller rejects a result below the current version).
    std::expected<int, std::string> migrate_quest_json(json & /*j*/, int from_version, const std::string & /*path*/) {
      return from_version;
    }

    Objective parse_objective(const json &obj_json, const std::string &ctx) {
      // Schema guarantees: text is present.
      Objective obj;
      obj.text = obj_json["text"].get<std::string>();
      if (obj_json.contains("done_condition")) {
        const std::string cond = obj_json["done_condition"].get<std::string>();
        // An empty expression compiles to a constant-true node, which would
        // silently mark the objective done (and auto-advance its stage).
        if (cond.empty())
          throw LoadError(std::format("[{}] \"done_condition\" must not be empty if present", ctx));
        auto compiled = dialogue::compile(cond);
        if (!compiled)
          throw LoadError(std::format("[{}] done_condition invalid: {}", ctx, compiled.error().message));
        obj.done_condition = std::move(*compiled);
      }
      return obj;
    }

    Stage parse_stage(const json &j, std::size_t index) {
      const auto ctx = std::format("stage[{}]", index);

      // Schema guarantees: name and sequence are present, sequence >= 1.
      Stage stage;
      stage.name = j["name"].get<std::string>();
      stage.sequence = j["sequence"].get<int>();

      if (j.contains("resolved"))
        stage.resolved = j["resolved"].get<bool>();

      if (j.contains("failed")) {
        stage.failed = j["failed"].get<bool>();
        if (stage.failed)
          stage.resolved = true;
      }

      // Schema guarantees: objectives is present.
      const auto &objs = j["objectives"];
      stage.objectives.reserve(objs.size());
      for (std::size_t i = 0; i < objs.size(); ++i)
        stage.objectives.push_back(parse_objective(objs[i], std::format("{} objective[{}]", ctx, i)));

      if (j.contains("advances_to")) {
        const auto &targets = j["advances_to"];
        stage.advances_to.reserve(targets.size());
        for (const auto &target : targets)
          stage.advances_to.push_back(target.get<std::string>());
      }

      if (j.contains("auto_advance_to")) {
        stage.auto_advance_to = j["auto_advance_to"].get<std::string>();
        if (stage.auto_advance_to->empty())
          throw LoadError(std::format("[{}] \"auto_advance_to\" must not be empty", ctx));
      }

      return stage;
    }

    /// Read @p path, migrate it forward, and return its schema-validated root.
    json load_validated_root(const std::string &path) {
      auto root_result = core::read_json(path, "quest JSON");
      if (!root_result)
        throw LoadError(std::move(root_result).error());
      json root = std::move(*root_result);

      // Schema version is read before validation so migrations run first.
      auto prepared =
          core::prepare_schema_version(root, k_quest_schema_version, k_asset_label, path, migrate_quest_json);
      if (!prepared)
        throw LoadError(std::move(prepared).error());

      auto validated = core::schema_catalog().quest_schema().validate(root);
      if (!validated)
        throw LoadError(std::format("[schema] {}: {}", path, validated.error()));

      // The "type" field is optional — directory context already tells us the type.
      if (root.contains("type") && root["type"].is_string() && root["type"] != "quest")
        std::println(stderr, R"([warning] quest file {} has type "{}" instead of "quest")", path,
                     root["type"].get<std::string>());

      return root;
    }

    /// Build a Quest from an already schema-validated root.
    Quest load_quest_impl(const std::string &path) {
      const json root = load_validated_root(path);

      // Schema guarantees: id, name, description are present; id and name are non-empty.
      Quest quest;
      // The root was migrated to the current shape, so this describes the loaded data.
      quest.schema_version = k_quest_schema_version;
      quest.quest_id = root["id"].get<std::string>();
      quest.name = root["name"].get<std::string>();
      quest.description = root["description"].get<std::string>();

      // Schema guarantees: stages is a non-empty array.
      const auto &stages = root["stages"];
      quest.stages.reserve(stages.size());
      for (std::size_t i = 0; i < stages.size(); ++i)
        quest.stages.push_back(parse_stage(stages[i], i));

      const ValidationResult validation = validate(quest);
      if (!validation.ok())
        throw LoadError(std::format("[{}] {}", k_ctx, validation.errors.front()));
      for (const auto &warning : validation.warnings)
        std::println(stderr, "[warning] quest {}: {}", path, warning);

      return quest;
    }

  } // namespace

  std::expected<Quest, std::string> load_quest(const std::filesystem::path &path) {
    try {
      return load_quest_impl(path.string());
    } catch (const std::exception &e) {
      return std::unexpected(std::string(e.what()));
    }
  }

} // namespace corundum::quest
