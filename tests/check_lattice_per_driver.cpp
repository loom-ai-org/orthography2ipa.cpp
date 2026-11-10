// Standalone validation program for src/lattice_per.cpp (the lattice_per.py
// port). Built by tests/lattice_per_parity.py's documented g++ line; NOT part
// of the library and NOT installed.
//
//   lattice_per_check cases <tsv>     compare every field against the reference dump
//   lattice_per_check score <hyp> <ref> <lang> <weighted> <beam>   print one result
//   lattice_per_check examples        the lattice_per.py docstring examples
//
// The case file is tab-separated: the three input strings are hex-encoded UTF-8
// (so no case can be mangled by a tab or a newline inside it), then weighted
// (0/1), beam width, and the reference's six numeric fields + resolved lang in
// hex, or the single token ERR when the reference raised.
#include "orthography2ipa/lattice_per.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace orthography2ipa;

namespace {

std::string unhex(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i + 1 < text.size(); i += 2)
        out.push_back(static_cast<char>(std::strtol(text.substr(i, 2).c_str(), nullptr, 16)));
    return out;
}

std::string hex(const std::string& text) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (const unsigned char c : text) {
        out.push_back(digits[c >> 4]);
        out.push_back(digits[c & 0x0F]);
    }
    return out;
}

std::string show(const std::string& text) {
    // ASCII is printed as-is; anything else as <hex> so a failure line stays
    // readable in a log.
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c >= 0x20 && c < 0x7F) out.push_back(static_cast<char>(c));
        else { out.push_back('<'); out += hex(text.substr(i, 1)); out.push_back('>'); }
    }
    return out;
}

std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    for (;;) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string::npos) { fields.push_back(line.substr(start)); break; }
        fields.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
    return fields;
}

std::string fmt(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.17g", value);
    return buffer;
}

void print_result(const per::LatticePER& r) {
    std::cout << "distance=" << fmt(r.distance) << " per=" << fmt(r.per)
              << " top_distance=" << fmt(r.top_distance) << " top_per=" << fmt(r.top_per)
              << " ref_segments=" << r.ref_segments << " hyp_segments=" << r.hyp_segments
              << " variant_credit=" << fmt(r.variant_credit()) << " lang=" << r.lang << "\n";
}

int examples() {
    // lattice_per.py's module docstring, verbatim.
    struct { const char* hyp; const char* ref; } CASES[] = {
        {"es\u033B pada", "ez bada"},
        {"es ba\u00f0a", "ez bada"},
    };
    const double expect_per[] = {0.16666666666666666, 0.16666666666666666};
    const double expect_vc[] = {0.0, 0.16666666666666666};
    const double expect_distance[] = {1.0, 1.0};
    int failures = 0;
    for (int i = 0; i < 2; ++i) {
        const per::LatticePER r = per::lattice_per(CASES[i].hyp, CASES[i].ref, "eu");
        std::cout << "docstring " << i << ": ";
        print_result(r);
        if (r.distance != expect_distance[i] || r.per != expect_per[i] ||
            r.variant_credit() != expect_vc[i]) {
            std::printf("  FAIL docstring example %d\n", i);
            ++failures;
        }
    }
    std::printf("docstring examples: %d failures\n", failures);
    return failures ? 1 : 0;
}

