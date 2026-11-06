cmake_minimum_required(VERSION 3.16)
project(orthography2ipaConsumer LANGUAGES CXX)
find_package(orthography2ipa CONFIG REQUIRED)
add_executable(orthography2ipa-consumer main.cpp)
target_link_libraries(orthography2ipa-consumer PRIVATE orthography2ipa::orthography2ipa)
# A shared library of the consumer links the installed library too, which
# needs position-independent code when that library is static.
add_library(orthography2ipa-consumer-shared SHARED shared.cpp)
target_link_libraries(orthography2ipa-consumer-shared PRIVATE orthography2ipa::orthography2ipa)
