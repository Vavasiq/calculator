# ДЗ-4. Задача 4 — deb-пакет через CPack

План изменений для задачи 4 из [task4.md](../task4.md).

**Критерий готовности (из задания):** пакет собирается CPack и содержит бинарник и конфиги.

**Что входит в задачу:**
- CPack-генератор `DEB` в [CMakeLists.txt](../CMakeLists.txt). В пакет попадают бинарник, unit-файл systemd и конфиг `/etc/calculator/calculator.json`;
- скрипты сопровождающего (`postinst`, `prerm`, `postrm`) и `conffiles`. Пакет сам создаёт пользователя `calculator`, выставляет права на конфиг и не затирает правки конфига при обновлении. В задаче 2 это делалось вручную;
- зависимости пакета вычисляются автоматически: во время работы нужен `libpq5`, Boost не нужен (он только заголовками, раздел 2 плана задачи 3);
- проверка: установка, запуск, обновление, удаление, purge;
- **финальный прогон ThreadSanitizer на итоговой версии** (раздел 3 задания, раздел 12 плана задачи 3).

**Вне рамок задачи:** подпись пакета, свой apt-репозиторий, чистый отчёт `lintian` (запустим для информации, но предупреждения не блокируют), создание базы и роли PostgreSQL из пакета (раздел 4).

---

## 1. Текущее состояние

Правила установки уже есть с задачи 2: `cmake --install` ставит
- `bin/calculator`;
- `lib/systemd/system/calculator.service` — шаблон, путь к бинарнику подставляется из префикса;
- `/etc/calculator/calculator.json` — только если файла ещё нет, с правами `0640`. В проверке учтён `DESTDIR`, чтобы CPack положил конфиг в пакет.

Зависимости в пакет не попадут: `INSTALL_GTEST OFF`, у spdlog, nlohmann_json и mathlib правил установки нет или они выключены. Тесты не устанавливаются.

Не хватает самого CPack и скриптов, которые в задаче 2 заменялись ручными командами: `useradd` и `chown`.

## 2. Сборка пакета

**Префикс `/usr`, отдельная директория сборки `build-deb`:**
```bash
cmake -S . -B build-deb -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release
cmake --build build-deb -j
cd build-deb && cpack            # -> calculator_1.0.0_amd64.deb
```

**`cpack` запускается без `sudo`.** Если сборка пакета когда-нибудь попробует записать мимо временного каталога, например в настоящий `/etc`, обычный пользователь получит `Permission denied`, и систему это не затронет.

**Почему префикс задаётся при configure, а не только в CPack.** Путь к бинарнику в unit-файле (`ExecStart=@CMAKE_INSTALL_FULL_BINDIR@/calculator`) подставляет `configure_file` на этапе configure. Если собрать с префиксом по умолчанию `/usr/local`, а пакет ставить в `/usr` (так CPack DEB делает по умолчанию), бинарник окажется в `/usr/bin`, а unit-файл будет запускать `/usr/local/bin/calculator`. Поэтому:
- `CPACK_PACKAGING_INSTALL_PREFIX` = `CMAKE_INSTALL_PREFIX`. Пакет кладёт файлы туда же, куда указывает unit-файл;
- если `cpack` запускают в сборке с префиксом не `/usr`, он **останавливается с ошибкой** и подсказкой, как пересобрать. Проверка лежит в `cmake/CPackProjectConfig.cmake` (`CPACK_PROJECT_CONFIG_FILE`) и срабатывает только при сборке пакета. Обычные сборки с `/usr/local` (`build`, `build-tsan`) её не видят. Политика Debian: пакеты ставятся в `/usr`, а `/usr/local` оставлен администратору.

**Содержимое пакета:**

| Путь | Права | Откуда |
|---|---|---|
| `/usr/bin/calculator` | `0755` | `install(TARGETS)` |
| `/usr/lib/systemd/system/calculator.service` | `0644` | `configure_file` + `install(FILES)` |
| `/etc/calculator/calculator.json` | `0640 root:root` в пакете, `root:calculator` после `postinst` | `install(CODE)` из задачи 2 |

Владельца `calculator` в самом пакете задать нельзя: при сборке пакета такого пользователя ещё нет. Поэтому группу на конфиг ставит `postinst`.

## 3. Переменные CPack

Отдельный файл `cmake/Packaging.cmake`, подключается в конце [CMakeLists.txt](../CMakeLists.txt) через `include(...)`, а внутри него `include(CPack)`:

| Переменная | Значение | Зачем |
|---|---|---|
| `project(... VERSION 1.0.0)` | `1.0.0` | Версия пакета берётся из проекта |
| `CPACK_GENERATOR` | `DEB` | |
| `CPACK_PACKAGE_NAME` | `calculator` | |
| `CPACK_PACKAGE_DESCRIPTION_SUMMARY` | `Calculator TCP service with PostgreSQL storage` | Строка в `apt show` |
| `CPACK_DEBIAN_PACKAGE_MAINTAINER` | `Vavasiq <vavasiq@example.com>` (раздел 8) | Поле обязательно для CPack DEB |
| `CPACK_DEBIAN_FILE_NAME` | `DEB-DEFAULT` | Имя по правилам Debian: `calculator_1.0.0_amd64.deb` |
| `CPACK_DEBIAN_PACKAGE_SECTION` / `PRIORITY` | `net` / `optional` | |
| `CPACK_DEBIAN_PACKAGE_SHLIBDEPS` | `ON` | `Depends` вычисляет `dpkg-shlibdeps` по библиотекам, с которыми слинкован бинарник: `libpq5`, `libc6`, `libstdc++6`, `libgcc-s1`. Boost сюда не попадёт: у бинарника нет зависимостей от его `.so`. Нужен пакет `dpkg-dev` на машине сборки |
| `CPACK_DEBIAN_PACKAGE_RECOMMENDS` | `postgresql` | БД может быть и на другом хосте (`database.host`), поэтому `Recommends`, а не `Depends` |
| `CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA` | `packaging/deb/conffiles;postinst;prerm;postrm` | Раздел 4 |
| `CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION` | `TRUE` | Права на скрипты в `control.tar` по политике Debian (`0755`, `conffiles` — `0644`) |
| `CPACK_STRIP_FILES` | `TRUE` | Бинарник в пакете без отладочных символов |
| `CPACK_PACKAGE_DESCRIPTION` | развёрнутое описание | lintian: `extended-description-is-empty`. Краткое описание не начинается с имени пакета (`description-starts-with-package-name`) |
| `CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS` | `0755` | Найдено lintian на первой сборке: каталоги получали `0775` из umask `0002` того, кто собирает, и `/etc/calculator` стал бы доступен на запись группе |
| `CPACK_PACKAGING_INSTALL_PREFIX` | `${CMAKE_INSTALL_PREFIX}` | Раздел 2 |
| `CPACK_SET_DESTDIR` | `ON` | CPack ставит файлы во временный каталог через `DESTDIR`. Без этого он подменяет только `CMAKE_INSTALL_PREFIX`, а абсолютный путь `/etc/calculator` остался бы настоящим `/etc`. Проверка «конфиг уже есть?» в `install(CODE)` из задачи 2 учитывает `DESTDIR`, поэтому во временном каталоге конфиг ставится и попадает в пакет |

## 4. Скрипты сопровождающего (`packaging/deb/`)

Все скрипты на `#!/bin/sh` с `set -e`. Вызовы `systemctl` выполняются только при запущенном systemd (`[ -d /run/systemd/system ]`), иначе установка в chroot или контейнер падала бы.

### `conffiles`
```
/etc/calculator/calculator.json
```
Тогда dpkg считает конфиг файлом администратора:
- при обновлении пакета правки сохраняются, а если изменились и файл на диске, и файл в пакете, dpkg спросит, какую версию оставить;
- `apt remove` конфиг не удаляет, удаляет только `apt purge`.

Это и есть замена правилу «копировать, только если нет» из задачи 2 для установки через пакет.

### `postinst` (аргумент `configure`)
1. **Пользователь.** `getent passwd calculator`. Если пользователя нет, создать его той же командой, что в задаче 2: `useradd --system --user-group --no-create-home --home-dir /nonexistent --shell /usr/sbin/nologin calculator`. Если пользователь уже есть (например, создан вручную в задаче 2), ничего не делать.
2. **Права на конфиг:** `chown root:calculator /etc/calculator/calculator.json` и `chmod 0640`. Пароль от БД читают только root и сервис.
3. **systemd:** `systemctl daemon-reload`.
4. **Запуск:**
   - **Первая установка:** сервис **не** включается и не запускается автоматически, пакет печатает подсказку `sudo systemctl enable --now calculator`. Причина: сервису нужна база и роль PostgreSQL, которые пакет не создаёт. Автозапуск без них сразу ушёл бы в цикл перезапусков, а затем в `failed` (`Restart=on-failure` + `StartLimit`). Альтернатива по-дебиановски — включать и запускать сразу (`deb-systemd-helper enable` + `deb-systemd-invoke start`). Если вам нужен такой вариант, скажите.
   - **Обновление** (сервис уже запущен): `systemctl try-restart calculator`. Сервис подхватывает новый бинарник, а не запущенный сервис так и остаётся остановленным.

