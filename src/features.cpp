// Direct port of orthography2ipa/features.py.
#include "orthography2ipa/features.hpp"

#include "orthography2ipa/orthography2ipa.hpp"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <map>

namespace orthography2ipa::features {

FeatureValue FeatureValue::boolean(bool value) {
    auto out = FeatureValue();
    out.kind_ = Kind::Bool;
    out.bool_ = value;
    return out;
}

FeatureValue FeatureValue::integer(std::size_t value) {
    auto out = FeatureValue();
    out.kind_ = Kind::Int;
    out.int_ = value;
    return out;
}

FeatureValue FeatureValue::number(double value) {
    auto out = FeatureValue();
    out.kind_ = Kind::Number;
    out.number_ = value;
    return out;
}

FeatureValue FeatureValue::text(std::string value) {
    auto out = FeatureValue();
    out.kind_ = Kind::String;
    out.string_ = std::move(value);
    return out;
}

namespace {

/// Python `repr(float)` / `json.dumps` float rendering: the shortest decimal
/// that round-trips, and a trailing `.0` so a float never reads as an int.
std::string json_number(double value) {
    if (std::isnan(value)) return "NaN";                 // json.dumps(float('nan'))
    if (std::isinf(value)) return value > 0 ? "Infinity" : "-Infinity";
    char buffer[40];
    for (int precision = 1; precision <= 17; ++precision) {
        std::snprintf(buffer, sizeof buffer, "%.*g", precision, value);
        if (std::strtod(buffer, nullptr) == value) break;
    }
    std::string text(buffer);
    if (text.find_first_of(".e") == std::string::npos) text += ".0";
    return text;
}

/// Python `json.dumps(str, ensure_ascii=False)`: the string is emitted as it
/// stands, except for the escapes JSON requires (quotation mark, backslash and
/// the control characters, which are \u00XX).
std::string json_string(const std::string& value) {
    std::string out = "\"";
    for (const char c : value) {
        const auto code = static_cast<unsigned char>(c);
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (code < 0x20) {
            char buffer[8];
            std::snprintf(buffer, sizeof buffer, "\\u%04x", code);
            out += buffer;
        } else {
            out += c;
        }
    }
    out += "\"";
    return out;
}

} // namespace

std::string FeatureValue::to_json() const {
    switch (kind_) {
        case Kind::Null: return "null";
        case Kind::Bool: return bool_ ? "true" : "false";
        case Kind::Int: return std::to_string(int_);
        case Kind::Number: return json_number(number_);
        case Kind::String: return json_string(string_);
    }
    return "null";
}

std::vector<std::pair<std::string, FeatureValue>> GraphemeFeatures::as_dict() const {
    // features.py GraphemeFeatures.as_dict, in the reference's key order. The
    // ranked `candidates` lattice is deliberately absent: it is not a scalar
    // CRF feature.
    return {
        {"grapheme", FeatureValue::text(grapheme)},
        {"index", FeatureValue::integer(index)},
        {"position", FeatureValue::text(position)},
        {"span_start", FeatureValue::integer(span.first)},
        {"span_end", FeatureValue::integer(span.second)},
        {"prev", prev ? FeatureValue::text(*prev) : FeatureValue()},
        {"next", next ? FeatureValue::text(*next) : FeatureValue()},
        {"prev2", prev2 ? FeatureValue::text(*prev2) : FeatureValue()},
        {"next2", next2 ? FeatureValue::text(*next2) : FeatureValue()},
        {"is_vowel", FeatureValue::boolean(is_vowel)},
        {"is_consonant", FeatureValue::boolean(is_consonant)},
        {"is_front", FeatureValue::boolean(is_front)},
        {"is_back", FeatureValue::boolean(is_back)},
        {"top1_ipa", FeatureValue::text(top1_ipa)},
        {"top1_cost", FeatureValue::number(top1_cost)},
        {"margin", margin ? FeatureValue::number(*margin) : FeatureValue()},
        {"n_candidates", FeatureValue::integer(n_candidates)},
        {"confidence", FeatureValue::number(confidence)},
        {"script", FeatureValue::text(script)},
        {"code", FeatureValue::text(code)},
    };
}

std::vector<std::vector<std::pair<std::string, FeatureValue>>> WordFeatures::as_dicts() const {
    std::vector<std::vector<std::pair<std::string, FeatureValue>>> out;
    out.reserve(graphemes.size());
    for (const auto& grapheme : graphemes) out.push_back(grapheme.as_dict());
    return out;
}

namespace {

/// features.py _position: the word-relative position of grapheme `index` of a
/// word with `total` graphemes. Index 0 wins over the final test, so a
/// one-grapheme word is "initial".
const char* position_of(std::size_t index, std::size_t total) {
    if (index == 0) return "initial";
    if (index == total - 1) return "final";
    return "medial";
}

} // namespace

WordFeatures build_word_features(const std::string& word,
                                 const std::vector<rescorer::SegmentSlot>& slots,
                                 const std::vector<GraphemeContext>& contexts,
                                 double confidence,
                                 const std::string& code,
                                 const std::string& script) {
    // features.py build_word_features: `span_map` keys each context by its
    // START offset, so a slot whose context is gone (a rescorer dropped the
    // token, or the two lists came from different passes) still produces a
    // record — with no neighbours and every predicate false.
    std::map<std::size_t, const GraphemeContext*> span_map;
    for (const auto& context : contexts) span_map[context.span().first] = &context;

    const std::size_t total = slots.size();
    WordFeatures out;
    out.word = word;
    out.code = code;
    out.script = script;
    out.confidence = confidence;

    for (std::size_t index = 0; index < total; ++index) {
        const auto& slot = slots[index];
        const auto found = span_map.find(slot.span.first);
        const GraphemeContext* ctx = found == span_map.end() ? nullptr : found->second;

        GraphemeFeatures record;
        record.grapheme = slot.grapheme;
        record.span = slot.span;
        record.index = index;
        record.position = position_of(index, total);
        for (const auto& candidate : slot.candidates)
            record.candidates.emplace_back(candidate.ipa, candidate.score);

        // features.py `_nb`: the neighbour grapheme `offset` positions away,
        // read off the matched context; nullopt with no context or across a
        // word edge (GraphemeContext::at already refuses to leave the run).
        auto neighbour = [ctx](int offset) -> std::optional<std::string> {
            if (ctx == nullptr) return std::nullopt;
            const GraphemeContext* other = ctx->at(offset);
            if (other == nullptr) return std::nullopt;
            return other->grapheme();
        };
        record.prev = neighbour(-1);
        record.next = neighbour(1);
        record.prev2 = neighbour(-2);
        record.next2 = neighbour(2);

        record.is_vowel = ctx != nullptr && ctx->is_vowel();
        record.is_consonant = ctx != nullptr && ctx->is_consonant();
        record.is_front = ctx != nullptr && ctx->is_front();
        record.is_back = ctx != nullptr && ctx->is_back();

        record.n_candidates = slot.candidates.size();
        record.top1_ipa = slot.candidates.empty() ? std::string() : slot.candidates[0].ipa;
        record.top1_cost = slot.candidates.empty() ? 0.0 : slot.candidates[0].score;
        record.margin = slot.candidates.size() >= 2
            ? std::optional<double>(slot.candidates[1].score - slot.candidates[0].score)
            : std::nullopt;
        record.confidence = confidence;
        record.script = script;
        record.code = code;
        out.graphemes.push_back(std::move(record));
    }
    return out;
}

} // namespace orthography2ipa::features
