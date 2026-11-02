// Direct port of rescorer.py (the lattice rescoring seam, Workstream B4)
// and allophony.py (the post-lexical allophone rule layer compiled into
// it, Workstream B8). A rescorer re-costs one resolved lattice slot's
// candidates as a pure function of that slot and its word-local context,
// before beam path selection; the AllophoneRescorer realizes a slot's
// chosen phoneme with the spec's declarative allophone_rules.
#pragma once

#include "orthography2ipa/phonetok.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace orthography2ipa::rescorer {

// phonetok.py SegmentSlot is declared in orthography2ipa/phonetok.hpp (the
// token model owns the slot dataclass); it is in scope here as
// rescorer::SegmentSlot.

// rescorer.py RescoreContext: everything a LatticeRescorer sees about one
// slot's context. All neighbour views describe the lattice AS RESOLVED
// BEFORE this rescorer ran — every rescorer sees pre-pass slot state, so
// rescorers compose order-independently at the slot level.
struct RescoreContext {
    /// The slot being rescored, with its current (pre-rescorer) candidates.
    const SegmentSlot* slot = nullptr;
    /// Zero-based position of this slot among its word's GRAPHEME slots.
    std::size_t index = 0;
    /// All GRAPHEME slots of the current word, in surface order.
    const std::vector<SegmentSlot>* slots = nullptr;
    /// The grapheme context view for this grapheme (word-local neighbours
    /// and phonological-class predicates).
    const GraphemeContext* grapheme = nullptr;
    /// Syllable index of this grapheme, or nullopt when the entry path has
    /// no stress context (the standalone tokenizer).
    std::optional<std::size_t> syll_idx;
    /// Index of the word's stressed syllable, or nullopt when unavailable
    /// (-1 = the engine's clitic sentinel: a clitic takes no stress).
    std::optional<long long> stressed_syll_idx;

    /// The resolved slot immediately before this one within the same word,
    /// or nullptr at a word start or when an earlier rescorer emptied
    /// (deleted) that neighbour.
    const SegmentSlot* prev_slot() const;
    /// The resolved slot immediately after this one within the same word,
    /// or nullptr at a word end or when a later neighbour was deleted.
    const SegmentSlot* next_slot() const;
    /// True if this is the first grapheme of its word.
    bool is_word_initial() const;
    /// True if every later grapheme of the word is silent (the word's last
    /// PRONOUNCED grapheme: final devoicing fires on the consonant before
    /// a mute letter, not on the mute letter itself).
    bool is_word_final() const;
    /// Whether this grapheme sits in the stressed syllable; nullopt when
    /// stress context is unavailable (standalone tokenizer path).
    std::optional<bool> is_stressed() const;
};

// rescorer.py LatticeRescorer: a pure, composable re-costing pass over one
// lattice slot. Return the existing candidates for a no-op, a new list to
// re-cost/reorder/add/remove, or an EMPTY vector to delete the slot.
class LatticeRescorer {
public:
    virtual ~LatticeRescorer() = default;
    virtual std::vector<Candidate> rescore(const SegmentSlot& slot,
                                           const RescoreContext& context) const = 0;
};

// rescorer.py apply_rescorers: apply *rescorers* in order to every slot,
// returning new slots. *slots* must be the fully resolved (post positional
// + weight) slots aligned 1:1 with *contexts*; the slots are segmented into
// word runs using the word-local contexts (a grapheme with no word-local
// predecessor begins a new run), and every RescoreContext sees only its own
// word's slots with a word-local index. Each rescorer sees the previous
// one's output; a rescorer that empties a slot deletes it.
std::vector<SegmentSlot> apply_rescorers(
    const std::vector<SegmentSlot>& slots,
    const std::vector<GraphemeContext>& contexts,
    const std::vector<const LatticeRescorer*>& rescorers,
    const std::vector<std::size_t>* syll_for_token = nullptr,
    std::optional<long long> stressed_syll_idx = std::nullopt);

