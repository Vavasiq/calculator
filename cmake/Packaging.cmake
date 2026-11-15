# deb-пакет через CPack (план: docs/task4-4-deb.md).
# Подключается в конце CMakeLists.txt, после всех правил install().
#
# Сборка пакета — в отдельной директории с префиксом /usr и без sudo:
#   cmake -S . -B build-deb -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-deb -j && (cd build-deb && cpack)

set(CPACK_GENERATOR "DEB")

set(CPACK_PACKAGE_NAME "calculator")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
# lintian: краткое описание не должно начинаться с имени пакета; развёрнутое — не пустое.
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "TCP service for integer calculations with PostgreSQL storage")
# Одна строка (string(CONCAT), а не несколько аргументов set(), иначе CMake склеит их через ';').
# Явные '\n': по правилам Debian строки развёрнутого описания не длиннее 80 символов.
string(CONCAT CPACK_PACKAGE_DESCRIPTION
    "Accepts calculation tasks as JSON lines over TCP (Boost.Asio) and\n"
    "returns JSON results. Results are stored in PostgreSQL and cached in\n"
    "memory. Runs as the systemd service calculator.service under the\n"
    "dedicated system user 'calculator'.")
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

# Каталоги в пакете — 0755 независимо от umask того, кто собирает (у пользователя в Ubuntu
# umask 0002 давал 0775, и /etc/calculator стал бы доступен на запись группе).
set(CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE)

# Установка во временный каталог через DESTDIR. Без этого CPack подменяет только
# CMAKE_INSTALL_PREFIX, и абсолютный путь /etc/calculator остался бы настоящим /etc.
# Проверка «конфиг уже есть?» в install(CODE) учитывает $ENV{DESTDIR}.
set(CPACK_SET_DESTDIR ON)

# Пакет кладёт файлы туда же, куда указывает ExecStart в unit-файле
# (путь подставлен configure_file из CMAKE_INSTALL_PREFIX).
set(CPACK_PACKAGING_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Скрипты сопровождающего и conffiles (план, раздел 4): postinst создаёт пользователя
# calculator и выставляет права на конфиг, conffiles сохраняет правки конфига при обновлении.
set(_calculator_deb_dir "${PROJECT_SOURCE_DIR}/packaging/deb")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${_calculator_deb_dir}/conffiles"
    "${_calculator_deb_dir}/postinst"
    "${_calculator_deb_dir}/prerm"
    "${_calculator_deb_dir}/postrm")
# Права в control.tar по политике Debian: скрипты 0755, conffiles 0644 — независимо от
# прав файлов в рабочей копии.
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)

# Проверка префикса выполняется только при запуске cpack, обычные сборки её не видят.
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_LIST_DIR}/CPackProjectConfig.cmake")

include(CPack)
