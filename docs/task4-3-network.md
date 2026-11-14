# ДЗ-4. Задача 3 — клиент-сервер на Boost.Asio

План изменений для задачи 3 из [task4.md](../task4.md).

**Критерий готовности (из задания):** выполнены требования разделов 4 и 5 задания:
- сервис получает задание от клиента по сети, считает и отправляет результат обратно;
- одновременно подключён один клиент;
- сеть сделана на Boost.Asio;
- тестовый клиент отправляет задания сервису, тесты проверяют результат, **полученный по сети**;
- в тестах нет `sleep_for`, ожидание идёт busy-циклами с ограничением по времени.

Ещё одно требование, из раздела 3 задания: итоговая версия проверяется ThreadSanitizer. После этой задачи модель потоков больше не меняется, поэтому основная TSan-проверка делается здесь. Финальный прогон, как требует задание, повторяется после задачи 4 (раздел 12).

**Что входит в задачу:**
- TCP-сервер на Boost.Asio в рабочем потоке. Порт берётся из секции `server` конфига;
- протокол «JSON-строка с `\n` → JSON-ответ с результатом и статусом»;
- `Runner` возвращает результат, а не печатает его в stdout;
- подписка на остановку в `StopFlag`: поток сигналов останавливает `io_context` через неё;
- защита от `SIGPIPE`;
- тестовый клиент на Boost.Asio и сетевые тесты.

**Вне рамок задачи:**
- deb-пакет (задача 4);
- одновременное обслуживание нескольких клиентов;
- TLS и аутентификация;
- таймаут простоя клиента (раздел 13).

---

## 1. Текущее состояние

- **Рабочий поток** ([WorkerThread.cpp](../src/WorkerThread.cpp)) создаёт `Runner`, а тот внутри создаёт `DataBase`. Затем поток выполняет задание из аргумента командной строки, если оно есть, и ждёт в `stopFlag.waitForStop()`.
- **`Runner::run(json)`** печатает результат через `Printer` в stdout/stderr и возвращает статус.
- **Задания** приходят только из аргумента командной строки. Под systemd сервис сейчас ничего не делает, только ждёт сигнала.
- **`StopFlag`** умеет только будить ждущих через `condition_variable`, подписки на остановку нет.

## 2. Откуда берётся Boost

**Рекомендация: пакет из apt, `libboost-dev`.**

```bash
sudo apt install libboost-dev      # Ubuntu 24.04 → Boost 1.83, Ubuntu 22.04 → 1.74
```

- **Нужны только заголовки.** Boost.Asio header-only, Boost.System header-only с версии 1.69. Бинарник не линкуется ни с одной `.so` из Boost.
- **Для deb-пакета (задача 4):** зависимости от Boost во время работы нет, только при сборке (`Build-Depends`). В `Depends` пакета останется `libpq5`.
- **Для TSan:** заголовки Asio компилируются в наших единицах трансляции с `-fsanitize=thread`. Поэтому Asio инструментируется вместе с нашим кодом. Обычная причина собирать зависимости из исходников ради санитайзеров здесь не действует.
- **В CMake:** `find_package(Boost 1.74 REQUIRED)`, цель `Boost::headers`. Минимум 1.74 оставлен, чтобы проект собирался и на Ubuntu 22.04. В CMake 3.30+ модуль `FindBoost` удалён (политика `CMP0167`). Ubuntu ставит `BoostConfig.cmake` вместе с пакетом, поэтому `find_package` будет работать и там. Политику ставим в `NEW`, если CMake её знает.

**Отклонённая альтернатива — FetchContent.**
- Релизный архив Boost весит десятки МБ, а в распакованном виде сотни.
- Он скачивается и конфигурируется в каждой директории сборки, а у нас их две: `build` и `build-tsan`. Configure становится дольше на минуты.
- Единственный выигрыш — версия зафиксирована в репозитории.

Если понадобится, можно позже добавить `option(CALCULATOR_FETCH_BOOST)` с `BOOST_INCLUDE_LIBRARIES asio`, не трогая остальной код.

## 3. Модель потоков

Потоки те же, что в задаче 1. Меняется только то, чем занят рабочий поток.

| Поток | Что делает |
|---|---|
| `main` | Блокирует `SIGINT`/`SIGTERM`, игнорирует `SIGPIPE`, читает конфиг, запускает два потока, ждёт их (`join`) |
| Поток сигналов | Без изменений: `sigwait`, затем `stopFlag.requestStop(Signal)`. Через подписку это вызывает `io_context.stop()` |
| Рабочий поток | `DataBase`: подключение и прогрев кеша → `Runner(db)` → `runServer(...)`: `io_context.run()` обслуживает клиентов → после `stop()` выход, деструкторы закрывают сокеты и соединение с БД |

**Инвариант из задачи 1 расширяется.** `io_context`, акцептор, сокет клиента, `Runner` и `DataBase` создаются, используются и уничтожаются только в рабочем потоке. Между потоками общими остаются:
- `StopFlag` — под мьютексом;
- вызов `io_context::stop()` — по документации Asio он потокобезопасен, исключения только `restart()` и `notify_fork()`;
- `Logger` — spdlog в `_mt`-варианте.

