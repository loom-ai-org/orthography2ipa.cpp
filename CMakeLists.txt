cmake_minimum_required(VERSION 3.16)
project(orthography2ipa VERSION 0.1.0 LANGUAGES CXX)

find_package(ICU REQUIRED COMPONENTS uc)
find_package(Boost REQUIRED)
# The reference reaches its Portuguese syllabifier through the optional
# orthography2ipa[portuguese] extra; the C++ port of that package is optional
# here in the same way.
find_package(silabificador CONFIG QUIET)
include(CTest)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

add_library(orthography2ipa
    src/allophony.cpp
    src/beam.cpp
    src/codepoint_regex.cpp
    src/conformance.cpp
    src/exceptions.cpp
    src/features.cpp
    src/feats.cpp
    src/inventory.cpp
    src/langcodes.cpp
    src/lattice_per.cpp
    src/linguatag.cpp
    src/lm.cpp
    src/markup.cpp
    src/orthography2ipa.cpp
    src/phonetok.cpp
    src/registry.cpp
    src/rescorer.cpp
    src/sandhi.cpp
    src/schema.cpp
    src/script_distance.cpp
    src/sentence.cpp
    src/stress.cpp
    src/syllabifiers.cpp
    src/tone.cpp
    src/transform.cpp
    src/types.cpp
    src/underspecification.cpp
    src/unicode_util.cpp
    src/vowels.cpp)
