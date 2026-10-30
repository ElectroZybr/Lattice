# Архитектура ядра

## Идея

Lattice организует взаимодействие объектов, но не определяет, как они написаны. Реализация —
обычный C++ код; Lattice отвечает за композицию, владение, зависимости, контексты и жизненный
цикл. Интеграция сторонней библиотеки не требует переписывать её классы под Lattice.

```text
Реализация     обычные классы, структуры, функции, библиотеки
    ↓ используются
Композиция     compose (и configure) пакета: собирает ветку дерева из этих объектов
    ↓ исполняется
Lattice        дерево, владение, blueprints, Context, lifecycle, загрузка пакетов, экспорты
```

Как писать композицию — [Composition.md](Composition.md).

## Основные решения

- **Дерево — модель владения.** Объект принадлежит одному узлу; ветка удаляется целиком.
  Зависимость не передаёт владение; удаление поставщика завершает его транзитивных
  потребителей, в том числе в других ветках.
- **Зависимости — аргументы конструктора.** Handle в аргументах `emplace` становится
  зависимостью; конструктор получает обычную ссылку. Runtime хранит связи по `NodeId` и
  поколениям handles.
- **Выбор — явный.** Context: 0 кандидатов — нет, 1 — разрешается сам, N — ошибка со списком,
  пока не выбран явно (`focus`). Слот выбирает и собирает реализацию. Смена выбора не
  переписывает уже внедрённые ссылки и ничего не пересобирает сама.
- **Изменение дерева — явными шагами:** изменить Context → retire старой ветки → дождаться
  удаления в `maintain` (без блокировки) → собрать заново. Автоматической пересборки и
  транзакционной замены нет.
- **Builders временные.** `NodeBuild`, `ObjectBuild`, `MountBuild` — move-only и действуют
  только во время композиции; во время работы — `Ref` и `Focus`.
- **Хранение и поиск разделены:** `NodeRegistry` хранит дерево, `NodeQuery` ищет.
- **Контракты без runtime.** Заголовочные типы — `Value`, `Exception`, `TableAPI`,
  `PipelineAPI` — реализации подключают, не линкуя runtime.

## Карта кода

| Задача | Файлы |
| --- | --- |
| Сборка ветки и локальный выбор | `kernel/NodeViews.hpp`: `NodeBuild`, `NodeConfigure` |
| Экспорты и lifecycle объекта | `kernel/NodeHandlers.hpp`: `ObjectBuild`; определения в `NodeViews.hpp` |
| Handles в обычных конструкторах | `kernel/CompositionArguments.hpp` |
| Регистрация типов и API | `kernel/ObjectBlueprint.hpp`, `kernel/Blueprints.*` |
| Контекстные роли, кандидаты, наследование | `kernel/NodeContext.*` |
| Выбор во время работы | `Focus<T>` в `NodeHandlers.hpp`, определения в `NodeViews.hpp` |
| Внешнее создание и удаление веток | `kernel/Builder.*` |
| Зависимости потребителей от поставщиков | `kernel/DependencyGraph.*` |
| Остановка, retire, уничтожение, `maintain` | `kernel/NodeOps.*` |
| Экспорты: параметры, действия, исполнители | `kernel/Exports.hpp`; вид для инструментов — `ExportsView` в `NodeViews.hpp` |
| Таблицы | `lattice-tools/TableAPI.hpp` (lattice-tools), `kernel/TableExports.hpp` |
| Конвейеры | `kernel/PipelineAPI.hpp`, `kernel/PipelineExports.hpp` |
| Сервисы | `kernel/ServiceAPI.*` |
| Загрузка пакетов | `kernel/PackageManager.*`, `kernel/Package.hpp` |
| Запуск runtime и `lattice.toml` | `kernel/Runtime.hpp`, `kernel/StartupConfig.hpp` |

## Библиотеки

`libs/` — переиспользуемые библиотеки без runtime: lattice-tools (header-only утилиты и
`Value`), lattice-test (проверки, результаты и стандартный main), ltest-app (программа `ltest`, запуск наборов), lattice-logger, lattice-sysinfo, lbm-runner, lbm-history и lbm-app (программа `lbm`). Каждая ставится своим xmake-пакетом
— таблица в [libs/README.md](../../../libs/README.md).

## Примеры

- [Композиция](../../../examples/composition/README.md): две симуляции, граф зависимостей,
  общие ресурсы через Context/mount, локальный Focus, пересборка.
- [Слот](../../../examples/slot/README.md): две реализации renderer, завершение потребителей и
  явная сборка замены.
