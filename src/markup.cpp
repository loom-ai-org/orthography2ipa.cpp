// markup.py port: <phoneme alphabet="ipa" ph="…">spelling</phoneme>.
//
// The reference is two compiled Python regexes and the checks around them, so
// this port is the same two regexes, matched over CODE POINTS with
// src/codepoint_regex.hpp, plus the same messages. Nothing here adds a rule
// the reference does not have.
#include "orthography2ipa/markup.hpp"

#include "codepoint_regex.hpp"
#include "unicode_util.hpp"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace orthography2ipa::markup {
namespace {

using uni::category;
using uni::lower;
using uni::lower_one;
using uni::to_utf32;
using uni::to_utf8;

// ─── the Python str primitives the reference leans on ───────────────────

/// The code points Python's `str.isspace()` accepts: what `\s` matches in a str
/// pattern and what `str.strip()` removes. re32's `\s` is general-category
/// based, so it misses U+001C..U+001F and U+0085; the reference's `\s` is
/// therefore spelled out from this table (the Zs/Zl/Zp members are exactly the
/// categories re32 uses, so nothing else shifts).
const std::vector<std::pair<char32_t, char32_t>>& space_ranges() {
    static const std::vector<std::pair<char32_t, char32_t>> ranges = {
        {0x09, 0x0d}, {0x1c, 0x20}, {0x85, 0x85}, {0xa0, 0xa0}, {0x1680, 0x1680},
        {0x2000, 0x200a}, {0x2028, 0x2029}, {0x202f, 0x202f}, {0x205f, 0x205f},
        {0x3000, 0x3000},
    };
    return ranges;
}

bool is_space(char32_t ch) {
    for (const auto& range : space_ranges())
        if (ch >= range.first && ch <= range.second) return true;
    return false;
}

/// `str.strip()`: both ends, no argument.
std::u32string strip(const std::u32string& text) {
    std::size_t first = 0;
    std::size_t last = text.size();
    while (first < last && is_space(text[first])) ++first;
    while (last > first && is_space(text[last - 1])) --last;
    return text.substr(first, last - first);
}

/// `str.isprintable()`: everything but the Other and Separator categories,
/// except ASCII space.
bool is_printable(char32_t ch) {
    if (ch == U' ') return true;
    const std::string cat = category(ch);
    if (cat.empty()) return false;
    switch (cat[0]) {
        case 'C': case 'Z': return false;
        default: break;
    }
    return true;
}

/// `repr(str)` — the two f-strings that quote a value interpolate it with `!r`,
/// so the quoting style and the escapes are part of the message.
std::string py_repr(const std::u32string& text) {
    char32_t quote = U'\'';
    if (text.find(U'\'') != std::u32string::npos && text.find(U'"') == std::u32string::npos)
        quote = U'"';
    std::u32string out(1, quote);
    const auto hex_digit = [](int value) {
        return static_cast<char32_t>(value < 10 ? U'0' + value : U'a' + (value - 10));
    };
    const auto push_hex = [&out, &hex_digit](char32_t ch, char32_t lead, int digits) {
        out.push_back(U'\\');
        out.push_back(lead);
        for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4)
            out.push_back(hex_digit((static_cast<int>(ch) >> shift) & 0xf));
    };
    for (const char32_t ch : text) {
        if (ch == quote || ch == U'\\') {
            out.push_back(U'\\');
            out.push_back(ch);
        } else if (ch == U'\t') {
            out.append(U"\\t");
        } else if (ch == U'\n') {
            out.append(U"\\n");
        } else if (ch == U'\r') {
            out.append(U"\\r");
        } else if (ch < 0x20 || (ch >= 0x7f && ch < 0xa0)) {
            push_hex(ch, U'x', 2);
        } else if (!is_printable(ch)) {
            if (ch < 0x100) push_hex(ch, U'x', 2);
            else if (ch < 0x10000) push_hex(ch, U'u', 4);
            else push_hex(ch, U'U', 8);
        } else {
            out.push_back(ch);
        }
    }
    out.push_back(quote);
    return to_utf8(out);
}

