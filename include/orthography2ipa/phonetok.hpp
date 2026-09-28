// Direct port of the Unicode token model in orthography2ipa/phonetok.py:
// TokenKind/Token, the Unicode-aware grapheme trie (maximal munch),
// language-aware casing, the script-scoped pre-tokenization
// normalizations (Arabic presentation forms and shadda gemination),
// the abugida / virama / dependent-vowel / preposed-vowel
// token-expansion behaviors, word-local grapheme contexts
// (GraphemeContext / TokenSequence / flat_contexts) and the
// pause-punctuation table. Positions and lengths are counted in Unicode
// code points, exactly as Python string indices are.
#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace orthography2ipa {

struct LanguageSpec;

enum class TokenKind { GRAPHEME, WHITESPACE, PUNCTUATION, DIGIT, UNKNOWN, BOS, EOS };

const char* to_string(TokenKind kind);

struct Token {
    TokenKind kind = TokenKind::UNKNOWN;
    /// The grapheme key this token matched (lower-cased for GRAPHEME
    /// tokens; original case for others). This is the table key, not
    /// necessarily the full input span: an abugida consonant followed by
    /// a virama matches the bare consonant key while consuming the
    /// virama too — `length` is authoritative, `grapheme.size()` is not.
    std::string grapheme;
    /// Possible IPA values for this token. Empty for non-GRAPHEME tokens.
    std::vector<std::string> ipa;
    /// Code-point offset into the original input string.
    std::size_t position = 0;
    /// Number of code points consumed from the input.
    std::size_t length = 0;

    /// The characters this token consumed from *text* (UTF-8). *text*
    /// must be the same (normalized) string the token was produced from,
    /// since `position` indexes into it by code point.
    std::string text_span(const std::string& text) const;
};

/// A context-aware view over one GRAPHEME Token — phonetok.py
/// GraphemeContext. Neighbours are word-local: the nearest GRAPHEME
/// tokens within the same run, never crossing a non-grapheme token.
/// Instances are flyweights created by PhonetokTokenizer::
/// tokenize_with_context / flat_contexts and hold back-references into
/// their owning TokenSequence; a TokenSequence must not be mutated after
/// construction (moving it is safe: the context vectors live on the
/// heap and keep their addresses).
struct TokenSequence;

struct GraphemeContext {
public:
    const Token& token() const;
    /// The surface grapheme string (lower-cased, as tokenised).
    const std::string& grapheme() const;
    /// Possible IPA values for this grapheme (from the token).
    const std::vector<std::string>& ipa() const;
    /// (start, end) code-point offsets locating this grapheme in the
    /// NFC-normalized, case-folded input: NFC(text)[start:end].lower()
    /// == grapheme.
    std::pair<std::size_t, std::size_t> span() const;
    /// Zero-based position among all GRAPHEME tokens in the sequence.
    std::size_t index() const { return index_; }

    /// The grapheme `offset` positions away within the same word, or
    /// nullptr when the offset falls outside the current run.
    const GraphemeContext* at(int offset) const;
    const GraphemeContext* prev() const { return at(-1); }
    const GraphemeContext* next() const { return at(1); }
    /// Up to 2*n neighbours within ±n, ordered left-to-right, self
    /// excluded; offsets past a word edge are omitted.
    std::vector<const GraphemeContext*> neighbors(int n = 1) const;

    bool is_vowel() const;
    bool is_consonant() const;
    bool is_front() const;
    bool is_back() const;
    bool is_palatal() const;
    bool is_emphatic() const;

private:
    friend struct TokenSequence;
    friend class PhonetokTokenizer;
    friend TokenSequence flat_contexts(const std::vector<Token>& g_tokens,
                                       const std::set<std::string>& vowel_graphemes);
    const std::vector<Token>* tokens_ = nullptr;
    std::size_t token_index_ = 0;
    std::size_t index_ = 0;
    const std::vector<GraphemeContext>* run_ = nullptr;
    std::size_t run_start_ = 0;  // global index of the run's first context
    std::size_t run_pos_ = 0;
    std::size_t run_size_ = 0;
    std::shared_ptr<const std::set<std::string>> vowel_graphemes_;
};

struct TokenSequence {
    /// The complete token list, exactly as tokenize() produced it (empty
    /// for a sequence built by flat_contexts over bare grapheme tokens).
    std::unique_ptr<std::vector<Token>> tokens = std::make_unique<std::vector<Token>>();
    /// All GraphemeContext views, in order; each context's word-local
    /// run is the contiguous range of contexts it was built with.
    std::unique_ptr<std::vector<GraphemeContext>> graphemes =
        std::make_unique<std::vector<GraphemeContext>>();

    TokenSequence() = default;
    TokenSequence(TokenSequence&&) noexcept = default;
    TokenSequence& operator=(TokenSequence&&) noexcept = default;
    TokenSequence(const TokenSequence&) = delete;
    TokenSequence& operator=(const TokenSequence&) = delete;

    std::size_t size() const { return graphemes->size(); }
    const GraphemeContext* at(std::size_t i) const {
        return i < graphemes->size() ? &(*graphemes)[i] : nullptr;
    }
};

/// Wrap a flat list of GRAPHEME tokens as one contiguous neighbour run —
/// phonetok.py flat_contexts. The engine calls this after word-splitting
/// has already stripped whitespace/punctuation, so the per-word grapheme
/// tokens (including any that flank an in-word UNKNOWN character) stay
/// adjacent.
TokenSequence flat_contexts(const std::vector<Token>& g_tokens,
                            const std::set<std::string>& vowel_graphemes = {});

/// Punctuation that writes a PAUSE, across every writing system —
/// phonetok.py PAUSE_PUNCTUATION: character → Unicode name.
const std::vector<std::pair<std::string, std::string>>& pause_punctuation();

