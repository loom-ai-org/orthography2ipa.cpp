// Standalone validation for the sentence.py port: dumps the C++ results of
// every case in tests/_scratch/sentence_ref_check.py as JSON, so the two can be
// diffed case by case. Same case families, same generation loops, same order.
#include "orthography2ipa/sentence.hpp"

#include <cstddef>
#include <cstdio>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace orthography2ipa;
using sentence::Position;

namespace {

// ── minimal JSON writers ─────────────────────────────────────────────────
std::string esc(const std::string& in) {
    std::string out;
    for (const unsigned char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

std::string q(const std::string& in) { return "\"" + esc(in) + "\""; }
std::string q(const char* in) { return q(std::string(in)); }
std::string b(bool value) { return value ? "true" : "false"; }
std::string num(long long value) { return std::to_string(value); }

template <class T>
std::string qopt(const std::optional<T>& value) {
    return value ? q(*value) : std::string("null");
}

std::string arr(const std::vector<std::string>& items) {
    std::string out = "[";
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i) out += ",";
        out += items[i];
    }
    out += "]";
    return out;
}

std::string obj(const std::vector<std::pair<std::string, std::string>>& fields) {
    std::string out = "{";
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i) out += ",";
        out += q(fields[i].first) + ":" + fields[i].second;
    }
    out += "}";
    return out;
}

std::vector<std::string> qs(const std::vector<std::string>& values) {
    std::vector<std::string> out;
    for (const std::string& v : values) out.push_back(q(v));
    return out;
}

// ── shared test data ─────────────────────────────────────────────────────
// Multibyte IPA on purpose: it must survive every join and comparison
// untouched, byte for byte.
const std::vector<std::string> kIpas = {"ˈa.vɛs", "", "duˈis", "ɨɾˈmɐ̃w̃", "ˈkaza", "ˈvɛɾdɨ"};
const std::vector<std::size_t> kSizes = {0, 1, 2, 3, 5};

std::size_t slot_count(std::size_t i) {
    const std::size_t pattern[6] = {0, 2, 1, 3, 0, 2};
    return pattern[i % 6];
}

bool is_pausal_word(std::size_t i, std::size_t total) {
    return i + 1 == total || (total >= 4 && i == 1);
}

// g2p.py G2P._word_positions: phrases are the pausal-bounded runs, so
// phrase_position and pausal stay consistent the way the engine leaves them.
std::vector<Position> phrase_positions(const std::vector<sentence::WordSlot>& words) {
    const std::size_t total = words.size();
    std::vector<Position> phrase(total, Position::MEDIAL);
    std::size_t start = 0;
    for (std::size_t i = 0; i < total; ++i) {
        if (words[i].pausal || i + 1 == total) {
            for (std::size_t j = start; j <= i; ++j)
                phrase[j] = sentence::span_position(static_cast<long long>(j),
                                                    static_cast<long long>(start),
                                                    static_cast<long long>(i + 1));
            start = i + 1;
        }
    }
    return phrase;
}

std::vector<sentence::WordSlot> make_words(std::size_t total) {
    std::vector<sentence::WordSlot> words(total);
    for (std::size_t i = 0; i < total; ++i) {
        sentence::WordSlot word;
        word.surface = "word" + std::to_string(i);
        word.ipa = kIpas[i % kIpas.size()];
        for (std::size_t k = 0; k < slot_count(i); ++k) {
            Candidate candidate{"s" + std::to_string(k), static_cast<double>(k)};
            rescorer::SegmentSlot slot;
            slot.grapheme = word.surface + "[" + std::to_string(k) + "]";
            slot.span = {k, k + 1};
            slot.candidates = {candidate};
            word.slots.push_back(std::move(slot));
        }
        word.index = i;
        word.pausal = is_pausal_word(i, total);
        words[i] = std::move(word);
    }
    const std::vector<Position> phrase = phrase_positions(words);
    for (std::size_t i = 0; i < total; ++i) {
        words[i].phrase_position = phrase[i];
        words[i].utterance_position = sentence::span_position(
            static_cast<long long>(i), 0, static_cast<long long>(total));
    }
    return words;
}

sentence::SentenceLattice make_lattice(std::size_t total) {
    sentence::SentenceLattice lattice;
    lattice.words = make_words(total);
    lattice.lang = "pt-PT";
    return lattice;
}

