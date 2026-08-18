cmake_minimum_required(VERSION 3.22)
project(calculator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
  mathlib
  GIT_REPOSITORY https://github.com/vavasiq/mathlib.git
  GIT_TAG        v0.1.1
)
FetchContent_MakeAvailable(mathlib)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG        v1.14.1
)
FetchContent_MakeAvailable(spdlog)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_library(calculator_lib
  src/Logger.cpp
  src/Parser.cpp
  src/Checker.cpp
  src/Calculator.cpp
  src/Printer.cpp
  src/Runner.cpp
)

target_include_directories(calculator_lib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(calculator_lib PRIVATE
  mathlib
  nlohmann_json::nlohmann_json
  spdlog::spdlog
)

target_compile_options(calculator_lib PRIVATE -Wall -Wextra -Wpedantic)

add_executable(calculator src/main.cpp)
target_link_libraries(calculator PRIVATE calculator_lib)
target_compile_options(calculator PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()

add_executable(calculator_tests tests/tests.cpp)
target_link_libraries(calculator_tests PRIVATE
  calculator_lib
  GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(calculator_tests)

find_program(CLANG_FORMAT_EXE NAMES clang-format)
if(CLANG_FORMAT_EXE)
  file(GLOB_RECURSE ALL_CXX_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/**/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
  )
  add_custom_target(clang-format
    COMMAND ${CLANG_FORMAT_EXE} -i ${ALL_CXX_SOURCES}
    COMMENT "Running clang-format"
  )
endif()

install(TARGETS calculator RUNTIME DESTINATION bin)
