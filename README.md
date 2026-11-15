# calculator

Linux-сервис (systemd) — калькулятор целых чисел. Принимает задания по TCP (Boost.Asio) строками JSON и отвечает строками JSON. Результаты хранятся в PostgreSQL и кешируются в памяти.

Планы изменений ДЗ-4: [задача 1](docs/task4-1-signals.md) (сигналы, потоки), [задача 2](docs/task4-2-systemd.md) (systemd), [задача 3](docs/task4-3-network.md) (сеть, тесты), [задача 4](docs/task4-4-deb.md) (deb-пакет). Текст задания: [task4.md](task4.md).

## Зависимости (Ubuntu 24.04)

```bash
sudo apt install build-essential cmake git libpq-dev libboost-dev postgresql
sudo apt install netcat-openbsd valgrind dpkg-dev lintian   # проверки и сборка пакета
```

Остальные зависимости (mathlib, nlohmann_json, spdlog, googletest) CMake скачивает сам через FetchContent.

## База данных и роль PostgreSQL

Сервис подключается с параметрами из секции `database` конфига. По умолчанию это `localhost:5432`, база `calculator`, пользователь и пароль `calculator`. Таблицу `calculation_history` сервис создаёт сам. Базу и роль нужно создать один раз:

```bash
sudo -u postgres psql -c "CREATE USER calculator WITH PASSWORD 'calculator';"
sudo -u postgres psql -c "CREATE DATABASE calculator OWNER calculator;"
psql "host=localhost dbname=calculator user=calculator password=calculator" -c "SELECT 1;"   # проверка
```

## Сборка и тесты

```bash
cmake -S . -B build && cmake --build build -j
ctest --test-dir build --output-on-failure      # unit- и сетевые тесты, интеграционный тест с БД, memcheck (Valgrind)
```

Сборка под ThreadSanitizer, в отдельной директории:

```bash
sudo sysctl vm.mmap_rnd_bits=28                 # Ubuntu 24.04: иначе TSan падает с "unexpected memory mapping"
cmake -S . -B build-tsan -DENABLE_TSAN=ON && cmake --build build-tsan -j
ctest --test-dir build-tsan --output-on-failure
```

Если PostgreSQL недоступен, интеграционный тест пропускается (`Skipped`). Другую базу для него можно задать через `CALCULATOR_TEST_CONFIG=<путь к конфигу>`.

## Конфигурация

`/etc/calculator/calculator.json` (пример: [packaging/config/calculator.json](packaging/config/calculator.json)):

```json
{
    "database": { "host": "localhost", "port": 5432, "name": "calculator", "user": "calculator", "password": "calculator" },
    "server":   { "address": "127.0.0.1", "port": 7777 }
}
```

Отсутствующие ключи берут эти значения по умолчанию. Путь к конфигу задаётся флагом `--config`. Ручной запуск из сборки:

```bash
./build/calculator --config packaging/config/calculator.json   # остановка: Ctrl+C или SIGTERM
```

Если установленный сервис уже занимает `7777`, для ручного запуска используйте свой конфиг с другим портом, например `{"server":{"port":7778}}`.

## deb-пакет

Пакет собирается с префиксом `/usr`, в отдельной директории и без `sudo`:

```bash
cmake -S . -B build-deb -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release
cmake --build build-deb -j && (cd build-deb && cpack)          # build-deb/calculator_1.0.0_amd64.deb
```

В пакете бинарник `/usr/bin/calculator`, unit-файл `/usr/lib/systemd/system/calculator.service` и конфиг `/etc/calculator/calculator.json` (conffile: правки сохраняются при обновлении).

Установка:

```bash
sudo apt install ./build-deb/calculator_1.0.0_amd64.deb   # postinst создаёт пользователя calculator
# создать базу и роль PostgreSQL (раздел выше), затем:
sudo systemctl enable --now calculator
```

Пакет не включает и не запускает сервис сам: без базы сервис ушёл бы в цикл перезапусков.

Управление и журнал:

```bash
sudo systemctl start calculator
sudo systemctl restart calculator
sudo systemctl stop calculator
journalctl -u calculator -f
```

Удаление: `sudo apt remove calculator` оставляет конфиг, `sudo apt purge calculator` удаляет и его.

## Проверка через nc

Сервис начинает слушать порт после подключения к БД, поэтому сначала ждём порт:

```bash
timeout 30 sh -c 'until nc -z 127.0.0.1 7777; do sleep 0.2; done'
printf '{"op":"+","a":2,"b":3}\n' | nc -N 127.0.0.1 7777        # {"result":5,"status":0}
printf '{"op":"/","a":5,"b":0}\n' | nc -N 127.0.0.1 7777        # {"error":"Division by zero","result":0,"status":2}
printf 'hello\n' | nc -N 127.0.0.1 7777                         # {"error":"JSON parse error: ...","result":0,"status":-1}
```

## Протокол

| Что | Значение |
|---|---|
| Запрос | Одна строка JSON с `\n`: `{"op":"+","a":2,"b":3}`, операции `+ - * / ^ !` (для `!` поле `b` не нужно), `a` и `b` — целые в диапазоне `int` |
| Ответ | Одна строка JSON: `result`, `status` и `error` (только при ошибке) |
| `status` | `0` — успех; `1` — переполнение, `2` — деление на ноль, `3` — вне области определения (коды `mathlib`); `-1` — некорректный запрос; `-2` — внутренняя ошибка (например, сбой БД) |
| Соединение | В одном соединении можно отправить сколько угодно запросов. Строка длиннее 4096 байт (вместе с `\n`) даёт ответ `-1` и закрытие соединения |

## Ограничения

- **Один клиент за раз.** Следующий клиент обслуживается после отключения текущего. Таймаута простоя нет: клиент, который подключился и молчит, держит сервис, пока не отключится.
- **Только локальные подключения по умолчанию.** Адрес `127.0.0.1`, аутентификации и TLS нет. Слушать все интерфейсы: `"server": {"address": "0.0.0.0"}`.
