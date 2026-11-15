// Reads one weighted grapheme value per line, {"ipa": [...], "weights": [...]},
// and writes two lines for tests/weights_parity.py: float(sum(weights)) and
// candidate_base_costs. Each is a list of repr() texts, or the OverflowError
// message where Python raises one.
#include "beam.hpp"
#include "orthography2ipa/orthography2ipa.hpp"
#include "py_float.hpp"

#include <boost/property_tree/json_parser.hpp>

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace orthography2ipa;

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream input(line);
        boost::property_tree::ptree value;
        boost::property_tree::read_json(input, value);
        const auto [ipa, weights] = normalize_grapheme_value(value);
        try {
            detail::PythonSum sum;
            for (const CandidateWeight& w : *weights)
                w.integer.empty() ? sum.add_float(w.value) : sum.add_int(w.integer);
            const double total = sum.value();
            std::cout << "sum " << detail::py_float(total) << '\n';
        } catch (const std::overflow_error& error) {
            std::cout << "sum OverflowError: " << error.what() << '\n';
        }
        try {
            const std::vector<double> costs = beam::candidate_base_costs(*ipa, weights, "g");
            std::cout << "costs";
            for (const double cost : costs) std::cout << ' ' << detail::py_float(cost);
            std::cout << '\n';
        } catch (const std::overflow_error& error) {
            std::cout << "costs OverflowError: " << error.what() << '\n';
        }
    }
}
