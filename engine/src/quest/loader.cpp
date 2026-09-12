// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/json_schema.hpp>
#include <corundum/dialogue/compiled_expr.hpp>
#include <corundum/quest/loader.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace corundum::quest {

  namespace {

    struct LoadError : std::runtime_error {
      using std::runtime_error::runtime_error;
    };

    constexpr std::string_view k_ctx = "quest";

    /// Parse the optional "schema_version" field. Absent -> legacy version 1.
    static std::expected<int, std::string> parse_schema_version(const json &j, const std::string &path) {
      if (!j.contains("schema_version"))
        return 1;
      if (!j["schema_version"].is_number_integer())
        return std::unexpected(std::format("Quest '{}' field 'schema_version' must be an integer", path));
      return j["schema_version"].get<int>();
    }

    /// Migrates a quest JSON object in place from @p from_version up to
    /// k_quest_schema_version. No migrations exist yet — schema_version 1 is both
    /// the legacy (absent-field) format and the current format, so this is a no-op
    /// today. Future steps are appended in order and never edited once shipped.
    static std::expected<void, std::string> migrate_quest_json(json & /*j*/, int from_version,
                                                               const std::string &path) {
      if (from_version < 1)
        return std::unexpected(std::format("Quest '{}' has invalid schema_version {}", path, from_version));
      return {};
    }

    static Objective parse_objective(const json &obj_json, const std::string &ctx) {
      // Schema guarantees: text is present and non-empty.
      Objective obj;
      obj.text = obj_json["text"].get<std::string>();
      if (obj_json.contains("done_condition")) {
        const std::string cond = obj_json["done_condition"].get<std::string>();
        auto compiled = dialogue::compile(cond);
        if (!compiled)
          throw LoadError(std::format("[{}] done_condition invalid: {}", ctx, compiled.error().message));
        obj.done_condition = std::move(*compiled);
      }
      return obj;
    }

    static Stage parse_stage(const json &j, std::size_t index) {
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
      for (std::size_t i = 0; i < objs.size(); ++i)
        stage.objectives.push_back(parse_objective(objs[i], std::format("{} objective[{}]", ctx, i)));

      if (j.contains("advances_to")) {
        for (const auto &target : j["advances_to"])
          stage.advances_to.push_back(target.get<std::string>());
      }

      if (j.contains("auto_advance_to")) {
        stage.auto_advance_to = j["auto_advance_to"].get<std::string>();
        if (stage.auto_advance_to->empty())
          throw LoadError(std::format("[{}] \"auto_advance_to\" must not be empty", ctx));
      }

      return stage;
    }

    static Quest load_quest_impl(const std::string &path) {
      std::ifstream f(path);
      if (!f)
        throw LoadError(std::format("cannot open quest file: {}", path));

      json root = [&] {
        try {
          return json::parse(f, nullptr, true, true);
        } catch (const json::exception &e) {
          throw LoadError(std::format("malformed quest JSON in {}: {}", path, e.what()));
        }
      }();

      // ── Schema version (before validation so migrations run first) ──────────
      auto version_result = parse_schema_version(root, path);
      if (!version_result)
        throw LoadError(std::move(version_result).error());
      const int schema_version = *version_result;
      if (schema_version > k_quest_schema_version)
        throw LoadError(std::format("Quest '{}' has schema_version {}, newer than this engine supports (max {}) — "
                                    "update the engine",
                                    path, schema_version, k_quest_schema_version));
      if (schema_version < k_quest_schema_version) {
        auto mig = migrate_quest_json(root, schema_version, path);
        if (!mig)
          throw LoadError(std::move(mig).error());
      }

      // ── Schema validation ──────────────────────────────────────────────────
      {
        auto sv = core::schema_catalog().quest_schema().validate(root);
        if (!sv)
          throw LoadError(std::format("[schema] {}: {}", path, sv.error()));
      }

      // ── Type field (optional — directory context tells us the type) ────────
      if (root.contains("type") && root["type"].is_string() && root["type"] != "quest")
        std::println(stderr, "[warning] quest file {} has type \"{}\" instead of \"quest\"", path,
                     root["type"].get<std::string>());

      // Schema guarantees: id, name, description are present; id and name are non-empty.
      Quest quest;
      quest.schema_version = schema_version;
      quest.quest_id = root["id"].get<std::string>();
      quest.name = root["name"].get<std::string>();
      quest.description = root["description"].get<std::string>();

      // Schema guarantees: stages is a non-empty array.
      const auto &stages = root["stages"];
      for (std::size_t i = 0; i < stages.size(); ++i)
        quest.stages.push_back(parse_stage(stages[i], i));

      if (const auto errors = validate(quest); !errors.empty())
        throw LoadError(std::format("[{}] {}", k_ctx, errors.front()));

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