/// rescorer.py `RescorerArg = Union[None, LatticeRescorer,
/// Iterable[LatticeRescorer]]` — the public `rescorer=` argument of
/// PhonetokTokenizer.ipa_beam/ipa_lattice and of G2P. The three Python forms
/// are the three implicit conversions here.
struct RescorerArg {
    RescorerArg() = default;                                     // None
    RescorerArg(std::nullptr_t) {}                                // None
    RescorerArg(const LatticeRescorer* one) : items{one} {}       // one
    RescorerArg(const std::vector<const LatticeRescorer*>& many)  // iterable
        : items(many) {}
    std::vector<const LatticeRescorer*> items;
};

/// rescorer.py normalize_rescorers: coerce the public `rescorer=` argument to
/// an ordered collection. nullptr yields the EMPTY collection — the
/// byte-identical default path — one rescorer yields a one-element collection,
/// and a collection keeps iteration order (duplicates included). A null entry
/// raises std::invalid_argument where the reference raises TypeError.
std::vector<const LatticeRescorer*> normalize_rescorers(const RescorerArg& rescorer);
std::vector<const LatticeRescorer*> normalize_rescorers(
        const std::vector<const LatticeRescorer*>& rescorers);

// allophony.py SegmentContext: one segment's phonological neighbourhood
// inside a word. Neighbours look across slot boundaries: the segment
// before the first segment of a slot is the LAST segment of the previous
// slot's top candidate (and symmetrically after the last). nullptr = word
// edge. The pointed-to strings are owned by the caller's segment vector.
struct SegmentContext {
    const std::string* prev = nullptr;
    const std::string* next = nullptr;
    bool is_word_initial = false;
    bool is_word_final = false;
};

// allophony.py AllophoneRescorer: a LatticeRescorer compiled from a spec's
// allophone_rules. For each slot it scans the slot's candidates; a
// candidate whose IPA is a target phoneme of the first rule whose context
// matches is rewritten to that rule's surface form (same cost) — pure
// realisation, not re-ranking. A multi-phoneme candidate (a Hangul block,
// an abugida CV unit) is segmented and rewritten segment by segment, with
// neighbour context read across slot boundaries.
class AllophoneRescorer : public LatticeRescorer {
public:
    AllophoneRescorer(std::vector<AllophoneRule> rules,
                      bool doubled_letters_geminate = true);

    std::vector<Candidate> rescore(const SegmentSlot& slot,
                                   const RescoreContext& context) const override;

    /// allophony.py _rule_atoms: every multi-character phoneme the rules
    /// mention, longest first — the segmentation atoms.
    const std::vector<std::string>& atoms() const { return atoms_; }

private:
    std::vector<AllophoneRule> rules_;
    std::vector<std::string> atoms_;
    bool doubling_is_geminate_ = true;

    std::optional<std::string> neighbor_mutation(const RescoreContext& context) const;
    std::optional<int> geminate_twin_side(const std::string& ipa,
                                          const RescoreContext& context) const;
    std::optional<std::string> realize(const std::string& ipa,
                                       const RescoreContext& context) const;
    std::optional<std::string> realize_segments(const std::vector<std::string>& segments,
                                                const RescoreContext& context) const;
    std::optional<std::string> realize_one_segment(
        const std::string& seg, const SegmentContext& seg_ctx,
        const RescoreContext& context) const;
    bool matches(const AllophoneRule& rule, const RescoreContext& ctx) const;
    bool matches_slot(const AllophoneRule& rule, const RescoreContext& ctx) const;
};

// allophony.py compile_allophone_rescorer: compile allophone_rules into a
// rescorer, or nullptr when the rules are empty ("no post-lexical pass").
std::unique_ptr<LatticeRescorer> compile_allophone_rescorer(
    const std::vector<AllophoneRule>& rules, bool doubled_letters_geminate = true);

} // namespace orthography2ipa::rescorer