/// Is *text* at `pos` a pause mark? (PAUSE_PUNCTUATION membership.)
bool is_pause_punctuation(const std::string& character);

/// Language-aware lowercasing for a whole string — phonetok.py
/// lower_str. Turkish dotted/dotless I is handled via explicit mapping;
/// every other language uses plain full Unicode lowercasing.
std::string lower_str(const std::string& text, const std::string& lang);

/// One whole-word beam path — phonetok.py IPAPath. `ipa` is the joined
/// segments (the reference exposes it as a computed property; C++
/// materializes it), `graphemes` the spelled grapheme keys in order.
struct IPAPath {
    std::string ipa;
    double score = 0.0;
    std::vector<std::string> graphemes;
    std::vector<std::string> segments;
};

/// The Unicode token model — phonetok.py PhonetokTokenizer. All
/// linguistic knowledge comes from the LanguageSpec; the algorithm is
/// language-agnostic (maximal munch over the spec's grapheme table).
class PhonetokTokenizer {
public:
    PhonetokTokenizer(const LanguageSpec& spec, bool add_bos = false, bool add_eos = false,
                      bool collapse_whitespace = true);
    ~PhonetokTokenizer();

    /// Tokenize *text* into a list of Tokens: NFC normalization,
    /// declared diacritic folding, Arabic-script pre-tokenization
    /// normalization, then a left-to-right scan trying whitespace,
    /// punctuation (unless a grapheme claims it), digits (same escape),
    /// longest trie grapheme match (with the abugida / preposed-vowel
    /// expansion behaviors), canonical-decomposition fallback and
    /// single unknown character — in that exact order.
    std::vector<Token> tokenize(const std::string& text) const;

    /// Just the grapheme strings (all token types).
    std::vector<std::string> graphemes(const std::string& text) const;
    /// Only GRAPHEME-kind tokens (skip whitespace, punct, etc.).
    std::vector<Token> grapheme_tokens(const std::string& text) const;
    /// Tokenise and return a context-aware TokenSequence.
    TokenSequence tokenize_with_context(const std::string& text) const;

    /// Per-candidate weights for *grapheme* (lookup key lower-cased), or
    /// nullopt when the grapheme declares no weights (rank cost).
    std::optional<std::vector<double>> weights_for(const std::string& grapheme) const;

    /// phonetok.py ipa_beam: beam search over the token stream —
    /// per-slot branch resolution (positional overrides, weights,
    /// allophone expansion), the nasal-carrier guard, expansion and
    /// pruning after every grapheme. Returns the paths within
    /// *beam_width*, sorted (score, ipa). When *include_special* is set,
    /// whitespace becomes `word_separator` and punctuation/digit/unknown
    /// tokens pass through as cost-0 segments spelling themselves.
    std::vector<IPAPath> ipa_beam(const std::string& text,
                                  std::size_t beam_width = 8,
                                  bool expand_allophones = false,
                                  const std::string& word_separator = " ",
                                  bool include_special = false) const;

    static constexpr const char* BOS_STR = "<bos>";
    static constexpr const char* EOS_STR = "<eos>";
    static constexpr const char* UNK_STR = "<unk>";
    static constexpr const char* WS_STR = " ";
    static constexpr const char* PUNCT_STR = "<punct>";
    static constexpr const char* DIGIT_STR = "<digit>";

private:
    struct TrieNode;
    struct GraphemeTrie;

    const LanguageSpec& spec_;
    std::map<std::u32string, std::vector<std::u32string>> grapheme_ipa_;
    std::map<std::u32string, std::vector<double>> grapheme_weights_;
    std::set<std::u32string> fold_diacritics_;
    bool add_bos_ = false;
    bool add_eos_ = false;
    bool collapse_whitespace_ = true;
    std::unique_ptr<GraphemeTrie> trie_;
    std::set<std::u32string> dependent_vowels_;
    std::vector<std::size_t> dependent_vowel_spans_;
    std::set<std::u32string> preposed_vowels_;
    bool coda_no_inherent_vowel_ = false;
    std::optional<std::u32string> inherent_vowel_final_;
    std::shared_ptr<const std::set<std::string>> vowel_graphemes_;

    // Internal token used while tokenize() is building the stream: the
    // abugida lookahead predicates read graphemes/IPA of earlier tokens.
    struct UToken {
        TokenKind kind = TokenKind::UNKNOWN;
        std::u32string grapheme;
        std::vector<std::u32string> ipa;
        std::size_t position = 0;
        std::size_t length = 0;
    };

    const std::vector<std::u32string>* ipa_for(const std::u32string& key) const;
    std::optional<std::vector<std::u32string>> positional_u32(const std::u32string& gkey,
                                                              const char* position) const;

    bool supplies_vowel_at(const std::u32string& text, std::size_t pos) const;
    bool spells_nothing(const std::u32string& ch) const;
    std::pair<std::optional<std::u32string>, std::size_t> match_transparent(
        const std::u32string& text, std::size_t start) const;
    std::optional<std::u32string> silent_marker_head(const std::u32string& gkey) const;
    bool marker_head_is_the_onset(const std::u32string& gkey, const std::u32string& ckey,
                                  const std::u32string& text, std::size_t after) const;
    bool supplies_vowel(char32_t ch) const;
    bool declares_postvocalic_reading(const std::u32string& gkey) const;
    bool syllable_has_nucleus(const std::vector<UToken>& tokens) const;
    bool prev_gives_nucleus(const UToken* prev_tok) const;
    bool silenced_before_consonant(const std::u32string& gkey) const;

    std::vector<UToken> tokenize_u32(const std::u32string& text) const;
};

} // namespace orthography2ipa
