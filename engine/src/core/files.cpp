#include <algorithm>
#include <cctype>
#include <corundum/core/files.hpp>

namespace corundum::core {

  std::vector<std::filesystem::path> list_files_in_dir(const std::filesystem::path &dir) {
    std::vector<std::filesystem::path> result;
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec))
      return result;

    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
      if (ec)
        break;
      result.push_back(entry.path());
    }
    return result;
  }

  std::vector<DirEntry> list_dir_entries(const std::filesystem::path &dir) {
    std::vector<DirEntry> result;
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec))
      return result;

    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
      if (ec)
        break;
      result.push_back({entry.path(), entry.path().filename().string(), entry.is_directory()});
    }
    std::ranges::sort(result, [](const DirEntry &a, const DirEntry &b) {
      if (a.is_dir != b.is_dir)
        return a.is_dir;
      return a.name < b.name;
    });
    return result;
  }

  bool has_extension(const std::filesystem::path &path, std::string_view ext) noexcept {
    std::string actual = path.extension().string();
    if (!actual.empty() && actual.front() == '.')
      actual.erase(0, 1);
    if (actual.size() != ext.size())
      return false;
    return std::ranges::equal(actual, ext,
                              [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
  }

} // namespace corundum::core
