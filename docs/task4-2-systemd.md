# ДЗ-4. Задача 2 — systemd-сервис

План изменений для задачи 2 из [task4.md](../task4.md).

**Критерий готовности (из задания):** приложение управляется командами `systemctl start/restart/stop`.

**Что входит в задачу:**
- unit-файл `calculator.service`: `Type=simple`, `ExecStart`, `Restart=on-failure`, `After=network.target postgresql.service`, отдельный `User`, `KillSignal=SIGTERM`;
- файл конфигурации в `/etc/calculator/`: параметры подключения к БД переезжают туда из кода;
- установка через `cmake --install`, чтобы задача 4 (CPack) переиспользовала те же правила;
- проверка start/stop/restart через `systemctl` и `journalctl`.

**Вне рамок задачи:** приём заданий по сети (задача 3) и deb-пакет (задача 4). В задаче 2 сервис после старта только подключается к БД, прогревает кеш и ждёт сигнала, а работу ему даст задача 3. Пользователь `calculator` в этой задаче создаётся вручную, в задаче 4 это будет делать `postinst` пакета.

**Опора на задачу 1:** `systemctl stop` шлёт `SIGTERM`. Поток сигналов принимает его, рабочий поток закрывает соединение, и процесс выходит с кодом `0`. Это уже проверено вручную через `kill -TERM`. Запуск без аргументов (режим сервиса) тоже уже поддержан.

---

## 1. Unit-файл

**Где лежит в репозитории:** `packaging/systemd/calculator.service.in` — шаблон, из которого CMake (`configure_file`) делает итоговый `calculator.service`.

**Зачем шаблон:** путь к бинарнику зависит от префикса установки. При ручной установке это `/usr/local/bin`, в deb-пакете будет `/usr/bin`. В шаблоне путь задаётся как `@CMAKE_INSTALL_FULL_BINDIR@/calculator`, поэтому unit-файл один на оба случая.

### `[Unit]`

| Директива | Значение | Зачем |
|---|---|---|
| `Description` | `Calculator service` | Строка в `systemctl status` и журнале |
| `After` | `network.target postgresql.service` | Порядок запуска: сервис стартует после сети и PostgreSQL, а при выключении системы останавливается **раньше** них. Поэтому соединение закрывается штатно, пока БД ещё жива |
| `Wants` | `postgresql.service` | **Предложение сверх заданного.** `After` задаёт только порядок, но не запускает PostgreSQL. `Wants` поднимет его при `systemctl start calculator`. `Requires` не берём: с ним перезапуск PostgreSQL останавливал бы и наш сервис |
| `StartLimitIntervalSec` / `StartLimitBurst` | `60` / `5` | Не больше 5 перезапусков за 60 с. Если БД долго недоступна, systemd перестанет бесконечно перезапускать сервис и переведёт его в `failed` |

**Тонкость Ubuntu (выяснили в задаче 1):** `postgresql.service` — это обёртка, а сервер работает в `postgresql@16-main.service`. Ordering через обёртку работает, если у экземпляра есть `Before=postgresql.service`. Это нужно проверить через `systemctl cat postgresql@16-main` (раздел 6, шаг 0). Если зависимости нет, в `After`/`Wants` указываем `postgresql@16-main.service`. Тогда unit привязан к версии кластера, и это стоит отметить в README.

**`network.target`, а не `network-online.target`:** БД на `localhost`, ждать внешней сети не нужно. Для удалённой БД понадобится `network-online.target` + `Wants=network-online.target`.

### `[Service]`

