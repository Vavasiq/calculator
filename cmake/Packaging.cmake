# deb-пакет через CPack (план: docs/task4-4-deb.md).
# Подключается в конце CMakeLists.txt, после всех правил install().
#
# Сборка пакета — в отдельной директории с префиксом /usr и без sudo:
#   cmake -S . -B build-deb -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-deb -j && (cd build-deb && cpack)

set(CPACK_GENERATOR "DEB")

set(CPACK_PACKAGE_NAME "calculator")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Calculator TCP service with PostgreSQL storage")
set(CPACK_PACKAGE_CONTACT "Vavasiq <vavasiq@example.com>")

set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT) # calculator_<версия>_<архитектура>.deb
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")

# Depends вычисляет dpkg-shlibdeps (пакет dpkg-dev) по библиотекам, с которыми слинкован
# бинарник: libpq5, libc6, libstdc++6, libgcc-s1. Boost сюда не попадёт: он только заголовками.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# БД может быть и на другом хосте (database.host), поэтому Recommends, а не Depends.
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "postgresql")

set(CPACK_STRIP_FILES TRUE)

# Установка во временный каталог через DESTDIR. Без этого CPack подменяет только
# CMAKE_INSTALL_PREFIX, и абсолютный путь /etc/calculator остался бы настоящим /etc.
# Проверка «конфиг уже есть?» в install(CODE) учитывает $ENV{DESTDIR}.
set(CPACK_SET_DESTDIR ON)

# Пакет кладёт файлы туда же, куда указывает ExecStart в unit-файле
# (путь подставлен configure_file из CMAKE_INSTALL_PREFIX).
set(CPACK_PACKAGING_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Проверка префикса выполняется только при запуске cpack, обычные сборки её не видят.
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_LIST_DIR}/CPackProjectConfig.cmake")

include(CPack)
