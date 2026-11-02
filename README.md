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

## Reference data

The language specs in `data/` and the tables in `src/*_tables.inc` are copied from one
release of the Python [orthography2ipa](https://pypi.org/project/orthography2ipa/). The
file `tests/reference_version.txt` names that release. Specs and engine code change
together: a spec field without the code that reads it is ignored. For this reason the
build never fetches specs. They change only through the sync script:

```
python3 tools/sync_reference.py 7.111.14a1   # or: latest
```

The script downloads the wheel from PyPI and checks its sha256. It then replaces
`data/`, writes the pin, and runs the `tests/gen_*.py` generators against that wheel.
Run the script with a Python that has the reference's dependencies installed. After
a sync, rebuild and run the parity suite with `O2I_PYTHON_ROOT` set to a reference of
the same version. To try newer specs at runtime without a rebuild, point
`set_data_directory()` at them.