| Директива | Значение | Зачем |
|---|---|---|
| `Type` | `simple` | Процесс не форкается и не демонизируется сам. systemd считает сервис запущенным сразу после `exec`. Ошибка подключения к БД придёт чуть позже и переведёт сервис в `failed`/перезапуск. `Type=notify` с `sd_notify` точнее, но требует libsystemd, поэтому вне рамок |
| `User` / `Group` | `calculator` / `calculator` | Не root. Системный пользователь без shell и home (раздел 3) |
| `ExecStart` | `@CMAKE_INSTALL_FULL_BINDIR@/calculator --config /etc/calculator/calculator.json` | Режим сервиса без задания в аргументах, путь к конфигу задан явно |
| `KillSignal` | `SIGTERM` | Совпадает со значением по умолчанию, но записан явно: именно его ждёт поток сигналов |
| `TimeoutStopSec` | `10` | Если за 10 с процесс не вышел, systemd пошлёт `SIGKILL`. Наша остановка занимает миллисекунды, так что таймаут — страховка |
| `Restart` | `on-failure` | Перезапуск при ненулевом коде выхода, смерти от «нечистого» сигнала (например, `SIGKILL`, `SIGSEGV`) или таймауте. После `systemctl stop` и штатного выхода с `0` перезапуска нет |
| `RestartSec` | `5` | Пауза между перезапусками, чтобы при недоступной БД не долбить её в цикле. 5 попыток × 5 с укладываются в `StartLimit` выше |
| `SyslogIdentifier` | `calculator` | Имя в журнале. Фильтровать будем по `-u calculator` |
| `NoNewPrivileges`, `ProtectSystem=strict`, `ProtectHome=read-only`, `PrivateTmp=yes` | `yes` / `strict` / `read-only` / `yes` | **Минимальная защита, предложение.** Сервису нужны только сеть и чтение `/etc/calculator`, он ничего не пишет на диск. `ProtectHome=read-only`, а не `yes`: при `yes` каталог `/home` недоступен (`EACCES`), а libpq при SSL-подключении (в Ubuntu SSL у PostgreSQL включён по умолчанию) считает ошибкой любой ответ, кроме «файла нет», на проверке `~/.postgresql/postgresql.crt`. Если какая-то директива помешает, её можно убрать без последствий для задания |

stdout и stderr по умолчанию уходят в журнал, поэтому `StandardOutput` не задаём.

### `[Install]`

`WantedBy=multi-user.target` — нужно для `systemctl enable` (автозапуск при загрузке). В задании это не требуется, но без этой секции `enable` выдаст предупреждение.

## 2. Файл конфигурации `/etc/calculator/calculator.json`

**Формат JSON:** `nlohmann_json` уже есть в зависимостях, новая библиотека не нужна. В задаче 3 сюда же добавится порт сервера.

**Содержимое** — параметры, которые сейчас захардкожены в `DataBase::Config`:

| Ключ | Значение по умолчанию |
|---|---|
| `database.host` | `localhost` |
| `database.port` | `5432` |
| `database.name` | `calculator` |
| `database.user` | `calculator` |
| `database.password` | `calculator` |

**Права:** в файле пароль, поэтому `root:calculator`, `0640`. Сервис читает файл, остальные пользователи — нет.

**Правила загрузки:**
- `--config <путь>` задан явно, а файла нет или он некорректен → ошибка в лог, код выхода `1`. Под systemd это `failed`, `Restart=on-failure` даст несколько попыток, затем сработает `StartLimit`.
- `--config` не задан → пробуем путь по умолчанию `/etc/calculator/calculator.json`. Если файла нет, работаем на встроенных значениях с предупреждением в лог. Так сборка для разработки (`./build/calculator`) работает без установки.
- Отсутствующий ключ берёт значение по умолчанию. Ключ с неверным типом (например, `port` строкой) — ошибка.

**Отклонённые альтернативы:**
- `EnvironmentFile=` с переменными `PG*`, которые libpq читает сама. Пароль в окружении виден через `/proc/<pid>/environ`, и всё равно пришлось бы менять код, потому что строка подключения сейчас собирается явно.
- `DynamicUser=yes`. С ним неудобно выдавать права на файл с паролем заранее, и пользователь получает случайный UID.

## 3. Пользователь `calculator`

