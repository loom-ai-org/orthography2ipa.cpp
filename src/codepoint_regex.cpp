#include "codepoint_regex.hpp"

#include "unicode_util.hpp"

#include <algorithm>
#include <functional>
#include <stdexcept>

namespace orthography2ipa::re32 {

namespace detail {

/// One member of a character class: a code-point range or one of Python's
/// category shorthands.
struct ClassItem {
    enum Kind { Range, Digit, NotDigit, Word, NotWord, Space, NotSpace } kind = Range;
    char32_t first = 0;
    char32_t last = 0;
};

struct Node {
    enum Kind { Literal, Any, Class, Anchor, Group, Repeat, Alt, Seq, Backref, Look,
                Atomic } kind = Literal;

    char32_t ch = 0;                 ///< Literal
    std::vector<ClassItem> items;    ///< Class
    bool negated = false;            ///< Class / Look

    enum AnchorKind { Start, End, TextStart, TextEnd, WordBoundary, NotWordBoundary };
    AnchorKind anchor = Start;       ///< Anchor

    NodePtr child;                   ///< Group / Repeat / Look
    int group = 0;                   ///< Group: capture index

    int min = 0;                     ///< Repeat
    int max = 0;                     ///< Repeat (negative = unbounded)
    bool greedy = true;              ///< Repeat

    bool ahead = true;               ///< Look: true = ahead, false = behind
    bool possessive = false;         ///< Repeat: greedy and never given back
    NodePtr atomic;                  ///< Atomic: the body matched once, atomically

    std::vector<NodePtr> branches;   ///< Alt
    std::vector<NodePtr> sequence;   ///< Seq

    int backref = 0;                 ///< Backref
    std::vector<int> backref_uses;   ///< every backref index seen, for validation
};

} // namespace detail

namespace {

using detail::ClassItem;
using detail::Node;
using NodePtr = std::shared_ptr<Node>;

// ─── character predicates ───────────────────────────────────────────────
// Python 3 patterns over `str` are Unicode-aware, so the classes are Unicode
// categories, not ASCII ranges: \d is Nd, \w is letter/number/underscore,
// \s is the separator categories plus the ASCII controls.
bool is_digit(char32_t ch) { return uni::category(ch) == "Nd"; }

bool is_word(char32_t ch) {
    if (ch == U'_') return true;
    const std::string cat = uni::category(ch);
    return !cat.empty() && (cat[0] == 'L' || cat[0] == 'N');
}

bool is_space(char32_t ch) {
    switch (ch) {
        case U' ': case U'\t': case U'\n': case U'\v': case U'\f': case U'\r': return true;
        default: break;
    }
    const std::string cat = uni::category(ch);
    return cat == "Zs" || cat == "Zl" || cat == "Zp";
}

bool class_item_matches(const ClassItem& item, char32_t ch) {
    switch (item.kind) {
        case ClassItem::Range:    return ch >= item.first && ch <= item.last;
        case ClassItem::Digit:    return is_digit(ch);
        case ClassItem::NotDigit: return !is_digit(ch);
        case ClassItem::Word:     return is_word(ch);
        case ClassItem::NotWord:  return !is_word(ch);
        case ClassItem::Space:    return is_space(ch);
        case ClassItem::NotSpace: return !is_space(ch);
    }
    return false;
}

bool class_matches(const Node& node, char32_t ch) {
    const bool hit = std::any_of(node.items.begin(), node.items.end(),
                                 [&](const ClassItem& i) { return class_item_matches(i, ch); });
    return node.negated ? !hit : hit;
}

[[noreturn]] void fail(const std::string& what) {
    throw std::runtime_error("invalid pattern: " + what);
}

int to_int(const std::u32string& digits) {
    int value = 0;
    for (char32_t c : digits) value = value * 10 + static_cast<int>(c - U'0');
    return value;
}

// ─── parser ─────────────────────────────────────────────────────────────
class Parser {
public:
    explicit Parser(const std::u32string& pattern) : p_(pattern) {}

