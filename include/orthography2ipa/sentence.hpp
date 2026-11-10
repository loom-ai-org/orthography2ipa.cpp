// Direct port of orthography2ipa/sentence.py — the cross-word
// (utterance-scope) seam.
//
// The per-word lattice (phonetok.py) and its rescorer seam are strictly
// WORD-LOCAL: a rescorer sees one slot and its immediate neighbours within a
// word and never crosses a word boundary. That is the right scope for
// word-internal phonology, but not for waṣl elision, pausal forms, French
// liaison or Portuguese external /s/-sandhi, each of which needs two things
// the word lattice does not provide: visibility into the ADJACENT word's edge
// (its final / initial candidates) and the word's PHRASE / UTTERANCE POSITION.
// This module is that shared seam.
//
// What it adds
// ------------
// SentenceLattice — the whole utterance as an ordered list of WordSlot
// objects, each carrying that word's per-grapheme lattice (a
// rescorer::SegmentSlot), its chosen IPA, and its phrase / utterance position.
// A consumer sees the ranked candidates of every word in order, not a
// flattened string.
//
// SentenceRescorer — the ABC mirroring rescorer::LatticeRescorer at SENTENCE
// SCOPE. It is invoked once per word with a SentenceRescoreContext that
// exposes the adjacent words (and their edge slots) plus phrase / utterance
// position, and returns that word's (possibly rewritten) IPA. Because it is
// invoked PER WORD, a boundary rewrite that must change BOTH words (liaison
// resyllabification, waṣl elision on the right word) is expressed naturally:
// the left word's invocation rewrites the left, the right word's invocation
// rewrites the right, both seeing the same boundary context — the
// bidirectionality sandhi::Engine lacks (it can only rewrite the left word of
// a pair).
//
// Opt-in and byte-identical when unused
// -------------------------------------
// Nothing here runs unless a caller passes a sentence rescorer: with none, the
// pipeline is byte-for-byte what it was, and the spec's declarative
// sandhi_rules keep working exactly as now and COEXIST with this seam (sentence
// rescorers run first, then the spec's sandhi pass).
//
// Composition and determinism
// ---------------------------
// A list of sentence rescorers composes like lattice rescorers: they apply in
// order, and each pass sees the previous pass's rewritten IPA (via
// SentenceRescoreContext::prev_word_ipa / next_word_ipa), while WITHIN a single
// pass every word reads the pre-pass snapshot, so the pass is order-independent
// and deterministic. A rescorer that returns the word's IPA unchanged is a
// no-op.
//
// What is still downstream
// ------------------------
// This seam supplies position and cross-word adjacency, not meaning: the
// context carries no POS or sense field, deliberately.
//
// All strings are UTF-8 at the boundary; this module only joins and selects
// them, so no code-point arithmetic happens here (see the reference's
// sentence.py docstrings for the contracts).
//
// Include this header at GLOBAL scope (the way orthography2ipa.hpp already
// includes rescorer.hpp). Including any of these headers from inside a
// reopened `namespace orthography2ipa { ... }` puts the declarations one level
// deeper, at orthography2ipa::orthography2ipa::sentence — GCC does that for
// every header in the project, not just this one.
#pragma once

#include "orthography2ipa/phonetok.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace orthography2ipa::sentence {

/// sentence.py Position(str, Enum): where a word sits inside a bounded span
/// (a phrase, or the utterance). A phrase is a run of words bounded by pause
/// punctuation or the utterance edges — the same boundary the engine already
/// marks as pausal. SOLE is the degenerate span of length one: the word is at
/// once initial and final, so both is_initial() and is_final() hold.
enum class Position { INITIAL, MEDIAL, FINAL, SOLE };

/// The reference's str value ("initial" | "medial" | "final" | "sole").
const char* to_string(Position position);

/// Position.is_initial(): true at the start of the span (INITIAL or SOLE).
bool is_initial(Position position);
/// Position.is_final(): true at the end of the span (FINAL or SOLE).
bool is_final(Position position);

/// sentence.py span_position: the Position of word *index* in the half-open
/// span [start, end). *start*/*end* bound a phrase (or the utterance) in word
/// indices; a length-one span yields Position::SOLE. Signed, because the
/// reference's `index == end - 1` comparison is Python integer arithmetic.
Position span_position(long long index, long long start, long long end);

/// sentence.py WordSlot: one word's place in a SentenceLattice. Carries the
/// word's per-grapheme lattice — so a consumer sees its ranked candidates, not
/// just the flattened string — alongside the position metadata a cross-word
/// rule needs. The reference dataclass requires surface, ipa, slots, index and
/// both positions; pausal defaults to false.
struct WordSlot {
    /// Orthographic surface form (after normalization), as split from input.
    std::string surface;
    /// The word's chosen IPA in isolation — the per-word engine output (after
    /// stress, before any sentence rescorer or the spec's sandhi pass). A
    /// SentenceRescorer reads this and returns a rewrite of it.
    std::string ipa;
    /// The word's pronunciation lattice: one SegmentSlot per GRAPHEME, in
    /// surface order, with ranked (ipa, cost) candidates. Empty for a
    /// lexicon word_exceptions override or an all-unmapped word.
    std::vector<rescorer::SegmentSlot> slots;
    /// Zero-based position of this word in the utterance.
    std::size_t index = 0;
    /// This word's Position within its punctuation-bounded phrase.
    Position phrase_position = Position::SOLE;
    /// This word's Position within the whole utterance.
    Position utterance_position = Position::SOLE;
    /// The word stands immediately before a pause (pause punctuation or the
    /// end of input) — i.e. it is phrase-final. Equivalent to
    /// is_final(phrase_position).
    bool pausal = false;