### `prerm`
- `remove`: `systemctl stop calculator` и `systemctl disable calculator`. Перед удалением файлов сервис останавливается штатно, по `SIGTERM` (задача 1).
- `upgrade`: ничего не делаем. Новый бинарник подхватит `try-restart` в `postinst` новой версии.

### `postrm`
- `remove` и `purge`: `systemctl daemon-reload`, потому что unit-файла больше нет.
- `purge`: конфиг и пустую `/etc/calculator` dpkg удаляет сам, это conffile. **Пользователя `calculator` не удаляем**, так рекомендует политика Debian: после него могут остаться файлы, а повторное создание выдало бы другой UID.

### Чего пакет не делает
- **Не создаёт базу и роль PostgreSQL.** Это настройка конкретного окружения, и БД может быть на другом хосте. Команды `CREATE USER` и `CREATE DATABASE` есть в [README](../README.md#база-данных-и-роль-postgresql). `postinst` после первой установки напоминает настроить доступ к БД в конфиге и включить сервис.

## 5. Ручная установка из задачи 2 на ВМ

На ВМ сейчас стоит ручная установка, и она помешает пакету:
- **Unit-файл:** `/usr/local/lib/systemd/system/calculator.service` у systemd **приоритетнее** `/usr/lib/systemd/system/` из пакета. Systemd продолжал бы запускать старый бинарник `/usr/local/bin/calculator`.
- **Конфиг:** `/etc/calculator/calculator.json` не принадлежит ни одному пакету. Когда dpkg ставит conffile поверх такого файла, он спрашивает, какую версию оставить. Кроме того, в нынешнем файле нет секции `server`.
- **Автозапуск:** symlink `enable` в `/etc/systemd/system/multi-user.target.wants/` указывает на unit в `/usr/local`.

Поэтому перед установкой пакета ручную установку убираем:
```bash
sudo systemctl disable --now calculator
sudo rm /usr/local/bin/calculator /usr/local/lib/systemd/system/calculator.service
sudo mv /etc/calculator/calculator.json /root/calculator.json.manual-install.bak
sudo systemctl daemon-reload
```
Пользователя `calculator` оставляем: `postinst` его найдёт и не станет пересоздавать.

## 6. Изменения по файлам

### Новые файлы
- **`cmake/Packaging.cmake`** — переменные CPack (раздел 3), проверка префикса (раздел 2), `include(CPack)`.
- **`packaging/deb/conffiles`, `postinst`, `prerm`, `postrm`** — раздел 4. В git сохраняется бит исполнения для скриптов: `git update-index --chmod=+x`, ведь файлы создаются на Windows. Без этого бита `CONTROL_STRICT_PERMISSION` всё равно выставит права в пакете, но так надёжнее.

### Изменяемые файлы
- **[CMakeLists.txt](../CMakeLists.txt)**: `project(calculator VERSION 1.0.0 LANGUAGES CXX)` и в конце `include(cmake/Packaging.cmake)`.
- **[docs/task4-2-systemd.md](task4-2-systemd.md)** — одна строка: при установке пакетом пользователя создаёт `postinst` (раздел 3 того плана уже это обещает, ссылка на этот документ).

### Без изменений
Код сервиса, тесты, unit-файл, конфиг по умолчанию. В задаче 4 код не меняется, поэтому модель потоков та же, что проверена TSan в задаче 3.

## 7. Проверка готовности

### 0. Инструменты
```bash
sudo apt install dpkg-dev lintian      # dpkg-shlibdeps для SHLIBDEPS; lintian — для информации
```

### 1. Сборка и содержимое пакета (критерий задания)
```bash
cmake -S . -B build-deb -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release
cmake --build build-deb -j && (cd build-deb && cpack)
DEB=$(ls build-deb/calculator_*_amd64.deb)
dpkg-deb -c "$DEB"     # только ./usr/bin/calculator, ./usr/lib/systemd/system/calculator.service, ./etc/calculator/calculator.json (+ каталоги)
dpkg-deb -I "$DEB"     # Depends: libpq5, libc6, libstdc++6, libgcc-s1 (без boost); Recommends: postgresql; conffiles; postinst/prerm/postrm
dpkg-deb --fsys-tarfile "$DEB" | tar -xO ./usr/lib/systemd/system/calculator.service | grep ExecStart   # /usr/bin/calculator
lintian "$DEB" || true # для информации
```

### 2. Установка и запуск
Сервис начинает слушать порт не сразу после `systemctl start`: сначала он подключается к БД и прогревает кеш (`Type=simple`). Поэтому перед запросом через `nc` ждём порт:
```bash
waitport() { timeout 30 sh -c "until nc -z 127.0.0.1 $1; do sleep 0.2; done"; }
```

```bash
# сначала убрать ручную установку (раздел 5)
sudo apt install ./"$DEB"
dpkg -s calculator | grep -E 'Status|Version'                   # install ok installed, 1.0.0
stat -c '%U:%G %a' /etc/calculator/calculator.json              # root:calculator 640
systemctl cat calculator | head -1                              # /usr/lib/systemd/system/calculator.service
sudo systemctl enable --now calculator && systemctl is-active calculator
waitport 7777
printf '{"op":"*","a":6,"b":7}\n' | nc -N 127.0.0.1 7777        # {"result":42,"status":0}
sudo systemctl restart calculator && sudo systemctl stop calculator && sudo systemctl start calculator
journalctl -u calculator -n 20 --no-pager
```

### 3. Обновление сохраняет правки конфига
```bash
sudo sed -i 's/"port": 7777/"port": 7779/' /etc/calculator/calculator.json
sudo apt install --reinstall ./"$DEB"
sudo grep '"port": 7779' /etc/calculator/calculator.json       # правка на месте (файл 0640, читать через sudo)
stat -c '%U:%G %a' /etc/calculator/calculator.json              # root:calculator 640
systemctl is-active calculator                                  # active: try-restart, сервис слушает 7779
waitport 7779
printf '{"op":"+","a":1,"b":2}\n' | nc -N 127.0.0.1 7779
sudo sed -i 's/"port": 7779/"port": 7777/' /etc/calculator/calculator.json && sudo systemctl restart calculator
```

### 4. Удаление и purge
```bash
sudo apt remove -y calculator
systemctl is-active calculator; ls /usr/bin/calculator          # inactive; файла нет
sudo ls -l /etc/calculator/calculator.json                      # конфиг остался (conffile)
sudo apt purge -y calculator
ls /etc/calculator 2>&1                                         # каталога нет
getent passwd calculator                                        # пользователь остался (раздел 4)
sudo apt install ./"$DEB" && sudo systemctl enable --now calculator   # вернуть рабочее состояние
```

### 5. Финальный прогон ThreadSanitizer (раздел 3 задания)
Это итоговая версия: код после этой задачи не меняется.
```bash
cmake --build build-tsan -j && ctest --test-dir build-tsan --output-on-failure   # все тесты, без отчётов TSan
# реальный процесс под TSan: запросы через nc и SIGTERM (как в разделе 15.3 плана задачи 3)
./build-tsan/calculator --config /tmp/calc-dev.json > /tmp/tsan-final.log 2>&1 & PID=$!
timeout 30 sh -c 'until nc -z 127.0.0.1 7778; do sleep 0.2; done'
for r in '{"op":"+","a":2,"b":3}' '{"op":"/","a":5,"b":0}' 'hello'; do printf '%s\n' "$r" | nc -N 127.0.0.1 7778; done
kill -TERM $PID; wait $PID; echo "exit=$?"                       # 0 (при отчёте TSan было бы 66)
grep -c 'WARNING: ThreadSanitizer' /tmp/tsan-final.log           # 0
```
TSan по умолчанию ищет и гонки данных, и deadlock (`detect_deadlocks=1`, отчёт `lock-order-inversion`). Задание требует проверять оба вида ошибок.

## 8. Решения

1. **`Maintainer`:** `Vavasiq <vavasiq@example.com>`.
2. **Автозапуск после установки:** не включается. Пакет только подсказывает `sudo systemctl enable --now calculator` (раздел 4).

## 9. Порядок реализации

1. **CPack и содержимое.** `project(VERSION)`, `cmake/Packaging.cmake`, проверка префикса. Готово, когда пакет собирается, а `dpkg-deb -c` и `-I` показывают нужное содержимое и зависимости (проверка 7.1). Это уже закрывает критерий задания.
2. **Скрипты сопровождающего и `conffiles`.** Готово, когда проходят проверки 7.2–7.4.
3. **Финальная проверка** — 7.5, финальный TSan.
