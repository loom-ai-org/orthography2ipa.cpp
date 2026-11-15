// Validation driver for the linguatag port: reads one request per line on
// stdin and writes the port's answer on one line of stdout, so that
// tests/linguatag_parity.py can compare it with the installed Python
// linguatag.
//
//   usage: orthography2ipa-check-linguatag < requests.tsv
//
// Request line: FN \t ARG... (TAB-separated). Answer line: the result, "None"
// for the source's None, "!ERR" where the call throws.
//
//   std tag                                  standardize_lang
//   dr tag                                   default_region
//   wdr tag                                  with_default_region
//   pu tag                                   private_use, joined by ','
//   wpu tag                                  without_private_use
//   steps a b                                lect_steps
//   dist desired supported mode              lang_distance
//   closest target max_distance mode cand... closest_lang
//
// mode is none, dialect or standard; none is the source's lects=None.
#include "orthography2ipa/linguatag.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace lt = orthography2ipa::linguatag;

namespace {

std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> out;
    std::string field;
    std::istringstream in(line);
    while (std::getline(in, field, '\t')) out.push_back(field);
    if (!line.empty() && line.back() == '\t') out.emplace_back();
    return out;
}

lt::Lects mode(const std::string& name) {
    if (name == "dialect") return lt::Lects::dialect;
    if (name == "standard") return lt::Lects::standard;
    return lt::Lects::none;
}

std::string answer(const std::vector<std::string>& f) {
    const std::string& fn = f.at(0);
    if (fn == "std") return lt::standardize_lang(f.at(1));
    if (fn == "dr") return lt::default_region(f.at(1)).value_or("None");
    if (fn == "wdr") return lt::with_default_region(f.at(1));
    if (fn == "pu") {
        std::string out;
        for (const auto& subtag : lt::private_use(f.at(1))) out += (out.empty() ? "" : ",") + subtag;
        return out;
    }
    if (fn == "wpu") return lt::without_private_use(f.at(1));
    if (fn == "steps") {
        const auto steps = lt::lect_steps(f.at(1), f.at(2));
        return steps ? std::to_string(*steps) : "None";
    }
    if (fn == "dist") return std::to_string(lt::lang_distance(f.at(1), f.at(2), mode(f.at(3))));
    if (fn == "closest") {
        const std::vector<std::string> available(f.begin() + 4, f.end());
        return lt::closest_lang(f.at(1), available, std::stoi(f.at(2)), mode(f.at(3))).value_or("None");
    }
    throw std::invalid_argument("unknown request " + fn);
}

}  // namespace

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            std::cout << answer(split(line)) << '\n';
        } catch (const std::exception&) {
            std::cout << "!ERR\n";
        }
    }
}
