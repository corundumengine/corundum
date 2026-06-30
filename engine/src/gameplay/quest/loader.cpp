#include <corundum/core/json_schema.hpp>
#include <corundum/gameplay/quest/loader.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace corundum::gameplay::quest {

  namespace {

    struct LoadError : std::runtime_error {
      using std::runtime_error::runtime_error;
    };

    constexpr std::string_view k_ctx = "quest";

    static Objective parse_objective(const json &obj_json) {
      // Schema guarantees: text is present and non-empty.
      Objective obj;
      obj.text = obj_json["text"].get<std::string>();
      if (obj_json.contains("done_condition"))
        obj.done_condition = obj_json["done_condition"].get<std::string>();
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
        stage.objectives.push_back(parse_objective(objs[i]));

      return stage;
    }

    static Quest load_quest_impl(const std::string &path) {
      std::ifstream f(path);
      if (!f)
        throw LoadError(std::format("cannot open quest file: {}", path));

      const json root = [&] {
        try {
          return json::parse(f);
        } catch (const json::exception &e) {
          throw LoadError(std::format("malformed quest JSON: {}", e.what()));
        }
      }();

      // ── Schema validation ──────────────────────────────────────────────────
      {
        auto sv = core::quest_schema().validate(root);
        if (!sv)
          throw LoadError(std::format("[schema] {}", sv.error()));
      }

      // ── Type field (optional — directory context tells us the type) ────────
      if (root.contains("type") && root["type"].is_string() && root["type"] != "quest")
        std::println(stderr, "[warning] quest file {} has type \"{}\" instead of \"quest\"", path,
                     root["type"].get<std::string>());

      // Schema guarantees: id, name, description are present; id and name are non-empty.
      Quest quest;
      quest.quest_id = root["id"].get<std::string>();
      quest.name = root["name"].get<std::string>();
      quest.description = root["description"].get<std::string>();

      // Schema guarantees: stages is a non-empty array.
      const auto &stages = root["stages"];
      for (std::size_t i = 0; i < stages.size(); ++i)
        quest.stages.push_back(parse_stage(stages[i], i));

      // Validate unique stage names via sort + adjacent_find
      {
        std::vector<std::string> names;
        names.reserve(quest.stages.size());
        for (const auto &stage : quest.stages)
          names.push_back(stage.name);
        std::ranges::sort(names);
        const auto dup = std::ranges::adjacent_find(names);
        if (dup != names.end())
          throw LoadError(std::format("[{}] duplicate stage name \"{}\"", k_ctx, *dup));
      }

      // Validate unique positive sequences via sort + adjacent_find
      // (schema already enforces >= 1 via minimum)
      {
        std::vector<int> seqs;
        seqs.reserve(quest.stages.size());
        for (const auto &stage : quest.stages)
          seqs.push_back(stage.sequence);
        std::ranges::sort(seqs);
        const auto dup = std::ranges::adjacent_find(seqs);
        if (dup != seqs.end())
          throw LoadError(std::format("[{}] duplicate sequence {} in stages", k_ctx, *dup));
      }

      // At least one resolved stage
      if (!std::ranges::any_of(quest.stages, &Stage::resolved))
        throw LoadError(std::format("[{}] \"{}\" has no resolved stage", k_ctx, quest.quest_id));

      return quest;
    }

  } // namespace

  std::expected<Quest, std::string> load_quest(const std::string &path) {
    try {
      return load_quest_impl(path);
    } catch (const std::exception &e) {
      return std::unexpected(std::string(e.what()));
    }
  }

} // namespace corundum::gameplay::quest
