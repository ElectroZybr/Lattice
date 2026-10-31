# Lattice

Небольшой C++ runtime, который собирает обычные C++ объекты в дерево владения. Композиция
описывает, какие объекты есть, кто кем владеет, кто от кого зависит и что выбрано в каком
месте дерева; сами объекты остаются обычными классами — без наследования от framework-классов
и без runtime handles внутри.

Lattice отвечает за дерево и владение, контекстный выбор (Context, Focus, слоты), lifecycle
(создание, retire, пересборка), загрузку пакетов, сервисы и потоки, экспорты (параметры и
действия) и конвейеры. Предметного поведения в ядре нет: оно — в пакетах потребителей
(например, [LatticeLab](../../../LatticeLab/docs/ru/README.md)).

## С чего начать

| Хочу | Читать |
| --- | --- |
| Понять идею и устройство ядра | [Архитектура](articles/Architecture.md) |
| Писать пакеты: композиция, Context, слоты, конвейеры, сервисы, экспорты | [Композиция](articles/Composition.md), [примеры](../../examples/README.md) |
| Собрать ядро, тесты, примеры, `lbm` | [Сборка](Build.md) |
| Менять ядро | [Правила](Rules.md), затем [антипаттерны](Antipatterns.md) |
| Выпустить версию, понять номер | [Версии](Versioning.md), [изменения](../../CHANGELOG.md) |
| Разобраться в словах | [Термины](articles/Glossary.md) |
| Найти и запустить тесты, получить компактный отчёт | [Тесты и ltest](articles/Testing.md) |
| Мерить производительность | [BmRunner](articles/BmRunner.md), [история бенчмарков](articles/BmHistory.md) |
| Узнать, что делается | [Планы](plans/README.md) |

## Документация

```text
docs/ru/
├── README.md          этот файл: о Lattice и карта документации
├── Rules.md           правила разработки ядра
├── Versioning.md      номер версии, изменения между релизами, релиз
├── Antipatterns.md    чего не делать — в ядре и в пакетах
├── Build.md           сборка, тесты, примеры, пакеты xmake, lbm
├── articles/
│   ├── Architecture.md    устройство ядра и карта кода
│   ├── Composition.md     API композиции: Context, Focus, mount, configure, слоты, конвейеры,
│   │                      сервисы, экспорты, пересборка
│   ├── Glossary.md        термины
│   ├── BmRunner.md        бенчмарки: фазы, исполнители, реализации
│   └── BmHistory.md       история бенчмарков, lbm
└── plans/             планы: список крупных блоков и план каждого
```

README библиотек и примеров лежат рядом с кодом: [Libs](../../libs/README.md),
[examples](../../examples/README.md).

## Репозиторий

```text
kernel/            дерево, Blueprints, Context, создание и retire, экспорты, пакеты, конвейеры
tools/             форматирование ядра
libs/              библиотеки без runtime: lattice-tools, lattice-test, ltest, lattice-logger, lattice-sysinfo, lbm-runner, lbm-history, lbm
examples/          исполняемые примеры композиции и слотов
tests/             проверки инвариантов ядра и тестовые пакеты (тесты библиотек — в их папках)
packages/l/        рецепты xmake-пакетов (lattice, lattice-tools, lattice-logger, …)
Lattice.hpp, Composition.hpp   публичные входные заголовки
```