// ─── the compiled patterns ───────────────────────────────────────────────

/// markup.py compiles both patterns with `re.IGNORECASE`. re32 has no flags,
/// and the Unicode fold set of each letter of the ASCII literal "phoneme" is
/// exactly its two cases, so the literal becomes one class per letter.
const std::u32string& phoneme_literal() {
    static const std::u32string folded = [] {
        std::u32string out;
        // The literal is uppercase so lower_one() supplies the other case.
        for (const char32_t ch : std::u32string(U"PHONEME")) {
            const std::u32string low = lower_one(ch);
            out.push_back(U'[');
            out.push_back(low.empty() ? ch : low[0]);
            out.push_back(ch);
            out.push_back(U']');
        }
        return out;
    }();
    return folded;
}

/// The `[...]` class for the reference's `\s`.
const std::u32string& space_class() {
    static const std::u32string cls = [] {
        std::u32string out(1, U'[');
        for (const auto& range : space_ranges()) {
            out.push_back(range.first);
            if (range.second != range.first) {
                out.push_back(U'-');
                out.push_back(range.second);
            }
        }
        out.push_back(U']');
        return out;
    }();
    return cls;
}

/// The `(?=[\s>])` that ends a tag name: whitespace or `>`.
std::u32string break_class() {
    std::u32string out = space_class();
    out.pop_back();                       // drop the closing ]
    out.push_back(U'>');
    out.push_back(U']');
    return out;
}

/// markup.py `_PHONEME`, with `re.VERBOSE` flattened (the insignificant
/// whitespace and comments are gone), `re.DOTALL` written as `[\s\S]` (re32's
/// `.` is Python's newline-excluding dot) and IGNORECASE folded in.
const re32::Regex& phoneme_pattern() {
    static const re32::Regex pattern = re32::Regex::compile(
        U"<" + phoneme_literal() + U"(?=" + break_class() + U")" +
        U"(?P<attrs>(?:" + space_class() + U"+[\\w-]+" + space_class() + U"*=" +
        space_class() + U"*(?:\"[^\"]*\"|'[^']*'))*)" + space_class() + U"*>" +
        U"(?P<text>[\\s\\S]*?)" +
        U"</" + phoneme_literal() + space_class() + U"*>");
    return pattern;
}

/// markup.py `_ATTR`. `(?P=quote)` is the same backreference by number: group 1
/// is `name`, group 2 `quote`, group 3 `value`. Here `.` keeps Python's
/// newline-excluding meaning, because `_ATTR` is compiled with no flags.
const re32::Regex& attr_pattern() {
    static const re32::Regex pattern = re32::Regex::compile(
        U"(?P<name>[\\w-]+)" + space_class() + U"*=" + space_class() +
        U"*(?P<quote>[\"'])(?P<value>.*?)\\2");
    return pattern;
}

/// markup.py `_reject_stray_tag`'s inline `re.search` pattern.
const re32::Regex& stray_pattern() {
    static const re32::Regex pattern = re32::Regex::compile(
        U"</?" + phoneme_literal() + U"(?=" + break_class() + U")");
    return pattern;
}

/// The span of the named group *name* — False when the group did not
/// participate, which is Python's `match["name"] is None`.
bool capture(const std::u32string& subject, const re32::Match& match, const char32_t* name,
             std::u32string* out) {
    for (std::size_t i = 0; i < match.names.size(); ++i) {
        if (match.names[i] != name) continue;
        std::size_t first = 0;
        std::size_t last = 0;
        if (!match.group(i + 1, &first, &last)) return false;
        *out = subject.substr(first, last - first);
        return true;
    }
    return false;
}

