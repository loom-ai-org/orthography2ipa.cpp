cmake_minimum_required(VERSION 3.16)
project(orthography2ipa VERSION 0.1.0 LANGUAGES CXX)

find_package(ICU REQUIRED COMPONENTS uc)
include(CTest)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

add_library(orthography2ipa src/orthography2ipa.cpp)
target_include_directories(orthography2ipa PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(orthography2ipa PUBLIC ICU::uc ${CMAKE_DL_LIBS})
target_compile_definitions(orthography2ipa PUBLIC
    O2I_DEFAULT_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    O2I_INSTALL_DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/orthography2ipa")
target_compile_options(orthography2ipa PRIVATE -Wall -Wextra -Wpedantic)

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
    add_executable(orthography2ipa-tests tests/test_core.cpp)
    target_link_libraries(orthography2ipa-tests PRIVATE orthography2ipa)
    add_test(NAME orthography2ipa-core COMMAND orthography2ipa-tests)
    configure_file(tests/install_check.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/install_check.cmake @ONLY)
    add_test(NAME orthography2ipa-install COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/install_check.cmake)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME orthography2ipa-python-parity
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/python_parity.py $<TARGET_FILE:orthography2ipa-cli>)
    endif()
endif()
