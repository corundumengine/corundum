// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/environment.hpp>

#include <cstddef>
#include <cstdlib>
#include <optional>
#include <string>

namespace corundum::core {

  namespace {

#ifdef _MSC_VER
    // The UCRT deprecates getenv as unsafe and points at _dupenv_s, which
    // allocates the result; free its buffer before returning.
    [[nodiscard]] std::optional<std::string> read_env_msvc(const char *name) {
      char *buffer = nullptr;
      std::size_t size = 0;
      if (_dupenv_s(&buffer, &size, name) != 0 || buffer == nullptr)
        return std::nullopt;
      std::string value(buffer);
      std::free(buffer);
      return value;
    }
#endif

  } // namespace

  std::optional<std::string> read_env(const char *name) {
    if (name == nullptr || name[0] == '\0')
      return std::nullopt;

#ifdef _MSC_VER
    return read_env_msvc(name);
#else
    const char *const value = std::getenv(name);
    if (value == nullptr)
      return std::nullopt;
    return std::string(value);
#endif
  }

} // namespace corundum::core