/// markup.py `_attrs`: `{m["name"].lower(): m["value"]}` over every match, so a
/// repeated attribute keeps the LAST value, as a dict comprehension does.
std::map<std::u32string, std::u32string> attrs_of(const std::u32string& raw) {
    std::map<std::u32string, std::u32string> attrs;
    std::size_t scan = 0;
    re32::Match match;
    while (attr_pattern().search(raw, &match, scan)) {
        std::u32string name;
        std::u32string value;
        capture(raw, match, U"name", &name);
        capture(raw, match, U"value", &value);
        attrs[lower(name)] = value;
        scan = match.end > match.start ? match.end : match.start + 1;
    }
    return attrs;
}

const std::u32string* find_attr(const std::map<std::u32string, std::u32string>& attrs,
                                const char32_t* name) {
    const auto found = attrs.find(std::u32string(name));
    return found == attrs.end() ? nullptr : &found->second;
}

} // namespace

Chunk::Chunk(std::string text, std::optional<std::string> forced_ipa)
    : text(std::move(text)), forced_ipa(std::move(forced_ipa)) {}

bool Chunk::operator==(const Chunk& other) const {
    return text == other.text && forced_ipa == other.forced_ipa;
}

std::vector<Chunk> parse_markup(const std::string& text) {
    const std::u32string subject = to_utf32(text);
    std::vector<Chunk> chunks;
    std::size_t pos = 0;
    std::size_t scan = 0;
    re32::Match match;

    // `_PHONEME.finditer(text)`.
    while (phoneme_pattern().search(subject, &match, scan)) {
        std::u32string raw_attrs;
        capture(subject, match, U"attrs", &raw_attrs);
        const auto attrs = attrs_of(raw_attrs);

        // alphabet is optional and, when given, must be ipa.
        if (const std::u32string* alphabet = find_attr(attrs, U"alphabet")) {
            if (lower(*alphabet) != U"ipa") {
                const std::string quoted = py_repr(*alphabet);
                throw MarkupError("<phoneme alphabet=" + quoted +
                                  ">: the only alphabet this library reads is 'ipa'. "
                                  "Reading " + quoted + " as if it were IPA would "
                                  "mispronounce the word silently, so it is refused.");
            }
        }

        const std::u32string* ph = find_attr(attrs, U"ph");
        if (ph == nullptr)
            throw MarkupError("<phoneme> with no `ph`: the element exists to give the "
                              "pronunciation, so it must carry one — "
                              "<phoneme ph=\"…\">word</phoneme>.");
        if (strip(*ph).empty())
            throw MarkupError("<phoneme ph=\"\"> is empty. To make a word silent, omit it "
                              "from the text; an empty pronunciation is more likely a "
                              "mistake than an intent.");

        std::u32string surface;
        capture(subject, match, U"text", &surface);
        if (strip(surface).empty())
            throw MarkupError("<phoneme ph=" + py_repr(*ph) +
                              "> wraps no text. The spelling is not decoration: "
                              "cross-word rules read the orthography, and a word with "
                              "none cannot take part in them.");

        if (match.start > pos)
            chunks.emplace_back(to_utf8(subject.substr(pos, match.start - pos)));
        chunks.emplace_back(to_utf8(surface), to_utf8(*ph));
        pos = match.end;
        scan = match.end > match.start ? match.end : match.start + 1;
    }

    if (pos < subject.size())
        chunks.emplace_back(to_utf8(subject.substr(pos)));

    // _reject_stray_tag over the plain runs, in the reference's own join.
    std::u32string remainder;
    for (const auto& chunk : chunks)
        if (!chunk.is_forced()) remainder += to_utf32(chunk.text);
    re32::Match stray;
    if (stray_pattern().search(remainder, &stray))
        throw MarkupError("unclosed <phoneme> tag: every <phoneme …> needs a matching "
                          "</phoneme>. Left as it is, the tag would be tokenised as "
                          "ordinary letters and read aloud.");

    if (chunks.empty()) chunks.emplace_back(std::string());   // `chunks or [Chunk("")]`
    return chunks;
}

} // namespace orthography2ipa::markup