**Почему асинхронный Asio, а не блокирующие `accept`/`read`.** Блокирующий вызов нельзя корректно прервать из другого потока. Если поток сигналов закроет сокет, пока рабочий поток стоит в `read()`, это одновременный доступ к одному объекту Asio: гонка данных для TSan и неопределённое поведение. В асинхронной модели рабочий поток ждёт внутри `io_context.run()` (epoll), а потокобезопасный `stop()` — единственное, что делает с ним поток сигналов.

### Последовательность при `SIGTERM`

```
поток сигналов                      рабочий поток
 sigwait(...)  [ждёт]                DataBase: connect + warmUpCache
                                     io_context, Server: bind + listen
                                     subscription = onStop([&]{ io.stop(); })
                                     io.run()  [обслуживает клиентов]
   ◀── SIGTERM (systemctl stop)
 requestStop(Signal, SIGTERM)
   └─ колбэк вне мьютекса: io.stop() ──▶ run() возвращается
 выход                               ~Subscription → ~Server (сокеты) → ~io_context
                                     ~DataBase → PQfinish
```

## 4. Протокол

**Запрос:** одна строка JSON, завершённая `\n`. Формат прежний, его разбирает существующий `Parser`:
```
{"op":"+","a":2,"b":3}
{"op":"!","a":5}
```

**Ответ:** одна строка JSON, завершённая `\n`:

| Ситуация | Ответ |
|---|---|
| Успех | `{"result":5,"status":0}` |
| Ошибка вычисления | `{"error":"Division by zero","result":0,"status":2}` |
| Некорректный запрос | `{"error":"Missing or invalid field: 'b'","result":0,"status":-1}` |
| Внутренняя ошибка (сбой записи в БД) | `{"error":"Internal error","result":0,"status":-2}` |

**Коды статуса:**
- `0..3` — коды `mathlib` (`MATH_OK=0`, `MATH_OVERFLOW=1`, `MATH_DIV0=2`, `MATH_DOMAIN=3`). Эти же коды хранятся в БД (ДЗ-3);
- **отрицательные** — коды протокола: `-1` некорректный запрос, `-2` внутренняя ошибка. С кодами `mathlib` они не пересекаются и в БД не попадают;
- поле `error` есть только при ненулевом статусе;
- nlohmann выводит ключи по алфавиту, поэтому клиент не должен зависеть от их порядка.

**Правила:**
- **Много запросов в одном соединении.** Ответы идут в порядке запросов. Если клиент прислал несколько строк одним пакетом (pipelining), каждая получит свой ответ.
- **`\r` в конце строки отбрасывается.** Это для совместимости с `nc` и telnet.
- **Лимит строки — 4096 байт с `\n`.** При превышении сервер отвечает `-1` `"Request too long"` и закрывает соединение: найти начало следующего запроса в потоке уже нельзя.
- **Пустая строка или мусор** дают ответ `-1`, соединение остаётся открытым.
- **Клиент закрыл соединение (EOF)** — сервер принимает следующего клиента.
- **Внутренняя ошибка** (например, БД отвалилась при `writeRecord`) — подробности пишутся в лог, клиент получает `-2` без деталей. Сервис продолжает работать: ответы из кеша по-прежнему отдаются.

Построчный JSON выбран за простоту: его можно проверить руками через `nc`, а `Parser` переиспользуется без изменений.

## 5. Подписка на остановку в `StopFlag`

