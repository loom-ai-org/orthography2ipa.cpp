// Reads one tab-separated lexicon command per line and writes one result line
// for tests/lexicon_parity.py: "ok<TAB>value", or the Python exception name and
// its message.
//   resolve<TAB>source      resolve_lexicon_source(source)
//   get<TAB>code<TAB>source register_lexicon, then get_lexicon(code)
//   path<TAB>code<TAB>source register_lexicon, then lexicon_path(code)
//   dir<TAB>dir<TAB>code    set_lexicon_directory, then lexicon_path(code)
//   envdir<TAB>code         lexicon_path(code) under $ORTHOGRAPHY2IPA_LEXICON_DIR
//   envget<TAB>code         get_lexicon(code) under $ORTHOGRAPHY2IPA_LEXICON_DIR
#include "orthography2ipa/orthography2ipa.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace orthography2ipa;

namespace {

std::string run(const std::vector<std::string>& field) {
    const std::string& command = field[0];
    if (command == "resolve") return resolve_lexicon_source(field[1]);
    if (command == "envget") {
        std::string entries;
        for (const auto& [word, ipa] : get_lexicon(field[1])) entries += word + "=" + ipa + ",";
        return entries;
    }
    if (command == "get" || command == "path") {
        register_lexicon(field[1], field[2]);
        if (command == "path") return lexicon_path(field[1]).value_or("None");
        std::string entries;
        for (const auto& [word, ipa] : get_lexicon(field[1])) entries += word + "=" + ipa + ",";
        return entries;
    }
    if (command == "dir") set_lexicon_directory(field[1]);
    return lexicon_path(field.back()).value_or("None");
}

} // namespace

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::vector<std::string> field;
        std::istringstream parts(line);
        for (std::string part; std::getline(parts, part, '\t');) field.push_back(part);
        try {
            const std::string value = run(field);
            std::cout << "ok\t" << value << '\n';
        } catch (const FileNotFoundError& error) {
            std::cout << "FileNotFoundError\t" << error.what() << '\n';
        } catch (const std::runtime_error& error) {
            std::cout << "RuntimeError\t" << error.what() << '\n';
        }
        clear_lexicons();
    }
}
