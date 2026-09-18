# orthography2ipa.cpp

C++17 port of the data-driven core of
[`orthography2ipa`](https://github.com/TigreGotico/orthography2ipa).

The project builds a reusable `orthography2ipa` library and the
`orthography2ipa` command-line program. It consumes the upstream JSON language
specifications from `data/`, including grapheme maps, positional mappings,
exceptions, stress metadata, inventories, locations, and inheritance.

## Build

```sh
cmake -S . -B build
cmake --build build -j
```

The catalog is bundled from the Python project. To use another catalog:

```sh
./build/orthography2ipa --data /path/to/data transcribe pt "olá mundo"
```

## CLI

```sh
orthography2ipa list
orthography2ipa list --families
orthography2ipa info pt-BR
orthography2ipa transcribe pt "olá mundo"
orthography2ipa transcribe en-GB --lexicon /path/to/en-GB.tsv "through"
orthography2ipa distance pt-BR pt-PT
```

## Library

Include `orthography2ipa/orthography2ipa.hpp` and link `orthography2ipa`:

```cpp
#include <orthography2ipa/orthography2ipa.hpp>

auto ipa = orthography2ipa::transcribe("olá mundo", "pt");
auto spec = orthography2ipa::get("en-GB");
```

The native port covers registry/spec loading, aliases, maximal-munch tokenization,
candidate beams, basic stress placement, detailed transcription, sidecar lexicons,
declarative allophone rules, and registered normalize/rescore/sandhi plugin hooks.
Plugin classes are declared in the public header and can be selected by the JSON
`plugins` field or explicit caller overrides. Runtime conformance checks and
`who_answers()` introspection are also available. Remote URL/Hugging Face lexicon
fetching and the most advanced Python allophone contexts remain future work.
