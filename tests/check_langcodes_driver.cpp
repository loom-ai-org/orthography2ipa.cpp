// Standalone validation driver for the langcodes port: reads tab-separated
// cases from a file and writes the port's own answers back as TSV, so that
// tests/langcodes_parity.py can diff them against the installed Python
// langcodes. It is not a unit test and it is not built by CMake.
//
//   usage: check_langcodes <cases.tsv>
//
// Case line:   MODE \t A \t B \t C      (unused columns are empty)
// Answer line: MODE \t A \t B \t C \t RESULT, RESULT == "!ERR" where langcodes
// would raise.
//
//   P tag         <normalize 0|1>   the parsed subtags and their to_tag()
//   S tag         <macro 0|1>       standardize_tag
//   M tag                   maximize(get(tag)).to_tag()
//   A tag                   assume_script(get(tag)).to_tag()
//   R tag                   simplify_script(get(tag)).to_tag()
//   Q tag                   broader_tags(get(tag))
//   D desired   supported <ignore_script 0|1> tag_distance
#include "orthography2ipa/langcodes.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace lc = orthography2ipa::langcodes;

namespace {

std::string join(const std::vector<std::string>& parts, const std::string& sep) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}

/// The parsed subtags: fields separated by \x1f, list items by ',' in the order
/// the reference stored them (to_tag() sorts them, so both views matter).
std::string fields(const lc::Language& lang) {
    std::ostringstream out;
    out << lang.language << '\x1f' << join(lang.extlangs, ",") << '\x1f' << lang.script
        << '\x1f' << lang.territory << '\x1f' << join(lang.variants, ",") << '\x1f'
        << join(lang.extensions, ",") << '\x1f' << lang.private_use << '\x1f'
        << lang.to_tag();
    return out.str();
}

bool flag(const std::string& text) { return text == "1"; }

std::string answer(const std::string& mode, const std::string& a, const std::string& b,
                   const std::string& c) {
    try {
        if (mode == "P") return fields(lc::get(a, flag(c)));
        if (mode == "S") return lc::standardize_tag(a, flag(c));
        if (mode == "M") return lc::max_form(a);
        if (mode == "A") return lc::assume_script(lc::get(a)).to_tag();
        if (mode == "R") return lc::simplify_script(lc::get(a)).to_tag();
        if (mode == "Q") return join(lc::broader_tags(lc::get(a)), " ");
        if (mode == "D") {
            const std::optional<int> distance = lc::tag_distance(a, b, flag(c));
            if (!distance) return "!ERR";
            return std::to_string(*distance);
        }
    } catch (const std::exception&) {
        return "!ERR";
    }
    return "!ERR";
}

std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> parts;
    std::string current;
    for (const char ch : line) {
        if (ch == '\t') {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    parts.push_back(current);
    while (parts.size() < 4) parts.emplace_back();
    return parts;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: check_langcodes <cases.tsv>\n";
        return 2;
    }
    if (!freopen(argv[1], "r", stdin)) {
        std::cerr << "cannot open " << argv[1] << "\n";
        return 2;
    }
    std::cout.tie(nullptr);
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        const std::vector<std::string> parts = split_tabs(line);
        const std::string mode = parts[0];
        const std::string& a = parts[1];
        const std::string& b = parts[2];
        const std::string& c = parts[3];
        std::cout << mode << '\t' << a << '\t' << b << '\t' << c << '\t'
                  << answer(mode, a, b, c) << '\n';
    }
    return 0;
}