Системный пользователь без входа в систему:
```bash
sudo useradd --system --no-create-home --home-dir /nonexistent --shell /usr/sbin/nologin calculator
```
`--home-dir /nonexistent` — так в Debian принято для системных пользователей. Без этого флага домашним каталогом записывается `/home/calculator`, которого нет. С `/nonexistent` libpq при поиске `~/.postgresql/` просто получает «файла нет» и не упирается в закрытый `/home` (см. `ProtectHome` в разделе 1).

В задаче 2 он создаётся вручную, в задаче 4 его создаёт `postinst` deb-пакета той же командой, а заодно выставляет `root:calculator 0640` на конфиг (см. [task4-4-deb.md](task4-4-deb.md), раздел 4). Это пользователь ОС, к роли PostgreSQL `calculator` он отношения не имеет: подключение к БД идёт по TCP с паролем из конфига.

## 4. Изменения по файлам

### Новые файлы

**`packaging/systemd/calculator.service.in`** — шаблон unit-файла (раздел 1).

**`packaging/config/calculator.json`** — конфиг по умолчанию со значениями из таблицы раздела 2. Устанавливается в `/etc/calculator/`.

**`include/calculator/Config.h`, `src/Config.cpp`** — загрузка конфигурации.
- `struct AppConfig { DataBase::Config database; }`. Отдельная структура верхнего уровня, чтобы в задаче 3 добавить `server` (порт) без изменения сигнатур.
- `AppConfig parseConfig(const std::string& json)` — разбор строки. Это чистая функция, её удобно тестировать.
- `AppConfig loadConfig(const std::string& path)` — читает файл и вызывает `parseConfig`. При ошибке чтения бросает исключение с путём в тексте.
- Ошибки разбора — `std::invalid_argument` с именем ключа, так же как в `Parser`.

### Изменяемые файлы

**[src/main.cpp](../src/main.cpp):**
- аргументы: `calculator [--config <путь>] ['<json>']`. Позиционный JSON-аргумент остаётся для ручной проверки, обновляется текст `Usage`;
- загрузка конфига идёт **после** блокировки сигналов (она остаётся первой строкой) и **до** создания потоков. Ошибка конфига означает код `1` без запуска потоков;
- `AppConfig::database` передаётся в `WorkerThread`.

**[WorkerThread.h](../include/calculator/WorkerThread.h) / [WorkerThread.cpp](../src/WorkerThread.cpp)** — конструктор дополнительно принимает `DataBase::Config` (по значению, перемещается в поле) и передаёт его в `Runner`. Конфиг копируется до старта потока и дальше живёт только в рабочем потоке, поэтому общих данных между потоками не появляется.

**[Runner.h](../include/calculator/Runner.h) / [Runner.cpp](../src/Runner.cpp)** — `explicit Runner(DataBase::Config config)` вместо конструктора по умолчанию. Конфиг передаётся в `DataBase(Config)`, такой конструктор уже есть.

**[CMakeLists.txt](../CMakeLists.txt):**
- `include(GNUInstallDirs)` — даёт `CMAKE_INSTALL_FULL_BINDIR` для шаблона;
- `src/Config.cpp` добавляется в `calculator_lib`, `nlohmann_json` уже прилинкован;
- `configure_file(packaging/systemd/calculator.service.in → ${CMAKE_BINARY_DIR}/calculator.service @ONLY)`;
- `install(FILES ${CMAKE_BINARY_DIR}/calculator.service DESTINATION lib/systemd/system)`. При префиксе `/usr/local` это `/usr/local/lib/systemd/system`, systemd ищет unit-файлы и там. При `/usr` (deb, задача 4) — `/usr/lib/systemd/system`;
- конфиг ставится в `/etc/calculator` **только если его там ещё нет**. Путь **абсолютный**: требование задания — `/etc/calculator`, а `GNUInstallDirs` при префиксе `/usr/local` дал бы `/usr/local/etc`.
  - **Почему не простой `install(FILES)`:** он перезаписывает файл при каждом `cmake --install`. Тогда сбрасываются владелец и права (`root:root 0644` вместо `root:calculator 0640`), а сервис теряет доступ к файлу. Пропадают и правки администратора: CMake сравнивает время изменения файлов, и отредактированный файл для него «устарел».
  - **Как сделать:** `install(CODE ...)` с проверкой `if(NOT EXISTS "$ENV{DESTDIR}/etc/calculator/calculator.json")`, внутри — `file(INSTALL ... PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ)`, то есть сразу `0640`. `DESTDIR` в проверке нужен для CPack в задаче 4: пакет собирается во временный каталог, файла там нет, и конфиг попадает в пакет. В установленной системе сохранность правок обеспечит уже dpkg (`conffiles`).
  - **Что остаётся вручную:** CMake не умеет менять владельца, поэтому `chown root:calculator` нужен один раз после первой установки (в задаче 4 это сделает `postinst`). При последующих установках файл не трогается, и права сохраняются.
  - **Обратная сторона:** если в репозитории изменится конфиг по умолчанию (например, в задаче 3 добавится порт), уже установленный файл не обновится, и новый ключ придётся дописать руками. Отсутствующие ключи берут значения по умолчанию (раздел 2), так что сервис от этого не сломается;