    NodePtr parse() {
        NodePtr result = parse_alt();
        if (!at_end()) fail("unbalanced parenthesis");
        for (const int index : backrefs_)
            if (index > groups) fail("invalid group reference");
        return result;
    }

    int groups = 0;
    std::vector<std::u32string> names;   ///< 1-based, parallel to group numbers

private:
    const std::u32string& p_;
    std::size_t i_ = 0;
    std::vector<int> backrefs_;

    bool at_end() const { return i_ >= p_.size(); }
    char32_t peek(std::size_t ahead = 0) const {
        return i_ + ahead < p_.size() ? p_[i_ + ahead] : U'\0';
    }
    bool eat(char32_t ch) { if (peek() != ch) return false; ++i_; return true; }

    static NodePtr make(Node::Kind kind) {
        auto node = std::make_shared<Node>();
        node->kind = kind;
        return node;
    }

    NodePtr parse_alt() {
        std::vector<NodePtr> branches;
        branches.push_back(parse_seq());
        while (eat(U'|')) branches.push_back(parse_seq());
        if (branches.size() == 1) return branches[0];
        auto node = make(Node::Alt);
        node->branches = std::move(branches);
        return node;
    }

    NodePtr parse_seq() {
        std::vector<NodePtr> items;
        while (!at_end() && peek() != U'|' && peek() != U')')
            items.push_back(parse_quantifier(parse_atom()));
        if (items.size() == 1) return items[0];
        auto node = make(Node::Seq);
        node->sequence = std::move(items);
        return node;
    }

    NodePtr parse_quantifier(NodePtr atom) {
        int min = 0, max = 0;
        bool repeat = true;
        if (eat(U'*')) { min = 0; max = -1; }
        else if (eat(U'+')) { min = 1; max = -1; }
        else if (eat(U'?')) { min = 0; max = 1; }
        else if (peek() == U'{') {
            const std::size_t save = i_;
            ++i_;
            std::u32string low, high;
            while (!at_end() && peek() >= U'0' && peek() <= U'9') low.push_back(p_[i_++]);
            const bool comma = eat(U',');
            if (comma)
                while (!at_end() && peek() >= U'0' && peek() <= U'9') high.push_back(p_[i_++]);
            if (at_end() || peek() != U'}' || (low.empty() && high.empty())) {
                i_ = save;                 // a lone '{' is a literal, as in Python
                repeat = false;
            } else {
                ++i_;
                min = low.empty() ? 0 : to_int(low);
                max = high.empty() ? -1 : to_int(high);
                if (max >= 0 && max < min) fail("lower bound greater than upper bound");
            }
        } else {
            repeat = false;
        }
        if (!repeat) return atom;

        bool greedy = true;
        bool possessive = false;
        if (eat(U'?')) greedy = false;
        else if (eat(U'+')) possessive = true;   // Python 3.11
        auto node = make(Node::Repeat);
        node->child = atom;
        node->min = min;
        node->max = max;
        node->greedy = greedy;
        // `x*+` is exactly `(?>x*)`: greedy, then committed (CPython documents
        // possessive quantifiers as atomic groups), so express it that way and
        // keep one implementation of each.
        if (possessive) {
            auto wrapper = make(Node::Atomic);
            wrapper->child = node;
            return wrapper;
        }
        return node;
    }

    NodePtr parse_atom() {
        switch (peek()) {
            case U'^': ++i_; { auto n = make(Node::Anchor); n->anchor = Node::Start; return n; }
            case U'$': ++i_; { auto n = make(Node::Anchor); n->anchor = Node::End; return n; }
            case U'.': ++i_; return make(Node::Any);
            case U'[': ++i_; return parse_class();
            case U'(': ++i_; return parse_group();
            case U'\\': ++i_; return parse_escape();
            case U')': fail("unbalanced parenthesis");
            case U'*': case U'+': case U'?': fail("nothing to repeat");
            default: {
                auto n = make(Node::Literal);
                n->ch = p_[i_++];
                return n;
            }
        }
    }

