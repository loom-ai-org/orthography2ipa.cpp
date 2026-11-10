// Reads one tab-separated command per line and writes one result line for
// tests/packaged_lexicon_parity.py. The lexicon state carries from one command
// to the next. A result is "ok<TAB>value" or the Python exception name and its
// message, then "<TAB>W:" and the warnings the command wrote, joined by " | ".
//   use[<TAB>code...]       use_packaged_lexicons(), or with the listed codes
//                           ("use<TAB>" alone passes an empty list)
//   sources                 packaged_lexicon_sources()
//   patch-sources           packaged_lexicon_sources(patches=true)
//   packaged<TAB>code       packaged_lexicon(code)
//   patched<TAB>code        packaged_lexicon(code, patches=true)
//   patched-word<TAB>code<TAB>word
//                           transcribe_word with an engine whose Pipeline
//                           lexicon is packaged_lexicon(code, patches=true)
//   available               available_lexicon_codes()
//   get<TAB>code            get_lexicon(code)
//   path<TAB>code           lexicon_path(code)
//   word<TAB>code<TAB>word  G2P(code).transcribe_word(word)
//   register<TAB>code<TAB>source, dir<TAB>path, clear
#include "orthography2ipa/orthography2ipa.hpp"

#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace orthography2ipa;

namespace {

std::string joined(const std::vector<std::string>& values) {
    std::string out;
    for (std::size_t i = 0; i < values.size(); ++i) out += (i ? "," : "") + values[i];
    return out;
}

std::string run(const std::vector<std::string>& field, std::map<std::string, std::unique_ptr<G2P>>& engines) {
    const std::string& command = field[0];
    if (command == "use") {
        if (field.size() == 1) return joined(use_packaged_lexicons());
        std::vector<std::string> codes;
        for (std::size_t i = 1; i < field.size(); ++i)
            if (!field[i].empty()) codes.push_back(field[i]);
        return joined(use_packaged_lexicons(codes));
    }
    if (command == "sources" || command == "patch-sources") {
        std::string out;
        for (const auto& [code, path] : packaged_lexicon_sources(command == "patch-sources"))
            out += code + "=" + path + ";";
        return out;
    }
    if (command == "packaged" || command == "patched") {
        std::string out;
        for (const auto& [word, ipa] : packaged_lexicon(field[1], command == "patched")) out += word + "=" + ipa + ",";
        return out;
    }
    if (command == "patched-word") {
        auto& engine = engines["patched\t" + field[1]];
        if (!engine)
            engine = std::make_unique<G2P>(field[1], std::map<std::string, std::vector<std::string>>{}, "", false,
                                           true, true, true, nullptr, nullptr,
                                           Pipeline{packaged_lexicon(field[1], true)});
        return engine->transcribe_word(field[2]);
    }
    if (command == "available") return joined(available_lexicon_codes());
    if (command == "get") {
        std::string out;
        for (const auto& [word, ipa] : get_lexicon(field[1])) out += word + "=" + ipa + ",";
        return out;
    }
    if (command == "path") return lexicon_path(field[1]).value_or("None");
    if (command == "word") {
        auto& engine = engines[field[1]];
        if (!engine) engine = std::make_unique<G2P>(field[1]);
        return engine->transcribe_word(field[2]);
    }
    if (command == "register") register_lexicon(field[1], field[2]);
    else if (command == "dir") set_lexicon_directory(field[1]);
    else if (command == "clear") clear_lexicons();
    else throw std::invalid_argument("unknown command " + command);
    return "";
}

} // namespace

int main() {
    std::map<std::string, std::unique_ptr<G2P>> engines;
    std::ostringstream warnings;
    std::streambuf* const stderr_buffer = std::cerr.rdbuf(warnings.rdbuf());
    std::string line;
    while (std::getline(std::cin, line)) {
        std::vector<std::string> field;
        std::istringstream parts(line);
        for (std::string part; std::getline(parts, part, '\t');) field.push_back(part);
        if (!line.empty() && line.back() == '\t') field.emplace_back();
        std::string result;
        try {
            result = "ok\t" + run(field, engines);
        } catch (const LookupError& error) {
            result = std::string("LookupError\t") + error.what();
        } catch (const FileNotFoundError& error) {
            result = std::string("FileNotFoundError\t") + error.what();
        }
        std::vector<std::string> written;
        std::istringstream lines(warnings.str());
        for (std::string warning; std::getline(lines, warning);) written.push_back(warning);
        warnings.str("");
        std::string joined_warnings;
        for (std::size_t i = 0; i < written.size(); ++i) joined_warnings += (i ? " | " : "") + written[i];
        std::cout << result << "\tW:" << joined_warnings << '\n';
    }
    std::cerr.rdbuf(stderr_buffer);
}
