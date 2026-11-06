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

Portuguese syllables come from the C++ port of
[silabificador](https://github.com/loom-ai-org/silabificador.cpp) when CMake
finds its `silabificador` package. The Python package loads the same plugin
through the `orthography2ipa[portuguese]` extra.

The plugin claims the `pt` codes at priority 50. Point CMake at the install
prefix:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/silabificador/prefix
```

When CMake does not find the package, the bundled vowel-group splitter
syllabifies Portuguese, as the Python package does without the extra. The
configure step prints a status line only when it finds silabificador.

The installed `orthography2ipaConfig.cmake` calls
`find_dependency(silabificador)` when the build used it. The one exception is
a shared orthography2ipa built against a static silabificador: the shared
library then contains silabificador, so its consumers do not need the package.

The parity tests compare against the Python reference, so the reference must
match the build. When the build uses silabificador, install the reference
with `orthography2ipa[portuguese]`.

The reference finds silabificador through an entry point, so a source
checkout of the reference must also be installed. When the build does not use
silabificador, the reference must not have it.

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

## Lexicons

A `Pipeline` gives one engine its own lexicon and syllabifier. Another engine in the
same process does not see them, so two engines can run different pipelines side by
side. The order of precedence is `spec.word_exceptions`, then the pipeline lexicon,
then the rules.

```cpp
using namespace orthography2ipa;
const Pipeline pipeline{load_lexicon("/path/to/en-GB.tsv")};
const G2P engine("en-GB", {}, "", false, true, true, true, nullptr, nullptr, pipeline);
```

`load_lexicon` reads a file into a mapping, and `packaged_lexicon` reads the mapping
that an installed package supplies. Neither sets any state. An empty mapping is a
lexicon with no entries. An engine with no pipeline lexicon reads the process-wide
functions: `register_lexicon`, `set_lexicon_directory`, `use_packaged_lexicons`,
`clear_lexicons` and `get_lexicon`. These functions are deprecated, and version
8.0.0 removes them.

## Packaged lexicons

A package such as [o2i-lexicons](https://pypi.org/project/o2i-lexicons/) supplies a
residual lexicon per language: the words that the rules get wrong, in a
`word<TAB>ipa` file. The install changes no output. The caller gives a packaged
lexicon to an engine in code, as in Python:

```cpp
orthography2ipa::packaged_lexicon_sources();          // lists the providers
orthography2ipa::packaged_lexicon("pt-PT");           // the mapping, or throws LookupError
```

Python finds the providers through the `orthography2ipa.lexicons`
entry points. C++ has no entry points, so the port reads a search path of provider
roots. Each root has the layout of the package's data directory,
`<root>/<code>/<source>.tsv`:

- `$ORTHOGRAPHY2IPA_PACKAGED_LEXICONS` lists the roots, separated by `:`, in order.
  An empty value names no root.
- When the variable is unset, the root is `<prefix>/share/o2i-lexicons`, the
  directory beside the installed spec data.

Each directory under a root that holds a `*.tsv` file is one provider for the code
that the directory names. The provider fails when it holds more than one `*.tsv`
file. A name that does not resolve exactly to a spec is skipped. When two
providers serve one spec, the one named for the exact code wins, then the one in
the earlier root. To use the lexicons of the installed Python package:

```sh
export ORTHOGRAPHY2IPA_PACKAGED_LEXICONS="$(python3 -c 'import o2i_lexicons; print(o2i_lexicons.DATA_DIR)')"
```

## Reference data

The language specs in `data/` and the tables in `src/*_tables.inc` are copied from one
release of the Python [orthography2ipa](https://pypi.org/project/orthography2ipa/). The
file `tests/reference_version.txt` names that release. Specs and engine code change
together: a spec field without the code that reads it is ignored. For this reason the
build never fetches specs. They change only through the sync script:

```
python3 tools/sync_reference.py 7.118.4a1  # or: latest
```

The script downloads the wheel from PyPI and checks its sha256. It then replaces
`data/`, writes the pin, and runs the `tests/gen_*.py` generators against that wheel.
Run the script with a Python that has the reference's dependencies installed. After
a sync, rebuild and run the parity suite with `O2I_PYTHON_ROOT` set to a reference of
the same version. To try newer specs at runtime without a rebuild, point
`set_data_directory()` at them.