    NodePtr parse_group() {
        enum Mode { Capture, Plain, Ahead, NotAhead, Behind, NotBehind, AtomicGroup }
            mode = Capture;
        std::u32string name;
        if (peek() == U'?') {
            ++i_;
            switch (peek()) {
                case U':': ++i_; mode = Plain; break;
                case U'=': ++i_; mode = Ahead; break;
                case U'!': ++i_; mode = NotAhead; break;
                case U'<': {
                    ++i_;
                    if (eat(U'=')) mode = Behind;
                    else if (eat(U'!')) mode = NotBehind;
                    else {
                        while (!at_end() && peek() != U'>') name.push_back(p_[i_++]);
                        if (!eat(U'>')) fail("bad named group");
                        mode = Capture;
                    }
                    break;
                }
                case U'P': {
                    ++i_;
                    if (!eat(U'<')) fail("bad named group");
                    while (!at_end() && peek() != U'>') name.push_back(p_[i_++]);
                    if (!eat(U'>')) fail("bad named group");
                    mode = Capture;
                    break;
                }
                case U'>': mode = AtomicGroup; break;
                default: fail("unsupported group extension");
            }
        }
        // Python numbers a group at its OPENING parenthesis, so an inner group
        // of `((a)b)` is 2, not 1: reserve the index before parsing the body.
        int index = 0;
        if (mode == Capture) {
            ++groups;
            index = groups;
            names.resize(static_cast<std::size_t>(groups));
            if (!name.empty()) names[static_cast<std::size_t>(groups - 1)] = name;
        }
        NodePtr body = parse_alt();
        if (!eat(U')')) fail("unbalanced parenthesis");

        if (mode == Plain) return body;
        if (mode == AtomicGroup) {
            auto node = make(Node::Atomic);
            node->child = body;
            return node;
        }
        if (mode == Capture) {
            auto node = make(Node::Group);
            node->group = index;
            node->child = body;
            return node;
        }
        auto node = make(Node::Look);
        node->ahead = (mode == Ahead || mode == NotAhead);
        node->negated = (mode == NotAhead || mode == NotBehind);
        node->child = body;
        return node;
    }

    static ClassItem shorthand(char32_t kind) {
        ClassItem item;
        switch (kind) {
            case U'd': item.kind = ClassItem::Digit; break;
            case U'D': item.kind = ClassItem::NotDigit; break;
            case U'w': item.kind = ClassItem::Word; break;
            case U'W': item.kind = ClassItem::NotWord; break;
            case U's': item.kind = ClassItem::Space; break;
            case U'S': item.kind = ClassItem::NotSpace; break;
            default: break;
        }
        return item;
    }

    static bool is_shorthand(char32_t ch) {
        return ch == U'd' || ch == U'D' || ch == U'w' || ch == U'W' || ch == U's' || ch == U'S';
    }

    NodePtr parse_escape() {
        const char32_t ch = p_[i_++];
        switch (ch) {
            case U'A': { auto n = make(Node::Anchor); n->anchor = Node::TextStart; return n; }
            case U'z': case U'Z': { auto n = make(Node::Anchor); n->anchor = Node::TextEnd; return n; }
            case U'b': { auto n = make(Node::Anchor); n->anchor = Node::WordBoundary; return n; }
            case U'B': { auto n = make(Node::Anchor); n->anchor = Node::NotWordBoundary; return n; }
            default: break;
        }
        if (is_shorthand(ch)) {
            auto n = make(Node::Class);
            n->items.push_back(shorthand(ch));
            return n;
        }
        if (ch >= U'1' && ch <= U'9') {
            int index = ch - U'0';
            while (!at_end() && peek() >= U'0' && peek() <= U'9') {
                const int trial = index * 10 + (p_[i_] - U'0');
                if (trial > 99) break;
                index = trial;
                ++i_;
            }
            backrefs_.push_back(index);
            auto n = make(Node::Backref);
            n->backref = index;
            return n;
        }
        auto n = make(Node::Literal);
        switch (ch) {
            case U'n': n->ch = U'\n'; break;
            case U't': n->ch = U'\t'; break;
            case U'r': n->ch = U'\r'; break;
            case U'f': n->ch = U'\f'; break;
            case U'v': n->ch = U'\v'; break;
            case U'a': n->ch = U'\a'; break;
            case U'0': n->ch = U'\0'; break;
            default: n->ch = ch; break;   // \. \[ \\ \- and friends stay literal
        }
        return n;
    }

