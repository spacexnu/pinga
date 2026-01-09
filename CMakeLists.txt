cmake_minimum_required(VERSION 3.20)

project(pinga LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)

add_executable(pinga
  src/main.c
  src/jsmn.c
)

set(PINGA_VERSION "dev" CACHE STRING "Version string")

target_compile_options(pinga PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(pinga PRIVATE CURL::libcurl)
target_compile_definitions(pinga PRIVATE PINGA_VERSION="${PINGA_VERSION}")

install(TARGETS pinga RUNTIME DESTINATION bin)

enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME pinga-mock COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/mock_test.py)
set_tests_properties(pinga-mock PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

option(ENABLE_NETWORK_TESTS "Enable tests that require network access" OFF)
if(ENABLE_NETWORK_TESTS)
  add_test(NAME pinga-httpbin COMMAND pinga ${CMAKE_SOURCE_DIR}/config.httpbin.json)
  set_tests_properties(pinga-httpbin PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
