// transforms.py port: the dialect de-biasing and rule engine. The tables come
// from src/transform_tables.inc, generated from the reference module itself.
#include "orthography2ipa/orthography2ipa.hpp"
#include "orthography2ipa/transform.hpp"

#include "codepoint_regex.hpp"
#include "unicode_util.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <stdexcept>

namespace orthography2ipa::transform {
namespace {

using uni::category;
using uni::lower;
using uni::to_utf32;
using uni::to_utf8;

#include "transform_tables.inc"

bool in_set(const char32_t* set, char32_t ch) {
    for (const char32_t* p = set; *p; ++p)
        if (*p == ch) return true;
    return false;
}

bool is_vowel(char32_t ch) { return in_set(VOWEL_SET, ch); }
bool is_palatal(char32_t ch) { return in_set(PALATAL_SET, ch); }

/// Python `str.split()` on whitespace, no empty fields.
std::vector<std::u32string> split_ws(const std::u32string& text) {
    const auto space = [](char32_t c) {
        switch (c) {
            case U' ': case U'\t': case U'\n': case U'\v': case U'\f': case U'\r': return true;
            default: break;
        }
        const std::string cat = category(c);
        return cat == "Zs" || cat == "Zl" || cat == "Zp";
    };
    std::vector<std::u32string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && space(text[i])) ++i;
        const std::size_t start = i;
        while (i < text.size() && !space(text[i])) ++i;
        if (i > start) out.push_back(text.substr(start, i - start));
    }
    return out;
}

std::u32string join_ws(const std::vector<std::u32string>& parts) {
    std::u32string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out.push_back(U' ');
        out.append(parts[i]);
    }
    return out;
}

/// Python `str.replace(from, to)`: every occurrence, code points.
std::u32string replace_all(const std::u32string& text, const std::u32string& from,
                           const std::u32string& to) {
    if (from.empty()) return text;
    std::u32string out;
    std::size_t i = 0;
    while (true) {
        const std::size_t hit = text.find(from, i);
        if (hit == std::u32string::npos) { out.append(text, i, std::u32string::npos); break; }
        out.append(text, i, hit - i);
        out.append(to);
        i = hit + from.size();
    }
    return out;
}

const re32::Regex& regex(const char* pattern) {
    static std::map<std::string, re32::Regex> cache;
    auto it = cache.find(pattern);
    if (it == cache.end())
        it = cache.emplace(pattern, re32::Regex::compile(to_utf32(pattern))).first;
    return it->second;
}

bool searched(const char* pattern, const std::u32string& text) {
    re32::Match m;
    return regex(pattern).search(text, &m);
}

/// Python `re.match`: the pattern must match AT the start of the text, which
/// is what distinguishes the r/C and nasal/C predicates from a search.
bool matched_at_start(const char* pattern, const std::u32string& text) {
    re32::Match m;
    return regex(pattern).search(text, &m, 0) && m.start == 0;
}

std::u32string resub(const char* pattern, const std::u32string& repl,
                     const std::u32string& text) {
    return regex(pattern).sub(text, repl);
}

// ─── transforms.py predicates ────────────────────────────────────────────

/// `_is_stressed_position`: a ˈ to the left with no vowel in between.
bool is_stressed_position(const std::u32string& ipa, std::size_t pos) {
    for (std::size_t i = pos; i-- > 0;) {
        if (ipa[i] == STRESS_MARK) return true;
        if (is_vowel(ipa[i])) break;
    }
    return false;
}

bool is_unstressed(const std::u32string& ipa, std::size_t pos) {
    return !is_stressed_position(ipa, pos);
}

/// `_is_word_final`: the match ends the string or is followed by a space.
bool is_word_final(const std::u32string& ipa, std::size_t pos, std::size_t length) {
    const std::size_t end = pos + length;
    if (end >= ipa.size()) return true;
    return ipa[end] == U' ' || ipa[end] == U'\t' || ipa[end] == U'\n';
}

/// `_adjacent_to_palatal`, including the two-character tʃ / dʒ spans.
bool adjacent_to_palatal(const std::u32string& ipa, std::size_t pos) {
    if (pos > 0 && is_palatal(ipa[pos - 1])) return true;
    if (pos + 1 < ipa.size() && is_palatal(ipa[pos + 1])) return true;
    for (const std::u32string palatal : {std::u32string(U"tʃ"), std::u32string(U"dʒ")})
        for (std::size_t offset = 0; offset < palatal.size(); ++offset) {
            if (pos < offset) continue;
            const std::size_t start = pos - offset;
            if (start + palatal.size() > ipa.size()) continue;
            if (ipa.compare(start, palatal.size(), palatal) == 0) return true;
        }
    return false;
}

/// `_before_r_or_closed`: a rhotic + consonant cluster right after the match.
bool before_r_or_closed(const std::u32string& ipa, std::size_t pos) {
    if (pos + 1 >= ipa.size()) return false;
    return matched_at_start(BEFORE_R_C, ipa.substr(pos + 1));
}

