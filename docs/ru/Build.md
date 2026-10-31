# Сборка

## Ядро

```sh
xmake f -y                                   # тесты, бенчи и примеры собираются по умолчанию
xmake f -y --lattice_build_extras=n          # только ядро и библиотеки
xmake build -y
```

Всегда передавай `-y`: без него xmake может молча ждать ответа на вопрос о конфигурации.

## Тесты

- Тесты инвариантов ядра — бинарник `Lattice.tests` (`TEST`, библиотека `lattice-test`).
- Тесты библиотек — в `libs/<имя>/tests`, бинарники `lattice-test.tests`, `ltest.tests`, `lattice-logger.tests`,
  `lbm-runner.tests`, `lbm-history.tests`:

```sh
xmake build -y lbm-runner.tests && xmake run lbm-runner.tests
```

`Lattice.tests` использует абсолютный путь тестовых пакетов, заданный при сборке.

```sh
xmake build -y Lattice.tests && (cd .. && Lattice/build/linux/x86_64/release/Lattice.tests)
```

## Бенчи

`Lattice.bench` — горячие пути ядра (BmRunner). С историей результатов:

```sh
lbm run Lattice Exports --save -m "что изменилось"
lbm compare
```

## Примеры

```sh
xmake build -y Lattice.example.composition && xmake run Lattice.example.composition
xmake build -y Lattice.example.slot && xmake run Lattice.example.slot
```

Что показывает каждый — [examples/README.md](../../examples/README.md).

## lbm

```sh
xmake install-lbm          # → ~/.local/opt/lattice-benchmarks, ссылка в ~/.local/bin
```

Команды и формат результатов — [BmHistory](articles/BmHistory.md).

## Потребителям

Ядро и библиотеки подключаются как xmake-пакеты из локального репозитория:

```lua
add_repositories("lattice-local /path/to/Lattice")
add_requires("lattice", {configs = {shared = true}})
add_requires("lattice-logger", "lbm-runner")
```

Пакеты ставятся один раз и сами не пересобираются. После правки исходников ядра потребитель
переставляет пакет и пересобирает свои цели заново — иначе объекты сохранят старую раскладку
классов из заголовков ядра:

```sh
xmake require -f -y lattice          # или lbm-runner, lattice-logger, …
xmake build -y -r <цели>
```

Тесты запускаются отдельными бинарниками; прежний runtime-режим `--tests` удалён.

## ltest

```sh
xmake build -y ltest
xmake install-ltest
ltest list
ltest run
ltest run Lattice 'Package_*' --json
```

[Тесты и ltest](articles/Testing.md): общий main, фильтры, отдельные логи и машинный отчёт.
Ядро тестируется из каталога проекта; пути тестовых пакетов заданы при сборке.

## Версии и CI

Версия ядра — одна на репозиторий: `set_version` в корневом `xmake.lua` и та же у каждой
библиотеки (`soname`). Выпуск — тег `v<версия>` (первый — `v1.0.0`); потребители берут пакет по
тегу.

- `.github/workflows/ci.yml` — на каждый push и PR: Linux (gcc 14), Windows (clang-cl), macOS
  (clang, пока не проверен — его результат не роняет прогон); сборка и `ltest run`. На раннерах нет
  GPU: GPU-проверки пропускаются сами.
- `.github/workflows/release.yml` — на тег `v*`: версии всех `set_version` совпадают с тегом,
  затем CI, затем GitHub Release с описанием из коммитов.
- Новая версия — `set_version` везде, коммит, тег: `git tag v1.1.0 && git push --tags`.
- Смена версии меняет `soname`: в существующем каталоге сборки — `xmake build -y -r`, иначе
  бинарники останутся слинкованы со старой библиотекой.