target_include_directories(orthography2ipa PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(orthography2ipa PUBLIC ICU::uc Boost::headers ${CMAKE_DL_LIBS})
# A static build links into the shared libraries of its consumers.
set_target_properties(orthography2ipa PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(orthography2ipa PUBLIC
    O2I_DEFAULT_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    O2I_INSTALL_DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/orthography2ipa")
# The specs relative to the installed library, or to the executable it is
# linked into: an install made with `cmake --install --prefix` into another
# prefix reads them before the source tree and the configured prefix. A
# program of this build tree keeps the source tree.
file(RELATIVE_PATH o2i_data_from_lib "${CMAKE_INSTALL_FULL_LIBDIR}" "${CMAKE_INSTALL_FULL_DATADIR}/orthography2ipa")
file(RELATIVE_PATH o2i_data_from_bin "${CMAKE_INSTALL_FULL_BINDIR}" "${CMAKE_INSTALL_FULL_DATADIR}/orthography2ipa")
target_compile_definitions(orthography2ipa PRIVATE
    O2I_DATA_FROM_LIBDIR="${o2i_data_from_lib}" O2I_DATA_FROM_BINDIR="${o2i_data_from_bin}"
    O2I_BUILD_DIR="${CMAKE_BINARY_DIR}")
target_compile_options(orthography2ipa PRIVATE -Wall -Wextra -Wpedantic)
if(silabificador_FOUND)
    message(STATUS "silabificador ${silabificador_VERSION}: Portuguese syllables come from it")
    target_link_libraries(orthography2ipa PRIVATE silabificador::silabificador)
    target_compile_definitions(orthography2ipa PRIVATE O2I_HAVE_SILABIFICADOR)
endif()
# Consumers need the silabificador package unless it is linked into a shared
# orthography2ipa: a static orthography2ipa hands silabificador on, and a
# shared silabificador stays a library of its own.
set(O2I_EXPORTS_SILABIFICADOR OFF)
if(silabificador_FOUND)
    get_target_property(o2i_library_type orthography2ipa TYPE)
    get_target_property(silabificador_library_type silabificador::silabificador TYPE)
    if(NOT (o2i_library_type STREQUAL "SHARED_LIBRARY" AND
            silabificador_library_type STREQUAL "STATIC_LIBRARY"))
        set(O2I_EXPORTS_SILABIFICADOR ON)
    endif()
endif()

add_executable(orthography2ipa-cli src/main.cpp)
target_link_libraries(orthography2ipa-cli PRIVATE orthography2ipa)
set_target_properties(orthography2ipa-cli PROPERTIES OUTPUT_NAME orthography2ipa)

install(TARGETS orthography2ipa orthography2ipa-cli
        EXPORT orthography2ipaTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY data/ DESTINATION ${CMAKE_INSTALL_DATADIR}/orthography2ipa)
install(EXPORT orthography2ipaTargets
        FILE orthography2ipaTargets.cmake
        NAMESPACE orthography2ipa::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/orthography2ipa)
configure_package_config_file(
    cmake/orthography2ipaConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/orthography2ipaConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/orthography2ipa)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/orthography2ipaConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/orthography2ipaConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/orthography2ipaConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/orthography2ipa)

if(BUILD_TESTING)
    find_package(Threads REQUIRED)
    add_executable(orthography2ipa-tests tests/test_core.cpp)
    target_link_libraries(orthography2ipa-tests PRIVATE orthography2ipa Threads::Threads)
    # The stress/ending parity checks reach the internal beam header
    # (src/beam.hpp), which owns the positional.py port.
    target_include_directories(orthography2ipa-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME orthography2ipa-core COMMAND orthography2ipa-tests)
    # One const pt-PT tokenizer shared by four threads: its closed-hiatus cache
    # must give each thread the tokens of a tokenizer that one thread uses.
    add_executable(orthography2ipa-threads-phonetok tests/threads_phonetok.cpp)
    target_link_libraries(orthography2ipa-threads-phonetok PRIVATE orthography2ipa Threads::Threads)
    add_test(NAME orthography2ipa-threads-phonetok COMMAND orthography2ipa-threads-phonetok)
    add_executable(orthography2ipa-packaged-lexicons tests/test_packaged_lexicons.cpp)
    target_link_libraries(orthography2ipa-packaged-lexicons PRIVATE orthography2ipa)
    add_test(NAME orthography2ipa-packaged-lexicons COMMAND orthography2ipa-packaged-lexicons)
    # `--json distance` writes Python repr floats, as the reference's json.dumps
    # does. The pt-PT/es-ES allophone overlap is 7/13.
    add_test(NAME orthography2ipa-cli-distance-json COMMAND orthography2ipa-cli --json distance pt-PT es-ES)
    set_tests_properties(orthography2ipa-cli-distance-json PROPERTIES
        PASS_REGULAR_EXPRESSION "\"allophone\":0\\.5384615384615384[,}]")

    # ── parity-harness drivers ────────────────────────────────────────────────
    # The per-module harnesses drive a small check binary instead of the CLI
    # (a process per spec for 7662 specs is not workable through the CLI).
    foreach(driver langcodes linguatag inventory lm_feats lattice_per features_conformance tables sentence py_float weights
                   packaged_lexicons)
        add_executable(orthography2ipa-check-${driver} tests/check_${driver}_driver.cpp)
        target_link_libraries(orthography2ipa-check-${driver} PRIVATE orthography2ipa)
        # Several drivers reach the library's private beam/unicode headers.
        target_include_directories(orthography2ipa-check-${driver} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    endforeach()
    # lexicon_parity.py drives it with HOME and the lexicon dir set per run.
    add_executable(orthography2ipa-check-lexicon tests/check_lexicon_driver.cpp)
    target_link_libraries(orthography2ipa-check-lexicon PRIVATE orthography2ipa)
    configure_file(tests/install_check.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/install_check.cmake @ONLY)
    add_test(NAME orthography2ipa-install COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/install_check.cmake)
    configure_file(tests/relocated_install_check.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/relocated_install_check.cmake @ONLY)
    add_test(NAME orthography2ipa-relocated-install
             COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/relocated_install_check.cmake)
    configure_file(tests/install_layouts_check.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/install_layouts_check.cmake @ONLY)
    add_test(NAME orthography2ipa-install-layouts
             COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/install_layouts_check.cmake)
    set_tests_properties(orthography2ipa-install-layouts PROPERTIES TIMEOUT 3600)
    # `list --family` follows cli.py _cmd_list: it matches the whole path or
    # any one step without regard to case, and an unknown family prints
    # "Unknown family: X" on stderr alone and exits 1.
    add_test(NAME orthography2ipa-cli-list-family
             COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:orthography2ipa-cli> -DARGS=list|--family|romance -DRESULT=0
                     "-DSTDOUT_MATCHES=\nIndo-European > Italic > Romance > Ibero-Romance: an, an-x-ansotano, "
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_check.cmake)
    add_test(NAME orthography2ipa-cli-list-family-unknown
             COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:orthography2ipa-cli> -DARGS=list|--family|Ger -DRESULT=1
                     "-DSTDERR_EQUALS=Unknown family: Ger\n"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_check.cmake)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        # ALL of the parity harnesses compare against the reference Python
        # package that lives NEXT DOOR — locally that is ../orthography2ipa,
        # and CI checks it out (see .github/workflows/ci.yml) and installs the
        # reference's own requirements.txt into its interpreter. Each harness
        # fails loudly when its reference is missing or its interpreter lacks
        # a dependency; the REGISTRATION here is what decides whether the
        # suite can run at all. Without the reference, CTest still runs the
        # pure C++ gates (core + install) and the build stays green on a
        # machine that has never seen the reference.
        # repr() comes from the interpreter alone, so this check needs no reference.
        add_test(NAME orthography2ipa-py-float-parity
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/py_float_parity.py
                         $<TARGET_FILE:orthography2ipa-check-py_float>)
        set(O2I_PARITY_ENABLED OFF)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../orthography2ipa/orthography2ipa/phonetok.py")
            set(ENV{O2I_PYTHON_ROOT} "${CMAKE_CURRENT_SOURCE_DIR}/../orthography2ipa")
            set(ENV{O2I_PYTHON} "${Python3_EXECUTABLE}")
            set(O2I_PARITY_ENABLED ON)
            set(O2I_REF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../orthography2ipa")
        endif()
        if(O2I_PARITY_ENABLED)
        add_test(NAME orthography2ipa-python-parity
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/python_parity.py $<TARGET_FILE:orthography2ipa-cli>)
        add_test(NAME orthography2ipa-token-parity
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/token_parity.py $<TARGET_FILE:orthography2ipa-cli>)
        add_test(NAME orthography2ipa-beam-parity
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/beam_parity.py $<TARGET_FILE:orthography2ipa-cli>)
        add_test(NAME orthography2ipa-stress-parity
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/stress_parity.py $<TARGET_FILE:orthography2ipa-cli>)
        add_test(NAME orthography2ipa-allophony-parity
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/allophony_parity.py $<TARGET_FILE:orthography2ipa-cli>)
        add_test(NAME orthography2ipa-tone-parity
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tone_parity.py $<TARGET_FILE:orthography2ipa-cli>)
        add_test(NAME orthography2ipa-sentence-parity
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/sentence_parity.py $<TARGET_FILE:orthography2ipa-cli> --sweep)
        # Section 8's intermediate gate over the six named languages: the
        # required battery compares the INTERMEDIATES (tokens/beam/lattice/
        # pre-rescore/stress/tone/word-candidates/confidence), not only the
        # final strings, and fails loudly on a missing surface.
        add_test(NAME orthography2ipa-reference-regression-required
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_regression.py
                         $<TARGET_FILE:orthography2ipa-cli> --battery required --surface all)
        # The registry resolution gate (section 7's last item): aliases, the
        # bare/region defaults, the private-use case fold and nearest-language
        # distance over the alias/catalog battery.
        add_test(NAME orthography2ipa-registry-parity
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/registry_parity.py
                         $<TARGET_FILE:orthography2ipa-cli> --pairs 24000 --mutations 2000)
        # The langcodes engine behind the registry port: standardize_tag,
        # tag_distance and the parse/maximize round-trips against the
        # installed langcodes (CTest-sized battery; --full is a manual sweep).
        add_test(NAME orthography2ipa-langcodes-parity
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/langcodes_parity.py
                         $<TARGET_FILE:orthography2ipa-check-langcodes>)
        # The ten requirement categories (Arabic, abugidas, tone languages,
        # combining marks, viramas, preposed vowels, geminates, clitics,
        # multi-candidate allophony) over every surface; `LABELS: sweep`
        # because it is the heavy battery.
        add_test(NAME orthography2ipa-reference-regression-categories
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_regression.py
                         $<TARGET_FILE:orthography2ipa-cli> --battery categories --surface all)
        # Multi-word cases: the confirmed spine plus two-word and comma
        # sentences from every sandhi spec.
        add_test(NAME orthography2ipa-reference-regression-sentence
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_regression.py
                         $<TARGET_FILE:orthography2ipa-cli> --battery sentence --surface sentence
                         --surface detailed --surface tokens --surface beam)
        # The catalog sweep (every spec's word_exceptions) stays a MANUAL step
        # on purpose: `ctest -R sweep` or the direct harness command.
        add_test(NAME orthography2ipa-reference-regression-sweep
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/reference_regression.py
                         $<TARGET_FILE:orthography2ipa-cli> --battery required --surface all --sweep)
        set_tests_properties(orthography2ipa-python-parity
                             orthography2ipa-token-parity
                             orthography2ipa-beam-parity
                             orthography2ipa-stress-parity
                             orthography2ipa-allophony-parity
                             orthography2ipa-tone-parity
                             orthography2ipa-sentence-parity
                             orthography2ipa-reference-regression-required
                             orthography2ipa-reference-regression-categories
                             orthography2ipa-reference-regression-sentence
                             PROPERTIES ENVIRONMENT "O2I_PYTHON_ROOT=${O2I_REF_ROOT}"
                                        TIMEOUT 3600)
        # The linguatag port called directly: every public function against the
        # installed linguatag, closest_lang tie lists included.
        add_test(NAME orthography2ipa-linguatag-parity
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/linguatag_parity.py
                         $<TARGET_FILE:orthography2ipa-check-linguatag>)
        set_tests_properties(orthography2ipa-registry-parity
                             orthography2ipa-langcodes-parity
                             orthography2ipa-linguatag-parity
                             PROPERTIES ENVIRONMENT "O2I_PYTHON_ROOT=${O2I_REF_ROOT};O2I_PYTHON=${Python3_EXECUTABLE}")
        set_tests_properties(orthography2ipa-reference-regression-sweep
                             PROPERTIES ENVIRONMENT "O2I_PYTHON_ROOT=${O2I_REF_ROOT}" TIMEOUT 14400)
        set_tests_properties(orthography2ipa-reference-regression-categories
                             PROPERTIES LABELS sweep TIMEOUT 10800)
        set_tests_properties(orthography2ipa-reference-regression-sweep
                             PROPERTIES LABELS sweep)
        set_tests_properties(orthography2ipa-registry-parity
                             PROPERTIES TIMEOUT 3600)

        # The module-level parity harnesses: each drives its own check binary.
        add_test(NAME orthography2ipa-inventory-parity
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/inventory_parity.py
                         $<TARGET_FILE:orthography2ipa-check-inventory> --battery 60)
        add_test(NAME orthography2ipa-lm-parity
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/lm_parity.py
                         $<TARGET_FILE:orthography2ipa-check-lm_feats>)
        add_test(NAME orthography2ipa-lattice-per-parity
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/lattice_per_parity.py
                         $<TARGET_FILE:orthography2ipa-check-lattice_per>)
        add_test(NAME orthography2ipa-features-conformance-parity
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/features_conformance_parity.py
                         $<TARGET_FILE:orthography2ipa-check-features_conformance>)
        add_test(NAME orthography2ipa-weights-parity
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/weights_parity.py
                         $<TARGET_FILE:orthography2ipa-check-weights>)
        set_tests_properties(orthography2ipa-inventory-parity
                             orthography2ipa-lm-parity
                             orthography2ipa-lattice-per-parity
                             orthography2ipa-features-conformance-parity
                             orthography2ipa-weights-parity
                             PROPERTIES ENVIRONMENT "O2I_PYTHON_ROOT=${O2I_REF_ROOT};O2I_PYTHON=${Python3_EXECUTABLE}"
                                        TIMEOUT 3600)
        add_test(NAME orthography2ipa-lexicon-parity
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/lexicon_parity.py
                         $<TARGET_FILE:orthography2ipa-check-lexicon>)
        set_tests_properties(orthography2ipa-lexicon-parity PROPERTIES ENVIRONMENT "O2I_PYTHON_ROOT=${O2I_REF_ROOT}")
        # Needs the o2i-lexicons package installed for the reference's python.
        add_test(NAME orthography2ipa-packaged-lexicon-parity
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/packaged_lexicon_parity.py
                         $<TARGET_FILE:orthography2ipa-check-packaged_lexicons>)
        set_tests_properties(orthography2ipa-packaged-lexicon-parity PROPERTIES ENVIRONMENT "O2I_PYTHON_ROOT=${O2I_REF_ROOT}")
        endif()
    endif()
    # Every test reads the specs of this source tree, whatever is installed.
    get_property(o2i_tests DIRECTORY PROPERTY TESTS)
    set_property(TEST ${o2i_tests} APPEND PROPERTY ENVIRONMENT
                 "ORTHOGRAPHY2IPA_DATA_DIR=${CMAKE_CURRENT_SOURCE_DIR}/data")
endif()