bool before_nasal_c(const std::u32string& ipa, std::size_t pos) {
    if (pos + 1 >= ipa.size()) return false;
    return matched_at_start(BEFORE_NASAL_C, ipa.substr(pos + 1));
}

/// transforms.py `_check_context`. *ortho* is nullopt exactly when the caller
/// passed no spelling at all — the `ortho_*` and `no_ortho_available` contexts
/// distinguish that from an empty one, so it is not collapsed to "".
bool check_context(const std::string& context, const std::u32string& ipa, std::size_t pos,
                   std::size_t length, const std::optional<std::u32string>& ortho) {
    const bool has_ortho = ortho.has_value();
    const std::u32string spelling = has_ortho ? *ortho : std::u32string();
    const auto contains = [&](const char* needle) {
        return has_ortho && spelling.find(to_utf32(needle)) != std::u32string::npos;
    };
    if (context == "stressed") return is_stressed_position(ipa, pos);
    if (context == "unstressed") return is_unstressed(ipa, pos);
    if (context == "word_final") return is_word_final(ipa, pos, length);
    if (context == "word_final_unstressed")
        return is_word_final(ipa, pos, length) && is_unstressed(ipa, pos);
    if (context == "coda") return pos > 0 && is_vowel(ipa[pos - 1]);
    if (context == "stressed_and_before_r_or_closed_syllable")
        return is_stressed_position(ipa, pos) && before_r_or_closed(ipa, pos);
    if (context == "stressed_and_adjacent_to_palatal")
        return is_stressed_position(ipa, pos) && adjacent_to_palatal(ipa, pos);
    if (context == "stressed_before_r_C")
        return is_stressed_position(ipa, pos) && before_r_or_closed(ipa, pos);
    if (context == "stressed_before_r_C_or_nasal_C")
        return is_stressed_position(ipa, pos)
            && (before_r_or_closed(ipa, pos) || before_nasal_c(ipa, pos));
    if (context == "unstressed_pretonic") {
        if (!is_unstressed(ipa, pos)) return false;
        return ipa.find(STRESS_MARK, pos) != std::u32string::npos;
    }
    if (context == "stressed_and_from_historical_ou")
        return has_ortho && contains("ou") && is_stressed_position(ipa, pos);
    if (context == "ortho_has_ou") return has_ortho && contains("ou");
    if (context == "ortho_source_is_ch") return has_ortho && contains("ch");
    if (context == "ortho_source_is_ss_or_s") return has_ortho && (contains("ss") || contains("s"));
    if (context == "ortho_source_is_c_or_ç")
        return has_ortho && (contains("ç") || searched(ORTHO_C_EI, spelling));
    if (context == "ortho_source_is_intervocalic_s")
        return has_ortho && searched(ORTHO_INTERVOCALIC_S, spelling);
    if (context == "ortho_source_is_z") return has_ortho && contains("z");
    if (context == "no_ortho_available") return !has_ortho;
    return false;   // unknown context: the reference skips the rule to be safe
}

/// transforms.py `_apply_contextual`: scan left to right for `find` and apply
/// the rule only where the named context holds.
std::u32string apply_contextual(const std::u32string& ipa, const Rule& rule,
                                const std::optional<std::u32string>& ortho) {
    const std::u32string find = to_utf32(rule.find);
    const std::u32string to = to_utf32(rule.replace);
    if (find.empty()) return ipa;
    std::u32string result;
    for (std::size_t i = 0; i < ipa.size();) {
        if (i + find.size() <= ipa.size() && ipa.compare(i, find.size(), find) == 0
                && check_context(rule.context, ipa, i, find.size(), ortho)) {
            result.append(to);
            i += find.size();
            continue;
        }
        result.push_back(ipa[i]);
        ++i;
    }
    return result;
}

/// transforms.py IPARule.applies: the orthographic word, case-folded.
bool lexical_applies(const Rule& rule, const std::u32string& ortho_word) {
    // IPALexicalRule.applies: a case-insensitive match on the whole word.
    return lower(ortho_word) == lower(to_utf32(rule.word));
}

} // namespace

Rule Rule::plain(const std::string& id, const std::string& name, const std::string& find,
                 const std::string& replace, const char* context, bool requires_ortho) {
    Rule rule;
    rule.kind = Kind::Plain;
    rule.id = id; rule.name = name; rule.find = find; rule.replace = replace;
    rule.context = context ? context : "";
    rule.requires_ortho = requires_ortho;
    return rule;
}

Rule Rule::chain(const std::string& id, const std::string& name, Mapping mapping,
                 const char* context) {
    Rule rule;
    rule.kind = Kind::Chain;
    rule.id = id; rule.name = name; rule.mapping = std::move(mapping);
    rule.context = context ? context : "";
    return rule;
}

Rule Rule::lexical(const std::string& id, const std::string& word, const std::string& find,
                   const std::string& replace) {
    Rule rule;
    rule.kind = Kind::Lexical;
    rule.id = id; rule.word = word; rule.find = find; rule.replace = replace;
    return rule;
}

const std::vector<Profile>& profiles() { return profiles_table(); }

std::vector<std::string> available_profiles() {
    std::vector<std::string> out;
    for (const auto& p : profiles_table()) out.push_back(p.code);
    std::sort(out.begin(), out.end());
    return out;
}

