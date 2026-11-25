// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/dialogue/action.hpp>
#include <corundum/world/flags.hpp>

#include <cctype>
#include <charconv>
#include <cstddef>
#include <exception>
#include <expected>
#include <format>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace corundum::gameplay::dialogue {

namespace {

struct ParseCursor {
  std::string_view src;
  std::size_t pos = 0;

  [[nodiscard]] bool at_end() const noexcept { return pos >= src.size(); }

  [[nodiscard]] char peek() const noexcept { return pos < src.size() ? src[pos] : '\0'; }

  void consume() noexcept { ++pos; }

  void skip_ws() noexcept {
    while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos])) != 0)
      ++pos;
  }

  /** Reads an identifier; '.' is permitted inside so dotted keys
   *  (e.g. `local.x`) parse as a single token.
   */
  std::string read_ident() {
    const auto start = pos;
    while (pos < src.size() &&
           (std::isalnum(static_cast<unsigned char>(src[pos])) != 0 || src[pos] == '_' || src[pos] == '.'))
      ++pos;
    return std::string(src.substr(start, pos - start));
  }

  int read_int() {
    const auto start = pos;
    if (pos < src.size() && src[pos] == '-')
      ++pos;
    while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos])) != 0)
      ++pos;
    int v = 0;
    const auto parsed = std::from_chars(src.data() + start, src.data() + pos, v);
    if (parsed.ec == std::errc::result_out_of_range)
      throw std::runtime_error(std::format("integer literal out of range: {}", src.substr(start, pos - start)));
    return v;
  }

  /** Reads a value token: INT | true | false */
  int read_value() {
    skip_ws();
    if (at_end())
      throw std::runtime_error("expected value but reached end of input");
    if (std::isdigit(static_cast<unsigned char>(peek())) != 0 ||
        (peek() == '-' && pos + 1 < src.size() && std::isdigit(static_cast<unsigned char>(src[pos + 1])) != 0))
      return read_int();
    const auto word = read_ident();
    if (word == "true")
      return 1;
    if (word == "false")
      return 0;
    throw std::runtime_error("expected int, true, or false, got: " + word);
  }

  /** Reads a single-quoted string literal; returns content without quotes. */
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

/** Parse a function call's argument list; the cursor is on the opening '('. `name` is the
 *  identifier that preceded it.
 */
EventAction parse_call(ParseCursor &ctx, std::string name) {
  ctx.consume(); // '('
  EventAction event;
  event.name = std::move(name);

  ctx.skip_ws();
  while (!ctx.at_end() && ctx.peek() != ')') {
    std::string arg;
    if (ctx.peek() == '\'') {
      arg = ctx.read_quoted();
    } else if (std::isdigit(static_cast<unsigned char>(ctx.peek())) != 0 ||
               (ctx.peek() == '-' && ctx.pos + 1 < ctx.src.size() &&
                std::isdigit(static_cast<unsigned char>(ctx.src[ctx.pos + 1])) != 0)) {
      arg = std::to_string(ctx.read_int());
    } else {
      arg = ctx.read_ident();
      if (arg.empty())
        throw std::runtime_error(std::format("unexpected character '{}' in function arguments", ctx.peek()));
    }
    event.args.push_back(std::move(arg));
    ctx.skip_ws();
    if (ctx.peek() == ',') {
      ctx.consume();
      ctx.skip_ws();
    }
  }
  if (ctx.peek() != ')')
    throw std::runtime_error("expected ')' to close function call");
  ctx.consume();
  return event;
}

Action parse_impl(std::string_view src) {
  ParseCursor ctx{.src = src};
  ctx.skip_ws();

  if (ctx.at_end())
    throw std::runtime_error("empty action string");

  auto name = ctx.read_ident();
  if (name.empty())
    throw std::runtime_error("expected identifier");

  ctx.skip_ws();

  // Function call: name(args...)
  if (ctx.peek() == '(')
    return parse_call(ctx, std::move(name));

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

/** Applies one parsed Action. It has one call operator per Action alternative, so visiting
 *  an Action that gains an alternative fails to compile until this handles it.
 */
struct ActionExecutor {
  std::vector<EventAction> *events{};

  corundum::world::FlagStore *flags{};

  std::string_view zone_id{};

  void operator()(const StateAction &action) const {
    int &flag = (*flags)[corundum::world::scoped_flag_key(action.var, zone_id)];
    switch (action.op) {
      case StateAction::Op::Assign: flag = action.value; return;
      case StateAction::Op::Add: flag += action.value; return;
      case StateAction::Op::Sub: flag -= action.value; return;
    }
  }

  void operator()(EventAction &&action) const { events->push_back(std::move(action)); }
};

} // namespace

std::expected<Action, ActionError> parse_action(std::string_view src) {
  try {
    return parse_impl(src);
  } catch (const std::exception &e) {
    return std::unexpected(ActionError{std::format("action parse error in '{}': {}", src, e.what())});
  }
}

std::vector<EventAction> execute_actions(std::span<const std::string> actions, corundum::world::FlagStore &flags,
                                         std::string_view zone_id) {
  std::vector<EventAction> events;
  const ActionExecutor executor{.events = &events, .flags = &flags, .zone_id = zone_id};
  for (const auto &src : actions) {
    auto result = parse_action(src);
    if (!result.has_value())
      continue; // validated at load time; runtime failures are a data bug

    std::visit(executor, std::move(*result));
  }
  return events;
}

} // namespace corundum::gameplay::dialogue
