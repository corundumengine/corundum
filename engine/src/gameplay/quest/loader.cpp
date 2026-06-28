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

    template <typename T> static T require(const json &obj, const std::string &key, const std::string &ctx) {
      if (!obj.contains(key))
        throw LoadError(std::format("[{}] missing required field \"{}\"", ctx, key));
      try {
        return obj[key].get<T>();
      } catch (const json::type_error &e) {
        throw LoadError(std::format("[{}] field \"{}\" has wrong type: {}", ctx, key, e.what()));
      }
    }

    static Objective parse_objective(const json &j, const std::string &stage_ctx) {
      Objective obj;
      obj.text = require<std::string>(j, "text", stage_ctx);
      if (j.contains("done_condition"))
        obj.done_condition = j["done_condition"].get<std::string>();
      return obj;
    }

    static Stage parse_stage(const json &j, std::size_t index) {
      const auto ctx = std::format("stage[{}]", index);

      Stage stage;
      stage.name = require<std::string>(j, "name", ctx);
      stage.sequence = require<int>(j, "sequence", ctx);

      if (j.contains("resolved"))
        stage.resolved = j["resolved"].get<bool>();

      if (j.contains("failed")) {
        stage.failed = j["failed"].get<bool>();
        if (stage.failed)
          stage.resolved = true;
      }

      const auto objs = require<json>(j, "objectives", ctx);
      if (!objs.is_array())
        throw LoadError(std::format("[{}] \"objectives\" must be an array", ctx));

      for (std::size_t i = 0; i < objs.size(); ++i)
        stage.objectives.push_back(parse_objective(objs[i], std::format("{}.objectives[{}]", ctx, i)));

      return stage;
    }

    static Quest load_quest_impl(const std::string &path) {
      std::ifstream f(path);
      if (!f)
        throw LoadError(std::format("cannot open quest file: {}", path));

      json j;
      try {
        j = json::parse(f);
      } catch (const json::exception &e) {
        throw LoadError(std::format("malformed quest JSON: {}", e.what()));
      }

      const auto ctx = std::string(k_ctx);

      const auto type_val = require<std::string>(j, "type", ctx);
      if (type_val != "quest")
        throw LoadError(std::format("[{}] \"type\" must be \"quest\", got \"{}\"", ctx, type_val));

      Quest quest;
      quest.quest_id = require<std::string>(j, "id", ctx);
      quest.name = require<std::string>(j, "name", ctx);
      quest.description = require<std::string>(j, "description", ctx);

      if (quest.quest_id.empty())
        throw LoadError(std::format("[{}] \"id\" must not be empty", k_ctx));
      if (quest.name.empty())
        throw LoadError(std::format("[{}] \"name\" must not be empty", k_ctx));

      const auto stages = require<json>(j, "stages", ctx);
      if (!stages.is_array())
        throw LoadError(std::format("[{}] \"stages\" must be an array", k_ctx));

      for (std::size_t i = 0; i < stages.size(); ++i)
        quest.stages.push_back(parse_stage(stages[i], i));

      if (quest.stages.empty())
        throw LoadError(std::format("[{}] must have at least one stage", k_ctx));

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
      {
        for (const auto &stage : quest.stages) {
          if (stage.sequence <= 0)
            throw LoadError(
                std::format("[{}] stage \"{}\" has non-positive sequence {}", k_ctx, stage.name, stage.sequence));
        }

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