- существующий `install(TARGETS calculator ...)` переводится на `${CMAKE_INSTALL_BINDIR}`;
- если `CMAKE_BUILD_TYPE` не задан, по умолчанию ставится `Release`: устанавливаемый бинарник должен быть оптимизированным.

**[tests/tests.cpp](../tests/tests.cpp)** — тесты `parseConfig`, без файлов и без БД:
- `ConfigTest.ParsesAllFields` — все ключи читаются;
- `ConfigTest.MissingKeysUseDefaults` — `{}` даёт значения по умолчанию;
- `ConfigTest.ThrowsOnWrongType` — `"port": "abc"` даёт `std::invalid_argument`;
- `ConfigTest.ThrowsOnInvalidJson`.

**Без изменений:** `StopFlag`, `SignalThread`, `DataBase`, `Postgres`, `Parser`, `Checker`, `Calculator`, `Printer`.

## 5. Риски

- **Буферизация stdout под systemd.** В журнал stdout идёт через pipe, и `std::cout` буферизуется целиком. spdlog сбрасывает буфер после каждого сообщения, а `Printer` (`std::cout << result << '\n'`) — нет. Под systemd `Printer` не используется (задания в аргументах нет), а в задаче 3 результат уйдёт в сеть. Если результат всё же понадобится видеть в журнале, достаточно сбрасывать буфер в `Printer`.
- **Цвета spdlog.** Если stdout не терминал, `stdout_color_mt` отключает ANSI-коды сам, и мусора в журнале быть не должно. Проверяется на шаге 2 ниже.
- **Сервис `active`, но БД недоступна.** С `Type=simple` сервис будет `active (running)` несколько миллисекунд, пока не упадёт подключение. Это ожидаемо, дальше работает `Restart=on-failure`.

## 6. Проверка готовности

### 0. Подготовка (один раз)
```bash
systemctl cat postgresql@16-main | grep -E 'Before|PartOf'   # есть ли Before=postgresql.service (раздел 1)
sudo useradd --system --no-create-home --home-dir /nonexistent --shell /usr/sbin/nologin calculator
cmake -S . -B build && cmake --build build -j && ctest --test-dir build
sudo cmake --install build                                   # префикс /usr/local
sudo chown root:calculator /etc/calculator/calculator.json
sudo chmod 640 /etc/calculator/calculator.json
sudo systemctl daemon-reload
systemd-analyze verify /usr/local/lib/systemd/system/calculator.service   # пустой вывод = ок
```

**Права на конфиг после повторной установки.** Простой `install(FILES)` перезаписывал бы `/etc/calculator/calculator.json` при каждом `sudo cmake --install build`: права сбрасывались бы в `root:root 0644`, а правки в файле терялись. Поэтому в разделе 4 конфиг копируется, только если его ещё нет:
- `chown`/`chmod` выше нужны **один раз**, после первой установки;
- при повторных установках (новая сборка бинарника) файл не трогается. В выводе `cmake --install` для него вместо `Installing:`/`Up-to-date:` появляется строка `Keeping existing config: /etc/calculator/calculator.json`;
- проверка после каждой установки:
  ```bash
  stat -c '%U:%G %a' /etc/calculator/calculator.json   # ожидается root:calculator 640
  ```
  Если вывод другой (например, конфиг удалили и он поставился заново), повторить `chown root:calculator` и `chmod 640`;