// ── dummy rescorers ──────────────────────────────────────────────────────
class Named : public sentence::SentenceRescorer {
public:
    explicit Named(std::string name) : name_(std::move(name)) {}
    const std::string& name() const { return name_; }
    static std::string dash(const std::optional<std::string>& value) {
        return value ? *value : std::string("-");
    }

protected:
    std::string name_;
};

/// Returning this_word_ipa() unchanged is the documented no-op.
class Noop : public Named {
public:
    Noop() : Named("noop") {}
    std::string rescore(const sentence::WordSlot&,
                        const sentence::SentenceRescoreContext& context) const override {
        return context.this_word_ipa();
    }
};

/// Records everything the context exposes, so a composition-order change shows.
class Mark : public Named {
public:
    Mark() : Named("mark") {}
    std::string rescore(const sentence::WordSlot&,
                        const sentence::SentenceRescoreContext& context) const override {
        return context.this_word_ipa() + "|" + dash(context.prev_word_ipa()) + "/" +
               dash(context.next_word_ipa()) + "@" +
               sentence::to_string(context.phrase_position()) + "," +
               sentence::to_string(context.utterance_position());
    }
};

class Pausal : public Named {
public:
    Pausal() : Named("pausal") {}
    std::string rescore(const sentence::WordSlot&,
                        const sentence::SentenceRescoreContext& context) const override {
        return context.this_word_ipa() + (context.is_phrase_final() ? "~" : ".");
    }
};

/// A rewrite keyed off the RIGHT neighbour: each word takes the next word's IPA
/// (last word unchanged), so a chain pins both the snapshot rule and the
/// utterance-edge rule.
class StealNext : public Named {
public:
    StealNext() : Named("steal_next") {}
    std::string rescore(const sentence::WordSlot&,
                        const sentence::SentenceRescoreContext& context) const override {
        const std::optional<std::string> next = context.next_word_ipa();
        return next ? *next : context.this_word_ipa();
    }
};

/// Reads the adjacent words' EDGE SLOTS — the cross-word visibility the seam
/// exists for (previous word's coda slot, next word's onset slot).
class EdgeSlot : public Named {
public:
    EdgeSlot() : Named("edge_slot") {}
    std::string rescore(const sentence::WordSlot& word,
                        const sentence::SentenceRescoreContext& context) const override {
        const sentence::WordSlot* prev = context.prev_word();
        const sentence::WordSlot* next = context.next_word();
        const rescorer::SegmentSlot* prev_slot = prev ? prev->final_slot() : nullptr;
        const rescorer::SegmentSlot* next_slot = next ? next->initial_slot() : nullptr;
        return context.this_word_ipa() + "[" +
               (prev_slot && !prev_slot->candidates.empty() ? prev_slot->top().ipa
                                                            : std::string("-")) +
               "->" +
               (next_slot && !next_slot->candidates.empty() ? next_slot->top().ipa
                                                            : std::string("-")) +
               "]#" + word.surface;
    }
};

const sentence::SentenceRescorer* make_rescorer(const std::string& name) {
    static const Noop kNoop;
    static const Mark kMark;
    static const Pausal kPausal;
    static const StealNext kStealNext;
    static const EdgeSlot kEdgeSlot;
    if (name == "noop") return &kNoop;
    if (name == "mark") return &kMark;
    if (name == "pausal") return &kPausal;
    if (name == "steal_next") return &kStealNext;
    if (name == "edge_slot") return &kEdgeSlot;
    return nullptr;   // the reference's non-SentenceRescorer entry
}

const std::vector<std::string> kChainNames = {
    "<none>", "noop", "mark", "pausal", "edge_slot", "steal_next",
    "mark,mark", "noop,pausal,mark", "steal_next,steal_next", "pausal,pausal",
    "edge_slot,mark", "noop,noop,noop"};

std::vector<std::string> chain_of(const std::string& spec) {
    std::vector<std::string> names;
    if (spec == "<none>") return names;
    std::size_t pos = 0;
    while (pos < spec.size()) {
        std::size_t comma = spec.find(',', pos);
        if (comma == std::string::npos) comma = spec.size();
        names.push_back(spec.substr(pos, comma - pos));
        pos = comma + 1;
    }
    return names;
}

