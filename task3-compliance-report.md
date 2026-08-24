# Проверка соответствия требованиям task3.md

Дата проверки: 2026-08-24
Ветка: `feat/postgres-calculator`, коммит `f70384c` ("feat: postgres storage with warm cache via libpq wrapper")
Метод проверки: статический разбор кода (сборка/запуск/видео не проверялись — см. раздел "Не проверено").

## Итог

Функциональные и архитектурные требования (хранение в PostgreSQL, кеш, прогрев,
RAII, запрет сырых указателей, запрет многопоточности, своя обёртка над libpq)
**выполнены в коде**. Не выполнены/не проверяемы организационные пункты
(PR с ментором-ревьюером, видео с psql/Valgrind/Perf) — они по своей природе
не являются частью кодовой базы.

**Обновление:** обнаружено и исправлено расхождение `main.cpp` с концептом —
отсутствовал цикл ожидания системных сигналов после `run()`. Исправлено, см.
раздел "Исправленные несоответствия" ниже.

## Требования и статус

| # | Требование | Статус | Подтверждение |
|---|---|---|---|
| 1 | БД хранит числа, операцию, результат, статус | ✅ Выполнено | Таблица `calculation_history(first_value, second_value, operation, result, status)` — [src/DataBase.cpp:109-121](src/DataBase.cpp#L109-L121) |
| 2 | Статус: 0 — успех, иначе код ошибки | ✅ Выполнено | `task.status = status` из `mathlib` (MATH_OK=0, MATH_OVERFLOW/MATH_DIV0/MATH_DOMAIN — ненулевые) — [src/Calculator.cpp:13-27](src/Calculator.cpp#L13-L27) |
| 3 | PostgreSQL как БД | ✅ Выполнено | `find_package(PostgreSQL REQUIRED)` — [CMakeLists.txt:10](CMakeLists.txt#L10), подключение через `libpq-fe.h` |
| 4 | Своя минимальная обёртка над libpq, libpqxx запрещена | ✅ Выполнено | [include/calculator/Postgres.h](include/calculator/Postgres.h) / [src/Postgres.cpp](src/Postgres.cpp) — только `libpq-fe.h`, никаких упоминаний libpqxx в проекте |
| 5 | Кеш на `std::unordered_map`, ключ вида "1+2" | ✅ Выполнено | `DataBase::cache_` — [include/calculator/DataBase.h:45](include/calculator/DataBase.h#L45), `makeTaskKey` — [include/calculator/Task.h:23-31](include/calculator/Task.h#L23-L31) |
| 6 | "1+2" и "2+1" — одна и та же операция (коммутативность) | ✅ Выполнено | `normalizeTask` сортирует операнды для `+`/`*` — [include/calculator/Task.h:16-21](include/calculator/Task.h#L16-L21); покрыто тестами `TaskKeyTest.AdditionIsCommutative`, `MultiplicationIsCommutative`, `SubtractionIsNotCommutative` — [tests/tests.cpp:126-136](tests/tests.cpp#L126-L136) |
| 7 | Прогрев кеша из БД при старте | ✅ Выполнено | `DataBase::warmUpCache()` читает всю историю в `cache_` — [src/DataBase.cpp:43-72](src/DataBase.cpp#L43-L72); вызывается в конструкторе `Runner` — [src/Runner.cpp:13-16](src/Runner.cpp#L13-L16) |
| 8 | При запросе — сперва кеш, при промахе — расчёт + запись в кеш и БД | ✅ Выполнено | `Runner::run()` — [src/Runner.cpp:28-33](src/Runner.cpp#L28-L33); `DataBase::writeRecord()` пишет и в БД, и в `cache_` — [src/DataBase.cpp:86-107](src/DataBase.cpp#L86-L107) |
| 9 | Классы обёртки соблюдают RAII | ✅ Выполнено | `PgConnection`/`PgResult` хранят ресурс в `std::unique_ptr` с кастомным делитером (`PQfinish`/`PQclear`), без explicit `disconnect()`/`close()` в деструкторе не требуется — освобождение автоматическое — [include/calculator/Postgres.h](include/calculator/Postgres.h), [src/Postgres.cpp](src/Postgres.cpp) |
| 10 | Запрет сырых указателей (только умные) | ✅ Выполнено | В `src/` и `include/` нет `new`/`delete`, единственные `T*` — параметр конструктора `PgResult(PGresult* result = nullptr)`, немедленно оборачиваемый в `unique_ptr` (владения снаружи нет) — [include/calculator/Postgres.h:13](include/calculator/Postgres.h#L13) |
| 11 | Запрет многопоточности | ✅ Выполнено | Нет `<thread>`, `std::async`, `std::mutex` и т.п. во всём проекте |
| 12 | Отдельная ветка для ДЗ | ✅ Выполнено | Текущая ветка `feat/postgres-calculator`, отдельная от `main` |
| 13 | PR с ментором в ревьюерах | ⚠️ Не проверено | GitHub CLI (`gh`) недоступен в этой среде; требует ручной проверки на github.com/Vavasiq/calculator |
| 14 | Видео: заполнение БД через psql | ⚠️ Не проверено | `psql` не установлен в этой среде — организационное действие, не часть кода |
| 15 | Видео: отсутствие утечек через Valgrind | ⚠️ Не проверено (сборка), но задел в коде есть | `valgrind` недоступен здесь; в `CMakeLists.txt` уже настроен CTest-таргет `memcheck`, запускающий тесты под Valgrind — [CMakeLists.txt:84-89](CMakeLists.txt#L84-L89) |
| 16 | (Со звёздочкой) Проверка Valgrind в тестах | ✅ Выполнено (в конфигурации сборки) | Тот же `memcheck`-таргет в CTest — фактически "встроенная в тесты" проверка Valgrind, срабатывает автоматически если `valgrind` найден в системе |
| 17 | Видео: анализ производительности через Perf | ⚠️ Не проверено | `perf` недоступен здесь — организационное действие |

## Не проверено в этой среде

Окружение, в котором выполнялась проверка, не содержит `cmake`, `psql`, `valgrind`,
`perf`, `gh` — поэтому не удалось:
- собрать проект и прогнать `calculator_tests` / `ctest`;
- реально подключиться к PostgreSQL и увидеть заполнение таблицы `calculation_history`;
- прогнать Valgrind и снять профиль Perf;
- проверить состояние PR и список ревьюеров на GitHub.

Эти пункты не являются дефектами кода — они требуют локального окружения
разработчика (где, судя по `CMakeLists.txt`, всё уже подготовлено: FetchContent
подтягивает зависимости, `find_package(PostgreSQL REQUIRED)` есть, `memcheck`
таргет настроен).

## Исправленные несоответствия

- **Отсутствовал цикл ожидания системных сигналов в `main.cpp`.** Концепт из
  задания заканчивает `main()` комментарием
  `// Вот тут запускаем цикл ожидания системных сигналов` — в коде до правки
  такого цикла не было вообще: процесс завершался сразу после `runner.run(...)`.
  Добавлено в [src/main.cpp](src/main.cpp): после `run()` устанавливаются
  обработчики `SIGINT`/`SIGTERM` (`sigaction`) и выполняется цикл `while (!stop) pause();`,
  который блокирует процесс до получения сигнала. Это без потоков (используется
  только POSIX `sigaction`/`pause`, что не нарушает запрет многопоточности) и
  гарантирует, что `calculator::Runner` (а значит и `DataBase`/`PgConnection`)
  разрушается через RAII именно по сигналу — что нужно для демонстрации
  корректного освобождения ресурсов под Valgrind при прерывании процесса.
  - ⚠️ Побочный эффект: теперь каждый запуск `calculator '<json>'` не завершается
    сам — процесс "висит" до `Ctrl+C`/`kill`. Это осознанный выбор (подтверждён
    пользователем) в пользу демонстрации graceful shutdown, а не прежнего
    поведения "посчитал и вышел". Учтите это при записи демо-видео.
  - Код использует POSIX API (`<unistd.h>`, `sigaction`) — требует Linux/POSIX
    окружения для сборки, как и остальной проект (Valgrind/Perf/psql тоже
    Linux-инструменты), на чистом MSVC/Windows без POSIX-слоя не соберётся.

## Замечания (не блокирующие, на усмотрение автора)

- Из проверки кода критических дефектов не найдено: нормализация ключа кеша
  консистентна с UNIQUE-constraint в БД (`UNIQUE(first_value, second_value, operation)`
  — [src/DataBase.cpp:118](src/DataBase.cpp#L118)), upsert через `ON CONFLICT ... DO UPDATE`
  корректно синхронизирует БД и кеш.
- В корне репозитория лежит незакоммиченный `task3.md` (текст задания) — стоит
  решить, коммитить его в репозиторий или добавить в `.gitignore`, прежде чем
  открывать PR.
