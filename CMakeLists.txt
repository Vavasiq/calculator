cmake_minimum_required(VERSION 3.22)
project(calculator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Устанавливаемый бинарник должен быть оптимизированным: Release, если тип сборки не задан.
# Задаётся до FetchContent, чтобы зависимости собирались так же.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Пути установки: CMAKE_INSTALL_BINDIR / CMAKE_INSTALL_FULL_BINDIR и т.п.
include(GNUInstallDirs)

# Конфиг всегда в /etc/calculator (требование задания), независимо от префикса:
# GNUInstallDirs при префиксе /usr/local дал бы /usr/local/etc.
set(CALCULATOR_CONFIG_DIR /etc/calculator)

# ThreadSanitizer. Глобальные опции задаются ДО FetchContent_MakeAvailable,
# чтобы инструментировались и зависимости (mathlib, spdlog, gtest).
# Собирать в отдельной директории: cmake -S . -B build-tsan -DENABLE_TSAN=ON
option(ENABLE_TSAN "Build with ThreadSanitizer" OFF)
if(ENABLE_TSAN)
  add_compile_options(-fsanitize=thread -g -O1)
  add_link_options(-fsanitize=thread)
endif()

include(FetchContent)

find_package(Threads REQUIRED)
find_package(PostgreSQL REQUIRED)

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
# иначе cmake --install / deb-пакет потащат заголовки и библиотеки gtest
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_library(calculator_lib
  src/Logger.cpp
  src/Parser.cpp
  src/Checker.cpp
  src/Calculator.cpp
  src/Printer.cpp
  src/Runner.cpp
  src/Postgres.cpp
  src/DataBase.cpp
  src/StopFlag.cpp
  src/SignalThread.cpp
  src/WorkerThread.cpp
  src/Config.cpp
)

target_include_directories(calculator_lib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(calculator_lib
  PRIVATE
    mathlib
    nlohmann_json::nlohmann_json
    spdlog::spdlog
  PUBLIC
    PostgreSQL::PostgreSQL
    Threads::Threads
)

target_compile_options(calculator_lib PRIVATE -Wall -Wextra -Wpedantic)

add_executable(calculator src/main.cpp)
target_link_libraries(calculator PRIVATE calculator_lib)
target_compile_options(calculator PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()

add_executable(calculator_tests tests/tests.cpp)
target_link_libraries(calculator_tests PRIVATE
  calculator_lib
  mathlib
  GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(calculator_tests)

# прогон тестов под valgrind (несовместим с TSan, поэтому только в обычной сборке)
find_program(VALGRIND_EXE valgrind)
if(VALGRIND_EXE AND NOT ENABLE_TSAN)
  add_test(NAME memcheck
    COMMAND ${VALGRIND_EXE} --leak-check=full --error-exitcode=1
            $<TARGET_FILE:calculator_tests>)
endif()

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

# --- Установка (план: docs/task4-2-systemd.md, раздел 4) ---

install(TARGETS calculator RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# unit-файл: путь к бинарнику подставляется из префикса установки
# (/usr/local/bin при ручной установке, /usr/bin в deb-пакете).
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/packaging/systemd/calculator.service.in
  ${CMAKE_CURRENT_BINARY_DIR}/calculator.service
  @ONLY
)
# Именно lib/, не ${CMAKE_INSTALL_LIBDIR}: на Debian/Ubuntu с префиксом /usr тот даёт
# lib/x86_64-linux-gnu, а systemd ищет unit-файлы в /usr/lib/systemd/system
# и /usr/local/lib/systemd/system.
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/calculator.service
        DESTINATION lib/systemd/system)

# Конфиг ставится, только если его ещё нет: иначе каждая установка сбрасывала бы
# права (root:calculator 0640 -> root:root 0644) и затирала правки администратора.
# DESTDIR учитывается в проверке, чтобы CPack (сборка во временный каталог) клал конфиг в пакет;
# file(INSTALL) добавляет DESTDIR к пути сам. Владельца CMake менять не умеет:
# chown root:calculator — вручную после первой установки (в deb — postinst).
install(CODE "
  set(_calculator_config \"\$ENV{DESTDIR}${CALCULATOR_CONFIG_DIR}/calculator.json\")
  if(EXISTS \"\${_calculator_config}\")
    message(STATUS \"Keeping existing config: \${_calculator_config}\")
  else()
    file(INSTALL
      DESTINATION \"${CALCULATOR_CONFIG_DIR}\"
      TYPE FILE
      PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ
      FILES \"${CMAKE_CURRENT_SOURCE_DIR}/packaging/config/calculator.json\")
  endif()
")
