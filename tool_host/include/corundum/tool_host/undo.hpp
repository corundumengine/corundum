#pragma once
#include <concepts>
#include <vector>

namespace corundum::tool_host {

  /** @brief Generic undo/redo stack for document-style edits.
   *
   * @tparam Doc A copyable snapshot type representing the full editor state
   *         at a point in time.
   */
  template <std::copyable Doc> class UndoStack {
    static constexpr int k_max_depth = 256;

  public:
    /// Push a new snapshot, discarding any redo history.
    void push(const Doc &snap) {
      if (cursor_ >= 0 && cursor_ < static_cast<int>(states_.size()) - 1)
        states_.resize(static_cast<std::size_t>(cursor_ + 1));
      if (static_cast<int>(states_.size()) >= k_max_depth)
        states_.erase(states_.begin());
      states_.push_back(snap);
      cursor_ = static_cast<int>(states_.size()) - 1;
    }

    [[nodiscard]] bool can_undo() const noexcept {
      return cursor_ > 0;
    }

    bool undo(Doc &out) noexcept {
      if (!can_undo())
        return false;
      --cursor_;
      out = states_[static_cast<std::size_t>(cursor_)];
      return true;
    }

    [[nodiscard]] bool can_redo() const noexcept {
      return cursor_ >= 0 && cursor_ < static_cast<int>(states_.size()) - 1;
    }

    bool redo(Doc &out) noexcept {
      if (!can_redo())
        return false;
      ++cursor_;
      out = states_[static_cast<std::size_t>(cursor_)];
      return true;
    }

    void clear() {
      states_.clear();
      cursor_ = -1;
    }

  private:
    std::vector<Doc> states_;
    int cursor_ = -1;
  };

} // namespace corundum::tool_host
