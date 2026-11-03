// Reads one double per line as a C hex float (Python float.hex()) and writes
// its py_float text, for tests/py_float_parity.py.
#include "py_float.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

int main() {
    std::string line;
    while (std::getline(std::cin, line))
        std::cout << orthography2ipa::detail::py_float(std::strtod(line.c_str(), nullptr)) << '\n';
}
