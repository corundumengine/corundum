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

} // namespace corundum::core
