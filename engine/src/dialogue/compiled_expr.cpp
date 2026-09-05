#include <corundum/dialogue/compiled_expr.hpp>
#include <corundum/quest/registry.hpp>
#include <corundum/quest/status.hpp>
#include <corundum/quest/system.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <stdexcept>
#include <string>
#include <utility>

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

    struct Token {
      TokKind kind = TokKind::End;
      int int_val = 0;
      std::string_view text;
    };

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

        // Identifier or keyword. '.' is permitted inside identifiers so dotted
        // flag keys (e.g. `local.x`, `quest.find_sword`) parse as a single token.
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
          while (pos_ < src_.size() &&
                 (std::isalnum(static_cast<unsigned char>(src_[pos_])) || src_[pos_] == '_' || src_[pos_] == '.'))
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

    // ── Recursive-descent parser emitting arena nodes ───────────────────────────

    class Parser {
    public:
      Parser(std::string_view src, std::vector<ExprNode> &arena) : lex_(src), arena_(arena) {
        advance();
      }

      /** @brief Parse the whole expression; returns the root arena index. */
      int parse() {
        const int root = parse_or();
        if (cur_.kind != TokKind::End)
          throw std::runtime_error("unexpected token: " + std::string(cur_.text));
        return root;
      }

    private:
      void advance() {
        cur_ = lex_.next();
      }

      int push(ExprNode node) {
        arena_.push_back(std::move(node));
        return static_cast<int>(arena_.size()) - 1;
      }

      int parse_or() {
        int val = parse_and();
        while (cur_.kind == TokKind::Or) {
          advance();
          const int rhs = parse_and();
          val = push(ExprNode{.kind = ExprNode::Kind::Or, .lhs = val, .rhs = rhs});
        }
        return val;
      }

      int parse_and() {
        int val = parse_not();
        while (cur_.kind == TokKind::And) {
          advance();
          const int rhs = parse_not();
          val = push(ExprNode{.kind = ExprNode::Kind::And, .lhs = val, .rhs = rhs});
        }
        return val;
      }

      int parse_not() {
        if (cur_.kind == TokKind::Not) {
          advance();
          return push(ExprNode{.kind = ExprNode::Kind::Not, .lhs = parse_not()});
        }
        return parse_cmp();
      }

      int parse_cmp() {
        const int lhs = parse_primary();

        if (cur_.kind == TokKind::EqEq || cur_.kind == TokKind::NotEq || cur_.kind == TokKind::Lt ||
            cur_.kind == TokKind::Gt || cur_.kind == TokKind::LtEq || cur_.kind == TokKind::GtEq) {
          const auto op = cmp_op(cur_.kind);
          advance();
          // Remember whether the RHS is a bool literal so evaluation can do a
          // truthiness comparison instead of an exact integer comparison.
          const bool rhs_is_bool = (cur_.kind == TokKind::True || cur_.kind == TokKind::False);
          const int rhs = parse_primary();
          return push(
              ExprNode{.kind = ExprNode::Kind::Cmp, .lhs = lhs, .op = op, .rhs = rhs, .rhs_is_bool = rhs_is_bool});
        }

        // Bare value — truthy if non-zero.
        return push(ExprNode{.kind = ExprNode::Kind::Bool, .lhs = lhs});
      }

      int parse_primary() {
        switch (cur_.kind) {
          case TokKind::Int: {
            const int v = cur_.int_val;
            advance();
            return push(ExprNode{.ival = v});
          }
          case TokKind::True:
            advance();
            return push(ExprNode{.ival = 1});
          case TokKind::False:
            advance();
            return push(ExprNode{.ival = 0});
          case TokKind::Ident: {
            const auto text = std::string(cur_.text);
            advance();
            if (cur_.kind == TokKind::LParen)
              return parse_quest_helper(text);
            return push(ExprNode{.kind = ExprNode::Kind::Ident, .name = text});
          }
          case TokKind::LParen: {
            advance();
            const int inner = parse_or();
            if (cur_.kind != TokKind::RParen)
              throw std::runtime_error("expected ')'");
            advance();
            return inner;
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
        advance(); // consume (
        const auto quest_id = expect_ident("for quest id in quest helper");

        if (name == "quest_is_at") {
          advance(); // consume comma
          const auto stage_name = expect_ident("for stage name in quest_is_at");
          expect_rparen();
          return push(ExprNode{.arg = quest_id, .arg2 = stage_name, .kind = ExprNode::Kind::Call, .name = name});
        }

        expect_rparen();
        if (name == "quest_is_started" || name == "quest_is_resolved" || name == "quest_is_failed" ||
            name == "has_item" || name == "item_count" || name == "rep")
          return push(ExprNode{.arg = quest_id, .kind = ExprNode::Kind::Call, .name = name});

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
      std::vector<ExprNode> &arena_;
    };

    // ── Arena evaluator ──────────────────────────────────────────────────────────

    class Evaluator {
    public:
      Evaluator(const std::vector<ExprNode> &nodes, int32_t root, const corundum::world::FlagStore &vars,
                const quest::Registry *quests, std::string_view zone_id)
          : nodes_(nodes), root_(root), vars_(vars), quests_(quests), zone_id_(zone_id) {}

      [[nodiscard]] bool run() const {
        return eval_index(root_) != 0;
      }

    private:
      [[nodiscard]] const ExprNode &node(int index) const {
        return nodes_[static_cast<std::size_t>(index)];
      }

      [[nodiscard]] int eval_index(int index) const {
        const ExprNode &n = node(index);
        switch (n.kind) {
          case ExprNode::Kind::Int:
            return n.ival;
          case ExprNode::Kind::Ident:
            return corundum::world::visit_count(vars_, corundum::world::scoped_flag_key(n.name, zone_id_));
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
          const bool lb = (lhs != 0);
          const bool rb = (rhs != 0);
          if (n.op == CmpOp::Eq)
            return (lb == rb) ? 1 : 0;
          return (lb != rb) ? 1 : 0;
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
        const auto flag_count = [&](const std::string &key) { return corundum::world::visit_count(vars_, key); };

        if (n.name == "quest_is_started")
          return flag_count(corundum::quest::quest_flag_key(n.arg)) > 0 ? 1 : 0;

        const auto *q = (quests_ != nullptr) ? quests_->find(n.arg) : nullptr;

        if (n.name == "quest_is_resolved")
          return (q != nullptr && corundum::quest::is_complete(*q, vars_)) ? 1 : 0;
        if (n.name == "quest_is_failed")
          return (q != nullptr && corundum::quest::is_failed(*q, vars_)) ? 1 : 0;
        if (n.name == "quest_is_at") {
          if (q == nullptr)
            return 0;
          const auto *s = q->find_stage(n.arg2);
          if (s == nullptr)
            return 0;
          return flag_count(corundum::quest::quest_flag_key(n.arg)) == s->sequence ? 1 : 0;
        }
        if (n.name == "has_item")
          return flag_count("item." + n.arg) > 0 ? 1 : 0;
        if (n.name == "item_count")
          return flag_count("item." + n.arg);
        if (n.name == "rep")
          return flag_count("rep." + n.arg);

        std::unreachable();
      }

      const std::vector<ExprNode> &nodes_;
      int32_t root_;
      const corundum::world::FlagStore &vars_;
      const quest::Registry *quests_;
      std::string_view zone_id_;
    };

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
      if (n.name == "quest_is_at" && !std::ranges::contains(out.quest_stages, std::pair(n.arg, n.arg2)))
        out.quest_stages.emplace_back(n.arg, n.arg2);
    }
    return out;
  }

  std::expected<CompiledExpr, ExprError> compile(std::string_view src) {
    CompiledExpr result;
    result.source_ = std::string(src);
    try {
      if (src.empty()) {
        result.nodes_.push_back(ExprNode{.ival = 1});
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
                std::string_view zone_id) {
    if (expr.root_ < 0)
      return false;
    return Evaluator(expr.nodes_, expr.root_, vars, quests, zone_id).run();
  }

} // namespace corundum::dialogue