- чтобы намеренно поставить свежий конфиг из репозитория, сначала удалить старый (`sudo rm /etc/calculator/calculator.json`), затем `sudo cmake --install build`, `chown` и `chmod`.

После обновления unit-файла (он перезаписывается при каждой установке, и это нормально) нужен `sudo systemctl daemon-reload`.

### 1–3. start / stop / restart

| Шаг | Команды | Ожидание |
|---|---|---|
| 1. start | `sudo systemctl start calculator`<br>`systemctl status calculator` | `Active: active (running)`, `Tasks: 3` (main + 2 потока) |
| 1a. пользователь | `ps -o user=,pid= -p $(systemctl show -p MainPID --value calculator)` | `calculator`, не `root` |
| 1b. журнал | `journalctl -u calculator -n 20 --no-pager` | `Connecting to PostgreSQL` → `Cache warmed up` → `Worker is waiting for stop request`. Без ANSI-мусора |
| 2. stop | `sudo systemctl stop calculator`<br>`journalctl -u calculator -n 10 --no-pager`<br>`systemctl show -p Result,ExecMainStatus calculator` | В журнале `Received SIGTERM` → `Worker is shutting down` → `Database connection closed` → `Stopped by signal 15`, затем от systemd `Deactivated successfully`. `Result=success`, `ExecMainStatus=0`. Нет строки `State 'stop-sigterm' timed out. Killing` |
| 3. restart | Во втором терминале `journalctl -u calculator -f`, затем `sudo systemctl restart calculator` | В журнале полная последовательность остановки, затем новый запуск. `MainPID` в `systemctl status` сменился |

### 4. `Restart=on-failure`

| Сценарий | Команды | Ожидание |
|---|---|---|
| Падение процесса | `sudo kill -KILL $(systemctl show -p MainPID --value calculator)` | Через 5 с сервис снова `active`, в журнале `Scheduled restart job, restart counter is at 1` |
| Штатный выход не перезапускается | `sudo kill -TERM $(systemctl show -p MainPID --value calculator)` | Сервис `inactive (dead)`, перезапуска нет: код выхода `0` |
| Нет БД, потом появилась | `sudo pg_ctlcluster 16 main stop`<br>`sudo systemctl start calculator`<br>через ~10 с `sudo pg_ctlcluster 16 main start` | В журнале `Connection refused`, `exit 1`, несколько `Scheduled restart job`. После запуска кластера очередная попытка успешна, сервис `active` |
| Лимит перезапусков | То же, но кластер не запускать 30+ с | `Start request repeated too quickly`, сервис `failed`. Сброс: `sudo systemctl reset-failed calculator` |

### 5. Конфигурация

| Сценарий | Действие | Ожидание |
|---|---|---|
| Конфиг читается | В `/etc/calculator/calculator.json` поставить неверный `database.port` (например, `5433`), `sudo systemctl restart calculator` | Ошибка подключения к порту `5433` в журнале. Вернуть `5432`, restart, сервис `active` |
| Битый конфиг | Сломать JSON, restart | Ошибка разбора с путём к файлу в журнале, сервис `failed` после лимита. Восстановить файл |

### 6. Автозапуск (по желанию)
```bash
sudo systemctl enable calculator && sudo reboot
# после загрузки:
systemctl status calculator          # active (running)
journalctl -u calculator -b          # запуск после PostgreSQL, без ошибок подключения
```

### Полезные флаги `journalctl`
- `-u calculator` — только наш сервис;
- `-f` — следить в реальном времени;
- `-b` — с текущей загрузки;
- `--since "5 min ago"` — за последние 5 минут;
- `-o cat` — без префиксов journald (у spdlog своё время).