Правила взяты из раздела 7 плана [задачи 1](task4-1-signals.md#7-как-это-ляжет-на-задачи-2-и-3). К ним добавляется отписка, зачем она нужна — ниже.

**API:**
- `[[nodiscard]] Subscription onStop(std::function<void()> callback)`;
- `Subscription` — RAII-объект, только перемещаемый. Деструктор отписывает колбэк.

**Семантика:**
1. **Подписка до остановки.** Колбэк сохраняется под мьютексом и будет вызван ровно один раз из `requestStop`.
2. **Подписка после остановки.** Колбэк вызывается **сразу** в вызывающем потоке, вне мьютекса. Возвращается пустая `Subscription`. Проверка флага и добавление колбэка делаются под одним захватом мьютекса, поэтому окна между ними нет. Для сервера это значит: `SIGTERM`, пришедший во время подключения к БД, не потеряется. `io.stop()` выполнится до `io.run()`, и `run()` вернётся сразу: по документации Asio остановленный `io_context` не запускается до `restart()`.
3. **`requestStop`.** Под мьютексом:
   - ставит флаг и причину;
   - переносит список колбэков в локальную переменную;
   - отмечает, что колбэки выполняются, и в каком потоке;
   - вызывает `notify_all` (как сейчас, под мьютексом).
   
   Затем **вне мьютекса** вызывает колбэки по порядку. После этого снова под мьютексом снимает отметку и вызывает `notify_all`. Колбэк может сам обращаться к `StopFlag` без самоблокировки.
4. **Исключение из колбэка** проглатывается. По смыслу колбэки должны быть `noexcept`, но так гарантируется, что остальные колбэки выполнятся, а отметка снимется.
5. **Отписка (добавлено к правилам задачи 1).** Колбэк сервера держит ссылку на `io_context`, а тот живёт на стеке рабочего потока. Без отписки возможны два сценария use-after-free:
   - рабочий поток упал с исключением, `io_context` уже уничтожен при раскрутке стека, а затем вызывается `requestStop(Error)`, который дёргает колбэк на мёртвом объекте;
   - поток сигналов уже забрал колбэк и вот-вот вызовет `io.stop()`, а рабочий поток в этот момент уничтожает `io_context`.

   Деструктор `Subscription` поступает так:
   - если колбэк ещё в списке, удаляет его;
   - если колбэк **прямо сейчас выполняется в другом потоке**, ждёт его завершения на том же `condition_variable`. Так работает `std::stop_callback` из C++20, но проект на C++17, поэтому реализация своя;
   - если колбэк выполняется в **этом же** потоке (колбэк отписывает сам себя или `requestStop` вызван из этого потока), не ждёт. Иначе была бы самоблокировка.
6. **Порядок объектов в `runServer`:** `io_context` → `Server` → `Subscription`. Уничтожаются они в обратном порядке, поэтому подписка снимается первой, и после этого ни один колбэк не тронет `io_context`.
7. **`StopFlag` должен пережить все подписки.** В `main` это выполняется: `StopFlag` объявлен до потоков и уничтожается после их `join`.

`waitForStop`/`waitForStopFor`/`waitingThreads` остаются без изменений. Сервис их больше не использует, но они нужны тестам.

## 6. Сервер

**`runServer(const ServerConfig&, Runner&, StopFlag&, onListening)`** — общая функция для рабочего потока и для тестов. Тесты проходят тот же путь, что и сервис, отличается только хранилище (раздел 7).
1. `boost::asio::io_context io;`
2. `Server server(io, config, runner);` — `open` → `reuse_address(true)` → `bind` → `listen`. Ошибка (например, порт занят) — исключение с `адрес:порт` в тексте. `reuse_address` нужен, чтобы `systemctl restart` не упирался в `Address already in use` из-за соединений в `TIME_WAIT`.
3. `onListening(server.localEndpoint())` — вызывается после `listen`. Сервис пишет в лог `Listening on 127.0.0.1:7777`, тесты публикуют реальный порт (они слушают порт `0`, раздел 11).
4. `auto subscription = stopFlag.onStop([&io] { io.stop(); });`
5. `io.run();` — возвращается после `stop()`.

**Один клиент за раз.** Следующий `async_accept` запускается только после конца текущей сессии.
- Второй клиент при этом всё равно подключится на уровне TCP (ядро держит его в очереди `listen`), но его запросы не читаются, пока первый не отключится.
- Это соответствует требованию «к серверу подключается только один клиент за раз». Отказ с ответом «занято» вне рамок.

**Цикл сессии** (все обработчики выполняются в рабочем потоке):
```
async_accept → async_read_until('\n') → runner.handle(строка) → async_write(ответ) → async_read_until …
                         └─ EOF / ошибка ─▶ закрыть сокет, очистить буфер ─▶ async_accept
```
- **Буфер** — `boost::asio::streambuf` с `max_size` 4096. Если `\n` не найден до заполнения, `read_until` завершается с ошибкой `not_found`. Это и есть случай «Request too long».
- **После чтения из буфера удаляется ровно одна строка.** Остаток остаётся в буфере, так работает pipelining.
- **Строка ответа хранится в поле `Server`**, пока `async_write` не завершится.
- **При закрытии сессии буфер очищается**, чтобы остаток данных предыдущего клиента не попал следующему.
- **`operation_aborted`** приходит при остановке: accept и чтение больше не перезапускаются.
- **Сетевые ошибки** (`connection_reset`, `broken_pipe`, `eof`) пишутся в лог, сессия закрывается, сервер принимает следующего клиента. Исключений из обработчиков нет.
- **Запрос обрабатывается синхронно внутри обработчика** (`Runner::handle`, возможно с записью в БД). Для одного клиента это нормально. `stop()` срабатывает после возврата текущего обработчика.
- **Исключение, вылетевшее из `io.run()`,** возможно только при ошибке в коде (например, `bad_alloc`). Оно доходит до `WorkerThread`: код выхода `1`, systemd перезапускает сервис.

## 7. `Runner` возвращает результат

- **`explicit Runner(TaskStorage& storage)`.** `Runner` больше не владеет БД, ею владеет `WorkerThread`.
- **`Response handle(const std::string& requestLine)`:**
  1. `Parser` → `Checker` → `storage.getRecord` → при промахе `Calculator` и `storage.writeRecord`;
  2. результат — `Response{status, result, error}`.

  Ошибки `Parser`/`Checker` (`std::invalid_argument`) дают `-1` с текстом ошибки. Остальные исключения (БД) дают `-2`, подробности только в лог.
- **В stdout ничего не печатается.** `Printer` удаляется, тексты ошибок `mathlib` переезжают в `Protocol` (`statusMessage`).
- **`TaskStorage`** — интерфейс с двумя методами `getRecord`/`writeRecord`. `DataBase` реализует его без изменения логики. Тесты используют `InMemoryStorage`.

  Зачем это нужно: сетевые тесты не должны требовать живой PostgreSQL и засорять `calculation_history`. Ещё они должны проходить под Valgrind (`memcheck`) и TSan на любой машине. Исключение одно — интеграционный тест с настоящей `DataBase`. Если PostgreSQL недоступен, он пропускается (раздел 11).

## 8. `SIGPIPE`

Запись в сокет, который клиент уже закрыл, порождает `SIGPIPE`. Действие по умолчанию — завершить процесс без деструкторов. Защита в два слоя:

1. **`main`.** `sigaction(SIGPIPE, SIG_IGN)` сразу после блокировки `SIGINT`/`SIGTERM`, до создания потоков. Для этого в модуле сигналов появляется функция `SignalThread::ignoreBrokenPipe()`. После неё запись возвращает `EPIPE`, Asio сообщает ошибку `broken_pipe`, и сессия закрывается.
   - Игнорируем, а не блокируем: заблокированный `SIGPIPE` висел бы в ожидающих сигналах, а `SIG_IGN` — стандартный путь для сетевых сервисов.
   - Это действует на весь процесс, включая libpq.
2. **Asio на Linux** отправляет данные с флагом `MSG_NOSIGNAL`, поэтому при записи в сокет `SIGPIPE` даже не генерируется.
   - Тестовый бинарник **нарочно не** игнорирует `SIGPIPE`. Тест `SurvivesClientDisconnectBeforeResponse` (раздел 11) проверяет второй слой: если он перестанет работать, тестовый процесс умрёт от `SIGPIPE`, и тест упадёт.
   - Первый слой проверяется вручную (раздел 15).

## 9. Конфигурация

- **Структуры:** `AppConfig { DataBase::Config database; ServerConfig server; }` и `ServerConfig { std::string address{"127.0.0.1"}; std::uint16_t port{7777}; }`.
- **Порт `7777`** обычно свободен и больше 1024. Поэтому сервису от пользователя `calculator` не нужны root или `CAP_NET_BIND_SERVICE`.
- **`address` (предложение сверх заданного, в задании требуется только порт).** По умолчанию `127.0.0.1`, то есть только локальные подключения. Открыть в сеть можно, указав в конфиге `"0.0.0.0"`.
- **`parseConfig`** разбирает секцию `server` теми же помощниками: `readString` для `address`, `readPort` для `port` (1..65535). Адрес проверяется через `boost::asio::ip::make_address`: при ошибке — `std::invalid_argument` с именем ключа. Так неверный адрес отсекается в `main` до запуска потоков.
- **Порт `0` в конфиге запрещён.** Тесты создают `ServerConfig` напрямую с портом `0`.
- **[packaging/config/calculator.json](../packaging/config/calculator.json)** получает секцию `server`. Уже установленный `/etc/calculator/calculator.json` не перезаписывается (правило задачи 2). Если секции в нём нет, действуют значения по умолчанию `127.0.0.1:7777`, и сервис работает без правок. Чтобы сменить порт, файл нужно поправить руками.

## 10. Изменения по файлам

### Новые файлы

- **`include/calculator/Protocol.h`, `src/Protocol.cpp`:**
  - `struct Response { int status; int result; std::string error; }`;
  - константы `kStatusInvalidRequest = -1`, `kStatusInternalError = -2`, `kMaxRequestSize = 4096`;
  - `std::string statusMessage(int status)` — тексты из `Printer`;
  - `std::string serializeResponse(const Response&)` — JSON без `\n`, перевод строки добавляет сервер.
- **`include/calculator/TaskStorage.h`** — интерфейс `getRecord`/`writeRecord`, только заголовок.
- **`include/calculator/Server.h`, `src/Server.cpp`** — класс `Server` (акцептор, сокет текущего клиента, буфер, строка ответа, ссылка на `Runner`) и функция `runServer` (раздел 6).
- **`tests/TestClient.h`, `tests/TestClient.cpp`, `tests/TestServer.h`, `tests/InMemoryStorage.h`, `tests/network_tests.cpp`** — вспомогательные классы, сетевые тесты и интеграционный тест с настоящей БД (раздел 11).

### Изменяемые файлы

- **[StopFlag.h](../include/calculator/StopFlag.h) / [StopFlag.cpp](../src/StopFlag.cpp)** — `onStop` и `Subscription` (раздел 5). Новые поля: список колбэков с идентификаторами, счётчик идентификаторов, отметка «колбэки выполняются» и id потока.
- **[SignalThread.h](../include/calculator/SignalThread.h) / [SignalThread.cpp](../src/SignalThread.cpp)** — `static void ignoreBrokenPipe()`.
- **[Config.h](../include/calculator/Config.h) / [Config.cpp](../src/Config.cpp)** — `ServerConfig`, секция `server`, проверка адреса.
- **[DataBase.h](../include/calculator/DataBase.h)** — `class DataBase : public TaskStorage`, у `getRecord`/`writeRecord` добавляется `override`. `DataBase.cpp` не меняется.
- **[Runner.h](../include/calculator/Runner.h) / [Runner.cpp](../src/Runner.cpp)** — `Runner(TaskStorage&)`, `handle(line) → Response`. `Parser`/`Checker`/`Calculator` становятся полями, а не создаются на каждый запрос.
- **[WorkerThread.h](../include/calculator/WorkerThread.h) / [WorkerThread.cpp](../src/WorkerThread.cpp)** — конструктор `(StopFlag&, AppConfig)`. В потоке:
  1. `DataBase db(config.database)`, затем `connect` + `warmUpCache`;
  2. `Runner runner(db)`;
  3. `runServer(config.server, runner, stopFlag_, лог «Listening on …»)`;
  4. лог `Server stopped`, после выхода из области видимости — `Database connection closed`.

  `waitForStop()` больше не вызывается, вместо него `io.run()`.
- **[main.cpp](../src/main.cpp):**
  - `ignoreBrokenPipe()` после `blockTerminationSignals()`;
  - позиционный аргумент с JSON-заданием **удаляется**: задания приходят только по сети, формат запуска `calculator [--config <путь>]`;
  - в `WorkerThread` передаётся весь `AppConfig`.

  Ручные проверки из задач 1–2 вида `./build/calculator '<json>'` заменяются на `nc` (раздел 15).
- **[Parser.cpp](../src/Parser.cpp)** — проверка диапазона `int` для `a`/`b` (раздел 13, шаг 5).
- **[Calculator.cpp](../src/Calculator.cpp)** — защита от тяжёлых входов для `!` и `^` (раздел 13, шаг 5).
- **[CMakeLists.txt](../CMakeLists.txt):**
  - `find_package(Boost 1.74 REQUIRED)` с политикой `CMP0167`;
  - `Boost::headers` линкуется в `calculator_lib` как `PUBLIC`, потому что `Server.h` открывает типы Asio;
  - новые исходники `src/Protocol.cpp`, `src/Server.cpp`; `src/Printer.cpp` убирается;
  - тестовые исходники `tests/network_tests.cpp`, `tests/TestClient.cpp`;
  - `nlohmann_json::nlohmann_json` для `calculator_tests`, чтобы клиент разбирал ответы;
  - `gtest_discover_tests(... PROPERTIES TIMEOUT 60)` и `TIMEOUT 300` для `memcheck`, чтобы зависший тест падал, а не вешал `ctest`.
- **[packaging/config/calculator.json](../packaging/config/calculator.json)** — секция `server`.
- **[tests/tests.cpp](../tests/tests.cpp)** — тесты подписки `StopFlag`, `Protocol`, `Runner`, секции `server` в конфиге (раздел 11).

### Удаляемые файлы

- **`include/calculator/Printer.h`, `src/Printer.cpp`** — заменены на `Protocol`.

### Без изменений

`Checker`, `Postgres`, `DataBase.cpp`, `Logger`, `Task.h`, unit-файл systemd. `ExecStart` не меняется, порт берётся из конфига.

## 11. Тесты

### Вспомогательные классы (`tests/`)

- **`InMemoryStorage`** — `unordered_map` по ключу `makeTaskKey` и счётчик записей `std::atomic<int>`. Атомик нужен потому, что счётчик пишет поток сервера, а читает поток теста (раздел 12).
- **`TestServer`** — RAII-обёртка.
  - Конструктор принимает `TaskStorage&`, хранилище создаёт сам тест. Обычно это `InMemoryStorage`, в интеграционном тесте — настоящая `DataBase`.
  - Конструктор запускает `std::thread` с `runServer`: порт `0`, переданное хранилище, собственный `StopFlag`.
  - `onListening` кладёт реальный порт в `std::atomic<std::uint16_t>`.
  - `waitUntilListening(timeout)` ждёт этого busy-циклом.
  - Деструктор вызывает `requestStop` и `join`.
  - Исключение из потока сервера сохраняется и проверяется тестом после `join`.
- **`TestClient`** — синхронный интерфейс поверх асинхронного Asio с `io_context::run_for(timeout)`, по образцу `blocking_tcp_client` из примеров Asio.
  - Методы: `connect(port)`, `request(line) → Response` (разобранный JSON), `sendRaw(bytes)`, `readLine()`, `isClosedByServer()`.
  - При таймауте сокет закрывается, и тест падает, а не висит.
  - Это не sleep: `run_for` ждёт событий и возвращается, как только операция завершилась.
- **`waitUntil(predicate, timeout)`** — busy-цикл с `std::this_thread::yield()` и дедлайном по `steady_clock`.

**Таймауты щедрые, 5 с.** `memcheck` прогоняет те же тесты под Valgrind, а он в 10–50 раз медленнее.

**Каждый сетевой тест поднимает свой сервер на порту `0`.** Поэтому `ctest -j` безопасен, и тесты не конфликтуют с установленным сервисом на `7777`.

### Список тестов

**`StopFlag`, подписка** (`tests.cpp`):
- `OnStopCalledOnRequest` — колбэк вызван после `requestStop`.
- `OnStopAfterRequestCalledImmediately` — подписка после `requestStop`: колбэк вызван внутри `onStop`.
- `CallbackCalledExactlyOnce` — два `requestStop` дают один вызов.
- `CallbackRunsOutsideLock` — колбэк вызывает `flag.isStopRequested()`. Если колбэк вызывается под мьютексом, тест зависнет, и его оборвёт `TIMEOUT` ctest.
- `UnsubscribedCallbackNotCalled` — `Subscription` уничтожена до `requestStop`, вызова нет.

Ожидание деструктора `Subscription`, пока колбэк выполняется в другом потоке, без sleep детерминированно не проверить. Его покрывают TSan и ревью кода.

**`Protocol`:** `SerializesSuccess`, `SerializesError` (поля и значения), `StatusMessages`.

**`Runner`** (без сети, `InMemoryStorage`):
- `ReturnsResult`;
- `CommutativeRequestHitsCache` — после `2+3` и `3+2` счётчик записей равен `1`;
- `InvalidJsonGivesInvalidRequest` — `-1`;
- `DivisionByZeroGivesMathStatus` — `2`, текст ошибки.

**`Config`:** `ParsesServerSection`, `ServerSectionDefaults`, `ThrowsOnInvalidAddress`.

**`Server`** (`network_tests.cpp`, через `TestServer` + `TestClient`, проверяется ответ, полученный по сети):

| Тест | Проверка |
|---|---|
| `ReturnsResultOverNetwork` | `{"op":"+","a":2,"b":3}` → `status 0`, `result 5` |
| `ReturnsMathErrorOverNetwork` | `5 / 0` → `status 2`, `result 0`, `error "Division by zero"` |
| `InvalidRequestKeepsConnection` | Мусор → `-1`. Следующий корректный запрос в **том же** соединении → правильный результат |
| `SeveralRequestsOnOneConnection` | `+ - * / ^ !` подряд, каждый результат сверяется |
| `PipelinedRequests` | Две строки одним `write` → два ответа в правильном порядке |
| `ServesNextClientAfterDisconnect` | Клиент 1 получил ответ и отключился, клиент 2 получает свой ответ |
| `TooLongRequestClosesConnection` | 5000 байт без `\n` → ответ `-1`, затем сервер закрыл соединение (EOF) |
| `SurvivesClientDisconnectBeforeResponse` | Клиент отправил запрос и сразу закрыл сокет. Процесс жив (проверка `SIGPIPE`), следующий клиент получает правильный результат |
| `StopsOnStopRequest` | `requestStop` → поток сервера завершился (`waitUntil` с таймаутом), новое подключение отклоняется |
| `StopBeforeRunIsNotLost` | `requestStop` до старта `TestServer` → `runServer` сразу возвращается. Сквозная проверка правила «подписка после остановки вызывается сразу» |
| `HeavyInputsAnsweredOverNetwork` (шаг 5) | `{"op":"!","a":100000000}` → `status 1` (переполнение) в пределах таймаута. Процесс жив, следующий запрос обслуживается |

### Интеграционный тест с настоящей БД

**`IntegrationTest.RealDatabaseOverNetwork`** (`network_tests.cpp`) — настоящая `DataBase` + `runServer` + `TestClient`.

1. **Конфиг БД.** Если задана переменная окружения `CALCULATOR_TEST_CONFIG`, конфиг читается по этому пути через `loadConfig`. Иначе берутся встроенные значения `DataBase::Config`: `localhost`, `calculator`/`calculator`, те же, что у сервиса на ВМ.
2. **Подключение или пропуск.** `DataBase db(config)`, затем `connect()` + `warmUpCache()` внутри `try`. При исключении — `GTEST_SKIP() << "PostgreSQL недоступен: " << e.what()`. ctest покажет тест как `Skipped`, прогон останется зелёным.
3. **Запрос по сети.** `TestServer server(db)`, затем `TestClient` отправляет `{"op":"-","a":-424242,"b":1}`. Проверяется ответ, полученный по сети: `status 0`, `result -424243`.
4. **Результат действительно в PostgreSQL**, а не только в кеше. После остановки сервера второй, независимый `DataBase` подключается, прогревает кеш из таблицы и находит эту задачу с тем же результатом.

**Детали:**
- **Владение `DataBase`.** Объект создаётся в потоке теста. Пока сервер работает, им пользуется только поток сервера, а после `join` объект уничтожается в потоке теста. Создание потока и `join` задают happens-before, и libpq видит соединение в одном потоке за раз (инвариант раздела 3 соблюдён).
- **Запись в реальную таблицу.** Тест пишет одну фиксированную строку. Запись идёт через upsert `ON CONFLICT DO UPDATE`, поэтому повторные прогоны новых строк не добавляют. Числа нарочно необычные, чтобы строку было легко узнать в `psql`. Чтобы не трогать рабочую базу, через `CALCULATOR_TEST_CONFIG` можно указать отдельную.
- **libpq не инструментирована TSan** (это системная библиотека). Отчётов изнутри неё не ожидается: каждое соединение используется одним потоком за раз. Если отчёты всё же появятся, добавим файл подавлений только для libpq, с объяснением.
- **Скорость пропуска.** Если на localhost PostgreSQL не запущен, подключение отклоняется сразу (`Connection refused`), так что пропуск не тормозит прогон. От зависания на недоступном удалённом хосте защищает общий `TIMEOUT` ctest.
- **Valgrind.** `memcheck` прогоняет и этот тест. На ВМ с PostgreSQL заодно проверяется освобождение ресурсов libpq, чего не было в ДЗ-3 (там тесты БД не трогали).

## 12. ThreadSanitizer

- **Основная проверка — здесь, финальная — после задачи 4.**
  - После этой задачи модель потоков не меняется, поэтому гонки, если они есть, найдутся уже на ней. Здесь их и исправляем.
  - Задание требует проверять итоговую версию «после реализации всех пунктов ДЗ». Задача 4 меняет сборку (CPack, параметры сборки, возможные правки по ходу). Поэтому после задачи 4 TSan-прогон **повторяется целиком**: `ctest --test-dir build-tsan` и ручной запуск TSan-бинарника с запросами через `nc` и `SIGTERM` (раздел 15.3). Этот шаг войдёт в план задачи 4 отдельным пунктом.
- **Данные, общие для потоков, в сервисе:** `StopFlag` (мьютекс), `io_context::stop()` (потокобезопасен), `Logger` (spdlog `_mt`). Всё остальное живёт в рабочем потоке (раздел 3).
- **Синхронизацию через сокет TSan не видит.** Если тест получил ответ по TCP, для TSan это не happens-before с потоком сервера. Поэтому всё, что тест читает из записанного сервером, должно быть атомиком (порт, счётчик записей) или читаться после `join` (исключение из потока сервера). Это главный источник ложных отчётов, его закладываем сразу.
- **Asio header-only**, поэтому инструментируется вместе с нашим кодом (раздел 2).
- **Где прогоняется:**
  - `ctest --test-dir build-tsan`, где сетевые тесты гоняют потоки теста и сервера;
  - ручной запуск `build-tsan/calculator` с запросами через `nc` и `SIGTERM` (раздел 15).

## 13. Риски и защита входа

Теперь задания приходят из сети, и любой клиент может прислать что угодно. Я посмотрел код `mathlib` v0.1.1 и нашёл два входа, которые роняют или подвешивают сервис одним запросом. Пункты 1–3 сделаны **сверх заданного** и включены в план последним шагом (раздел 14, шаг 5).

1. **Переполнение `int` в `Parser`.** `"a": 3000000000` проходит проверку `is_number_integer()`, а `get<int>()` молча превращает его в другое число (найдено ещё при первом разборе проекта). Исправление: проверять диапазон `INT_MIN..INT_MAX`, иначе `-1`. Тест `ParserTest.ThrowsOnIntOverflow`.
2. **`mathlib::fact` рекурсивна на глубину `n`.** `{"op":"!","a":100000000}` переполнит стек рабочего потока: `SIGSEGV`, сервис падает. systemd его перезапустит, но уронить сервис может любой клиент. Поскольку уже `13!` не помещается в `int`, `Calculator` при `n > 12` сразу возвращает `MATH_OVERFLOW`, не вызывая `fact`. Тест `CalculatorTest.LargeFactorialOverflowsFast`.
3. **`mathlib::powi` крутит цикл `exp` раз.** Для оснований `0`, `1`, `-1` и `exp = 2000000000` это секунды CPU в рабочем потоке: сервис не отвечает, остановка задерживается. При `|base| ≥ 2` переполнение наступает максимум за 31 итерацию. `Calculator` считает эти три основания напрямую: `1 → 1`; `0 → (exp == 0 ? 1 : 0)`; `-1 → ±1` по чётности. Тест `CalculatorTest.PowerOfOneWithHugeExponentIsFast`.

   Пункты 2–3 можно исправить и в самой `mathlib` (выпустить v0.1.2 и поднять `GIT_TAG`). Защита в `Calculator` локальная и проверяется тестами уже сейчас.
4. **Простаивающий клиент занимает единственное место навсегда** (модель «один клиент за раз»). Решение — таймаут простоя на `steady_timer` в сессии. Вне рамок, но стоит упомянуть в README.
5. **Без аутентификации и TLS.** По умолчанию сервер слушает только `127.0.0.1`.

## 14. Порядок реализации

Пять шагов, каждый — отдельный коммит. После каждого шага проект собирается, все тесты зелёные в обеих сборках (`build` и `build-tsan`), и шаг можно проверить на ВМ до перехода к следующему.

### Шаг 1. Подписка в `StopFlag`
- **Что:** `onStop` и `Subscription` по разделу 5. Плюс `TIMEOUT` для ctest: он нужен уже здесь, потому что `CallbackRunsOutsideLock` при ошибке зависает.
- **Файлы:** `StopFlag.h`/`.cpp`, `CMakeLists.txt` (`TIMEOUT`).
- **Тесты:** пять тестов подписки `StopFlag` (раздел 11).
- **Готово, когда:** тесты зелёные, в TSan-сборке нет предупреждений. Поведение сервиса не меняется: подписку пока никто не использует.

### Шаг 2. `Protocol` и `Runner`
- **Что:**
  - `Protocol` (`Response`, коды, `statusMessage`, `serializeResponse`);
  - интерфейс `TaskStorage`, `DataBase` его реализует;
  - `Runner(TaskStorage&)` с `handle(line) → Response`;
  - `Printer` удаляется.

  Чтобы сборка оставалась рабочей, `WorkerThread` в этом шаге сам создаёт `DataBase` и `Runner(db)`. Задание из аргумента командной строки он **временно** выводит в stdout как JSON-ответ. Это уберётся на шаге 3.
- **Файлы:** `Protocol.h`/`.cpp`, `TaskStorage.h`, `DataBase.h`, `Runner.h`/`.cpp`, `WorkerThread.cpp`, `CMakeLists.txt` (исходники), удаление `Printer.h`/`.cpp`, `tests/InMemoryStorage.h`.
- **Тесты:** `Protocol`, `Runner` (раздел 11).
- **Готово, когда:** тесты зелёные, а `./build/calculator --config packaging/config/calculator.json '{"op":"+","a":2,"b":3}'` печатает `{"result":5,"status":0}`.

### Шаг 3. Сервер, конфиг, `main`, CMake
- **Что:**
  - `Server` и `runServer` (раздел 6);
  - `ServerConfig` и секция `server` в конфиге (раздел 9);
  - `ignoreBrokenPipe()` (раздел 8);
  - `main` без позиционного аргумента;
  - `WorkerThread` вызывает `runServer`;
  - Boost в CMake (раздел 2);
  - секция `server` в `packaging/config/calculator.json`.
- **Файлы:** `Server.h`/`.cpp`, `Config.h`/`.cpp`, `SignalThread.h`/`.cpp`, `main.cpp`, `WorkerThread.h`/`.cpp`, `CMakeLists.txt`, `packaging/config/calculator.json`.
- **Тесты:** `Config` для секции `server` (раздел 11).
- **Готово, когда:** тесты зелёные, ручные проверки через `nc` из раздела 15.1 проходят. Автоматические сетевые тесты появятся на шаге 4, поэтому здесь проверка ручная.

### Шаг 4. Сетевые тесты
- **Что:** `TestClient`, `TestServer`, `waitUntil`, сетевые тесты из таблицы раздела 11 и интеграционный тест с настоящей БД.
- **Файлы:** `tests/TestClient.h`/`.cpp`, `tests/TestServer.h`, `tests/network_tests.cpp`, `CMakeLists.txt` (тестовые исходники, `nlohmann_json` для тестов).
- **Готово, когда:**
  - тесты зелёные в обеих сборках, в TSan-сборке нет предупреждений;
  - `memcheck` проходит;
  - интеграционный тест прошёл на ВМ с PostgreSQL или показан как `Skipped` без него.

### Шаг 5. Защита входа (раздел 13)
- **Что:** проверка диапазона `int` в `Parser`; в `Calculator` — быстрый ответ для `!` при `n > 12` и для `^` с основаниями `0`, `1`, `-1`.
- **Файлы:** `Parser.cpp`, `Calculator.cpp`.
- **Тесты:** `ParserTest.ThrowsOnIntOverflow`, `CalculatorTest.LargeFactorialOverflowsFast`, `CalculatorTest.PowerOfOneWithHugeExponentIsFast`, сетевой `HeavyInputsAnsweredOverNetwork`.
- **Готово, когда:** тесты зелёные, полная проверка раздела 15 пройдена (systemd, TSan и Valgrind на реальном процессе). На этом задача 3 закрыта. Финальный TSan-прогон повторяется после задачи 4 (раздел 12).

## 15. Проверка готовности

### 0. Сборка и тесты
```bash
sudo apt install libboost-dev netcat-openbsd
cd ~/calculator && git pull
cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
cmake -S . -B build-tsan -DENABLE_TSAN=ON && cmake --build build-tsan -j && ctest --test-dir build-tsan --output-on-failure
```

### 1. Ручной запуск из сборки
Установленный сервис уже слушает `7777`, поэтому для ручного запуска берём отдельный порт:
```bash
echo '{"server":{"port":7778}}' > /tmp/calc-dev.json
./build/calculator --config /tmp/calc-dev.json & PID=$!
```

| Сценарий | Команда | Ожидание |
|---|---|---|
| Результат | `printf '{"op":"+","a":2,"b":3}\n' \| nc -N 127.0.0.1 7778` | `{"result":5,"status":0}` |
| Ошибка вычисления | `printf '{"op":"/","a":5,"b":0}\n' \| nc -N 127.0.0.1 7778` | `{"error":"Division by zero","result":0,"status":2}` |
| Некорректный запрос | `printf 'hello\n' \| nc -N 127.0.0.1 7778` | `"status":-1` |
| Несколько запросов | `nc 127.0.0.1 7778`, ввести несколько строк | Ответ на каждую строку |
| Один клиент за раз | Два терминала с `nc`: во втором ввести запрос, затем закрыть первый | Второй получает ответ только после закрытия первого |
| `SIGPIPE` игнорируется | `grep SigIgn /proc/$PID/status` | В маске установлен бит `0x1000` (сигнал 13) |
| Остановка с подключённым клиентом | Открыть `nc 127.0.0.1 7778` и не закрывать, затем `kill -TERM $PID` | Процесс сразу завершается с кодом `0`, `nc` видит закрытие соединения |
| Быстрый перезапуск | Сразу после остановки запустить снова | Нет `Address already in use` |

### 2. Под systemd
```bash
sudo cmake --install build && sudo systemctl daemon-reload && sudo systemctl restart calculator
sudo ss -ltnp | grep 7777                                  # слушает процесс calculator
printf '{"op":"*","a":6,"b":7}\n' | nc -N 127.0.0.1 7777   # {"result":42,"status":0}
journalctl -u calculator -n 20 --no-pager                  # Listening on 127.0.0.1:7777, Client connected, запрос, Client disconnected
# в другом терминале: nc 127.0.0.1 7777 (не закрывать)
sudo systemctl stop calculator                             # останавливается сразу, без "stop-sigterm timed out"
```

### 3. TSan и Valgrind на реальном процессе
- **TSan:** `./build-tsan/calculator --config /tmp/calc-dev.json`, несколько запросов через `nc`, затем `kill -TERM`. Нет `WARNING: ThreadSanitizer`.
- **Valgrind:** `valgrind --leak-check=full ./build/calculator --config /tmp/calc-dev.json`, запросы, `kill -TERM`. `definitely lost: 0`, `ERROR SUMMARY: 0 errors`.
