// See syllabifiers.hpp. The language codes and the priority are the
// reference's; the provider indirection is documented there.
#include "orthography2ipa/syllabifiers.hpp"

#include <mutex>
#include <stdexcept>
#include <utility>

namespace orthography2ipa {
namespace syllabifiers {

struct SilabificadorSyllabifier::Impl {
    SilabificadorSyllabifier::Provider provider;
};

namespace {

std::mutex& provider_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::shared_ptr<SilabificadorSyllabifier::Provider>& provider_slot() {
    static std::shared_ptr<SilabificadorSyllabifier::Provider> slot;
    return slot;
}

} // namespace

bool SilabificadorSyllabifier::provide(Provider provider) {
    const std::lock_guard<std::mutex> guard(provider_mutex());
    provider_slot() = std::make_shared<Provider>(std::move(provider));
    return true;
}

SilabificadorSyllabifier::SilabificadorSyllabifier() {
    std::shared_ptr<Provider> provider;
    {
        const std::lock_guard<std::mutex> guard(provider_mutex());
        provider = provider_slot();
    }
    if (provider == nullptr)
        // The reference's `from silabificador import Syllabifier` — an
        // ImportError the discovery layer catches and carries on without.
        throw std::runtime_error(
            "the 'silabificador' package is not present: Portuguese"
            " syllables beyond the built-in splitter need"
            " orthography2ipa[portuguese] (silabificador.Syllabifier)");
    impl_ = std::make_shared<Impl>();
    impl_->provider = *provider;
}

std::vector<std::string> SilabificadorSyllabifier::language_codes() const {
    // syllabifiers.py language_codes: the Portuguese lects the package covers.
    // The regional pt-BR specs read the pt-BR grapheme table (graphemes_base),
    // so they take the pt-BR syllables with it.
    return {"pt", "pt-PT", "pt-BR", "pt-AO", "pt-MZ", "pt-TL",
            "pt-CV", "pt-GW", "pt-ST", "pt-MO", "pt-UY",
            "pt-BR-x-bahia", "pt-BR-x-caipira", "pt-BR-x-ce",
            "pt-BR-x-cuiaba", "pt-BR-x-floripa", "pt-BR-x-fluminen",
            "pt-BR-x-mg", "pt-BR-x-norte", "pt-BR-x-pr", "pt-BR-x-recife",
            "pt-BR-x-rj", "pt-BR-x-sp", "pt-BR-x-sul"};
}

std::vector<std::string> SilabificadorSyllabifier::syllabify(
        const std::string& word, const std::string& lang) const {
    (void)lang;   // silabificador.syllabify(word) takes only the word
    return impl_->provider(word);
}

} // namespace syllabifiers
} // namespace orthography2ipa
