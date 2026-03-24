cmake_minimum_required(VERSION 3.22)
project(calculator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

#FetchConten
FetchContent_Declare(
  mathlib
  GIT_REPOSITORY https://github.com/vavasiq/mathlib.git
  GIT_TAG v0.1.0
)
FetchContent_MakeAvailable(mathlib)

add_executable(calculator
  src/main.cpp
)

target_link_libraries(calculator PRIVATE mathlib)
target_compile_options(calculator PRIVATE -Wall -Wextra -Wpedantic)

#clang-tidy
find_program(CLANG_TIDY_EXE NAMES clang-tidy)
if(CLANG_TIDY_EXE)
  set_target_properties(calculator PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
endif()

#clang-format
find_program(CLANG_FORMAT_EXE NAMES clang-format)
if(CLANG_FORMAT_EXE)
  add_custom_target(clang-format
    COMMAND ${CLANG_FORMAT_EXE} -i ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running clang-format"
  )
endif()

#install
install(TARGETS calculator RUNTIME DESTINATION bin)
