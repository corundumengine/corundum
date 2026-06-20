#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace corundum::gameplay {

  /// Splits `text` into lines no wider than `max_width` as reported by `measure`.
  /// Hard '\n' characters always force a line break. A single word wider than
  /// `max_width` is placed on its own line rather than looping forever.
  /// @param measure Callable (std::string_view) -> float returning rendered width.
  template <typename MeasureFn>
  [[nodiscard]] std::vector<std::string> wrap_text(std::string_view text, float max_width, MeasureFn measure) {
    std::vector<std::string> result;

    size_t seg_start = 0;
    while (true) {
      // Split on hard newlines first
      const size_t nl = text.find('\n', seg_start);
      const size_t seg_end = (nl == std::string_view::npos) ? text.size() : nl;
      const auto seg = text.substr(seg_start, seg_end - seg_start);

      // Greedy word-wrap within the segment
      std::string line;
      size_t w = 0;
      while (w <= seg.size()) {
        const size_t sp = seg.find(' ', w);
        const size_t end = (sp == std::string_view::npos) ? seg.size() : sp;
        const std::string_view word = seg.substr(w, end - w);

        if (!word.empty()) {
          std::string candidate = line.empty() ? std::string(word) : line + ' ' + std::string(word);

          if (measure(std::string_view{candidate}) <= max_width) {
            line = std::move(candidate);
          } else {
            if (!line.empty())
              result.push_back(std::move(line));
            line = std::string(word); // force oversized word onto its own line
          }
        }
        w = end + 1;
      }
      result.push_back(std::move(line)); // flush; empty string == blank line

      if (nl == std::string_view::npos)
        break;
      seg_start = nl + 1;
    }
    return result;
  }

} // namespace corundum::gameplay
