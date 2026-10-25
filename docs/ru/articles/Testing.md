# Тесты и ltest

Три части, каждая со своей ответственностью:

| Часть | Что делает | Зависимости |
| --- | --- | --- |
| [lattice-test](../../../libs/lattice-test/README.md) | `TEST`, `REQUIRE`, `CHECK`, `SKIP`; реестр, запуск в процессе, результаты, формат отчёта | lattice-tools |
| `lattice-test-main` (там же) | стандартный `main` тестового бинарника: фильтр, список, вывод, отчёт для ltest | lattice-test, lattice-logger |
| [ltest](../../../libs/ltest/README.md) | приложение: поиск наборов, отдельный процесс на набор, таймаут, общая сводка | lattice-test, lattice-logger |

Набор — это только сценарии: `main` в тестовых файлах не пишется. Цель зависит от
`lattice-test-main` (в ядре) или подключает пакет `lattice-test` (в проектах) — статический
`main` попадает в бинарник сам, а набор со своим `main` его не берёт.

```lua
target("Example.tests")
    set_kind("binary")
    add_files("tests/*.cpp")
    add_packages("lattice-test")
```

## Исполнение и результаты

`TEST(Name, Fixture, "описание")` регистрирует сценарий. `Test::list(mask)` перечисляет его
без создания фикстур, `Test::run(mask, callbacks)` создаёт фикстуру на сценарий и возвращает
`Testing::RunResult` — обычные данные: статус (`Listed`, `Passed`, `Failed`, `Skipped`),
длительность, диагностика (вид проверки, выражение, файл, строка, сообщение). Callbacks
`start` и `result` — на один прогон. `Test` ничего не пишет в консоль.

`REQUIRE` прерывает сценарий, `CHECK` продолжает. `SKIP(reason)` пропускает явно; ошибка до
него остаётся ошибкой. Исключение из тела или конструктора фикстуры — неудача сценария.

`Report.hpp` — хранение результата: версионированный TSV `lattice-test-v1` (протокол между
набором и ltest; парсер отклоняет оборванный и противоречивый отчёт) и JSON для внешних
потребителей.

## Набор напрямую

```sh
./build/tests/Tasks.tests                # краткий вывод: проваленные, пропущенные, сводка
./build/tests/Tasks.tests 'Tasks_queue*' # фильтр по имени
./build/tests/Tasks.tests -v             # живой ход каждого теста
./build/tests/Tasks.tests --list
./build/tests/Tasks.tests --json
```

Коды выхода: 0 — успех, 1 — проваленные сценарии, 2 — ничего не выбрано или ошибка.

## ltest

```sh
ltest list                              # наборы проекта, ничего не запускается
ltest list Boundaries '*Walls*'         # сценарии, без фикстур
ltest run                               # всё, краткая сводка
ltest run Tasks -v                      # живой ход тестов
ltest run '*' --details                 # строка на каждый сценарий
ltest run Boundaries '*Walls*' --json   # схема ltest-v1
ltest run Tasks --build                 # сначала xmake build -y Tasks.tests
```

В xmake-проекте наборы — бинарные цели `*.tests` из `xmake show` (устаревшие файлы сборки
не считаются); в остальных каталогах — исполняемые `*.tests`. В Lattice цели тестов включает
`xmake f -y --lattice_build_extras=y`.

Набор запускается без shell, в каталоге проекта, своей группой процессов, с таймаутом (30 с).
Его вывод и отчёт живут во временном каталоге и удаляются после прогона — истории нет.
Падение, ошибка загрузчика, таймаут, отсутствие отчёта или код выхода не по отчёту — ошибка
набора, а не пустой прогон: в сводке «could not run» и конец лога процесса, код 2.
`--logs <dir>` сохраняет логи и отчёты. С `--json -v` JSON остаётся в stdout, ход тестов —
в stderr. Внешний запуск пока только на POSIX.

## Встраивание

```cpp
// Внутри процесса: результаты без вывода, представление — своё.
auto report = Lattice::Test::instance().run("Parser_*", {
    .result = [](const Lattice::Testing::TestResult& result) { /* обновить UI */ },
});
```

Для изоляции процессом приложение запускает `ltest run ... --json` и читает `ltest-v1`.
В LatticeLab `./LatticeLab -t [аргументы ltest]` до создания runtime передаёт аргументы
`ltest` из пакета.