int cases(const std::string& path) {
    std::ifstream input(path);
    if (!input) { std::fprintf(stderr, "cannot open %s\n", path.c_str()); return 2; }
    std::string line;
    std::size_t total = 0, exact = 0, within = 0, failed = 0, errored = 0;
    double max_delta = 0.0, max_vc_delta = 0.0;
    std::size_t credited = 0;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const std::vector<std::string> f = split_tabs(line);
        // A row is either 5 input columns + 7 expected values, or 5 input
        // columns + the single ERR token (the reference raised on it).
        const bool expect_error = f.size() >= 6 && f[5] == "ERR";
        if (f.size() < (expect_error ? 6 : 12)) {
            std::fprintf(stderr, "malformed row: %s\n", line.c_str());
            return 2;
        }
        ++total;
        const std::string hyp = unhex(f[0]);
        const std::string ref = unhex(f[1]);
        const std::string lang = unhex(f[2]);
        const bool weighted = f[3] == "1";
        const std::size_t beam = static_cast<std::size_t>(std::strtoull(f[4].c_str(), nullptr, 10));

        std::string error;
        per::LatticePER got;
        try {
            got = per::lattice_per(hyp, ref, lang, weighted, beam);
        } catch (const std::exception& e) {
            error = e.what();
        }
        if (expect_error) {
            if (error.empty()) { ++failed; std::printf("FAIL(no-throw) %s\n", line.c_str()); }
            else ++errored;
            continue;
        }
        if (!error.empty()) {
            ++failed;
            std::printf("FAIL(throw) %s -> %s\n", line.c_str(), error.c_str());
            continue;
        }
        const double want_distance = std::strtod(f[5].c_str(), nullptr);
        const double want_per = std::strtod(f[6].c_str(), nullptr);
        const double want_top_distance = std::strtod(f[7].c_str(), nullptr);
        const double want_top_per = std::strtod(f[8].c_str(), nullptr);
        const std::size_t want_ref = std::strtoull(f[9].c_str(), nullptr, 10);
        const std::size_t want_hyp = std::strtoull(f[10].c_str(), nullptr, 10);
        const std::string want_lang = unhex(f[11]);

        const double deltas[4] = {std::abs(got.distance - want_distance),
                                  std::abs(got.per - want_per),
                                  std::abs(got.top_distance - want_top_distance),
                                  std::abs(got.top_per - want_top_per)};
        double worst = 0.0;
        for (const double d : deltas) worst = std::max(worst, d);
        max_delta = std::max(max_delta, worst);
        // The reference's variant_credit is the property `top_per - per`; the
        // port's is the same expression, so this checks the method against the
        // reference's own arithmetic, not a second implementation of it.
        const double want_vc = want_top_per - want_per;
        const double vc_delta = std::abs(got.variant_credit() - want_vc);
        if (vc_delta > max_vc_delta) max_vc_delta = vc_delta;
        if (want_vc != 0.0 || got.variant_credit() != 0.0) ++credited;
        const bool ints_ok = got.ref_segments == want_ref && got.hyp_segments == want_hyp;
        const bool lang_ok = got.lang == want_lang;
        const bool vc_ok = vc_delta <= 1e-12;
        if (worst == 0.0 && ints_ok && lang_ok && vc_ok) ++exact;
        if (worst <= 1e-12 && ints_ok && lang_ok && vc_ok) ++within;
        else {
            ++failed;
            std::printf("FAIL %s\n", line.c_str());
            std::printf("  hyp=%s ref=%s lang=%s weighted=%d beam=%zu\n",
                        show(hyp).c_str(), show(ref).c_str(), show(lang).c_str(),
                        static_cast<int>(weighted), beam);
            std::printf("  cpp:  distance=%s per=%s top=%s top_per=%s ref_seg=%zu hyp_seg=%zu lang=%s\n",
                        fmt(got.distance).c_str(), fmt(got.per).c_str(), fmt(got.top_distance).c_str(),
                        fmt(got.top_per).c_str(), got.ref_segments, got.hyp_segments,
                        got.lang.c_str());
            std::printf("  py:   distance=%s per=%s top=%s top_per=%s ref_seg=%zu hyp_seg=%zu lang=%s\n",
                        f[5].c_str(), f[6].c_str(), f[7].c_str(), f[8].c_str(), want_ref, want_hyp,
                        want_lang.c_str());
            std::printf("  max|delta|=%.3g ints_ok=%d lang_ok=%d vc_delta=%.3g\n", worst,
                        static_cast<int>(ints_ok), static_cast<int>(lang_ok), vc_delta);
        }
    }
    std::printf("lattice_per cpp-vs-reference: total=%zu exact=%zu within_tol=%zu failed=%zu "
                "both_threw=%zu max_delta=%.3g max_variant_credit_delta=%.3g "
                "rows_with_variant_credit=%zu\n",
                total, exact, within, failed, errored, max_delta, max_vc_delta, credited);
    return failed ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "examples") return examples();
    if (argc >= 3 && std::string(argv[1]) == "cases") return cases(argv[2]);
    if (argc >= 7 && std::string(argv[1]) == "score") {
        const bool weighted = std::string(argv[5]) == "1";
        const std::size_t beam = std::strtoull(argv[6], nullptr, 10);
        print_result(per::lattice_per(argv[2], argv[3], argv[4], weighted, beam));
        return 0;
    }
    std::fprintf(stderr,
                 "usage: lattice_per_check cases <tsv> | score <hyp> <ref> <lang> <w> <beam> | examples\n");
    return 2;
}
