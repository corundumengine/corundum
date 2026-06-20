#include <corundum/gameplay/dialogue/action.hpp>

#include <cctype>
#include <charconv>
#include <format>
#include <stdexcept>
#include <utility>

namespace corundum::gameplay::dialogue {

  namespace {

    // ── Parsing helpers ───────────────────────────────────────────────────────────

    struct Ctx {
      std::string_view src;
      std::size_t pos = 0;

      bool at_end() const noexcept {
        return pos >= src.size();
      }

      char peek() const noexcept {
        return pos < src.size() ? src[pos] : '\0';
      }

      void consume() noexcept {
        ++pos;
      }

      void skip_ws() noexcept {
        while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos])))
          ++pos;
      }

      std::string read_ident() {
        const auto start = pos;
        while (pos < src.size() && (std::isalnum(static_cast<unsigned char>(src[pos])) || src[pos] == '_'))
          ++pos;
        return std::string(src.substr(start, pos - start));
      }

      int read_int() {
        const auto start = pos;
        if (pos < src.size() && src[pos] == '-')
          ++pos;
        while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos])))
          ++pos;
        int v = 0;
        std::from_chars(src.data() + start, src.data() + pos, v);
        return v;
      }

      // Reads a value token: INT | true | false
      int read_value() {
        skip_ws();
        if (at_end())
          throw std::runtime_error("expected value but reached end of input");
        if (std::isdigit(static_cast<unsigned char>(peek())) ||
            (peek() == '-' && pos + 1 < src.size() && std::isdigit(static_cast<unsigned char>(src[pos + 1]))))
          return read_int();
        const auto word = read_ident();
        if (word == "true")
          return 1;
        if (word == "false")
          return 0;
        throw std::runtime_error("expected int, true, or false, got: " + word);
      }

      // Reads a single-quoted string literal; returns content without quotes.
      std::string read_quoted() {
        if (peek() != '\'')
          throw std::runtime_error("expected single-quoted string");
        consume();
        const auto start = pos;
        while (pos < src.size() && src[pos] != '\'')
          ++pos;
        if (pos >= src.size())
          throw std::runtime_error("unterminated string literal");
        const auto result = std::string(src.substr(start, pos - start));
        consume(); // closing '
        return result;
      }
    };

    Action parse_impl(std::string_view src) {
      Ctx ctx{src};
      ctx.skip_ws();

      if (ctx.at_end())
        throw std::runtime_error("empty action string");

      const auto name = ctx.read_ident();
      if (name.empty())
        throw std::runtime_error("expected identifier");

      ctx.skip_ws();

      // Function call: name(args...)
      if (ctx.peek() == '(') {
        ctx.consume();
        EventAction ev;
        ev.name = name;

        ctx.skip_ws();
        while (!ctx.at_end() && ctx.peek() != ')') {
          std::string arg;
          if (ctx.peek() == '\'') {
            arg = ctx.read_quoted();
          } else if (std::isdigit(static_cast<unsigned char>(ctx.peek())) ||
                     (ctx.peek() == '-' && ctx.pos + 1 < ctx.src.size() &&
                      std::isdigit(static_cast<unsigned char>(ctx.src[ctx.pos + 1])))) {
            arg = std::to_string(ctx.read_int());
          } else {
            arg = ctx.read_ident();
          }
          ev.args.push_back(std::move(arg));
          ctx.skip_ws();
          if (ctx.peek() == ',') {
            ctx.consume();
            ctx.skip_ws();
          }
        }
        if (ctx.peek() != ')')
          throw std::runtime_error("expected ')' to close function call");
        ctx.consume();
        return ev;
      }

      // Assignment: name op value
      StateAction sa;
      sa.var = name;

      const auto remaining = ctx.src.substr(ctx.pos);
      if (remaining.starts_with("+=")) {
        ctx.pos += 2;
        sa.op = StateAction::Op::Add;
      } else if (remaining.starts_with("-=")) {
        ctx.pos += 2;
        sa.op = StateAction::Op::Sub;
      } else if (ctx.peek() == '=') {
        ctx.consume();
        sa.op = StateAction::Op::Assign;
      } else {
        throw std::runtime_error(std::format("expected '=', '+=', '-=', or '(' after '{}'", name));
      }

      sa.value = ctx.read_value();
      return sa;
    }

  } // namespace

  // ── Public API ────────────────────────────────────────────────────────────────

  std::expected<Action, ActionError> parse_action(std::string_view src) {
    try {
      return parse_impl(src);
    } catch (const std::exception &e) {
      return std::unexpected(ActionError{std::format("action parse error in '{}': {}", src, e.what())});
    }
  }

  std::vector<EventAction> execute_actions(std::span<const std::string> actions, corundum::gameplay::FlagStore &flags) {
    std::vector<EventAction> events;
    for (const auto &src : actions) {
      auto result = parse_action(src);
      if (!result.has_value())
        continue; // validated at load time; runtime failures are a data bug

      std::visit(
          [&](auto &&a) {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, StateAction>) {
              switch (a.op) {
              case StateAction::Op::Assign:
                flags[a.var] = a.value;
                break;
              case StateAction::Op::Add:
                flags[a.var] += a.value;
                break;
              case StateAction::Op::Sub:
                flags[a.var] -= a.value;
                break;
              default:
                std::unreachable();
              }
            } else if constexpr (std::is_same_v<T, EventAction>) {
              events.push_back(std::move(a));
            }
          },
          *result);
    }
    return events;
  }

} // namespace corundum::gameplay::dialogue
