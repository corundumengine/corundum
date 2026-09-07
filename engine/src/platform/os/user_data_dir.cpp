#include <corundum/platform/paths.hpp>

#include <cstdlib>
#include <optional>

namespace corundum::platform {

  namespace {

    // Returns the value of an environment variable, or nullopt when it is unset
    // or empty (an empty value is treated as absent, matching common OS behaviour).
    [[nodiscard]] std::optional<std::filesystem::path> env_dir(const char *name) {
      const char *const value = std::getenv(name);
      if (value == nullptr || value[0] == '\0')
        return std::nullopt;
      return std::filesystem::path(value);
    }

  } // namespace

  std::expected<std::filesystem::path, std::string> user_data_dir(std::string_view app_name) {
    if (app_name.empty())
      return std::unexpected("app_name must not be empty");

#if defined(_WIN32)
    if (const auto roaming = env_dir("APPDATA"))
      return *roaming / app_name;
    if (const auto profile = env_dir("USERPROFILE"))
      return *profile / "AppData" / "Roaming" / app_name;
    return std::unexpected("APPDATA and USERPROFILE are both unset or empty");
#elif defined(__APPLE__)
    const auto home = env_dir("HOME");
    if (!home)
      return std::unexpected("HOME is unset or empty");
    return *home / "Library" / "Application Support" / app_name;
#else
    if (const auto xdg = env_dir("XDG_DATA_HOME"))
      return *xdg / app_name;
    const auto home = env_dir("HOME");
    if (!home)
      return std::unexpected("XDG_DATA_HOME and HOME are both unset or empty");
    return *home / ".local" / "share" / app_name;
#endif
  }

} // namespace corundum::platform
