# Читается cpack при сборке пакета (CPACK_PROJECT_CONFIG_FILE), а не при обычном configure.
#
# Путь к бинарнику в unit-файле подставлен на этапе configure из CMAKE_INSTALL_PREFIX.
# С префиксом по умолчанию /usr/local unit-файл в пакете запускал бы /usr/local/bin/calculator,
# а по политике Debian пакет ставится в /usr. Поэтому пакет собирается только с /usr.
if(CPACK_GENERATOR STREQUAL "DEB" AND NOT CPACK_PACKAGING_INSTALL_PREFIX STREQUAL "/usr")
  message(FATAL_ERROR
    "The deb package must be built with CMAKE_INSTALL_PREFIX=/usr, "
    "this build directory uses '${CPACK_PACKAGING_INSTALL_PREFIX}'.\n"
    "Use a separate build directory:\n"
    "  cmake -S . -B build-deb -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release\n"
    "  cmake --build build-deb -j && (cd build-deb && cpack)")
endif()
