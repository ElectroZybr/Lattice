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
- Тесты библиотек — в `libs/<имя>/tests`, бинарники `lattice-test.tests`, `lattice-logger.tests`,
  `lbm-runner.tests`, `lbm-history.tests`:

```sh
xmake build -y lbm-runner.tests && xmake run lbm-runner.tests
```

`Lattice.tests` запускается из каталога над ядром: тесты загрузки пакетов ищут
`Lattice/tests/TestPackages/...` относительно него.

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
