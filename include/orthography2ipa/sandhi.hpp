// Direct port of orthography2ipa/sandhi.py — cross-word-boundary phonological
// rule engine.
//
// A rule sees the IPA of BOTH words at a boundary and may rewrite either side
// (`transform` for the left word, `right_transform` for the right); apply()
// resolves the two sides independently, so at most one rule fires per side per
// boundary. Contexts are ALWAYS matched against the original pair, so the side
// that fires first cannot mask the other's trigger. A rule is defined on a
// prosodic domain and nothing reaches outside it: the pause the tokenizer read
// off the punctuation blocks the boundary (`pausal[i]` = word *i* stands before
// a pause).
//
// The contexts are Python `re` patterns authored against `str`, so they match
// over CODE POINTS (src/codepoint_regex.hpp), never over UTF-8 bytes.
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace orthography2ipa {

struct SandhiRule;

namespace sandhi {

/// sandhi.py SandhiEngine: the spec's declarative rules, compiled once.
class Engine {
public:
    Engine();
    ~Engine();
    Engine(Engine&&);
    Engine& operator=(Engine&&);
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    /// Compile every rule's two contexts. Throws std::runtime_error naming the
    /// rule when a context is not a valid pattern (`re.compile` at least).
    explicit Engine(const std::vector<SandhiRule>& rules);

    /// sandhi.py `SandhiEngine.apply`. `pausal` may be empty to mean "no
    /// pause information" (a single phrase); when it is given it must have one
    /// flag per word, and `pausal[i]` blocks the boundary after word *i*.
    std::vector<std::string> apply(const std::vector<std::string>& words_ipa,
                                   bool obligatory_only = false,
                                   const std::vector<bool>& pausal = {}) const;

    bool empty() const;
    std::size_t size() const;

private:
    struct Impl;                        ///< the rules with their compiled contexts
    std::unique_ptr<Impl> impl_;
};

} // namespace sandhi
} // namespace orthography2ipa
