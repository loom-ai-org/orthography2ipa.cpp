// Direct port of the Unicode token model in orthography2ipa/phonetok.py.
// Every set, predicate and branch mirrors the reference exactly; the
// Python docstrings carry the linguistic rationale. All internals work on
// std::u32string (Python code points); Token positions/lengths count
// code points in the normalized input, exactly like Python indices.
#include "orthography2ipa/phonetok.hpp"

#include "beam.hpp"
#include "orthography2ipa/orthography2ipa.hpp"
#include "orthography2ipa/vowels.hpp"
#include "unicode_util.hpp"

#include <algorithm>
#include <cstdint>

namespace orthography2ipa {
namespace {

using vowels::is_ipa_vowel;

std::u32string u32(std::string_view utf8) { return uni::to_utf32(utf8); }
std::string utf8(std::u32string_view text) { return uni::to_utf8(text); }

std::vector<std::u32string> u32vec(const std::vector<std::string>& values) {
    std::vector<std::u32string> out;
    out.reserve(values.size());
    for (const auto& v : values) out.push_back(u32(v));
    return out;
}

// ═══════════════════════════════════════════════════════════════════════
// Locale-aware casing (phonetok.py _TR_LOWER_MAP / _lower)
// ═══════════════════════════════════════════════════════════════════════

bool is_turkish(const std::string& lang) {
    return lang == "tr" || lang.rfind("tr-", 0) == 0;
}

// Language-aware lowercasing for a single character (phonetok.py _lower).
std::u32string lower_char(char32_t ch, const std::string& lang) {
    if (!lang.empty() && is_turkish(lang)) {
        if (ch == U'I') return std::u32string(1, U'\u0131');  // ı
        if (ch == U'\u0130') return std::u32string(1, U'i');  // İ → i
    }
    return uni::lower_one(ch);
}

// ═══════════════════════════════════════════════════════════════════════
// Mark / nucleus predicates (phonetok.py module level)
// ═══════════════════════════════════════════════════════════════════════

// phonetok.py _is_a_mark: a combining mark rides the letter it is on.
bool is_a_mark(char32_t ch) { return uni::combining(ch) != 0 || uni::category(ch) == "Mn"; }

// phonetok.py _is_virama: ccc == 9, the Brahmic virama/halant class.
bool is_virama_char(char32_t ch) { return uni::combining(ch) == 9; }

bool is_syllabic_mark(char32_t ch) { return ch == U'\u0329' || ch == U'\u030D'; }

// phonetok.py _is_nucleus: a vowel or a syllabic consonant.
bool is_nucleus_ipa(const std::u32string& ipa) {
    return std::any_of(ipa.begin(), ipa.end(), [](char32_t c) { return is_ipa_vowel(c); }) ||
           std::any_of(ipa.begin(), ipa.end(), is_syllabic_mark);
}

// phonetok.py _ends_in_a_vowel: ends on a vowel, ignoring length/tone
// diacritics.
bool ends_in_a_vowel(const std::u32string& ipa) {
    for (auto it = ipa.rbegin(); it != ipa.rend(); ++it) {
        const std::string cat = uni::category(*it);
        if (cat == "Mn" || cat == "Lm" || *it == U'\u02D0' || *it == U'\u02D1') continue;
        return is_ipa_vowel(*it);
    }
    return false;
}

// Combining marks that move their base letter between the consonant
// REGISTERS of a two-series abugida (phonetok.py _REGISTER_SHIFTER_NAMES).
bool is_register_shifter_name(const std::string& name) {
    return name == "KHMER SIGN MUUSIKATOAN" || name == "KHMER SIGN TRIISAP";
}

// phonetok.py _is_subjoined_letter_cluster: a base letter followed by
// ONSET CONSONANT marks (subjoined letters, medials, register shifters),
// decided from the Unicode NAME rather than a codepoint range.
bool is_subjoined_letter_cluster(const std::u32string& grapheme) {
    if (grapheme.size() < 2) return false;
    const std::string head_cat = uni::category(grapheme[0]);
    if (head_cat == "Mn" || head_cat == "Mc") return false;
    for (std::size_t i = 1; i < grapheme.size(); ++i) {
        const std::string name = uni::char_name(grapheme[i]);
        const bool ok = name.find("SUBJOINED LETTER") != std::string::npos ||
                        name.find("CONSONANT SIGN MEDIAL") != std::string::npos ||
                        is_register_shifter_name(name);
        if (!ok) return false;
    }
    return true;
}

// ═══════════════════════════════════════════════════════════════════════
// Arabic-script pre-tokenization normalization (phonetok.py)
// ═══════════════════════════════════════════════════════════════════════

constexpr char32_t AR_SHADDA = U'\u0651';

// phonetok.py _arabic_script_letters: every Lo letter in the Arabic
// blocks the specs draw on (category-selected, so the Perso-Arabic
// consonants are included; marks, digits and modifier letters are not).
const std::set<char32_t>& ar_letters() {
    static const std::set<char32_t> letters = [] {
        std::set<char32_t> out;
        const std::pair<char32_t, char32_t> blocks[] = {
            {0x0600, 0x06FF},  // Arabic
            {0x0750, 0x077F},  // Arabic Supplement
            {0x0870, 0x089F},  // Arabic Extended-B
            {0x08A0, 0x08FF},  // Arabic Extended-A
        };
        for (const auto& [lo, hi] : blocks)
            for (char32_t cp = lo; cp <= hi; ++cp)
                if (uni::category(cp) == "Lo") out.insert(cp);
        return out;
    }();
    return letters;
}

// phonetok.py _AR_HARAKAT: short-vowel / nunation / sukun / superscript
// alef marks (the class "ً-ِْٰ" = U+064B..U+0650 plus U+0652 and U+0670).
bool ar_is_harakat(char32_t cp) {
    return (cp >= 0x064B && cp <= 0x0650) || cp == 0x0652 || cp == 0x0670;
}

// phonetok.py _decompose_arabic_presentation_forms: NFKC-decompose only
// Presentation-Form codepoints; every other codepoint is untouched.
std::u32string decompose_arabic_presentation_forms(const std::u32string& text) {
    const auto is_presentation_form = [](char32_t cp) {
        return (cp >= 0xFB50 && cp <= 0xFDFF) || (cp >= 0xFE70 && cp <= 0xFEFF);
    };
    if (std::none_of(text.begin(), text.end(), is_presentation_form)) return text;
    std::u32string out;
    out.reserve(text.size());
    for (char32_t ch : text) {
        if (is_presentation_form(ch))
            out += uni::nfkc(std::u32string(1, ch));
        else
            out.push_back(ch);
    }
    return out;
}

// phonetok.py _expand_arabic_gemination: double the carrying consonant,
// drop the shadda. Handles both mark orderings, then any remaining bare
// shadda — each as a leftmost non-overlapping substitution pass, exactly
// like the three re.sub calls in order.
std::u32string expand_arabic_gemination(const std::u32string& text) {
    if (text.find(AR_SHADDA) == std::u32string::npos) return text;
    // Each pass is a leftmost non-overlapping substitution, exactly like
    // the three re.sub calls in the reference, applied in order.
    // consonant + shadda + harakat  →  consonant consonant harakat
    std::u32string text1;
    {
        std::size_t i = 0;
        while (i < text.size()) {
            if (i + 3 <= text.size() && ar_letters().count(text[i]) && text[i + 1] == AR_SHADDA &&
                ar_is_harakat(text[i + 2])) {
                text1.push_back(text[i]);
                text1.push_back(text[i]);
                text1.push_back(text[i + 2]);
                i += 3;
            } else {
                text1.push_back(text[i]);
                ++i;
            }
        }
    }
    // consonant + harakat + shadda  →  consonant consonant harakat
    std::u32string text2;
    {
        std::size_t i = 0;
        while (i < text1.size()) {
            if (i + 3 <= text1.size() && ar_letters().count(text1[i]) &&
                ar_is_harakat(text1[i + 1]) && text1[i + 2] == AR_SHADDA) {
                text2.push_back(text1[i]);
                text2.push_back(text1[i]);
                text2.push_back(text1[i + 1]);
                i += 3;
            } else {
                text2.push_back(text1[i]);
                ++i;
            }
        }
    }
    // consonant + shadda (no adjacent harakat)  →  consonant consonant
    std::u32string text3;
    {
        std::size_t i = 0;
        while (i < text2.size()) {
            if (i + 2 <= text2.size() && ar_letters().count(text2[i]) &&
                text2[i + 1] == AR_SHADDA) {
                text3.push_back(text2[i]);
                text3.push_back(text2[i]);
                i += 2;
            } else {
                text3.push_back(text2[i]);
                ++i;
            }
        }
    }
    return text3;
}

// ═══════════════════════════════════════════════════════════════════════
// Punctuation / digit / whitespace character classes (phonetok.py regexes)
// ═══════════════════════════════════════════════════════════════════════

struct PauseEntry {
    char32_t cp;
    const char* name;
};

// phonetok.py PAUSE_PUNCTUATION — every entry named, in module order.
const std::vector<PauseEntry>& pause_entries() {
    static const std::vector<PauseEntry> entries = {
        {U'.', "FULL STOP"},
        {U',', "COMMA"},
        {U';', "SEMICOLON"},
        {U':', "COLON"},
        {U'!', "EXCLAMATION MARK"},
        {U'?', "QUESTION MARK"},
        {U'\u2026', "HORIZONTAL ELLIPSIS"},
        {U'\u037E', "GREEK QUESTION MARK"},
        {U'\u2E2E', "REVERSED QUESTION MARK"},
        {U'\u0589', "ARMENIAN FULL STOP"},
        {U'\u05C3', "HEBREW PUNCTUATION SOF PASUQ"},
        {U'\u060C', "ARABIC COMMA"},
        {U'\u061B', "ARABIC SEMICOLON"},
        {U'\u061E', "ARABIC TRIPLE DOT PUNCTUATION MARK"},
        {U'\u061F', "ARABIC QUESTION MARK"},
        {U'\u06D4', "ARABIC FULL STOP"},
        {U'\u0700', "SYRIAC END OF PARAGRAPH"},
        {U'\u0701', "SYRIAC SUPRALINEAR FULL STOP"},
        {U'\u0702', "SYRIAC SUBLINEAR FULL STOP"},
        {U'\u0703', "SYRIAC SUPRALINEAR COLON"},
        {U'\u0704', "SYRIAC SUBLINEAR COLON"},
        {U'\u07F8', "NKO COMMA"},
        {U'\u07F9', "NKO EXCLAMATION MARK"},
        {U'\u0964', "DEVANAGARI DANDA"},
        {U'\u0965', "DEVANAGARI DOUBLE DANDA"},
        {U'\u104A', "MYANMAR SIGN LITTLE SECTION"},
        {U'\u104B', "MYANMAR SIGN SECTION"},
        {U'\u17D4', "KHMER SIGN KHAN"},
        {U'\u17D5', "KHMER SIGN BARIYOOSAN"},
        {U'\u1944', "LIMBU EXCLAMATION MARK"},
        {U'\u1945', "LIMBU QUESTION MARK"},
        {U'\u1C7E', "OL CHIKI PUNCTUATION MUCAAD"},
        {U'\u1C7F', "OL CHIKI PUNCTUATION DOUBLE MUCAAD"},
        {U'\uA9C7', "JAVANESE PADA PANGKAT"},
        {U'\uA9C8', "JAVANESE PADA LINGSA"},
        {U'\uA9C9', "JAVANESE PADA LUNGSI"},
        {U'\uAA5D', "CHAM PUNCTUATION DANDA"},
        {U'\uAA5E', "CHAM PUNCTUATION DOUBLE DANDA"},
        {U'\uAA5F', "CHAM PUNCTUATION TRIPLE DANDA"},
        {U'\uABEB', "MEETEI MAYEK CHEIKHEI"},
        {U'\u1362', "ETHIOPIC FULL STOP"},
        {U'\u1363', "ETHIOPIC COMMA"},
        {U'\u1364', "ETHIOPIC SEMICOLON"},
        {U'\u1365', "ETHIOPIC COLON"},
        {U'\u1367', "ETHIOPIC QUESTION MARK"},
        {U'\u166E', "CANADIAN SYLLABICS FULL STOP"},
        {U'\u1802', "MONGOLIAN COMMA"},
        {U'\u1803', "MONGOLIAN FULL STOP"},
        {U'\uA4FE', "LISU PUNCTUATION COMMA"},
        {U'\uA4FF', "LISU PUNCTUATION FULL STOP"},
        {U'\uA60D', "VAI COMMA"},
        {U'\uA60E', "VAI FULL STOP"},
        {U'\uA60F', "VAI QUESTION MARK"},
        {U'\u3001', "IDEOGRAPHIC COMMA"},
        {U'\u3002', "IDEOGRAPHIC FULL STOP"},
        {U'\uFF01', "FULLWIDTH EXCLAMATION MARK"},
        {U'\uFF0C', "FULLWIDTH COMMA"},
        {U'\uFF0E', "FULLWIDTH FULL STOP"},
        {U'\uFF1A', "FULLWIDTH COLON"},
        {U'\uFF1B', "FULLWIDTH SEMICOLON"},
        {U'\uFF1F', "FULLWIDTH QUESTION MARK"},
        {U'\uFF61', "HALFWIDTH IDEOGRAPHIC FULL STOP"},
        {U'\uFF64', "HALFWIDTH IDEOGRAPHIC COMMA"},
    };
    return entries;
}

const std::set<char32_t>& pause_codepoints() {
    static const std::set<char32_t> cps = [] {
        std::set<char32_t> out;
        for (const auto& entry : pause_entries()) out.insert(entry.cp);
        return out;
    }();
    return cps;
}

// phonetok.py _PUNCT_RE character class: the pause marks spliced in plus
// the broad Latin/CJK/typographic ranges.
bool is_punct_class(char32_t cp) {
    if (pause_codepoints().count(cp)) return true;
    return (cp >= 0x0021 && cp <= 0x002F) ||   // ! " # $ % & ' ( ) * + , - . /
           (cp >= 0x003A && cp <= 0x0040) ||   // : ; < = > ? @
           (cp >= 0x005B && cp <= 0x0060) ||   // [ \ ] ^ _ `
           (cp >= 0x007B && cp <= 0x007E) ||   // { | } ~
           (cp >= 0x00A1 && cp <= 0x00BF) ||   // ¡ ¢ … ¿
           (cp >= 0x2010 && cp <= 0x2027) ||   // ‐ – — ― … ‧
           (cp >= 0x2030 && cp <= 0x205E) ||   // ‰ ′ ″ …
           (cp >= 0x3001 && cp <= 0x3003) ||   // 、。〃 (CJK)
           (cp >= 0xFF01 && cp <= 0xFF0F) ||   // ！ … ／ (fullwidth)
           (cp >= 0xFF1A && cp <= 0xFF20) ||   // ： … ＠
           (cp >= 0xFF3B && cp <= 0xFF40) ||   // ［ … ｀
           (cp >= 0xFF5B && cp <= 0xFF65) ||   // ｛ … ･
           cp == 0x00AB || cp == 0x00BB ||     // « »
           (cp >= 0x2018 && cp <= 0x201F) ||   // ' ' ‚ ‛ " " „ ‟
           cp == 0x2039 || cp == 0x203A;       // ‹ ›
}

// phonetok.py _DIGIT_RE: ASCII, Arabic-Indic, Extended Arabic-Indic and
// Devanagari digit runs.
bool is_digit_class(char32_t cp) {
    return (cp >= 0x0030 && cp <= 0x0039) || (cp >= 0x0660 && cp <= 0x0669) ||
           (cp >= 0x06F0 && cp <= 0x06F9) || (cp >= 0x0966 && cp <= 0x096F);
}

// phonetok.py _WS_RE: Python's \s for str patterns — the exact
// str.isspace() set (includes U+001C..U+001F, which are not Unicode
// White_Space).
bool is_ws_class(char32_t cp) {
    return (cp >= 0x0009 && cp <= 0x000D) || (cp >= 0x001C && cp <= 0x001F) || cp == 0x0020 ||
           cp == 0x0085 || cp == 0x00A0 || cp == 0x1680 ||
           (cp >= 0x2000 && cp <= 0x200A) || cp == 0x2028 || cp == 0x2029 || cp == 0x202F ||
           cp == 0x205F || cp == 0x3000;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════
// Token basics
// ═══════════════════════════════════════════════════════════════════════

const char* to_string(TokenKind kind) {
    switch (kind) {
        case TokenKind::GRAPHEME: return "GRAPHEME";
        case TokenKind::WHITESPACE: return "WHITESPACE";
        case TokenKind::PUNCTUATION: return "PUNCTUATION";
        case TokenKind::DIGIT: return "DIGIT";
        case TokenKind::UNKNOWN: return "UNKNOWN";
        case TokenKind::BOS: return "BOS";
        case TokenKind::EOS: return "EOS";
    }
    return "UNKNOWN";
}

std::string Token::text_span(const std::string& text) const {
    const std::u32string units = uni::to_utf32(text);
    if (position >= units.size()) return "";
    const std::size_t end = std::min(position + length, units.size());
    return uni::to_utf8(units.substr(position, end - position));
}

const std::vector<std::pair<std::string, std::string>>& pause_punctuation() {
    static const std::vector<std::pair<std::string, std::string>> table = [] {
        std::vector<std::pair<std::string, std::string>> out;
        for (const auto& entry : pause_entries())
            out.emplace_back(uni::to_utf8(std::u32string(1, entry.cp)), entry.name);
        return out;
    }();
    return table;
}

bool is_pause_punctuation(const std::string& character) {
    const std::u32string units = uni::to_utf32(character);
    return units.size() == 1 && pause_codepoints().count(units[0]) != 0;
}

std::string lower_str(const std::string& text, const std::string& lang) {
    const std::u32string units = uni::to_utf32(text);
    if (!lang.empty() && is_turkish(lang)) {
        std::u32string out;
        out.reserve(units.size());
        for (char32_t ch : units) out += lower_char(ch, lang);
        return uni::to_utf8(out);
    }
    return uni::to_utf8(uni::lower(units));
}

// ═══════════════════════════════════════════════════════════════════════
// GraphemeContext / TokenSequence
// ═══════════════════════════════════════════════════════════════════════

const Token& GraphemeContext::token() const { return (*tokens_)[token_index_]; }
const std::string& GraphemeContext::grapheme() const { return token().grapheme; }
const std::vector<std::string>& GraphemeContext::ipa() const { return token().ipa; }

std::pair<std::size_t, std::size_t> GraphemeContext::span() const {
    return {token().position, token().position + token().length};
}

const GraphemeContext* GraphemeContext::at(int offset) const {
    // run_pos_ is the position WITHIN the word-local run; translate to a
    // global index via run_start_ so multi-word sequences stay word-local.
    const std::int64_t j = static_cast<std::int64_t>(run_start_ + run_pos_) + offset;
    if (j < static_cast<std::int64_t>(run_start_) ||
        j >= static_cast<std::int64_t>(run_start_ + run_size_))
        return nullptr;
    return &(*run_)[static_cast<std::size_t>(j)];
}

std::vector<const GraphemeContext*> GraphemeContext::neighbors(int n) const {
    std::vector<const GraphemeContext*> out;
    if (n < 1) return out;
    for (int k = -n; k <= n; ++k) {
        if (k == 0) continue;
        if (const GraphemeContext* ctx = at(k)) out.push_back(ctx);
    }
    return out;
}

bool GraphemeContext::is_vowel() const {
    return vowels::grapheme_is_vowel(grapheme(), ipa(), *vowel_graphemes_);
}
bool GraphemeContext::is_consonant() const { return !grapheme().empty() && !is_vowel(); }
bool GraphemeContext::is_front() const {
    return vowels::grapheme_vowel_axis(grapheme(), ipa(), *vowel_graphemes_) == "front";
}
bool GraphemeContext::is_back() const {
    return vowels::grapheme_vowel_axis(grapheme(), ipa(), *vowel_graphemes_) == "back";
}
bool GraphemeContext::is_palatal() const {
    return !ipa().empty() && vowels::is_palatal_consonant(ipa()[0]);
}
bool GraphemeContext::is_emphatic() const {
    return !ipa().empty() && vowels::is_pharyngealized_consonant(ipa()[0]);
}

TokenSequence flat_contexts(const std::vector<Token>& g_tokens,
                             const std::set<std::string>& vowel_graphemes) {
    TokenSequence seq;
    *seq.tokens = g_tokens;
    auto overrides = std::make_shared<const std::set<std::string>>(vowel_graphemes);
    std::vector<GraphemeContext>& contexts = *seq.graphemes;
    contexts.reserve(g_tokens.size());
    for (std::size_t i = 0; i < g_tokens.size(); ++i) {
        GraphemeContext ctx;
        ctx.tokens_ = seq.tokens.get();
        ctx.token_index_ = i;
        ctx.index_ = i;
        ctx.run_ = seq.graphemes.get();
        ctx.run_pos_ = i;
        ctx.run_size_ = g_tokens.size();
        ctx.vowel_graphemes_ = overrides;
        contexts.push_back(ctx);
    }
    return seq;
}

// ═══════════════════════════════════════════════════════════════════════
// PhonetokTokenizer
// ═══════════════════════════════════════════════════════════════════════

struct PhonetokTokenizer::TrieNode {
    std::map<char32_t, std::unique_ptr<TrieNode>> children;
    std::optional<std::u32string> key;  // set at leaf
};

// phonetok.py _GraphemeTrie: prefix trie over lowercased grapheme keys,
// supporting case-insensitive longest-match lookup.
struct PhonetokTokenizer::GraphemeTrie {
    std::unique_ptr<TrieNode> root = std::make_unique<TrieNode>();
    std::size_t max_len = 0;
    std::string lang;

    void build(const std::map<std::u32string, std::vector<std::u32string>>& graphemes,
               const std::string& language) {
        lang = language;
        for (const auto& [key, value] : graphemes) {
            const std::u32string lk = uni::lower(key);
            TrieNode* node = root.get();
            for (char32_t ch : lk) {
                auto it = node->children.find(ch);
                if (it == node->children.end())
                    it = node->children.emplace(ch, std::make_unique<TrieNode>()).first;
                node = it->second.get();
            }
            node->key = lk;
            max_len = std::max(max_len, lk.size());
        }
    }

    // Return the longest grapheme key matching at *start*, or nullopt.
    // A character whose language-aware lowering expands to several code
    // points (İ outside Turkish) never matches a child, exactly as a
    // multi-character string is never a child key in the Python trie.
    std::optional<std::u32string> longest_match(const std::u32string& text,
                                                std::size_t start) const {
        const TrieNode* node = root.get();
        std::optional<std::u32string> best;
        const std::size_t limit = std::min(start + max_len, text.size());
        for (std::size_t i = start; i < limit; ++i) {
            const std::u32string ch = lower_char(text[i], lang);
            if (ch.size() != 1) break;
            const auto it = node->children.find(ch[0]);
            if (it == node->children.end()) break;
            node = it->second.get();
            if (node->key) best = node->key;
        }
        return best;
    }
};

PhonetokTokenizer::~PhonetokTokenizer() = default;

PhonetokTokenizer::PhonetokTokenizer(const LanguageSpec& spec, bool add_bos, bool add_eos,
                                     bool collapse_whitespace)
    : spec_(spec),
      add_bos_(add_bos),
      add_eos_(add_eos),
      collapse_whitespace_(collapse_whitespace) {
    // Normalised lookup (lowercase keys — Python lowercases the map keys
    // themselves, not just the trie) from the base grapheme table.
    for (const auto& [key, values] : spec.graphemes)
        grapheme_ipa_[uni::lower(u32(key))] = u32vec(values);

    // Per-candidate weights aligned to grapheme_ipa_ (lowercased keys);
    // sparse — absent → weights_for returns nullopt → rank cost.
    for (const auto& [key, weights] : spec.grapheme_weights)
        grapheme_weights_[uni::lower(u32(key))] = weights;

    // Grapheme keys that exist only as positional overrides must still be
    // discoverable by the maximal-munch trie; seed a fallback IPA value
    // from their positional candidates (DEFAULT position preferred, else
    // the first declared position).
    for (const auto& [grapheme, pos_map] : spec.positional_graphemes) {
        if (pos_map.empty()) continue;
        const std::u32string key = uni::lower(u32(grapheme));
        if (grapheme_ipa_.count(key)) continue;
        const std::vector<std::string>* candidates = nullptr;
        const auto def = pos_map.find("default");
        if (def != pos_map.end()) candidates = &def->second;
        if (candidates == nullptr) candidates = &pos_map.begin()->second;
        grapheme_ipa_[key] = u32vec(*candidates);
    }

    trie_ = std::make_unique<GraphemeTrie>();
    trie_->build(grapheme_ipa_, spec.code);

    for (const auto& mark : spec.fold_diacritics) fold_diacritics_.insert(u32(mark));
    for (const auto& vowel : spec.dependent_vowels) dependent_vowels_.insert(u32(vowel));
    // Declared dependent-vowel graphemes longer than one character, by
    // descending length — the lengths supplies_vowel_at must try first.
    std::set<std::size_t, std::greater<std::size_t>> spans;
    for (const auto& vowel : dependent_vowels_)
        if (vowel.size() > 1) spans.insert(vowel.size());
    dependent_vowel_spans_.assign(spans.begin(), spans.end());
    for (const auto& vowel : spec.preposed_vowels) preposed_vowels_.insert(uni::lower(u32(vowel)));
    coda_no_inherent_vowel_ = spec.coda_no_inherent_vowel;
    if (spec.inherent_vowel_final) inherent_vowel_final_ = u32(*spec.inherent_vowel_final);
    vowel_graphemes_ =
        std::make_shared<const std::set<std::string>>(spec.vowel_graphemes.begin(),
                                                      spec.vowel_graphemes.end());
}

const std::vector<std::u32string>* PhonetokTokenizer::ipa_for(const std::u32string& key) const {
    const auto it = grapheme_ipa_.find(key);
    return it == grapheme_ipa_.end() ? nullptr : &it->second;
}

std::optional<std::vector<double>> PhonetokTokenizer::weights_for(
    const std::string& grapheme) const {
    const auto it = grapheme_weights_.find(uni::lower(u32(grapheme)));
    if (it == grapheme_weights_.end()) return std::nullopt;
    return it->second;
}

// phonetok.py PhonetokTokenizer._supplies_vowel_at: True if the grapheme
// starting at *pos* supplies a syllable nucleus.
bool PhonetokTokenizer::supplies_vowel_at(const std::u32string& text, std::size_t pos) const {
    // A mark the spec maps to NOTHING is transparent here.
    while (pos < text.size() && spells_nothing(std::u32string(1, text[pos]))) ++pos;
    if (pos >= text.size()) return false;
    for (std::size_t span : dependent_vowel_spans_) {
        const std::u32string key = text.substr(pos, span);
        if (dependent_vowels_.count(key)) {
            const auto* ipa_vals = ipa_for(key);
            return ipa_vals && !ipa_vals->empty() && !(*ipa_vals)[0].empty() &&
                   is_nucleus_ipa((*ipa_vals)[0]);
        }
    }
    return supplies_vowel(text[pos]);
}

// phonetok.py PhonetokTokenizer._spells_nothing: a combining mark (or a
// letter with its marks) the spec unconditionally maps to nothing.
bool PhonetokTokenizer::spells_nothing(const std::u32string& ch) const {
    const auto* ipa_vals = ipa_for(ch);
    if (ipa_vals == nullptr || ipa_vals->size() != 1 || !(*ipa_vals)[0].empty()) return false;
    if (ch.size() == 1) return is_a_mark(ch[0]);
    const std::string head_cat = uni::category(ch[0]);
    return (head_cat[0] == 'L' || is_a_mark(ch[0])) &&
           std::all_of(ch.begin() + 1, ch.end(), [](char32_t c) { return is_a_mark(c); });
}

// phonetok.py PhonetokTokenizer._match_transparent: longest trie match at
// *start*, stepping over runs of marks the spec reads as nothing without
// moving the trie node (so Lao ົາ still matches ົ + tone mark + າ).
// Returns the matched key and the text offset just past it.
std::pair<std::optional<std::u32string>, std::size_t> PhonetokTokenizer::match_transparent(
    const std::u32string& text, std::size_t start) const {
    const TrieNode* node = trie_->root.get();
    std::size_t i = start;
    const std::size_t n = text.size();
    std::optional<std::u32string> best;
    std::size_t best_end = start;
    bool started = false;
    while (i < n) {
        const std::u32string ch = lower_char(text[i], trie_->lang);
        if (ch.size() == 1) {
            const auto it = node->children.find(ch[0]);
            if (it != node->children.end()) {
                node = it->second.get();
                ++i;
                started = true;
                if (node->key) {
                    best = node->key;
                    best_end = i;
                }
                continue;
            }
        }
        if (started && spells_nothing(std::u32string(1, text[i]))) {
            ++i;
            continue;
        }
        break;
    }
    return {best, best_end};
}

// phonetok.py PhonetokTokenizer._silent_marker_head: the leading letter a
// digraph declares silent (Thai ho nam หม reads /m/, same as ม alone).
std::optional<std::u32string> PhonetokTokenizer::silent_marker_head(
    const std::u32string& gkey) const {
    if (gkey.size() < 2) return std::nullopt;
    const char32_t head = gkey[0];
    const std::u32string tail = gkey.substr(1);
    const auto* head_ipa = ipa_for(std::u32string(1, head));
    if (head_ipa == nullptr || head_ipa->empty() || (*head_ipa)[0].empty()) return std::nullopt;
    const auto* g_ipa = ipa_for(gkey);
    const auto* t_ipa = ipa_for(tail);
    // Python compares against `(None,)` when the tail has no entry — an
    // IPA list never equals that sentinel.
    if (t_ipa == nullptr) return std::nullopt;
    if (g_ipa != nullptr && *g_ipa == *t_ipa) return std::u32string(1, head);
    return std::nullopt;
}

// phonetok.py PhonetokTokenizer._marker_head_is_the_onset: True when a
// preposed vowel makes a digraph's silent head the onset (โหม is /hoːm/,
// not */moː/ — only when the vowel ends open and nothing follows the
// tail is the tail the one available coda).
bool PhonetokTokenizer::marker_head_is_the_onset(const std::u32string& gkey,
                                                  const std::u32string& ckey,
                                                  const std::u32string& text,
                                                  std::size_t after) const {
    const auto* v_ipa = ipa_for(gkey);
    if (v_ipa == nullptr || v_ipa->empty() || !ends_in_a_vowel((*v_ipa)[0])) return false;
    std::size_t pos = after + ckey.size();
    bool first = true;
    while (pos < text.size()) {
        const auto nkey = trie_->longest_match(text, pos);
        if (!nkey) return !uni::is_alpha(text[pos]);
        const auto* n_ipa = ipa_for(*nkey);
        if (n_ipa != nullptr &&
            std::any_of(n_ipa->begin(), n_ipa->end(),
                        [](const std::u32string& s) { return !s.empty(); }))
            return false;
        if (first && spells_nothing(*nkey)) return false;
        first = false;
        pos += nkey->size();
    }
    return true;
}

// phonetok.py PhonetokTokenizer._supplies_vowel: a combining mark that
// supplies a vowel of its own (data-driven: the spec maps it to a
// nucleus), replacing — not adding to — the consonant's inherent vowel.
bool PhonetokTokenizer::supplies_vowel(char32_t ch) const {
    const std::string cat = uni::category(ch);
    if (cat != "Mn" && cat != "Mc" && !dependent_vowels_.count(std::u32string(1, ch)))
        return false;
    const auto* ipa_vals = ipa_for(std::u32string(1, ch));
    if (ipa_vals == nullptr || ipa_vals->empty()) return false;
    const std::u32string& first = (*ipa_vals)[0];
    if (first.empty()) return false;
    if (uni::combining(first[0]) != 0) return false;  // a diacritic modifies, never supplies
    return is_nucleus_ipa(first);
}

// phonetok.py PhonetokTokenizer._declares_postvocalic_reading: the spec
// gives this key its own reading AFTER a vowel (the coda declaration).
bool PhonetokTokenizer::declares_postvocalic_reading(const std::u32string& gkey) const {
    const auto it = spec_.positional_graphemes.find(utf8(gkey));
    if (it == spec_.positional_graphemes.end() || it->second.empty()) return false;
    return it->second.count("after_vowel") != 0;
}

// phonetok.py PhonetokTokenizer._syllable_has_nucleus: True if the
// syllable being built already has its vowel; the walk back is bounded by
// the spec's own post-vocalic (coda) declaration, and marks that spell
// nothing are transparent.
bool PhonetokTokenizer::syllable_has_nucleus(const std::vector<UToken>& tokens) const {
    for (auto it = tokens.rbegin(); it != tokens.rend(); ++it) {
        const UToken& tok = *it;
        if (tok.kind == TokenKind::GRAPHEME && tok.ipa.size() == 1 && tok.ipa[0].empty() &&
            spells_nothing(tok.grapheme))
            continue;
        if (prev_gives_nucleus(&tok)) return true;
        if (tok.kind != TokenKind::GRAPHEME) return false;
        const std::u32string first = tok.ipa.empty() ? std::u32string{} : tok.ipa[0];
        if (!first.empty() && !is_nucleus_ipa(first) &&
            declares_postvocalic_reading(tok.grapheme))
            continue;  // a declared coda letter: look further back
        return false;
    }
    return false;
}

// phonetok.py PhonetokTokenizer._prev_gives_nucleus: the previous token
// already supplied the current syllable's vowel (a dependent/preposed
// vowel sign, or a consonant whose IPA was itself extended with a vowel).
bool PhonetokTokenizer::prev_gives_nucleus(const UToken* prev_tok) const {
    if (prev_tok == nullptr || prev_tok->kind != TokenKind::GRAPHEME) return false;
    if (dependent_vowels_.count(prev_tok->grapheme) ||
        preposed_vowels_.count(prev_tok->grapheme))
        return true;
    return !prev_tok->ipa.empty() && !prev_tok->ipa[0].empty() &&
           is_nucleus_ipa(prev_tok->ipa[0]);
}

// phonetok.py PhonetokTokenizer._silenced_before_consonant: the spec
// gives this key NO realisation before a consonant — a silent letter
// carries no inherent vowel.
bool PhonetokTokenizer::silenced_before_consonant(const std::u32string& gkey) const {
    const auto it = spec_.positional_graphemes.find(utf8(gkey));
    if (it == spec_.positional_graphemes.end() || it->second.empty()) return false;
    const auto pit = it->second.find("before_consonant");
    if (pit == it->second.end() || pit->second.empty()) return false;
    return pit->second.size() == 1 && pit->second[0].empty();
}

// The positional entry for a key, as u32 candidates, or nullopt when the
// key declares none (a declared-but-empty entry yields an empty vector —
// Python's truthiness distinguishes the two and so do callers).
std::optional<std::vector<std::u32string>> PhonetokTokenizer::positional_u32(
    const std::u32string& gkey, const char* position) const {
    const auto it = spec_.positional_graphemes.find(utf8(gkey));
    if (it == spec_.positional_graphemes.end()) return std::nullopt;
    const auto pit = it->second.find(position);
    if (pit == it->second.end()) return std::nullopt;
    return u32vec(pit->second);
}

// phonetok.py PhonetokTokenizer.tokenize — the core tokenization scan.
std::vector<PhonetokTokenizer::UToken> PhonetokTokenizer::tokenize_u32(
    const std::u32string& input) const {
    // NFC normalization handles combining marks (Arabic harakat,
    // Devanagari matras, accented Latin characters).
    std::u32string text = uni::nfc(input);

    // Fold away the diacritics the spec declares as segment-less:
    // decompose so a precomposed letter exposes its combining marks, drop
    // the listed ones, recompose.
    if (!fold_diacritics_.empty()) {
        const std::u32string decomposed = uni::nfd(text);
        std::u32string stripped;
        stripped.reserve(decomposed.size());
        for (char32_t c : decomposed)
            if (!fold_diacritics_.count(std::u32string(1, c))) stripped.push_back(c);
        text = uni::nfc(stripped);
    }

    // Arabic-script pre-tokenization normalization (script-scoped).
    if (spec_.script == "Arabic") {
        text = decompose_arabic_presentation_forms(text);
        // Gemination is only expanded for specs that actually model
        // shadda; this leaves Arabic-script specs that do not (e.g.
        // Persian) byte-identical.
        if (grapheme_ipa_.count(std::u32string(1, AR_SHADDA)))
            text = expand_arabic_gemination(text);
    }

    std::vector<UToken> tokens;
    const std::size_t n = text.size();
    std::size_t pos = 0;

    if (add_bos_)
        tokens.push_back({TokenKind::BOS, u32(BOS_STR), {}, 0, 0});

    while (pos < n) {
        // (a) Whitespace
        if (is_ws_class(text[pos])) {
            const std::size_t begin = pos;
            do ++pos; while (pos < n && is_ws_class(text[pos]));
            const std::u32string span = text.substr(begin, pos - begin);
            tokens.push_back({TokenKind::WHITESPACE,
                              collapse_whitespace_ ? std::u32string(1, U' ') : span, {},
                              begin, span.size()});
            continue;
        }

        // (b) Punctuation — unless a grapheme claims these characters, in
        // which case the grapheme wins (maximal munch must hold here too:
        // the spec may register the punctuation span itself, or a longer
        // grapheme may merely open with punctuation).
        if (is_punct_class(text[pos])) {
            const std::size_t begin = pos;
            do ++pos; while (pos < n && is_punct_class(text[pos]));
            const std::u32string span = text.substr(begin, pos - begin);
            const bool claimed_by_grapheme =
                grapheme_ipa_.count(span) != 0 || trie_->longest_match(text, begin).has_value();
            if (!claimed_by_grapheme) {
                tokens.push_back({TokenKind::PUNCTUATION, span, {}, begin, span.size()});
                continue;
            }
            pos = begin;  // fall through to digit/grapheme matching
        }

        // (c) Digits — the same escape maximal munch grants punctuation
        // (numbered Pinyin tones, Arabic chat alphabet ⟨3⟩ for /ʕ/).
        if (is_digit_class(text[pos])) {
            const std::size_t begin = pos;
            do ++pos; while (pos < n && is_digit_class(text[pos]));
            const std::u32string span = text.substr(begin, pos - begin);
            const bool claimed_by_grapheme =
                grapheme_ipa_.count(span) != 0 || trie_->longest_match(text, begin).has_value();
            if (!claimed_by_grapheme) {
                tokens.push_back({TokenKind::DIGIT, span, {}, begin, span.size()});
                continue;
            }
            pos = begin;
        }

        // (d) Longest grapheme match (trie)
        std::optional<std::u32string> gkey = trie_->longest_match(text, pos);
        if (gkey.has_value() && preposed_vowels_.count(*gkey) != 0) {
            // Preposed dependent vowel (Thai เ แ โ ใ ไ, Lao ເ ແ ໂ ໃ ໄ):
            // written before the consonant, pronounced after it. The
            // token LIST stays in written order; the vowel token is
            // emitted SILENT and the consonant token's candidates carry
            // the vowel's reading appended after the consonant's.
            const std::size_t consumed_v = gkey->size();
            const std::size_t after = pos + consumed_v;
            std::optional<std::u32string> ckey = trie_->longest_match(text, after);
            if (ckey.has_value()) {
                const std::optional<std::u32string> head = silent_marker_head(*ckey);
                if (head.has_value() &&
                    marker_head_is_the_onset(*gkey, *ckey, text, after))
                    ckey = head;
            }
            const std::vector<std::u32string>* c_ipa_vals =
                ckey.has_value() ? ipa_for(*ckey) : nullptr;
            // A preposed sign IS the syllable's nucleus, so the grapheme
            // it attaches to fills the syllable-INITIAL slot; a spec's
            // word_initial entry for it outranks the flat reading here.
            const std::optional<std::vector<std::u32string>> initial_vals =
                ckey.has_value() ? positional_u32(*ckey, "word_initial") : std::nullopt;
            const bool onset_reading =
                initial_vals.has_value() && !initial_vals->empty() && c_ipa_vals != nullptr &&
                !c_ipa_vals->empty() && is_nucleus_ipa((*c_ipa_vals)[0]) &&
                !is_nucleus_ipa((*initial_vals)[0]);
            std::vector<std::u32string> initial_storage;
            if (onset_reading) {
                initial_storage = *initial_vals;
                c_ipa_vals = &initial_storage;
            }
            // A grapheme written with a combining mark is a dependent
            // vowel sign only when it READS as one.
            const bool is_c_dependent_vowel = ckey.has_value() && !onset_reading &&
                                             (((uni::category(ckey->back()) == "Mn" ||
                                                uni::category(ckey->back()) == "Mc") &&
                                               !(c_ipa_vals != nullptr && !c_ipa_vals->empty() &&
                                                 !(*c_ipa_vals)[0].empty() &&
                                                 !is_nucleus_ipa((*c_ipa_vals)[0]))) ||
                                              dependent_vowels_.count(*ckey) != 0 ||
                                              preposed_vowels_.count(*ckey) != 0);
            if (ckey.has_value() && c_ipa_vals != nullptr && !c_ipa_vals->empty() &&
                !is_c_dependent_vowel && !is_nucleus_ipa((*c_ipa_vals)[0])) {
                const std::size_t consumed_c = ckey->size();
                const std::size_t post_pos = after + consumed_c;
                const std::vector<std::u32string>& v_ipa_vals = grapheme_ipa_.at(*gkey);
                // Circumfix continuation: the postposed half of a Tai
                // circumfix vowel (Lao ເ◌ືອ) may follow, possibly with a
                // transparent tone mark swallowed inside the key.
                const auto [pkey, pkey_end] = match_transparent(text, post_pos);
                const std::vector<std::u32string>* p_ipa_vals =
                    pkey.has_value() ? ipa_for(*pkey) : nullptr;
                const bool is_circumfix_tail =
                    pkey.has_value() && p_ipa_vals != nullptr && !p_ipa_vals->empty() &&
                    ((uni::category((*pkey)[0]) == "Mn" || uni::category((*pkey)[0]) == "Mc") ||
                     dependent_vowels_.count(*pkey) != 0) &&
                    is_nucleus_ipa((*p_ipa_vals)[0]);
                std::vector<std::u32string> c_combined;
                if (is_circumfix_tail) {
                    c_combined = *c_ipa_vals;
                } else {
                    c_combined.reserve(c_ipa_vals->size() * v_ipa_vals.size());
                    for (const auto& c : *c_ipa_vals)
                        for (const auto& v : v_ipa_vals) c_combined.push_back(c + v);
                }
                tokens.push_back(
                    {TokenKind::GRAPHEME, *gkey, {}, pos, consumed_v});
                tokens.push_back(
                    {TokenKind::GRAPHEME, *ckey, c_combined, after, consumed_c});
                if (is_circumfix_tail && pkey_end > post_pos) {
                    // ``grapheme`` is the actual raw span, not the trie
                    // key: the key skipped a mark that sits BETWEEN its
                    // characters.
                    tokens.push_back({TokenKind::GRAPHEME,
                                      text.substr(post_pos, pkey_end - post_pos), *p_ipa_vals,
                                      post_pos, pkey_end - post_pos});
                    pos = pkey_end;
                } else {
                    pos = post_pos;
                }
                continue;
            }
            // No consonant follows (word-final preposed vowel, or two
            // preposed vowels in a row): fall through to ordinary
            // single-grapheme handling below, which reads gkey normally.
        }

        if (gkey.has_value()) {
            // Vowel-gated digraph. A consonant-initial digraph ending
            // in a high glide letter ⟨u⟩/⟨i⟩ uses that letter as a
            // mute/glide marker only when a vowel follows; before a
            // consonant or word-end the same letter is a syllabic
            // nucleus. When the digraph-minus-glide is itself a mapped
            // grapheme, back off to it.
            if (gkey->size() >= 2 && (gkey->back() == U'u' || gkey->back() == U'i') &&
                !vowels::is_orthographic_vowel((*gkey)[0]) &&
                grapheme_ipa_.count(gkey->substr(0, gkey->size() - 1)) != 0) {
                const std::size_t after = pos + gkey->size();
                const bool has_next = after < n;
                const char32_t next_ch = has_next ? text[after] : 0;
                const std::u32string nlow =
                    has_next ? lower_char(next_ch, spec_.code) : std::u32string{};
                if (!has_next || !vowels::is_orthographic_vowel(next_ch)) {
                    // (1) Before a consonant or word-end the ⟨u⟩/⟨i⟩ may
                    // be a syllabic nucleus the digraph wrongly
                    // swallows; only back off when the reading that
                    // would apply here spells the letter as no vowel.
                    // (A missing positional entry falls back to DEFAULT,
                    // then to the flat table; a declared-but-empty entry
                    // does NOT fall back — Python's None vs [].)
                    std::optional<std::vector<std::u32string>> reading =
                        has_next ? positional_u32(*gkey, "before_consonant")
                                 : positional_u32(*gkey, "word_final");
                    if (!reading.has_value()) {
                        // Python: `pg.get(DEFAULT) or self._grapheme_ipa.get(gkey)`
                        // — a declared-but-empty DEFAULT falls through to
                        // the flat table too.
                        reading = positional_u32(*gkey, "default");
                        if (!reading.has_value() || reading->empty()) {
                            if (const auto* flat = ipa_for(*gkey))
                                reading = std::optional<std::vector<std::u32string>>(*flat);
                        }
                    }
                    const bool reading_voices_letter =
                        reading.has_value() && !reading->empty() &&
                        std::any_of((*reading)[0].begin(), (*reading)[0].end(),
                                    [](char32_t c) { return is_ipa_vowel(c); });
                    if (!reading_voices_letter) *gkey = gkey->substr(0, gkey->size() - 1);
                } else if (!vowels::is_front_vowel(nlow)) {
                    // (2) Glide before a back/central vowel, after a
                    // plain vowel: when the spec reads the digraph as a
                    // [w]/[j] glide and the glide+vowel is itself a
                    // registered digraph, back off to C + glide-digraph
                    // (agua = [aɣwa]).
                    const UToken* prev_tok = nullptr;
                    for (auto it = tokens.rbegin(); it != tokens.rend(); ++it)
                        if (it->kind == TokenKind::GRAPHEME) {
                            prev_tok = &*it;
                            break;
                        }
                    const std::u32string prev_ipa =
                        prev_tok != nullptr && !prev_tok->ipa.empty() ? prev_tok->ipa[0]
                                                                       : std::u32string{};
                    const bool prev_is_vowel =
                        !prev_ipa.empty() && is_ipa_vowel(prev_ipa.back());
                    std::optional<std::vector<std::u32string>> marker =
                        positional_u32(*gkey, "before_a");
                    if (!marker.has_value() || marker->empty())
                        marker = positional_u32(*gkey, "before_back_vowel");
                    if (prev_is_vowel && marker.has_value() && !marker->empty() &&
                        ((*marker)[0].find(U'w') != std::u32string::npos ||
                         (*marker)[0].find(U'j') != std::u32string::npos) &&
                        grapheme_ipa_.count(std::u32string(1, gkey->back()) + nlow) != 0)
                        *gkey = gkey->substr(0, gkey->size() - 1);
                }
            }
            std::vector<std::u32string> ipa_vals;
            if (const auto* values = ipa_for(*gkey)) ipa_vals = *values;
            std::size_t consumed = gkey->size();
            // Inherent vowel for abugidas: a consonant letter carries
            // one, cancelled by a virama or a dependent vowel sign; marks
            // that supply no vowel leave it standing; a conjunct stack
            // (subjoined letters) is a consonant letter all the same.
            if (!spec_.inherent_vowel.empty() && !ipa_vals.empty()) {
                const std::u32string& first_ipa = ipa_vals[0];
                const bool is_mark = (uni::category(gkey->back()) == "Mn" ||
                                      uni::category(gkey->back()) == "Mc") &&
                                     !is_subjoined_letter_cluster(*gkey);
                if (!first_ipa.empty() && !is_mark && !is_nucleus_ipa(first_ipa)) {
                    const std::size_t next_pos = pos + consumed;
                    const bool has_next = next_pos < n;
                    const char32_t next_ch = has_next ? text[next_pos] : 0;
                    if (has_next && is_virama_char(next_ch)) {
                        // Virama — bare consonant; consume the mark so
                        // C+virama+C falls out as a cluster.
                        consumed += 1;
                    } else if (!supplies_vowel_at(text, next_pos)) {
                        // Nothing supplies a vowel — but a consonant
                        // closing an already-nucleused syllable gets no
                        // inherent vowel of its own (Tai coda rule),
                        // nor does a letter the spec silences before a
                        // consonant.
                        const bool skip =
                            (coda_no_inherent_vowel_ && syllable_has_nucleus(tokens)) ||
                            (has_next && silenced_before_consonant(*gkey));
                        if (!skip) {
                            // Word-finally the spec may realise the
                            // inherent vowel differently (or not at
                            // all), but never at the cost of the word's
                            // last nucleus.
                            std::u32string vowel = u32(spec_.inherent_vowel);
                            if (!has_next && inherent_vowel_final_.has_value() &&
                                syllable_has_nucleus(tokens))
                                vowel = *inherent_vowel_final_;
                            for (auto& value : ipa_vals) value += vowel;
                        }
                    }
                    // else: a dependent vowel sign follows and is
                    // tokenised on the next pass.
                }
            }
            tokens.push_back({TokenKind::GRAPHEME, *gkey, ipa_vals, pos, consumed});
            pos += consumed;
            continue;
        }

        // (d2) Canonical decomposition fallback. When a character has no
        // trie entry of its own, decompose it canonically; if EVERY piece
        // is a mapped grapheme, emit ONE token for the original character
        // whose candidates are the (ranked, capped) combinations of the
        // pieces' candidates. All pieces must map: a partial match would
        // silently drop phonemes, and UNKNOWN is the honest answer there.
        const char32_t ch = text[pos];
        const std::u32string decomposed = uni::nfd(std::u32string(1, ch));
        if (decomposed != std::u32string(1, ch)) {
            bool all_mapped = true;
            std::vector<const std::vector<std::u32string>*> piece_vals;
            piece_vals.reserve(decomposed.size());
            for (char32_t piece : decomposed) {
                const auto* values = ipa_for(std::u32string(1, piece));
                if (values == nullptr) {
                    all_mapped = false;
                    break;
                }
                piece_vals.push_back(values);
            }
            if (all_mapped) {
                // Cartesian product in itertools order (last piece
                // varies fastest), capped at _MAX_DECOMPOSED_COMBOS = 4.
                constexpr std::size_t MAX_DECOMPOSED_COMBOS = 4;
                std::vector<std::u32string> combos{std::u32string()};
                for (const auto* values : piece_vals) {
                    std::vector<std::u32string> next;
                    for (const auto& prefix : combos) {
                        for (const auto& value : *values) {
                            next.push_back(prefix + value);
                            if (next.size() >= MAX_DECOMPOSED_COMBOS) break;
                        }
                        if (next.size() >= MAX_DECOMPOSED_COMBOS) break;
                    }
                    combos = std::move(next);
                }
                tokens.push_back({TokenKind::GRAPHEME, std::u32string(1, ch), combos, pos, 1});
                ++pos;
                continue;
            }
        }

        // (e) Unknown single character
        tokens.push_back({TokenKind::UNKNOWN, std::u32string(1, ch), {}, pos, 1});
        ++pos;
    }

    if (add_eos_) tokens.push_back({TokenKind::EOS, u32(EOS_STR), {}, n, 0});

    return tokens;
}

std::vector<Token> PhonetokTokenizer::tokenize(const std::string& text) const {
    const std::vector<UToken> utokens = tokenize_u32(uni::to_utf32(text));
    std::vector<Token> tokens;
    tokens.reserve(utokens.size());
    for (const auto& t : utokens) {
        Token token;
        token.kind = t.kind;
        token.grapheme = utf8(t.grapheme);
        token.ipa.reserve(t.ipa.size());
        for (const auto& value : t.ipa) token.ipa.push_back(utf8(value));
        token.position = t.position;
        token.length = t.length;
        tokens.push_back(std::move(token));
    }
    return tokens;
}

std::vector<std::string> PhonetokTokenizer::graphemes(const std::string& text) const {
    std::vector<std::string> out;
    for (const auto& token : tokenize(text)) out.push_back(token.grapheme);
    return out;
}

std::vector<Token> PhonetokTokenizer::grapheme_tokens(const std::string& text) const {
    std::vector<Token> out;
    for (const auto& token : tokenize(text))
        if (token.kind == TokenKind::GRAPHEME) out.push_back(token);
    return out;
}

TokenSequence PhonetokTokenizer::tokenize_with_context(const std::string& text) const {
    TokenSequence seq;
    *seq.tokens = tokenize(text);
    // Any non-grapheme token ends the current word run; runs are
    // contiguous ranges over the contexts, so record boundaries and fill
    // run sizes in a second pass.
    std::vector<std::pair<std::size_t, std::size_t>> runs;  // (start, length)
    std::size_t run_start = 0;
    for (std::size_t token_index = 0; token_index < seq.tokens->size(); ++token_index) {
        if ((*seq.tokens)[token_index].kind != TokenKind::GRAPHEME) continue;
        GraphemeContext ctx;
        ctx.tokens_ = seq.tokens.get();
        ctx.token_index_ = token_index;
        ctx.index_ = seq.graphemes->size();
        ctx.run_ = seq.graphemes.get();
        ctx.vowel_graphemes_ = vowel_graphemes_;
        if (seq.graphemes->empty() ||
            (*seq.graphemes)[seq.graphemes->size() - 1].token_index_ + 1 != token_index) {
            run_start = seq.graphemes->size();
        }
        ctx.run_start_ = run_start;
        ctx.run_pos_ = ctx.index_ - run_start;
        seq.graphemes->push_back(ctx);
        if (ctx.run_pos_ == 0) runs.push_back({run_start, 0});
        ++runs.back().second;
    }
    for (const auto& [start, length] : runs)
        for (std::size_t i = 0; i < length; ++i)
            (*seq.graphemes)[start + i].run_size_ = length;
    return seq;
}

// phonetok.py PhonetokTokenizer.ipa_beam: beam search over the token
// stream, sharing resolve_branches/constrain_nasal_carriers/_expand_beam
// with the engine's positional beam.
std::vector<IPAPath> PhonetokTokenizer::ipa_beam(
        const std::string& text, std::size_t beam_width, bool expand_allophones,
        const std::string& word_separator, bool include_special) const {
    const std::vector<Token> tokens = tokenize(text);
    const TokenSequence seq = tokenize_with_context(text);
    const std::vector<GraphemeContext>& contexts = *seq.graphemes;

    const std::map<std::string, std::vector<std::string>>* allophone_map =
        expand_allophones ? &spec_.allophones : nullptr;

    // Pre-resolve every grapheme slot's branches (no stress context on
    // the standalone path — the full engine supplies it).
    std::vector<std::vector<beam::Branch>> slot_branches;
    slot_branches.reserve(contexts.size());
    for (const auto& ctx : contexts)
        slot_branches.push_back(beam::resolve_branches(spec_, ctx, *this, allophone_map));
    beam::constrain_nasal_carriers(slot_branches);

    std::vector<beam::Hypothesis> beam{{{}, 0.0}};
    std::vector<std::string> spelled;
    std::size_t g_idx = 0;
    for (const auto& token : tokens) {
        if (token.kind == TokenKind::GRAPHEME) {
            const auto& branches = slot_branches[g_idx++];
            if (branches.empty()) {
                // No candidates — a rescorer deleted the slot, or the
                // grapheme is deliberately silent. It contributes no
                // segment and leaves the hypotheses untouched.
                continue;
            }
            spelled.push_back(token.grapheme);
            beam = beam::expand_beam(std::move(beam), branches, beam_width);
        } else if (include_special) {
            if (token.kind == TokenKind::WHITESPACE) {
                spelled.push_back(token.grapheme);
                beam = beam::expand_beam(
                    std::move(beam), {{word_separator, 0.0}}, beam_width);
            } else if (token.kind == TokenKind::PUNCTUATION ||
                       token.kind == TokenKind::DIGIT ||
                       token.kind == TokenKind::UNKNOWN) {
                spelled.push_back(token.grapheme);
                beam = beam::expand_beam(
                    std::move(beam), {{token.grapheme, 0.0}}, beam_width);
            }
        }
    }

    std::vector<IPAPath> paths;
    paths.reserve(beam.size());
    for (const auto& hyp : beam) {
        IPAPath path;
        path.score = hyp.score;
        path.segments = hyp.segments;
        path.graphemes = spelled;
        for (const auto& segment : hyp.segments) path.ipa += segment;
        paths.push_back(std::move(path));
    }
    std::sort(paths.begin(), paths.end(), [](const IPAPath& a, const IPAPath& b) {
        if (a.score != b.score) return a.score < b.score;
        return a.ipa < b.ipa;
    });
    return paths;
}

} // namespace orthography2ipa