    /// WordSlot.initial_slot: the word's first lattice slot (its ONSET edge),
    /// or nullptr when the word has no lattice. A cross-word rule reads this to
    /// inspect the next word's initial candidates from the boundary.
    const rescorer::SegmentSlot* initial_slot() const;
    /// WordSlot.final_slot: the word's last lattice slot (its CODA edge), or
    /// nullptr when the word has no lattice. A cross-word rule reads this to
    /// inspect the previous word's final candidates from the boundary.
    const rescorer::SegmentSlot* final_slot() const;
};

/// sentence.py SentenceLattice: the whole utterance as an ordered list of
/// WordSlot. The sentence-scope analogue of the per-word lattice: every word's
/// ranked candidates IN ORDER with word boundaries and phrase / utterance
/// position, so a consumer can reason over the entire utterance's search space
/// rather than a flattened IPA string.
struct SentenceLattice {
    /// The utterance's words, in surface order.
    std::vector<WordSlot> words;
    /// The resolved canonical language code used.
    std::string lang;

    /// SentenceLattice.ipa: each word's WordSlot::ipa joined with spaces,
    /// SKIPPING empties. This is the pre-sentence-rescore, pre-sandhi reading;
    /// it matches G2P::transcribe only when no sentence rescorer and no spec
    /// sandhi_rules apply.
    std::string ipa() const;
    /// SentenceLattice.__len__.
    std::size_t size() const;
    /// SentenceLattice.__getitem__ (words[i]); throws std::out_of_range where
    /// Python raises IndexError.
    const WordSlot& at(std::size_t i) const;
    /// SentenceLattice.__iter__.
    std::vector<WordSlot>::const_iterator begin() const;
    std::vector<WordSlot>::const_iterator end() const;
};

/// sentence.py SentenceRescoreContext: everything a SentenceRescorer sees about
/// one word's place in the utterance. Like rescorer::RescoreContext for a slot,
/// this is a pure, immutable view: the ADJACENT WORDS (with their edge slots)
/// and this word's PHRASE / UTTERANCE POSITION — the two things the word-local
/// lattice cannot give. *word* and *words* are non-owning views set by
/// apply_sentence_rescorers; the reference dataclass requires them.
struct SentenceRescoreContext {
    /// The word being rescored.
    const WordSlot* word = nullptr;
    /// Zero-based position of *word* in the utterance (== word->index;
    /// (*words)[index] == word).
    std::size_t index = 0;
    /// All words of the utterance, in surface order.
    const std::vector<WordSlot>* words = nullptr;
    /// The resolved canonical language code.
    std::string lang;
    /// sentence.py _ipas: the chain snapshot of every word's current IPA (as
    /// left by earlier rescorers in the chain). Read via prev_word_ipa /
    /// next_word_ipa / this_word_ipa; internal otherwise. EMPTY means "first
    /// pass, no snapshot", and the views fall back to WordSlot::ipa — the
    /// reference's `if self._ipas` truthiness test.
    std::vector<std::string> ipas;

    // ── adjacency (honouring utterance edges) ──
    /// The preceding word, or nullptr at the utterance start. Its final
    /// candidates are prev_word()->final_slot().
    const WordSlot* prev_word() const;
    /// The following word, or nullptr at the utterance end. Its initial
    /// candidates are next_word()->initial_slot().
    const WordSlot* next_word() const;
    /// This word's current IPA in the chain (== the ipa a no-op should return).
    /// Equals WordSlot::ipa on the first pass; a later pass sees an earlier
    /// pass's rewrite.
    std::string this_word_ipa() const;
    /// The preceding word's current IPA in the chain, or nullopt at the
    /// utterance start. Reflects earlier rescorers' rewrites.
    std::optional<std::string> prev_word_ipa() const;
    /// The following word's current IPA in the chain, or nullopt at the
    /// utterance end. Reflects earlier rescorers' rewrites.
    std::optional<std::string> next_word_ipa() const;

    // ── position convenience ──
    /// This word's Position within its punctuation-bounded phrase.
    Position phrase_position() const;
    /// This word's Position within the whole utterance.
    Position utterance_position() const;
    /// True if this word opens its phrase (conditions e.g. hamzat al-waṣl only
    /// at an utterance start — combine with is_utterance_initial()).
    bool is_phrase_initial() const;
    /// True if this word closes its phrase (conditions e.g. Arabic pausal
    /// forms). Equivalent to word->pausal.
    bool is_phrase_final() const;
    /// True if this word opens the utterance.
    bool is_utterance_initial() const;
    /// True if this word closes the utterance.
    bool is_utterance_final() const;
};

