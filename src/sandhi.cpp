#include "orthography2ipa/orthography2ipa.hpp"
#include "orthography2ipa/sandhi.hpp"

#include "codepoint_regex.hpp"
#include "unicode_util.hpp"

#include <stdexcept>
#include <utility>

namespace orthography2ipa::sandhi {
namespace {

using uni::to_utf32;
using uni::to_utf8;

} // namespace

/// rule + its two compiled contexts (sandhi.py `self._compiled`).
struct Engine::Impl {
    struct Compiled {
        SandhiRule rule;
        re32::Regex left;
        re32::Regex right;
    };
    std::vector<Compiled> compiled;
};

Engine::Engine() = default;
Engine::~Engine() = default;
Engine::Engine(Engine&&) = default;
Engine& Engine::operator=(Engine&&) = default;
bool Engine::empty() const { return !impl_ || impl_->compiled.empty(); }
std::size_t Engine::size() const { return impl_ ? impl_->compiled.size() : 0; }

Engine::Engine(const std::vector<SandhiRule>& rules) : impl_(std::make_unique<Impl>()) {
    // sandhi.py compiles in __init__: a malformed context is an error here,
    // before any word has been transcribed, exactly as `re.compile` raises.
    impl_->compiled.reserve(rules.size());
    for (const auto& rule : rules) {
        Impl::Compiled compiled;
        compiled.rule = rule;
        try {
            compiled.left = re32::Regex::compile(to_utf32(rule.left_context));
            compiled.right = re32::Regex::compile(to_utf32(rule.right_context));
        } catch (const std::exception& e) {
            throw std::runtime_error("invalid sandhi rule " + rule.id + ": " + e.what());
        }
        impl_->compiled.push_back(std::move(compiled));
    }
}

std::vector<std::string> Engine::apply(const std::vector<std::string>& words_ipa,
                                       bool obligatory_only,
                                       const std::vector<bool>& pausal) const {
    // Validate BEFORE the early return: a caller that mismatched the two lists
    // has a bug whether or not the utterance happens to be one word.
    if (!pausal.empty() && pausal.size() != words_ipa.size())
        throw std::invalid_argument("pausal has " + std::to_string(pausal.size()) +
                                    " flags for " + std::to_string(words_ipa.size()) +
                                    " words");
    if (words_ipa.size() <= 1) return words_ipa;

    if (!impl_ || impl_->compiled.empty()) return words_ipa;
    std::vector<std::string> result = words_ipa;
    const bool have_pausal = !pausal.empty() && pausal.size() == result.size();
    for (std::size_t i = 0; i + 1 < result.size(); ++i) {
        // Nothing joins the two words a pause separates: the intonational
        // phrase the punctuation writes is the rule's prosodic domain.
        if (have_pausal && pausal[i]) continue;
        const std::u32string left = to_utf32(result[i]);
        const std::u32string right = to_utf32(result[i + 1]);
        // The two sides of a boundary are resolved independently: the first
        // matching rule wins PER SIDE, and contexts are matched against the
        // ORIGINAL words, so a rewrite cannot feed another rule here.
        bool left_done = false, right_done = false;
        for (const auto& compiled : impl_->compiled) {
            if (left_done && right_done) break;
            if (obligatory_only && !compiled.rule.obligatory) continue;
            re32::Match ignored;
            if (!compiled.left.search(left, &ignored)) continue;
            if (!compiled.right.search(right, &ignored)) continue;
            if (compiled.rule.transform && !left_done) {
                result[i] = to_utf8(compiled.left.sub(
                    left, to_utf32(*compiled.rule.transform)));
                left_done = true;
            }
            if (compiled.rule.right_transform && !right_done) {
                result[i + 1] = to_utf8(compiled.right.sub(
                    right, to_utf32(*compiled.rule.right_transform)));
                right_done = true;
            }
        }
    }
    return result;
}

} // namespace orthography2ipa::sandhi
