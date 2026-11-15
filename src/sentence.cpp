// orthography2ipa/sentence.py — the cross-word (utterance-scope) seam:
// position classification, the utterance lattice, and the per-word rescorer
// chain over it.
#include "orthography2ipa/sentence.hpp"

#include <stdexcept>

namespace orthography2ipa::sentence {

const char* to_string(Position position) {
    // Position is a str Enum: these are the reference's values.
    switch (position) {
        case Position::INITIAL: return "initial";
        case Position::MEDIAL: return "medial";
        case Position::FINAL: return "final";
        case Position::SOLE: return "sole";
    }
    return "medial";
}

bool is_initial(Position position) {
    // Position.is_initial: INITIAL or SOLE.
    return position == Position::INITIAL || position == Position::SOLE;
}

bool is_final(Position position) {
    // Position.is_final: FINAL or SOLE.
    return position == Position::FINAL || position == Position::SOLE;
}

Position span_position(long long index, long long start, long long end) {
    const bool first = index == start;
    const bool last = index == end - 1;
    if (first && last) return Position::SOLE;
    if (first) return Position::INITIAL;
    if (last) return Position::FINAL;
    return Position::MEDIAL;
}

const rescorer::SegmentSlot* WordSlot::initial_slot() const {
    return slots.empty() ? nullptr : &slots.front();
}

const rescorer::SegmentSlot* WordSlot::final_slot() const {
    return slots.empty() ? nullptr : &slots.back();
}

std::string SentenceLattice::ipa() const {
    // " ".join(w.ipa for w in words if w.ipa) — an empty word contributes no
    // token, so it does not leave a double space behind.
    std::string joined;
    for (const WordSlot& word : words) {
        if (word.ipa.empty()) continue;
        if (!joined.empty()) joined += " ";
        joined += word.ipa;
    }
    return joined;
}

std::size_t SentenceLattice::size() const { return words.size(); }

const WordSlot& SentenceLattice::at(std::size_t i) const { return words.at(i); }

std::vector<WordSlot>::const_iterator SentenceLattice::begin() const {
    return words.begin();
}

std::vector<WordSlot>::const_iterator SentenceLattice::end() const {
    return words.end();
}

const WordSlot* SentenceRescoreContext::prev_word() const {
    // words[index - 1] if index > 0 else None
    return index > 0 ? &words->at(index - 1) : nullptr;
}

const WordSlot* SentenceRescoreContext::next_word() const {
    // words[index + 1] if index + 1 < len(words) else None
    return index + 1 < words->size() ? &words->at(index + 1) : nullptr;
}

std::string SentenceRescoreContext::this_word_ipa() const {
    // _ipas[index] if _ipas else word.ipa — an empty snapshot means "no chain
    // has run yet", so the word's own isolation IPA is the current one.
    return ipas.empty() ? word->ipa : ipas.at(index);
}

std::optional<std::string> SentenceRescoreContext::prev_word_ipa() const {
    if (index == 0) return std::nullopt;
    return ipas.empty() ? std::optional<std::string>(words->at(index - 1).ipa)
                        : std::optional<std::string>(ipas.at(index - 1));
}

std::optional<std::string> SentenceRescoreContext::next_word_ipa() const {
    if (index + 1 >= words->size()) return std::nullopt;
    return ipas.empty() ? std::optional<std::string>(words->at(index + 1).ipa)
                        : std::optional<std::string>(ipas.at(index + 1));
}

Position SentenceRescoreContext::phrase_position() const {
    return word->phrase_position;
}

Position SentenceRescoreContext::utterance_position() const {
    return word->utterance_position;
}

bool SentenceRescoreContext::is_phrase_initial() const {
    return is_initial(word->phrase_position);
}

bool SentenceRescoreContext::is_phrase_final() const {
    return is_final(word->phrase_position);
}

bool SentenceRescoreContext::is_utterance_initial() const {
    return is_initial(word->utterance_position);
}

bool SentenceRescoreContext::is_utterance_final() const {
    return is_final(word->utterance_position);
}

SentenceRescorers normalize_sentence_rescorers(const SentenceRescorer* rescorer) {
    // None -> (); a single rescorer -> (rescorer,)
    if (rescorer == nullptr) return {};
    return {rescorer};
}

SentenceRescorers normalize_sentence_rescorers(const SentenceRescorerArg& arg) {
    // The union form: None (empty items) stays empty, one or many are
    // validated entry by entry.
    return normalize_sentence_rescorers(arg.items);
}

SentenceRescorers normalize_sentence_rescorers(const SentenceRescorers& rescorers) {
    // for r in rescorer: if not isinstance(r, SentenceRescorer): raise TypeError
    SentenceRescorers out;
    out.reserve(rescorers.size());
    for (const SentenceRescorer* r : rescorers) {
        if (r == nullptr)
            throw std::invalid_argument(
                "sentence_rescorer entries must be SentenceRescorer, got None");
        out.push_back(r);
    }
    return out;
}

std::vector<std::string> apply_sentence_rescorers(const SentenceLattice& lattice,
                                                 const SentenceRescorers& rescorers) {
    std::vector<std::string> ipas;
    ipas.reserve(lattice.words.size());
    for (const WordSlot& word : lattice.words) ipas.push_back(word.ipa);

    for (const SentenceRescorer* rescorer : rescorers) {
        // The pass reads the snapshot taken BEFORE it and writes the live
        // chain, so every word in one pass sees the same pre-pass state while
        // the next pass sees this pass's output.
        const std::vector<std::string> snapshot = ipas;
        for (std::size_t i = 0; i < lattice.words.size(); ++i) {
            const SentenceRescoreContext context{
                &lattice.words[i], i, &lattice.words, lattice.lang, snapshot};
            ipas[i] = rescorer->rescore(lattice.words[i], context);
        }
    }
    return ipas;
}

} // namespace orthography2ipa::sentence