/// sentence.py SentenceRescorer: a pure, composable rewrite pass over ONE word,
/// with cross-word context. The sentence-scope analogue of
/// rescorer::LatticeRescorer. Implement rescore() to return the word's new IPA
/// given its SentenceRescoreContext. Returning word.ipa (or
/// context.this_word_ipa()) unchanged is a no-op; returning a different string
/// rewrites that word.
///
/// Bidirectional by construction: because rescore() runs once per word, a
/// boundary process that must change BOTH words is expressed by having each
/// word's invocation rewrite its own side while reading the shared boundary
/// (context.prev_word() / context.next_word() and their edge slots).
///
/// Keep rescore() free of global state so a list of rescorers composes
/// deterministically (see apply_sentence_rescorers).
class SentenceRescorer {
public:
    virtual ~SentenceRescorer() = default;
    /// sentence.py SentenceRescorer.rescore: return the (possibly rewritten)
    /// IPA for *word*. Must be a pure function of *word* and *context*.
    virtual std::string rescore(const WordSlot& word,
                                const SentenceRescoreContext& context) const = 0;
};

/// sentence.py SentenceRescorerArg = Union[None, SentenceRescorer,
/// Iterable[SentenceRescorer]]: nothing, one, or an ordered collection. C++
/// spells "nothing" as nullptr and the collection as a vector of non-owning
/// pointers, so the argument is either overload of
/// normalize_sentence_rescorers below.
using SentenceRescorers = std::vector<const SentenceRescorer*>;

/// sentence.py `SentenceRescorerArg = Union[None, SentenceRescorer,
/// Iterable[SentenceRescorer]]` — the union the G2P constructor takes. The
/// three Python forms are the three implicit conversions here, so a caller
/// writes `nullptr`, one rescorer, or a list exactly as in Python.
struct SentenceRescorerArg {
    SentenceRescorerArg() = default;                                  // None
    SentenceRescorerArg(std::nullptr_t) {}                            // None
    SentenceRescorerArg(const SentenceRescorer* one) : items{one}, none(false) {}  // one
    SentenceRescorerArg(const SentenceRescorers& many) : items(many), none(false) {}  // iterable
    SentenceRescorers items;
    /// `sentence_rescorer is None`: an empty list is an argument, and None is not.
    bool none = true;
};

/// sentence.py normalize_sentence_rescorers: coerce the public
/// sentence_rescorer= argument to an ordered collection. nullptr (the default
/// byte-identical path) yields an empty one, a single rescorer yields a
/// one-element collection, and a collection is kept in iteration order
/// (duplicates included, as the reference keeps them). The reference's
/// TypeError on a non-SentenceRescorer entry is a compile error here for
/// anything else; a null entry raises std::invalid_argument instead.
SentenceRescorers normalize_sentence_rescorers(const SentenceRescorer* rescorer);
SentenceRescorers normalize_sentence_rescorers(const SentenceRescorers& rescorers);
SentenceRescorers normalize_sentence_rescorers(const SentenceRescorerArg& arg);

/// sentence.py apply_sentence_rescorers: apply *rescorers* in order to
/// *lattice*, returning the per-word IPAs aligned 1:1 with lattice.words.
/// For each rescorer, every word's IPA is recomputed from the CURRENT chain
/// snapshot and the whole pass is committed at once, so a single pass reads a
/// stable pre-pass snapshot (order-independent and deterministic) while the
/// next pass sees this pass's output. Pure: it never mutates *lattice* and
/// holds no state.
std::vector<std::string> apply_sentence_rescorers(const SentenceLattice& lattice,
                                                  const SentenceRescorers& rescorers);

/// sentence.py WordContext: context for a word in a sentence, enabling
/// sandhi/liaison. What a word can see of its neighbours. It lives in this
/// module because this is where cross-word context lives (the plugin base class
/// it used to sit beside is gone; the TYPE is real and is shared vocabulary).
/// All fields default so existing call sites keep working — engines populate as
/// much as they know. Field order is the reference dataclass's.
struct WordContext {
    /// IPA of the preceding word, if already transcribed.
    std::optional<std::string> prev_word_ipa;
    /// IPA of the following word, if already transcribed.
    std::optional<std::string> next_word_ipa;
    /// The word stands before a pause — punctuation or end of input. Pausal
    /// position conditions e.g. Arabic tanwīn suppression and tāʾ marbūṭa
    /// realisation.
    bool is_pausal = false;
    /// The word opens the sentence (conditions e.g. hamzat al-waṣl).
    bool is_sentence_initial = false;
    /// Orthographic form of the preceding word.
    std::optional<std::string> prev_word;
    /// Orthographic form of the following word.
    std::optional<std::string> next_word;
    /// The word closes the sentence.
    bool is_sentence_final = false;
    /// Zero-based position of the word in the sentence.
    std::size_t word_index = 0;
    /// Total number of words in the sentence.
    std::size_t word_count = 1;
    /// Resolved BCP-47 code the transcription was requested for — lets a
    /// multi-dialect plugin pick the right variety.
    std::optional<std::string> lang;
};

} // namespace orthography2ipa::sentence