const Profile* profile_for(const std::string& code) {
    for (const auto& p : profiles_table())
        if (p.code == code) return &p;
    return nullptr;
}

std::string debias_lisbon(const std::string& ipa, const std::string& ortho) {
    std::u32string out = to_utf32(ipa);
    const bool has_ortho = !ortho.empty();
    const std::u32string spelling = to_utf32(ortho);
    // DB1 ɐj → ej: with an ortho that spells ‹ei›, everywhere; without one,
    // only after a non-vowel (the heuristic that guesses ‹ei› from the IPA).
    if (has_ortho && spelling.find(U"ei") != std::u32string::npos)
        out = replace_all(out, U"ɐj", U"ej");
    else if (!has_ortho)
        out = resub(DB1_RE, U"ej", out);
    // DB2: restore /ow/ where Lisbon monophthongized it.
    if (has_ortho && spelling.find(U"ou") != std::u32string::npos)
        out = resub(DB2_RE, U"ow", out);
    // DB4–DB6b: normalize the allophonic spirants and the coda-l velarization.
    out = replace_all(out, U"β", U"b");
    out = replace_all(out, U"ð", U"d");
    out = replace_all(out, U"ɣ", U"ɡ");
    out = replace_all(out, U"ɫ", U"l");
    return to_utf8(out);
}

std::string debias_lisbon_preserve_spirants(const std::string& ipa, const std::string& ortho) {
    std::u32string out = to_utf32(ipa);
    const bool has_ortho = !ortho.empty();
    const std::u32string spelling = to_utf32(ortho);
    if (has_ortho && spelling.find(U"ei") != std::u32string::npos)
        out = replace_all(out, U"ɐj", U"ej");
    else if (!has_ortho)
        out = resub(DB1_RE, U"ej", out);
    if (has_ortho && spelling.find(U"ou") != std::u32string::npos)
        out = resub(DB2_RE, U"ow", out);
    out = replace_all(out, U"ɫ", U"l");   // DB4–DB6 skipped: spirants are real here
    return to_utf8(out);
}

std::string apply_transform(const std::string& ipa, const std::string& profile,
                            const std::string& ortho, bool debias,
                            double spirantization_rate) {
    const Profile* found = profile_for(profile);
    if (found == nullptr) throw std::invalid_argument("unknown dialect profile: " + profile);

    const std::optional<std::u32string> spelling =
        ortho.empty() ? std::optional<std::u32string>{} : std::optional<std::u32string>(to_utf32(ortho));
    std::u32string out = to_utf32(ipa);

    // Step 0: de-bias the Lisbon prestige norm the input is usually written in.
    if (debias && found->requires_debiasing)
        out = to_utf32(spirantization_rate > 0.02
                           ? debias_lisbon_preserve_spirants(to_utf8(out), ortho)
                           : debias_lisbon(to_utf8(out), ortho));

    // Step 1: chain shifts, simultaneous — before the sequential rules, so one
    // mapping's output never becomes another's input.
    for (const auto& rule : found->rules) {
        if (rule.kind != Kind::Chain) continue;
        const bool stressed_only = rule.context == "stressed";
        bool in_stressed = false;
        std::u32string shifted;
        for (const char32_t ch : out) {
            if (ch == STRESS_MARK) { in_stressed = true; shifted.push_back(ch); continue; }
            if (stressed_only && !in_stressed) { shifted.push_back(ch); continue; }
            bool mapped = false;
            for (const auto& pair : rule.mapping) {
                const std::u32string from = to_utf32(pair.first);
                if (from.size() == 1 && from[0] == ch) {
                    shifted.append(to_utf32(pair.second));
                    mapped = true;
                    break;
                }
            }
            if (!mapped) shifted.push_back(ch);
            if (is_vowel(ch) && in_stressed) in_stressed = false;
        }
        out = shifted;
    }

    // Step 2: lexical rules, per word — only when the IPA and the spelling
    // still have the same number of words.
    if (spelling.has_value()) {
        auto words_ipa = split_ws(out);
        const auto words_ortho = split_ws(*spelling);
        if (words_ipa.size() == words_ortho.size()) {
            for (std::size_t i = 0; i < words_ipa.size(); ++i) {
                for (const auto& rule : found->rules) {
                    if (rule.kind != Kind::Lexical) continue;
                    if (!lexical_applies(rule, words_ortho[i])) continue;
                    words_ipa[i] = replace_all(words_ipa[i], to_utf32(rule.find),
                                               to_utf32(rule.replace));
                }
            }
            out = join_ws(words_ipa);
        }
    }

    // Step 3: the ordered phonological rules.
    for (const auto& rule : found->rules) {
        if (rule.kind != Kind::Plain) continue;
        if (rule.requires_ortho && !spelling.has_value()) continue;
        if (rule.context.empty())
            out = replace_all(out, to_utf32(rule.find), to_utf32(rule.replace));
        else
            out = apply_contextual(out, rule, spelling);
    }
    return to_utf8(out);
}

} // namespace orthography2ipa::transform