    char32_t parse_class_char() {
        const char32_t ch = p_[i_++];
        if (ch != U'\\' || at_end()) return ch;
        const char32_t esc = p_[i_++];
        switch (esc) {
            case U'n': return U'\n';
            case U't': return U'\t';
            case U'r': return U'\r';
            case U'f': return U'\f';
            case U'v': return U'\v';
            case U'a': return U'\a';
            default: return esc;
        }
    }

    NodePtr parse_class() {
        auto node = make(Node::Class);
        node->negated = eat(U'^');
        bool first = true;
        while (!at_end() && (peek() != U']' || first)) {
            first = false;
            if (peek() == U'\\' && i_ + 1 < p_.size() && is_shorthand(p_[i_ + 1])) {
                node->items.push_back(shorthand(p_[i_ + 1]));
                i_ += 2;
                continue;
            }
            const char32_t low = parse_class_char();
            if (!at_end() && peek() == U'-' && i_ + 1 < p_.size() && p_[i_ + 1] != U']') {
                ++i_;
                const char32_t high = parse_class_char();
                if (high < low) fail("bad character range");
                node->items.push_back({ClassItem::Range, low, high});
                continue;
            }
            node->items.push_back({ClassItem::Range, low, low});
        }
        if (!eat(U']')) fail("unterminated character set");
        return node;
    }
};

// ─── matcher ────────────────────────────────────────────────────────────
using Caps = std::vector<std::pair<std::size_t, std::size_t>>;
using Accept = std::function<bool(std::size_t)>;

bool ends_at(const std::u32string& s, std::size_t pos) {
    // Python's `$` matches at the end of the subject, or immediately before a
    // single newline AT the end — which is why `c$` matches "abc\n".
    return pos == s.size() || (pos + 1 == s.size() && s[pos] == U'\n');
}

/// No useful iteration can consume less than zero characters more times than
/// the subject has positions, so this bounds every repetition loop.
static std::size_t kRepeatGuard = 0;

bool walk(const std::vector<NodePtr>& nodes, std::size_t index, const std::u32string& s,
          std::size_t pos, Caps& caps, const Accept& accept);

bool match_node(const NodePtr& node, const std::u32string& s, std::size_t pos, Caps& caps,
                const Accept& accept) {
    switch (node->kind) {
        case Node::Literal:
            if (pos < s.size() && s[pos] == node->ch) return accept(pos + 1);
            return false;
        case Node::Any:
            // Python without DOTALL: `.` matches anything but '\n'.
            if (pos < s.size() && s[pos] != U'\n') return accept(pos + 1);
            return false;
        case Node::Class:
            if (pos < s.size() && class_matches(*node, s[pos])) return accept(pos + 1);
            return false;
        case Node::Anchor: {
            switch (node->anchor) {
                case Node::Start: case Node::TextStart:
                    // `^` and `\A` are both "start of the subject" without
                    // MULTILINE; they differ only for a subject that begins
                    // with a newline, and then only for `^` in MULTILINE mode.
                    return pos == 0 ? accept(pos) : false;
                case Node::End:
                    return ends_at(s, pos) ? accept(pos) : false;
                case Node::TextEnd:
                    return pos == s.size() ? accept(pos) : false;
                case Node::WordBoundary: case Node::NotWordBoundary: {
                    const bool before = pos > 0 && is_word(s[pos - 1]);
                    const bool after = pos < s.size() && is_word(s[pos]);
                    const bool boundary = before != after;
                    return (node->anchor == Node::WordBoundary) == boundary ? accept(pos)
                                                                            : false;
                }
            }
            return false;
        }
        case Node::Backref: {
            const std::size_t g = static_cast<std::size_t>(node->backref);
            if (g >= caps.size()) return false;
            // An unmatched group matches the empty string, as in Python.
            if (caps[g].first == static_cast<std::size_t>(-1)) return accept(pos);
            const std::size_t len = caps[g].second - caps[g].first;
            if (pos + len > s.size()) return false;
            for (std::size_t k = 0; k < len; ++k)
                if (s[pos + k] != s[caps[g].first + k]) return false;
            return accept(pos + len);
        }
        case Node::Seq:
            return walk(node->sequence, 0, s, pos, caps, accept);
        case Node::Alt: {
            for (const auto& branch : node->branches) {
                Caps before = caps;
                if (match_node(branch, s, pos, caps, accept)) return true;
                caps = before;
            }
            return false;
        }
        case Node::Group: {
            Caps before = caps;
            const std::size_t g = static_cast<std::size_t>(node->group);
            const bool ok = match_node(node->child, s, pos, caps, [&](std::size_t np) {
                const auto saved = caps[g];
                caps[g] = {pos, np};
                if (accept(np)) return true;
                caps[g] = saved;
                return false;
            });
            if (!ok) caps = before;
            return ok;
        }
        case Node::Look: {
            bool ok = false;
            Caps inner = caps;
            if (node->ahead) {
                ok = match_node(node->child, s, pos, inner,
                                [](std::size_t) { return true; });
            } else {
                // Python allows arbitrary-width look-behind: try every start
                // that could end exactly at this position, longest first.
                for (std::size_t start = pos + 1; start-- > 0;) {
                    inner = caps;
                    if (match_node(node->child, s, start, inner,
                                   [&](std::size_t np) { return np == pos; })) {
                        ok = true;
                        break;
                    }
                }
            }
            if (ok == node->negated) return false;
            caps = inner;
            return accept(pos);
        }
        case Node::Atomic: {
            // Match *child* once, take the first (leftmost-greedy) path it
            // offers, and never backtrack into it: if the continuation refuses
            // that end position, the whole group fails (Python's `(?>...)`).
            Caps before = caps;
            std::size_t reached = pos;
            const bool matched = match_node(node->child, s, pos, caps,
                                            [&](std::size_t np) {
                                                reached = np;
                                                return true;
                                            });
            if (!matched) { caps = before; return false; }
            if (accept(reached)) return true;
            caps = before;
            return false;
        }
        case Node::Repeat: {
            // Continuation-passing repeat: every end position the body can
            // produce is offered to the continuation in order (greedy = longest
            // first, lazy = shortest first). A zero-width iteration never loops
            // — it is offered once and the body then keeps backtracking, so a
            // refusal by the continuation can still be satisfied by a LONGER
            // match of the same iteration.
            std::function<bool(int, std::size_t)> rep = [&](int count, std::size_t at) -> bool {
                auto finish = [&]() -> bool { return count >= node->min && accept(at); };
                auto more = [&]() -> bool {
                    if (node->max >= 0 && count >= node->max) return false;
                    if (static_cast<std::size_t>(count) >= kRepeatGuard) return false;
                    Caps before = caps;
                    const bool ok = match_node(node->child, s, at, caps,
                                               [&](std::size_t np) {
                        if (np == at) {              // this iteration consumed nothing
                            if (count + 1 >= node->min && accept(at)) return true;
                            return false;            // let the body try longer
                        }
                        return rep(count + 1, np);
                    });
                    if (!ok) caps = before;
                    return ok;
                };
                return node->greedy ? (more() || finish()) : (finish() || more());
            };
            return rep(0, pos);
        }
    }
    return false;
}

bool walk(const std::vector<NodePtr>& nodes, std::size_t index, const std::u32string& s,
          std::size_t pos, Caps& caps, const Accept& accept) {
    if (index == nodes.size()) return accept(pos);
    Caps before = caps;
    const bool ok = match_node(nodes[index], s, pos, caps,
                               [&](std::size_t np) {
                                   return walk(nodes, index + 1, s, np, caps, accept);
                               });
    if (!ok) caps = before;
    return ok;
}

} // namespace

Regex Regex::compile(const std::u32string& pattern) {
    Parser parser(pattern);
    Regex regex;
    regex.root_ = parser.parse();
    regex.source_ = pattern;
    regex.group_count_ = static_cast<std::size_t>(parser.groups);
    regex.group_names_ = std::move(parser.names);
    return regex;
}

bool Regex::search(const std::u32string& subject, Match* out, std::size_t from) const {
    if (from > subject.size()) return false;
    kRepeatGuard = subject.size() + 1;
    const Caps fresh(group_count_ + 1,
                    {static_cast<std::size_t>(-1), static_cast<std::size_t>(-1)});
    for (std::size_t start = from; start <= subject.size(); ++start) {
        Caps caps = fresh;
        std::size_t stop = start;
        const bool ok = !root_
                            ? true
                            : match_node(root_, subject, start, caps,
                                         [&](std::size_t np) { stop = np; return true; });
        if (!ok) continue;
        if (out) {
            out->start = start;
            out->end = stop;
            out->groups = caps;
            if (!out->groups.empty()) out->groups[0] = {start, stop};
            out->names = group_names_;
        }
        return true;
    }
    return false;
}

namespace {
struct ScanState {
    const Regex* self;
};
} // namespace

/// The leftmost match starting EXACTLY at *pos* that consumes at least one
/// character — what Python's scanner looks for right after an empty match.
static bool progress_at(const Regex& self, const NodePtr& root, std::size_t group_count,
                        const std::u32string& s, std::size_t pos, Match* out) {
    Caps caps(group_count + 1, {static_cast<std::size_t>(-1), static_cast<std::size_t>(-1)});
    std::size_t stop = pos;
    if (!match_node(root, s, pos, caps, [&](std::size_t np) {
            if (np == pos) return false;   // force progress; keep backtracking
            stop = np;
            return true;
        }))
        return false;
    if (out) {
        out->start = pos;
        out->end = stop;
        out->groups = caps;
        if (!out->groups.empty()) out->groups[0] = {pos, stop};
    }
    return true;
}

bool Regex::fullmatch(const std::u32string& subject, Match* out) const {
    Match m;
    if (!search(subject, &m, 0)) return false;
    if (m.start != 0 || m.end != subject.size()) return false;
    if (out) *out = m;
    return true;
}

std::u32string expand_template(const std::u32string& tmpl, const std::u32string& subject,
                               const Match& match, const std::vector<std::u32string>& names,
                               std::size_t group_count) {
    // CPython's `re._parser.parse_template`, rule for rule:
    //   \g<name|number>, \0 (octal, up to three digits), \NN (a GROUP number,
    //   error if it exceeds the group count), the ESCAPES table, and any other
    //   non-letter escape taken literally. A backslash before an ASCII letter
    //   that ESCAPES does not know is an error, as in Python.
    std::u32string out;
    auto octal = [](char32_t c) { return c >= U'0' && c <= U'7'; };
    auto digit = [](char32_t c) { return c >= U'0' && c <= U'9'; };
    auto add_group = [&](std::size_t index, std::size_t at) {
        if (index > group_count) fail("invalid group reference " + std::to_string(index));
        (void)at;
        if (index < match.groups.size() &&
            match.groups[index].first != static_cast<std::size_t>(-1))
            out.append(subject, match.groups[index].first,
                       match.groups[index].second - match.groups[index].first);
        // An unmatched group contributes nothing (Python substitutes "").
    };
    for (std::size_t i = 0; i < tmpl.size(); ++i) {
        if (tmpl[i] != U'\\' || i + 1 >= tmpl.size()) { out.push_back(tmpl[i]); continue; }
        char32_t c = tmpl[++i];
        if (c == U'g') {
            if (i + 1 >= tmpl.size() || tmpl[i + 1] != U'<') fail("missing <");
            std::size_t j = i + 2;
            std::u32string ref;
            while (j < tmpl.size() && tmpl[j] != U'>') ref.push_back(tmpl[j++]);
            if (j >= tmpl.size()) fail("missing >");
            i = j;
            const bool numeric = !ref.empty() &&
                                 std::all_of(ref.begin(), ref.end(),
                                             [](char32_t ch) { return ch >= U'0' && ch <= U'9'; });
            if (numeric) {
                std::size_t index = 0;
                for (char32_t ch : ref) index = index * 10 + static_cast<std::size_t>(ch - U'0');
                add_group(index, i);
            } else {
                bool found = false;
                for (std::size_t g = 0; g < names.size(); ++g)
                    if (names[g] == ref) { add_group(g + 1, i); found = true; break; }
                if (!found) fail("unknown group name");
            }
            continue;
        }
        if (c == U'0') {                     // octal escape, never group 0
            std::u32string digits(1, U'0');
            while (digits.size() < 3 && i + 1 < tmpl.size() && octal(tmpl[i + 1]))
                digits.push_back(tmpl[++i]);
            unsigned value = 0;
            for (char32_t ch : digits) value = value * 8 + static_cast<unsigned>(ch - U'0');
            out.push_back(static_cast<char32_t>(value & 0xff));
            continue;
        }
        if (c >= U'1' && c <= U'9') {
            std::u32string digits(1, c);
            // Python reads a second digit unconditionally, and a third only
            // when the whole run stays octal — so "\12" is group 12, never
            // group 1 followed by '2'.
            if (i + 1 < tmpl.size() && digit(tmpl[i + 1])) {
                digits.push_back(tmpl[++i]);
                if (octal(c) && digits.size() >= 2 && octal(digits[1]) &&
                    i + 1 < tmpl.size() && octal(tmpl[i + 1])) {
                    digits.push_back(tmpl[++i]);
                    unsigned value = 0;
                    for (char32_t ch : digits)
                        value = value * 8 + static_cast<unsigned>(ch - U'0');
                    if (value > 0xff) fail("octal escape value outside of range 0-0o377");
                    out.push_back(static_cast<char32_t>(value));
                    continue;
                }
            }
            std::size_t index = 0;
            for (char32_t ch : digits) index = index * 10 + static_cast<std::size_t>(ch - U'0');
            add_group(index, i);
            continue;
        }
        switch (c) {
            case U'n': out.push_back(U'\n'); break;
            case U't': out.push_back(U'\t'); break;
            case U'r': out.push_back(U'\r'); break;
            case U'f': out.push_back(U'\f'); break;
            case U'v': out.push_back(U'\v'); break;
            case U'a': out.push_back(U'\a'); break;
            case U'\\': out.push_back(U'\\'); break;
            default:
                if ((c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z'))
                    fail("bad escape");                 // Python: bad escape \c
                out.push_back(c); break;               // \. \- \  stay literal
        }
    }
    return out;
}

std::u32string Regex::sub(const std::u32string& subject, const std::u32string& tmpl) const {
    // CPython's scanner (`_sre.c`, the behaviour `re.sub` has had since 3.7):
    // take the leftmost match at or after the scan position; when that match
    // is EMPTY, immediately look for a match starting at the same position
    // that consumes at least one character, and only then step forward. So
    // `re.sub(".*?", "X", "xbb")` replaces seven spans, not four.
    std::u32string out;
    std::size_t last = 0;
    std::size_t pos = 0;
    while (pos <= subject.size()) {
        Match m;
        if (!search(subject, &m, pos)) break;
        out.append(subject, last, m.start - last);
        out.append(expand_template(tmpl, subject, m, group_names_, group_count_));
        last = m.end;
        std::size_t next = m.end;
        if (m.end == m.start) {
            Match again;
            if (progress_at(*this, root_, group_count_, subject, m.start, &again)) {
                out.append(expand_template(tmpl, subject, again, group_names_, group_count_));
                last = again.end;
                next = again.end;
            } else {
                next = m.start + 1;
            }
        }
        if (next > subject.size()) { pos = next; break; }
        pos = next;
    }
    if (last < subject.size()) out.append(subject, last, std::u32string::npos);
    return out;
}

} // namespace orthography2ipa::re32