std::string slot_json(const rescorer::SegmentSlot* slot) {
    if (slot == nullptr) return "null";
    return obj({{"grapheme", q(slot->grapheme)},
                {"span", arr({num(static_cast<long long>(slot->span.first)),
                              num(static_cast<long long>(slot->span.second))})},
                {"top", slot->candidates.empty() ? std::string("null")
                                                 : q(slot->top().ipa)}});
}

std::string word_json(const sentence::WordSlot& word) {
    return obj({{"surface", q(word.surface)},
                {"ipa", q(word.ipa)},
                {"slots", num(static_cast<long long>(word.slots.size()))},
                {"index", num(static_cast<long long>(word.index))},
                {"phrase_position", q(sentence::to_string(word.phrase_position))},
                {"utterance_position", q(sentence::to_string(word.utterance_position))},
                {"pausal", b(word.pausal)},
                {"initial_slot", slot_json(word.initial_slot())},
                {"final_slot", slot_json(word.final_slot())}});
}

// One row per (index, start, end) triple — same bounds as the reference.
std::string span_cases() {
    std::vector<std::string> rows;
    for (long long start = 0; start < 5; ++start)
        for (long long end = 0; end < 7; ++end)
            for (long long index = -2; index < 7; ++index)
                rows.push_back(arr({num(index), num(start), num(end),
                                    q(sentence::to_string(
                                        sentence::span_position(index, start, end)))}));
    return arr(rows);
}

std::string position_cases() {
    std::vector<std::string> rows;
    for (const Position p : {Position::INITIAL, Position::MEDIAL, Position::FINAL,
                             Position::SOLE})
        rows.push_back(arr({q(sentence::to_string(p)),
                            b(sentence::is_initial(p)), b(sentence::is_final(p))}));
    return arr(rows);
}

std::string lattice_cases() {
    std::vector<std::string> rows;
    for (const std::size_t total : kSizes) {
        const sentence::SentenceLattice lattice = make_lattice(total);
        std::vector<std::string> words;
        for (const auto& word : lattice) words.push_back(word_json(word));
        std::string at;
        for (std::size_t i = 0; i < lattice.size(); ++i) at += lattice.at(i).surface;
        std::vector<std::string> iter;
        for (const auto& word : lattice) iter.push_back(q(word.surface));
        rows.push_back(obj({{"size", num(static_cast<long long>(lattice.size()))},
                            {"ipa", q(lattice.ipa())},
                            {"lang", q(lattice.lang)},
                            {"at", q(at)},
                            {"iter", arr(iter)},
                            {"words", arr(words)}}));
    }
    return arr(rows);
}

/// The three chain snapshots: no snapshot (the reference's empty `_ipas`), a
/// full one, and a short one (an out-of-range read -> IndexError).
std::vector<std::string> snapshot_of(std::size_t variant, std::size_t total) {
    std::vector<std::string> ipas(total, "");
    if (variant == 0) return {};
    for (std::size_t k = 0; k < ipas.size(); ++k)
        ipas[k] = (variant == 1 ? "p" : "q") + std::to_string(k);
    if (variant == 2 && ipas.size() > 2) ipas.resize(2);
    return ipas;
}

std::string context_cases() {
    std::vector<std::string> rows;
    for (const std::size_t total : {std::size_t{1}, std::size_t{2}, std::size_t{3},
                                    std::size_t{5}}) {
        const std::vector<sentence::WordSlot> words = make_words(total);
        for (std::size_t i = 0; i <= total; ++i) {
            for (std::size_t variant = 0; variant < 3; ++variant) {
                const std::vector<std::string> ipas =
                    snapshot_of(variant, total);
                std::string result = "null";
                try {
                    const sentence::SentenceRescoreContext context{
                        &words[i < total ? i : total - 1], i, &words, "pt-PT", ipas};
                    const sentence::WordSlot* prev = context.prev_word();
                    const sentence::WordSlot* next = context.next_word();
                    result = obj({
                        {"size", num(static_cast<long long>(total))},
                        {"index", num(static_cast<long long>(i))},
                        {"snapshot", num(static_cast<long long>(ipas.size()))},
                        {"prev_word", prev ? q(prev->surface) : std::string("null")},
                        {"next_word", next ? q(next->surface) : std::string("null")},
                        {"this_word_ipa", q(context.this_word_ipa())},
                        {"prev_word_ipa", qopt(context.prev_word_ipa())},
                        {"next_word_ipa", qopt(context.next_word_ipa())},
                        {"phrase_position",
                         q(sentence::to_string(context.phrase_position()))},
                        {"utterance_position",
                         q(sentence::to_string(context.utterance_position()))},
                        {"is_phrase_initial", b(context.is_phrase_initial())},
                        {"is_phrase_final", b(context.is_phrase_final())},
                        {"is_utterance_initial", b(context.is_utterance_initial())},
                        {"is_utterance_final", b(context.is_utterance_final())},
                        {"lang", q(context.lang)}});
                } catch (const std::out_of_range&) {
                    result = "\"ERROR\"";
                }
                rows.push_back(std::move(result));
            }
        }
    }
    return arr(rows);
}

