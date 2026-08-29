#include <algorithm>
#include <cctype>
#include <corundum/core/files.hpp>
#include <format>

namespace corundum::core {

  std::expected<std::vector<DirEntry>, std::string> list_dir_entries(const std::filesystem::path &dir,
                                                                     ListOptions opts) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec))
      return std::unexpected(std::format("Directory does not exist: {}", dir.string()));
    if (!std::filesystem::is_directory(dir, ec))
      return std::unexpected(std::format("Path is not a directory: {}", dir.string()));

    std::vector<DirEntry> result;

    auto visit = [&](const auto &entry) {
      const bool is_dir = entry.is_directory(ec);
      if (ec)
        return;
      if (!is_dir && !opts.extensions.empty()) {
        const bool any_match = std::ranges::any_of(
            opts.extensions, [&](std::string_view ext) { return has_extension(entry.path(), ext); });
        if (!any_match)
          return;
      }
      result.push_back({is_dir, entry.path().filename().string(), entry.path()});
    };

    if (opts.recursive) {
      for (const auto &entry : std::filesystem::recursive_directory_iterator(dir, ec)) {
        visit(entry);
      }
    } else {
      for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
        visit(entry);
      }
    }

    if (ec)
      return std::unexpected(std::format("Error walking {}: {}", dir.string(), ec.message()));

    std::ranges::sort(result, [](const DirEntry &a, const DirEntry &b) {
      if (a.is_dir != b.is_dir)
        return a.is_dir;
      return a.name < b.name;
    });

    return result;
  }

  bool has_extension(const std::filesystem::path &path, std::string_view ext) noexcept {
    const std::string actual = path.extension().string();
    const std::string_view lhs =
        (!actual.empty() && actual.front() == '.') ? std::string_view(actual).substr(1) : std::string_view(actual);
    if (lhs.size() != ext.size())
      return false;
    return std::ranges::equal(lhs, ext,
                              [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
  }

} // namespace corundum::core
