#include <corundum/gameplay/dialogue/expr.hpp>
#include <corundum/gameplay/quest/system.hpp>

#include <cctype>
#include <charconv>
#include <format>
#include <stdexcept>
#include <string>

namespace corundum::gameplay::dialogue {

  namespace {

    // ── Tokens ───────────────────────────────────────────────────────────────────

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

    struct Token {
      TokKind kind = TokKind::End;
      int int_val = 0;
      std::string_view text;
    };

    // ── Lexer ─────────────────────────────────────────────────────────────────────

    class Lexer {
    public:
      explicit Lexer(std::string_view src) : src_(src) {}

      Token next() {
        skip_ws();
        if (pos_ >= src_.size())
          return {TokKind::End, 0, {}};

        const auto start = pos_;
        const char c = src_[pos_];
        const char c2 = (pos_ + 1 < src_.size()) ? src_[pos_ + 1] : '\0';

        // Two-character operators
        if (c == '=' && c2 == '=') {
          pos_ += 2;
          return {TokKind::EqEq, 0, src_.substr(start, 2)};
        }
        if (c == '!' && c2 == '=') {
          pos_ += 2;
          return {TokKind::NotEq, 0, src_.substr(start, 2)};
        }
        if (c == '<' && c2 == '=') {
          pos_ += 2;
          return {TokKind::LtEq, 0, src_.substr(start, 2)};
        }
        if (c == '>' && c2 == '=') {
          pos_ += 2;
          return {TokKind::GtEq, 0, src_.substr(start, 2)};
        }
        if (c == '&' && c2 == '&') {
          pos_ += 2;
          return {TokKind::And, 0, src_.substr(start, 2)};
        }
        if (c == '|' && c2 == '|') {
          pos_ += 2;
          return {TokKind::Or, 0, src_.substr(start, 2)};
        }

        // Single-character operators
        switch (c) {
        case '<':
          ++pos_;
          return {TokKind::Lt, 0, src_.substr(start, 1)};
        case '>':
          ++pos_;
          return {TokKind::Gt, 0, src_.substr(start, 1)};
        case '!':
          ++pos_;
          return {TokKind::Not, 0, src_.substr(start, 1)};
        case '(':
          ++pos_;
          return {TokKind::LParen, 0, src_.substr(start, 1)};
        case ')':
          ++pos_;
          return {TokKind::RParen, 0, src_.substr(start, 1)};
        case ',':
          ++pos_;
          return {TokKind::Comma, 0, src_.substr(start, 1)};
        default:
          break;
        }

        // Integer literal (with optional leading minus)
        const bool neg = (c == '-' && std::isdigit(static_cast<unsigned char>(c2)));
        if (neg || std::isdigit(static_cast<unsigned char>(c))) {
          if (neg)
            ++pos_;
          while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_])))
            ++pos_;
          const auto sv = src_.substr(start, pos_ - start);
          int val = 0;
          std::from_chars(sv.data(), sv.data() + sv.size(), val);
          return {TokKind::Int, val, sv};
        }

        // Identifier or keyword
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
          while (pos_ < src_.size() && (std::isalnum(static_cast<unsigned char>(src_[pos_])) || src_[pos_] == '_'))
            ++pos_;
          const auto sv = src_.substr(start, pos_ - start);
          if (sv == "true")
            return {TokKind::True, 1, sv};
          if (sv == "false")
            return {TokKind::False, 0, sv};
          return {TokKind::Ident, 0, sv};
        }

        throw std::runtime_error(std::string("unexpected character in expression: ") + c);
      }

    private:
      void skip_ws() {
        while (pos_ < src_.size() && std::isspace(static_cast<unsigned char>(src_[pos_])))
          ++pos_;
      }

      std::string_view src_;
      std::size_t pos_ = 0;
    };

    // ── Recursive-descent parser/evaluator ───────────────────────────────────────

    class Parser {
    public:
      Parser(std::string_view src, const corundum::gameplay::FlagStore &vars,
             const corundum::gameplay::quest::Registry *quests)
          : lex_(src), vars_(vars), quests_(quests) {
        advance();
      }

      bool parse() {
        const bool val = parse_or();
        if (cur_.kind != TokKind::End)
          throw std::runtime_error("unexpected token: " + std::string(cur_.text));
        return val;
      }

    private:
      void advance() {
        cur_ = lex_.next();
      }

      bool parse_or() {
        bool val = parse_and();
        while (cur_.kind == TokKind::Or) {
          advance();
          const bool rhs = parse_and();
          val = val || rhs;
        }
        return val;
      }

      bool parse_and() {
        bool val = parse_not();
        while (cur_.kind == TokKind::And) {
          advance();
          const bool rhs = parse_not();
          val = val && rhs;
        }
        return val;
      }

      bool parse_not() {
        if (cur_.kind == TokKind::Not) {
          advance();
          return !parse_not();
        }
        return parse_cmp();
      }

      bool parse_cmp() {
        const int lhs = parse_primary();
        const auto op = cur_.kind;

        if (op == TokKind::EqEq || op == TokKind::NotEq || op == TokKind::Lt || op == TokKind::Gt ||
            op == TokKind::LtEq || op == TokKind::GtEq) {
          advance();
          // Remember whether the RHS is a bool literal so we can do a truthiness
          // comparison instead of an exact integer comparison.
          const bool rhs_is_bool = (cur_.kind == TokKind::True || cur_.kind == TokKind::False);
          const int rhs = parse_primary();

          if (rhs_is_bool && (op == TokKind::EqEq || op == TokKind::NotEq)) {
            const bool lb = (lhs != 0);
            const bool rb = (rhs != 0);
            return (op == TokKind::EqEq) ? (lb == rb) : (lb != rb);
          }

          switch (op) {
          case TokKind::EqEq:
            return lhs == rhs;
          case TokKind::NotEq:
            return lhs != rhs;
          case TokKind::Lt:
            return lhs < rhs;
          case TokKind::Gt:
            return lhs > rhs;
          case TokKind::LtEq:
            return lhs <= rhs;
          case TokKind::GtEq:
            return lhs >= rhs;
          default:
            break;
          }
        }

        // Bare value — truthy if non-zero
        return lhs != 0;
      }

      int parse_primary() {
        switch (cur_.kind) {
        case TokKind::Int: {
          const int v = cur_.int_val;
          advance();
          return v;
        }
        case TokKind::True:
          advance();
          return 1;
        case TokKind::False:
          advance();
          return 0;
        case TokKind::Ident: {
          const auto name = std::string(cur_.text);
          advance();
          if (cur_.kind == TokKind::LParen)
            return parse_quest_helper(name);
          return corundum::gameplay::visit_count(vars_, name);
        }
        case TokKind::LParen: {
          advance();
          const bool v = parse_or();
          if (cur_.kind != TokKind::RParen)
            throw std::runtime_error("expected ')'");
          advance();
          return v ? 1 : 0;
        }
        default:
          throw std::runtime_error("expected value, got: " + std::string(cur_.text));
        }
      }

      std::string expect_ident(std::string_view context) {
        if (cur_.kind != TokKind::Ident)
          throw std::runtime_error(std::format("expected identifier {}", context));
        const auto text = std::string(cur_.text);
        advance();
        return text;
      }

      void expect_rparen() {
        if (cur_.kind != TokKind::RParen)
          throw std::runtime_error("expected ')'");
        advance();
      }

      int parse_quest_helper(const std::string &name) {
        if (!quests_)
          return 0;

        advance(); // consume (
        const auto quest_id = expect_ident("for quest id in quest helper");

        if (name == "quest_is_started") {
          expect_rparen();
          const auto key = corundum::gameplay::quest::quest_flag_key(quest_id);
          return corundum::gameplay::visit_count(vars_, key) > 0 ? 1 : 0;
        }

        if (name == "quest_is_resolved") {
          expect_rparen();
          const auto *q = quests_->find(quest_id);
          return (q && corundum::gameplay::quest::is_complete(*q, vars_)) ? 1 : 0;
        }

        if (name == "quest_is_failed") {
          expect_rparen();
          const auto *q = quests_->find(quest_id);
          return (q && corundum::gameplay::quest::is_failed(*q, vars_)) ? 1 : 0;
        }

        if (name == "quest_is_at") {
          advance(); // consume comma
          const auto stage_name = expect_ident("for stage name in quest_is_at");
          expect_rparen();
          const auto *q = quests_->find(quest_id);
          if (!q)
            return 0;
          const auto *s = q->find_stage(stage_name);
          if (!s)
            return 0;
          const auto key = corundum::gameplay::quest::quest_flag_key(quest_id);
          return corundum::gameplay::visit_count(vars_, key) == s->sequence ? 1 : 0;
        }

        throw std::runtime_error("unknown quest helper: " + name);
      }

      Lexer lex_;
      Token cur_;
      const corundum::gameplay::FlagStore &vars_;
      const corundum::gameplay::quest::Registry *quests_;
    };

  } // namespace

  // ── Public API ────────────────────────────────────────────────────────────────

  std::expected<bool, ExprError> eval_condition(std::string_view expr, const corundum::gameplay::FlagStore &vars,
                                                const quest::Registry *quests) {
    if (expr.empty())
      return true;
    try {
      return Parser(expr, vars, quests).parse();
    } catch (const std::exception &e) {
      return std::unexpected(ExprError{e.what()});
    }
  }

} // namespace corundum::gameplay::dialogue
