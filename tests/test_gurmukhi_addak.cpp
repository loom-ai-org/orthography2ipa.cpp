// The Gurmukhi addak doubles the consonant letter after it. The cases of the
// reference's tests/test_gurmukhi_addak.py.
//
// Unicode Standard, chapter 12, Gurmukhi: "U+0A71 GURMUKHI ADDAK is a special
// sign to indicate that the following consonant is geminate", and ISCII-1991
// spells ਪੱਗ pagg as <pa, ga, virama, ga>.
#include "orthography2ipa/orthography2ipa.hpp"
#include "orthography2ipa/phonetok.hpp"

#include <iostream>
#include <string>

using namespace orthography2ipa;

namespace {

int failures = 0;

#define CHECK_EQ(actual, expected)                                                     \
    do {                                                                               \
        const std::string got = (actual);                                              \
        if (got != (expected)) {                                                       \
            std::cout << "FAIL " << __LINE__ << ": " #actual " gave " << got << "\n";  \
            ++failures;                                                                \
        }                                                                              \
    } while (0)

}  // namespace

int main() {
    const G2P pa("pa");
    CHECK_EQ(pa.transcribe("ਬੱਚਾ"), "bətʃtʃaː");
    CHECK_EQ(pa.transcribe("ਪੱਗ"), "pəɡɡ");
    CHECK_EQ(pa.transcribe("ਕਿੱਤਾ"), "kɪt̪t̪aː");
    // A repeated addak doubles the letter once.
    CHECK_EQ(pa.transcribe("ਕੱੱਕ"), "kəkk");
    CHECK_EQ(pa.transcribe("ਕੱੱਕ"), pa.transcribe("ਕੱਕ"));
    // The Unicode spelling reads as the ISCII cluster.
    CHECK_EQ(pa.transcribe("ਪੱਗ"), pa.transcribe("ਪ" "ਗ" "੍" "ਗ"));
    // The doubled letter keeps its nukta.
    CHECK_EQ(pa.transcribe("ਪ" "ੱ" "ਸ" "਼" "ਾ"), pa.transcribe("ਪ" "ਸ" "਼" "੍" "ਸ" "਼" "ਾ"));
    // Word grouping indexes the prepared string.
    CHECK_EQ(pa.transcribe("ਬੱਚਾ ਪੱਗ"), "bətʃtʃaː pəɡɡ");

    const PhonetokTokenizer tokenizer(get("pa"));
    const std::string once = tokenizer.prepare("ਕੱੱਕ");
    CHECK_EQ(tokenizer.prepare(once), once);

    if (failures == 0) std::cout << "gurmukhi addak: ok\n";
    return failures == 0 ? 0 : 1;
}
