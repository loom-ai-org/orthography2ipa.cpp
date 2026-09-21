cmake_minimum_required(VERSION 3.16)
project(orthography2ipa VERSION 0.1.0 LANGUAGES CXX)

find_package(ICU REQUIRED COMPONENTS uc)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

add_library(orthography2ipa src/orthography2ipa.cpp)
target_include_directories(orthography2ipa PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
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
