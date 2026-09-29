#include "orthography2ipa/orthography2ipa.hpp"
#include "orthography2ipa/rescorer.hpp"

#include <algorithm>

namespace orthography2ipa::rescorer {

// ── RescoreContext convenience views (rescorer.py) ──────────────────────

const SegmentSlot* RescoreContext::prev_slot() const {
    if (grapheme == nullptr || grapheme->prev() == nullptr || index == 0)
        return nullptr;
    const SegmentSlot& prev = (*slots)[index - 1];
    return prev.candidates.empty() ? nullptr : &prev;
}

const SegmentSlot* RescoreContext::next_slot() const {
    if (grapheme == nullptr || grapheme->next() == nullptr ||
        index + 1 >= slots->size())
        return nullptr;
    const SegmentSlot& next = (*slots)[index + 1];
    return next.candidates.empty() ? nullptr : &next;
}

bool RescoreContext::is_word_initial() const {
    return grapheme == nullptr || grapheme->prev() == nullptr;
}

bool RescoreContext::is_word_final() const {
    // Not merely the last grapheme: a word-final silent letter emits
    // nothing, so the consonant before it is what a listener hears at the
    // word's end. True when every later grapheme in the word is silent —
    // read off each later slot's TOP candidate.
    if (grapheme->next() == nullptr) return true;
    for (std::size_t j = index + 1; j < slots->size(); ++j) {
        const SegmentSlot& slot = (*slots)[j];
        if (!slot.candidates.empty() && !slot.top().ipa.empty()) return false;
    }
    return true;
}

std::optional<bool> RescoreContext::is_stressed() const {
    if (!stressed_syll_idx.has_value() || !syll_idx.has_value()) return std::nullopt;
    return static_cast<long long>(*syll_idx) == *stressed_syll_idx;
}

// ── apply_rescorers (rescorer.py) ────────────────────────────────────────

std::vector<SegmentSlot> apply_rescorers(
        const std::vector<SegmentSlot>& slots,
        const std::vector<GraphemeContext>& contexts,
        const std::vector<const LatticeRescorer*>& rescorers,
        const std::vector<std::size_t>* syll_for_token,
        std::optional<long long> stressed_syll_idx) {
    const std::size_t n = contexts.size();
    // Word-run boundaries from the word-local contexts: a grapheme with no
    // word-local predecessor starts a new run.
    std::vector<std::pair<std::size_t, std::size_t>> runs;
    std::size_t start = 0;
    for (std::size_t i = 1; i < n; ++i) {
        if (contexts[i].prev() == nullptr) {
            runs.emplace_back(start, i);
            start = i;
        }
    }
    if (n > 0) runs.emplace_back(start, n);

    std::vector<SegmentSlot> current = slots;
    for (const LatticeRescorer* rescorer : rescorers) {
        std::vector<SegmentSlot> rescored = current;
        for (const auto& [run_start, run_end] : runs) {
            // The rescorer reads the word-local slots as left by the
            // PREVIOUS rescorer (or the input), never its own output.
            const std::vector<SegmentSlot> local(current.begin() + static_cast<std::ptrdiff_t>(run_start),
                                                 current.begin() + static_cast<std::ptrdiff_t>(run_end));
            for (std::size_t local_idx = 0; local_idx < local.size(); ++local_idx) {
                const std::size_t gi = run_start + local_idx;
                RescoreContext ctx;
                ctx.slot = &local[local_idx];
                ctx.index = local_idx;
                ctx.slots = &local;
                ctx.grapheme = &contexts[gi];
                if (syll_for_token != nullptr && gi < syll_for_token->size())
                    ctx.syll_idx = (*syll_for_token)[gi];
                ctx.stressed_syll_idx = stressed_syll_idx;
                std::vector<Candidate> new_cands =
                    rescorer->rescore(local[local_idx], ctx);
                // Returning the slot's existing candidates is a no-op; an
                // empty list deletes the slot.
                if (new_cands.size() == local[local_idx].candidates.size() &&
                        std::equal(new_cands.begin(), new_cands.end(),
                                   local[local_idx].candidates.begin()))
                    continue;
                std::stable_sort(new_cands.begin(), new_cands.end(),
                                 [](const Candidate& a, const Candidate& b) {
                                     if (a.score != b.score) return a.score < b.score;
                                     return a.ipa < b.ipa;
                                 });
                SegmentSlot& rebuilt = rescored[gi];
                rebuilt.candidates = std::move(new_cands);
            }
        }
        current = std::move(rescored);
    }
    return current;
}

} // namespace orthography2ipa::rescorer