std::string word_context_cases() {
    const sentence::WordContext empty;
    sentence::WordContext full;
    full.prev_word_ipa = "ˈɐ̃.tɨ";
    full.next_word_ipa = "a.ˈvɛs";
    full.is_pausal = true;
    full.is_sentence_initial = true;
    full.prev_word = "ante";
    full.next_word = "aves";
    full.is_sentence_final = false;
    full.word_index = 2;
    full.word_count = 5;
    full.lang = "pt-PT";
    auto dump = [](const sentence::WordContext& c) {
        return obj({{"prev_word_ipa", qopt(c.prev_word_ipa)},
                    {"next_word_ipa", qopt(c.next_word_ipa)},
                    {"is_pausal", b(c.is_pausal)},
                    {"is_sentence_initial", b(c.is_sentence_initial)},
                    {"prev_word", qopt(c.prev_word)},
                    {"next_word", qopt(c.next_word)},
                    {"is_sentence_final", b(c.is_sentence_final)},
                    {"word_index", num(static_cast<long long>(c.word_index))},
                    {"word_count", num(static_cast<long long>(c.word_count))},
                    {"lang", qopt(c.lang)}});
    };
    return arr({dump(empty), dump(full)});
}

std::string normalize_cases() {
    const sentence::SentenceRescorer* a = make_rescorer("noop");
    const sentence::SentenceRescorer* c = make_rescorer("mark");
    std::vector<std::string> rows;
    const std::vector<sentence::SentenceRescorers> lists = {
        {}, {}, {a}, {a, c}, {a, a}, {}, {a, nullptr}};
    for (std::size_t i = 0; i < lists.size(); ++i) {
        const bool use_list = i != 0 && i != 1;
        std::string out = "null";
        try {
            const sentence::SentenceRescorers normalized =
                use_list ? sentence::normalize_sentence_rescorers(lists[i])
                         : sentence::normalize_sentence_rescorers(
                               i == 0 ? nullptr : a);
            std::vector<std::string> names;
            for (const sentence::SentenceRescorer* r : normalized)
                names.push_back(q(static_cast<const Named*>(r)->name()));
            out = arr(names);
        } catch (const std::invalid_argument&) {
            out = "\"ERROR\"";
        }
        rows.push_back(std::move(out));
    }
    return arr(rows);
}

std::string apply_cases() {
    std::vector<std::string> rows;
    for (const std::size_t total : kSizes) {
        for (const std::string& spec : kChainNames) {
            const sentence::SentenceLattice lattice = make_lattice(total);
            sentence::SentenceRescorers rescorers;
            std::vector<std::string> names;
            for (const std::string& name : chain_of(spec)) {
                const sentence::SentenceRescorer* r = make_rescorer(name);
                if (r == nullptr) {
                    throw std::runtime_error("unknown rescorer: " + name);
                }
                rescorers.push_back(r);
                names.push_back(name);
            }
            const std::vector<std::string> ipas =
                sentence::apply_sentence_rescorers(lattice, rescorers);
            rows.push_back(obj({{"size", num(static_cast<long long>(total))},
                                {"chain", arr(qs(names))},
                                {"result", arr(qs(ipas))},
                                {"lattice_untouched",
                                 b(lattice.ipa() == make_lattice(total).ipa())}}));
        }
    }
    return arr(rows);
}

} // namespace

int main() {
    std::cout << "{"
              << "\"span\":" << span_cases() << ","
              << "\"position\":" << position_cases() << ","
              << "\"lattice\":" << lattice_cases() << ","
              << "\"context\":" << context_cases() << ","
              << "\"wordcontext\":" << word_context_cases() << ","
              << "\"normalize\":" << normalize_cases() << ","
              << "\"apply\":" << apply_cases()
              << "}\n";
    return 0;
}
