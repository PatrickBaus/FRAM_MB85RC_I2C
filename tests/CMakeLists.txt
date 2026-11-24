cmake_minimum_required(VERSION 3.14)
project(FRAM_MB85RC_I2C_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON) # The library uses binary literals like the Arduino toolchains (gnu++)

option(FRAM_COVERAGE "Instrument the library for coverage analysis (gcov)" OFF)
option(FRAM_SANITIZE "Build with the address and undefined behaviour sanitizers" OFF)

# Use the system GoogleTest (e.g. `apt install libgtest-dev libgmock-dev`) or download it
find_package(GTest QUIET)
if(NOT TARGET GTest::gmock)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE
  )
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

enable_testing()
include(GoogleTest)

set(LIBRARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

# The library is configured at compile time, so the test suite is built once per configuration.
#   default:  what users get out of the box (no WP pin management, no debug output)
#   wp_debug: WP pin management and debug output enabled
function(add_fram_test_suite name)
  add_executable(${name}
    ${LIBRARY_DIR}/FRAM_MB85RC_I2C.cpp
    mocks/mocks.cpp
    test_fram.cpp
  )
  target_include_directories(${name} PRIVATE ${LIBRARY_DIR} mocks)
  target_compile_definitions(${name} PRIVATE ARDUINO=10819 ${ARGN})
  target_compile_options(${name} PRIVATE -Wall -Wextra)
  target_link_libraries(${name} PRIVATE GTest::gtest GTest::gmock GTest::gtest_main)
  if(FRAM_COVERAGE)
    target_compile_options(${name} PRIVATE --coverage -O0 -g)
    target_link_options(${name} PRIVATE --coverage)
  endif()
  if(FRAM_SANITIZE)
    target_compile_options(${name} PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
    target_link_options(${name} PRIVATE -fsanitize=address,undefined)
  endif()
  gtest_discover_tests(${name} TEST_PREFIX "${name}.")
endfunction()

add_fram_test_suite(fram_tests_default)
add_fram_test_suite(fram_tests_wp_debug MANAGE_WP=true SERIAL_DEBUG=1)
