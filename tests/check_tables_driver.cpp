// Standalone validation program for the three table-shaped ports:
// script_distance.py, markup.py and underspecification.py.
//
// It is a thin, format-stable shell around the C++ API so the Python driver
// (tests/_scratch/tables_parity.py) can drive the same cases through the
// reference and compare byte for byte. Every string crosses the boundary as
// lowercase hex over the UTF-8 bytes, so a case can contain a tab, a newline, a
// control character or an unpaired surrogate-free Unicode string without the
// framing changing.
//
//   tables_check registry                     the C++ SCRIPT_REGISTRY table
//   tables_check script-pairs                 script_distance_by_name over the
//                                             catalogued scripts, in key order
//   tables_check script-features              script_distance over one bundle
//                                             pair per line (14 tab fields)
//   tables_check markup                       parse_markup, one hex case per line
//   tables_check und                          the three underspecification
//                                             functions: "code<TAB>hex(word)"
#include "orthography2ipa/markup.hpp"
#include "orthography2ipa/orthography2ipa.hpp"
#include "orthography2ipa/script_distance.hpp"
#include "orthography2ipa/underspecification.hpp"

#include <cstdio>
#include <stdexcept>   // orthography2ipa.hpp uses std::runtime_error without it
#include <iostream>
#include <string>
#include <vector>

using namespace orthography2ipa;

namespace {

std::string hex(const std::string& text) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(text.size() * 2);
    for (const unsigned char ch : text) {
        out.push_back(digits[ch >> 4]);
        out.push_back(digits[ch & 0xf]);
    }
    return out;
}

std::string unhex(const std::string& text) {
    static const auto value = [](char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    std::string out;
    for (std::size_t i = 0; i + 1 < text.size(); i += 2)
        out.push_back(static_cast<char>(value(text[i]) * 16 + value(text[i + 1])));
    return out;
}

std::vector<std::string> split(const std::string& line, char sep) {
    std::vector<std::string> fields;
    std::string current;
    for (const char ch : line) {
        if (ch == sep) { fields.push_back(current); current.clear(); }
        else current.push_back(ch);
    }
    fields.push_back(current);
    return fields;
}

std::string number(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.17g", value);
    return std::string(buffer);
}

void mode_registry() {
    for (const auto& entry : script_distance::registry()) {
        const auto& f = entry.second;
        std::string ancestors;
        for (std::size_t i = 0; i < f.ancestor_scripts.size(); ++i) {
            if (i) ancestors += ",";
            ancestors += f.ancestor_scripts[i];
        }
        std::cout << hex(f.name) << "\t" << hex(f.script_type) << "\t"
                  << hex(f.directionality) << "\t" << (f.has_case ? "1" : "0") << "\t"
                  << hex(f.vowel_marking) << "\t" << hex(f.unicode_block) << "\t"
                  << hex(ancestors) << "\n";
    }
}

void mode_script_pairs() {
    std::vector<std::string> names;
    for (const auto& entry : script_distance::registry()) names.push_back(entry.first);
    for (const auto& a : names) {
        for (const auto& b : names) {
            std::cout << hex(a) << "\t" << hex(b) << "\t"
                      << number(script_distance::script_distance_by_name(a, b)) << "\n";
        }
    }
    // The forms the reference refuses: an unknown name is a KeyError.
    for (const char* bad : {"", "latin", "Latin ", "Phoenician", "Etruscan",
                            "IPA-reconstruction "}) {
        const std::string name(bad);
        for (const int swap : {0, 1}) {
            try {
                const double d = swap ? script_distance::script_distance_by_name("Latin", name)
                                      : script_distance::script_distance_by_name(name, "Latin");
                std::cout << hex(swap ? "Latin" : name) << "\t" << hex(swap ? name : "Latin")
                          << "\t" << number(d) << "\n";
            } catch (const std::out_of_range&) {
                std::cout << hex(swap ? "Latin" : name) << "\t" << hex(swap ? name : "Latin")
                          << "\tKEYERROR\n";
            }
        }
    }
}

/// One line: name, script_type, directionality, has_case, vowel_marking, block,
/// ancestors (comma separated) for each of the two bundles.
void mode_script_features() {
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto fields = split(line, '\t');
        if (fields.size() != 14) { std::cout << "BADLINE\n"; continue; }
        script_distance::ScriptFeatures a;
        script_distance::ScriptFeatures b;
        const std::string* src = fields.data();
        auto fill = [&src](script_distance::ScriptFeatures& f) {
            f.name = src[0];
            f.script_type = src[1];
            f.directionality = src[2];
            f.has_case = src[3] == "1";
            f.vowel_marking = src[4];
            f.unicode_block = src[5];
            std::string rest = src[6];
            if (!rest.empty()) {
                for (const auto& part : split(rest, ','))
                    if (!part.empty()) f.ancestor_scripts.push_back(part);
            }
            src += 7;
        };
        fill(a);
        fill(b);
        std::cout << line << "\t" << number(script_distance::script_distance(a, b)) << "\n";
    }
}

void mode_markup() {
    std::string line;
    while (std::getline(std::cin, line)) {
        // Each case is "x" + hex, so the empty input is a case and not a blank
        // line; a blank line is the stream's own terminator.
        if (line.empty()) continue;
        if (line[0] != 'x') { std::cout << line << "\tBADFRAME\n"; continue; }
        const std::string text = unhex(line.substr(1));
        std::cout << line << "\t";
        try {
            const auto chunks = markup::parse_markup(text);
            std::cout << "OK";
            for (const auto& chunk : chunks) {
                std::cout << "\t" << hex(chunk.text)
                          << (chunk.is_forced() ? "|" + hex(*chunk.forced_ipa) : "|");
            }
            std::cout << "\n";
        } catch (const markup::MarkupError& error) {
            std::cout << "ERR\t" << hex(std::string(error.what())) << "\n";
        } catch (const std::exception& error) {
            std::cout << "OTHER\t" << hex(std::string(error.what())) << "\n";
        }
    }
}

void mode_und() {
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto fields = split(line, '\t');
        if (fields.size() != 2) { std::cout << "BADLINE\n"; continue; }
        const std::string word = unhex(fields[1]);
        std::cout << fields[0] << "\t" << fields[1] << "\t";
        try {
            const LanguageSpec& spec = get(fields[0]);
            const auto positions = underspecification::underdetermined_positions(word, spec);
            std::string joined;
            for (const auto index : positions) {
                if (!joined.empty()) joined += ",";
                joined += std::to_string(index);
            }
            std::cout << (underspecification::is_underdetermined(word, spec) ? "1" : "0") << "\t"
                      << number(underspecification::mark_density(word, spec)) << "\t"
                      << joined << "\n";
        } catch (const std::exception& error) {
            std::cout << "ERR\t" << hex(std::string(error.what())) << "\n";
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "registry") mode_registry();
    else if (mode == "script-pairs") mode_script_pairs();
    else if (mode == "script-features") mode_script_features();
    else if (mode == "markup") mode_markup();
    else if (mode == "und") mode_und();
    else {
        std::cerr << "usage: tables_check registry|script-pairs|script-features|markup|und\n";
        return 2;
    }
    return std::cout.bad() ? 1 : 0;
}
