#include <orthography2ipa/orthography2ipa.hpp>

int main() {
    return orthography2ipa::transcribe("olá", "pt").empty() ? 1 : 0;
}
