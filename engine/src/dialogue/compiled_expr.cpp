// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/dialogue/compiled_expr.hpp>
#include <corundum/dialogue/query.hpp>
#include <corundum/quest/registry.hpp>
#include <corundum/quest/status.hpp>
#include <corundum/quest/system.hpp>
#include <corundum/world/flags.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace corundum::dialogue {

  namespace {

    // ── Tokens (grammar identical to the original string evaluator) ─────────────

    enum class TokKind : uint8_t {
      Int,    // integer literal
      True,   // true
      False,  // false
      Ident,  // identifier
      EqEq,   // ==
      NotEq,  // !=
      Lt,     // <
      Gt,     // >
      LtEq,   // <=
      GtEq,   // >=
      And,    // &&
      Or,     // ||
      Not,    // !
      LParen, // (
      Comma,  // ,
      RParen, // )
      End,    // end of input
    };

    class Token {
    public:
      /// Build from kind + source span; an Int's value is derived here so the
      /// lexer never has to know how a token stores its value.
      static Token make(TokKind kind, std::string_view text = {}) {
        Token token;
        token.kind_ = kind;
        token.text_ = text;

        if (kind == TokKind::Int) {
          const auto parsed = std::from_chars(text.data(), text.data() + text.size(), token.int_value_);
          if (parsed.ec == std::errc::result_out_of_range)
            throw std::runtime_error(std::format("integer literal out of range: {}", text));
        }
        return token;
      }

      [[nodiscard]] bool is(TokKind kind) const noexcept {
        return kind_ == kind;
      }

      [[nodiscard]] bool is_comparison() const noexcept {
        return kind_ == TokKind::EqEq || kind_ == TokKind::NotEq || kind_ == TokKind::Lt || kind_ == TokKind::Gt ||
               kind_ == TokKind::LtEq || kind_ == TokKind::GtEq;
      }

      [[nodiscard]] TokKind kind() const noexcept {
        return kind_;
      }

      [[nodiscard]] int int_value() const noexcept {
        return int_value_;
      }

      [[nodiscard]] std::string_view text() const noexcept {
        return text_;
      }

    private:
      TokKind kind_ = TokKind::End;
      int int_value_ = 0;
      std::string_view text_{};
    };

    [[nodiscard]] bool is_digit(char c) noexcept {
      return std::isdigit(static_cast<unsigned char>(c)) != 0;
    }

    [[nodiscard]] bool is_alpha(char c) noexcept {
      return std::isalpha(static_cast<unsigned char>(c)) != 0;
    }

    [[nodiscard]] bool is_alnum(char c) noexcept {
      return std::isalnum(static_cast<unsigned char>(c)) != 0;
    }

    [[nodiscard]] bool is_space(char c) noexcept {
      return std::isspace(static_cast<unsigned char>(c)) != 0;
    }

    [[nodiscard]] bool is_identifier_char(char c) noexcept {
      return is_alnum(c) || c == '_' || c == '.';
    }

    /// Kind of the single-character operator @p c, or nullopt when it is not one.
    [[nodiscard]] std::optional<TokKind> single_char_operator(char c) noexcept {
      switch (c) {
        case '<':
          return TokKind::Lt;
        case '>':
          return TokKind::Gt;
        case '!':
          return TokKind::Not;
        case '(':
          return TokKind::LParen;
        case ')':
          return TokKind::RParen;
        case ',':
          return TokKind::Comma;
        default:
          return std::nullopt;
      }
    }

    class Lexer {
    public:
      explicit Lexer(std::string_view src) : src_(src) {}

      Token next() {
        skip_ws();
        if (pos_ >= src_.size())
          return Token::make(TokKind::End);

        if (const auto token = scan_operator())
          return *token;
        if (const auto token = scan_int())
          return *token;
        if (const auto token = scan_identifier())
          return *token;

        throw std::runtime_error(std::string("unexpected character in expression: ") + src_[pos_]);
      }

    private:
      /// Consume a single- or two-character operator, if one starts at the cursor.
      [[nodiscard]] std::optional<Token> scan_operator() {
        const auto start = pos_;
        const char c = src_[pos_];
        const char next_char = (pos_ + 1 < src_.size()) ? src_[pos_ + 1] : '\0';

        if (c == '=' && next_char == '=') {
          pos_ += 2;
          return Token::make(TokKind::EqEq, src_.substr(start, 2));
        }
        if (c == '!' && next_char == '=') {
          pos_ += 2;
          return Token::make(TokKind::NotEq, src_.substr(start, 2));
        }
        if (c == '<' && next_char == '=') {
          pos_ += 2;
          return Token::make(TokKind::LtEq, src_.substr(start, 2));
        }
        if (c == '>' && next_char == '=') {
          pos_ += 2;
          return Token::make(TokKind::GtEq, src_.substr(start, 2));
        }
        if (c == '&' && next_char == '&') {
          pos_ += 2;
          return Token::make(TokKind::And, src_.substr(start, 2));
        }
        if (c == '|' && next_char == '|') {
          pos_ += 2;
          return Token::make(TokKind::Or, src_.substr(start, 2));
        }

        const std::optional<TokKind> kind = single_char_operator(c);
        if (!kind.has_value())
          return std::nullopt;
        ++pos_;
        return Token::make(*kind, src_.substr(start, 1));
      }

      /// Consume an integer literal (optional leading minus), if one starts here.
      [[nodiscard]] std::optional<Token> scan_int() {
        const char c = src_[pos_];
        const char next_char = (pos_ + 1 < src_.size()) ? src_[pos_ + 1] : '\0';
        const bool negative = (c == '-' && is_digit(next_char));

        if (!negative && !is_digit(c))
          return std::nullopt;

        const auto start = pos_;
        if (negative)
          ++pos_;
        while (pos_ < src_.size() && is_digit(src_[pos_]))
          ++pos_;
        return Token::make(TokKind::Int, src_.substr(start, pos_ - start));
      }

      /// Consume an identifier or keyword, if one starts here. '.' is permitted inside
      /// so dotted flag keys (e.g. `local.x`, `quest.find_sword`) parse as one token.
      [[nodiscard]] std::optional<Token> scan_identifier() {
        const char c = src_[pos_];
        if (!is_alpha(c) && c != '_')
          return std::nullopt;

        const auto start = pos_;
        while (pos_ < src_.size() && is_identifier_char(src_[pos_]))
          ++pos_;
        const std::string_view text = src_.substr(start, pos_ - start);
        if (text == "true")
          return Token::make(TokKind::True, text);
        if (text == "false")
          return Token::make(TokKind::False, text);
        return Token::make(TokKind::Ident, text);
      }

      void skip_ws() {
        while (pos_ < src_.size() && is_space(src_[pos_]))
          ++pos_;
      }

      std::string_view src_;
      std::size_t pos_ = 0;
    };

    // ── Recursive-descent parser emitting arena nodes ───────────────────────────

    // The grammar and the node tree are processed by recursive descent / recursive
    // walk; the mutual recursion between precedence levels is inherent to that design,
    // so misc-no-recursion does not apply across these two classes.
    // NOLINTBEGIN(misc-no-recursion)
    class Parser {
    public:
      Parser(std::string_view src, std::vector<ExprNode> &arena) : lex_(src), arena_(&arena) {
        advance();
      }

      /** @brief Parse the whole expression; returns the root arena index. */
      int parse() {
        const int root = parse_or();
        if (!cur_.is(TokKind::End))
          throw std::runtime_error("unexpected token: " + std::string(cur_.text()));
        return root;
      }

    private:
      void advance() {
        cur_ = lex_.next();
      }

      int push(ExprNode node) {
        arena_->push_back(std::move(node));
        return static_cast<int>(arena_->size()) - 1;
      }

      int parse_or() {
        int val = parse_and();
        while (cur_.is(TokKind::Or)) {
          advance();
          const int rhs = parse_and();
          val = push(ExprNode{.kind = ExprNode::Kind::Or, .lhs = val, .rhs = rhs});
        }
        return val;
      }

      int parse_and() {
        int val = parse_not();
        while (cur_.is(TokKind::And)) {
          advance();
          const int rhs = parse_not();
          val = push(ExprNode{.kind = ExprNode::Kind::And, .lhs = val, .rhs = rhs});
        }
        return val;
      }

      int parse_not() {
        if (cur_.is(TokKind::Not)) {
          advance();
          return push(ExprNode{.kind = ExprNode::Kind::Not, .lhs = parse_not()});
        }
        return parse_cmp();
      }

      int parse_cmp() {
        const int lhs = parse_primary();

        if (cur_.is_comparison()) {
          const auto op = cmp_op(cur_.kind());
          advance();
          // Remember whether the RHS is a bool literal so evaluation can do a
          // truthiness comparison instead of an exact integer comparison.
          const bool rhs_is_bool = cur_.is(TokKind::True) || cur_.is(TokKind::False);
          const int rhs = parse_primary();
          return push(
              ExprNode{.kind = ExprNode::Kind::Cmp, .lhs = lhs, .op = op, .rhs = rhs, .rhs_is_bool = rhs_is_bool});
        }

        // Bare value — truthy if non-zero.
        return push(ExprNode{.kind = ExprNode::Kind::Bool, .lhs = lhs});
      }

      int parse_primary() {
        switch (cur_.kind()) {
          case TokKind::Int: {
            const int v = cur_.int_value();
            advance();
            return push(ExprNode{.value = v});
          }
          case TokKind::True:
            advance();
            return push(ExprNode{.value = 1});
          case TokKind::False:
            advance();
            return push(ExprNode{.value = 0});
          case TokKind::Ident: {
            const auto text = std::string(cur_.text());
            advance();
            if (cur_.is(TokKind::LParen))
              return parse_call_helper(text);
            return push(ExprNode{.kind = ExprNode::Kind::Ident, .name = text});
          }
          case TokKind::LParen: {
            advance();
            const int inner = parse_or();
            if (!cur_.is(TokKind::RParen))
              throw std::runtime_error("expected ')'");
            advance();
            return inner;
          }
          default:
            throw std::runtime_error("expected value, got: " + std::string(cur_.text()));
        }
      }

      std::string expect_ident(std::string_view context) {
        if (!cur_.is(TokKind::Ident))
          throw std::runtime_error(std::format("expected identifier {}", context));
        const auto text = std::string(cur_.text());
        advance();
        return text;
      }

      void expect_rparen() {
        if (!cur_.is(TokKind::RParen))
          throw std::runtime_error("expected ')'");
        advance();
      }

      int parse_call_helper(const std::string &name) {
        advance(); // consume (
        const auto arg = expect_ident("for first argument in call helper");

        if (name == "quest_is_at") {
          advance(); // consume comma
          const auto stage_name = expect_ident("for stage name in quest_is_at");
          expect_rparen();
          return push(ExprNode{.arg = arg, .kind = ExprNode::Kind::Call, .name = name, .stage_name = stage_name});
        }

        expect_rparen();
        if (name == "quest_is_started" || name == "quest_is_resolved" || name == "quest_is_failed" ||
            name == "has_item" || name == "item_count" || name == "rep" || name == "seen" || name == "visits")
          return push(ExprNode{.arg = arg, .kind = ExprNode::Kind::Call, .name = name});

        throw std::runtime_error("unknown quest helper: " + name);
      }

      static CmpOp cmp_op(TokKind kind) {
        switch (kind) {
          case TokKind::EqEq:
            return CmpOp::Eq;
          case TokKind::NotEq:
            return CmpOp::Ne;
          case TokKind::Lt:
            return CmpOp::Lt;
          case TokKind::Gt:
            return CmpOp::Gt;
          case TokKind::LtEq:
            return CmpOp::Le;
          case TokKind::GtEq:
            return CmpOp::Ge;
          default:
            std::unreachable();
        }
      }

      Lexer lex_;
      Token cur_;
      std::vector<ExprNode> *arena_;
    };

    // ── Arena evaluator ──────────────────────────────────────────────────────────

    class Evaluator {
    public:
      Evaluator(const std::vector<ExprNode> &nodes, int32_t root, const corundum::world::FlagStore &vars,
                const quest::Registry *quests, std::string_view graph_id, std::string_view zone_id)
          : nodes_(&nodes), root_(root), vars_(&vars), quests_(quests), graph_id_(graph_id), zone_id_(zone_id) {}

      [[nodiscard]] bool run() const {
        return eval_index(root_) != 0;
      }

    private:
      [[nodiscard]] int flag_count(const std::string &key) const {
        return corundum::world::visit_count(*vars_, key);
      }

      [[nodiscard]] const ExprNode &node(int index) const {
        return (*nodes_)[static_cast<std::size_t>(index)];
      }

      [[nodiscard]] int eval_index(int index) const {
        const ExprNode &n = node(index);
        switch (n.kind) {
          case ExprNode::Kind::Int:
            return n.value;
          case ExprNode::Kind::Ident:
            return corundum::world::visit_count(*vars_, corundum::world::scoped_flag_key(n.name, zone_id_));
          case ExprNode::Kind::Bool:
            return eval_index(n.lhs) != 0 ? 1 : 0;
          case ExprNode::Kind::Not:
            return eval_index(n.lhs) == 0 ? 1 : 0;
          case ExprNode::Kind::And:
            return (eval_index(n.lhs) != 0 && eval_index(n.rhs) != 0) ? 1 : 0;
          case ExprNode::Kind::Or:
            return (eval_index(n.lhs) != 0 || eval_index(n.rhs) != 0) ? 1 : 0;
          case ExprNode::Kind::Cmp:
            return eval_cmp(n);
          case ExprNode::Kind::Call:
            return eval_call(n);
        }
        std::unreachable();
      }

      [[nodiscard]] int eval_cmp(const ExprNode &n) const {
        const int lhs = eval_index(n.lhs);
        const int rhs = eval_index(n.rhs);

        if (n.rhs_is_bool && (n.op == CmpOp::Eq || n.op == CmpOp::Ne)) {
          const bool lhs_truthy = (lhs != 0);
          const bool rhs_truthy = (rhs != 0);
          if (n.op == CmpOp::Eq)
            return (lhs_truthy == rhs_truthy) ? 1 : 0;
          return (lhs_truthy != rhs_truthy) ? 1 : 0;
        }

        switch (n.op) {
          case CmpOp::Eq:
            return lhs == rhs ? 1 : 0;
          case CmpOp::Ne:
            return lhs != rhs ? 1 : 0;
          case CmpOp::Lt:
            return lhs < rhs ? 1 : 0;
          case CmpOp::Gt:
            return lhs > rhs ? 1 : 0;
          case CmpOp::Le:
            return lhs <= rhs ? 1 : 0;
          case CmpOp::Ge:
            return lhs >= rhs ? 1 : 0;
        }
        std::unreachable();
      }

      [[nodiscard]] int eval_call(const ExprNode &n) const {
        if (const auto value = eval_visit_helper(n))
          return *value;
        if (const auto value = eval_quest_helper(n))
          return *value;

        if (n.name == "has_item")
          return flag_count("item." + n.arg) > 0 ? 1 : 0;
        if (n.name == "item_count")
          return flag_count("item." + n.arg);
        if (n.name == "rep")
          return flag_count("rep." + n.arg);

        std::unreachable();
      }

      /// seen/visits — resolved against the owning graph; 0 without graph context.
      [[nodiscard]] std::optional<int> eval_visit_helper(const ExprNode &n) const {
        if (n.name == "seen") {
          if (graph_id_.empty())
            return 0;
          return flag_count(visit_flag_key(graph_id_, n.arg)) > 0 ? 1 : 0;
        }
        if (n.name == "visits") {
          if (graph_id_.empty())
            return 0;
          return flag_count(visit_flag_key(graph_id_, n.arg));
        }
        return std::nullopt;
      }

      [[nodiscard]] std::optional<int> eval_quest_helper(const ExprNode &n) const {
        if (n.name == "quest_is_started")
          return flag_count(corundum::quest::quest_flag_key(n.arg)) > 0 ? 1 : 0;

        const auto *quest = (quests_ != nullptr) ? quests_->find(n.arg) : nullptr;

        if (n.name == "quest_is_resolved")
          return (quest != nullptr && corundum::quest::is_complete(*quest, *vars_)) ? 1 : 0;
        if (n.name == "quest_is_failed")
          return (quest != nullptr && corundum::quest::is_failed(*quest, *vars_)) ? 1 : 0;
        if (n.name == "quest_is_at") {
          if (quest == nullptr)
            return 0;
          const auto *stage = quest->find_stage(n.stage_name);
          if (stage == nullptr)
            return 0;
          return flag_count(corundum::quest::quest_flag_key(n.arg)) == stage->sequence ? 1 : 0;
        }
        return std::nullopt;
      }

      const std::vector<ExprNode> *nodes_;
      int32_t root_;
      const corundum::world::FlagStore *vars_;
      const quest::Registry *quests_;
      std::string_view graph_id_;
      std::string_view zone_id_;
    };

    // NOLINTEND(misc-no-recursion)

    bool is_quest_helper(std::string_view name) noexcept {
      return name == "quest_is_started" || name == "quest_is_resolved" || name == "quest_is_failed" ||
             name == "quest_is_at";
    }

  } // namespace

  ExprRefs CompiledExpr::refs() const {
    ExprRefs out;
    for (const auto &n : nodes_) {
      if (n.kind == ExprNode::Kind::Ident) {
        if (!std::ranges::contains(out.idents, n.name))
          out.idents.push_back(n.name);
        continue;
      }
      if (n.kind != ExprNode::Kind::Call || !is_quest_helper(n.name))
        continue;

      if (!std::ranges::contains(out.quest_ids, n.arg))
        out.quest_ids.push_back(n.arg);
      if (n.name == "quest_is_at" && !std::ranges::contains(out.quest_stages, std::pair(n.arg, n.stage_name)))
        out.quest_stages.emplace_back(n.arg, n.stage_name);
    }
    return out;
  }

  std::expected<CompiledExpr, ExprError> compile(std::string_view src) {
    CompiledExpr result;
    result.source_ = std::string(src);
    try {
      if (src.empty()) {
        result.nodes_.push_back(ExprNode{.value = 1});
        result.root_ = 0;
      } else {
        Parser parser(src, result.nodes_);
        result.root_ = parser.parse();
      }
      return result;
    } catch (const std::exception &e) {
      return std::unexpected(ExprError{e.what()});
    }
  }

  bool evaluate(const CompiledExpr &expr, const corundum::world::FlagStore &vars, const quest::Registry *quests,
                std::string_view graph_id, std::string_view zone_id) {
    if (expr.root_ < 0)
      return false;
    return Evaluator(expr.nodes_, expr.root_, vars, quests, graph_id, zone_id).run();
  }

} // namespace corundum::dialogue
