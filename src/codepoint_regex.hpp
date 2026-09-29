// A backtracking regular-expression matcher over Unicode CODE POINTS
// (std::u32string), reproducing the semantics of the Python `re` subset the
// reference's declarative rule contexts rely on (sandhi.py, transforms.py).
//
// std::regex is NOT usable for these patterns: it matches UTF-8 BYTES, so a
// class like [ËˆËŒ] offers the individual bytes of a multi-byte character and
// matches no character at all, while regex_replace cuts characters in half and
// puts U+FFFD in the output. Here a position, a length and `.` are all code
// points, exactly as they are for a Python str.
//
// Supported: literals, `.`, character classes (ranges, negation, escapes),
// `^` `$` `\A` `\Z`, capturing / non-capturing / named groups, alternation,
// greedy and lazy `? * + {m,n}`, look-ahead `(?= (?!`, look-behind `(?<= (?<!`,
// word boundaries `\b \B`, the category classes `\d \w \s` and their
// negations, backreferences, and Python's `re.sub` template rules
// (`\1`, `\g<1>`, `\g<name>`, `\0`; an unknown escape of a non-letter is
// literal, as in Python).
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace orthography2ipa::re32 {

namespace detail { struct Node; }
using NodePtr = std::shared_ptr<detail::Node>;

/// One match: the whole span plus every capture group. A group that did not
/// participate has first == second == SIZE_MAX (Python's None).
struct Match {
    std::size_t start = 0;
    std::size_t end = 0;
    /// groups[0] is the whole match; groups[i] is capture group i.
    std::vector<std::pair<std::size_t, std::size_t>> groups;
    /// 1-based capture-group names, parallel to groups[1..]; empty when unnamed.
    std::vector<std::u32string> names;

    /// The span of capture group *index* (0 = whole match). False when the
    /// group did not participate, mirroring Python's None.
    bool group(std::size_t index, std::size_t* first, std::size_t* last) const {
        if (index >= groups.size()) return false;
        *first = groups[index].first;
        *last = groups[index].second;
        return *first != static_cast<std::size_t>(-1);
    }
};

/// A compiled pattern: immutable, cheap to share, re-entrant.
class Regex {
public:
    Regex() = default;

    /// Compile *pattern*. Throws std::runtime_error on a malformed pattern;
    /// the reference lets `re.error` escape the same way, so the caller turns
    /// it into its own "invalid rule" message.
    static Regex compile(const std::u32string& pattern);

    /// Python `pattern.search(subject, pos)`: the leftmost match at or after
    /// *from*.
    bool search(const std::u32string& subject, Match* out, std::size_t from = 0) const;
    /// Python `pattern.fullmatch(subject)`.
    bool fullmatch(const std::u32string& subject, Match* out) const;
    /// Python `pattern.sub(template, subject)`.
    std::u32string sub(const std::u32string& subject, const std::u32string& tmpl) const;

    /// Python `pattern.pattern`: the source, kept for diagnostics.
    const std::u32string& source() const { return source_; }
    std::size_t group_count() const { return group_count_; }
    const std::vector<std::u32string>& group_names() const { return group_names_; }

private:
    NodePtr root_;
    std::u32string source_;
    std::size_t group_count_ = 0;
    std::vector<std::u32string> group_names_;
};

/// Expand a Python `re.sub` replacement template over *subject* for *match*
/// (`\1`, `\g<1>`, `\g<name>`, `\0`). Exposed for direct testing.
std::u32string expand_template(const std::u32string& tmpl, const std::u32string& subject,
                               const Match& match,
                               const std::vector<std::u32string>& names,
                               std::size_t group_count);

} // namespace orthography2ipa::re32
