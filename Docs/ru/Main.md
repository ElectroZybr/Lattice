# Lattice

Lattice собирает обычные C++ объекты в дерево владения. Композиция описывает
конкретные экземпляры, зависимости и контекстные решения; реализации не обязаны
наследоваться от framework-классов или хранить runtime handles.

## Структура

- `Kernel/` — дерево, Blueprints, Context, создание, retirement и экспорты.
- `Tools/` — логирование, форматирование, тестовая инфраструктура.
- `examples/slot/` — две композиции Renderer и замена после lifecycle.
- `examples/composition/` — shared library с одним `extern "C" compose` и приложение.
- `tests/` — проверки инвариантов ядра и тестовые динамические пакеты.
- `packages/l/lattice/` — описание SDK-пакета для xmake.

## Где искать код

| Задача | Файлы |
| --- | --- |
| Описать сборку и локальный выбор | `Kernel/NodeViews.hpp`: `NodeBuild` |
| Экспорты и внешний lifecycle объекта | `Kernel/NodeHandlers.hpp`: `ObjectBuild`; определения в `NodeViews.hpp` |
| Внедрение handles в обычные конструкторы | `Kernel/CompositionArguments.hpp` |
| Автоматическая регистрация типов и API | `Kernel/ObjectBlueprint.hpp` |
| Контекстные роли, кандидаты и наследование | `Kernel/NodeContext.hpp`, `NodeContext.cpp` |
| Runtime handle текущего выбора | `Focus<T>` в `NodeHandlers.hpp`, определения в `NodeViews.hpp` |
| Внешнее создание и удаление веток | `Kernel/Builder.hpp`, `Builder.cpp` |
| Зависимости потребителей от поставщиков | `Kernel/DependencyGraph.hpp`, `DependencyGraph.cpp` |
| Остановка и фактическое уничтожение | `Kernel/NodeOps.hpp`, `NodeOps.cpp` |
| Загрузка единственной точки входа `compose` | `Kernel/PackageManager.cpp` |

## Основные правила

Объект принадлежит одному физическому узлу дерева. Зависимость не передаёт владение.
Конструкторы получают обычные ссылки и коллекции; runtime сохраняет связи по NodeId
и поколениям handles. Query/storage разделены: `NodeRegistry` хранит дерево,
`NodeQuery` выполняет семантический поиск.

`NodeBuild`, `ObjectBuild` и `MountBuild` — временные move-only capabilities.
`Ref` — runtime ссылка; `Focus` — изменяемый контекстный выбор. Смена выбора не
перенастраивает ссылки уже созданных объектов.

См. [композицию, Context и пересборку](Kernel/Composition.md) и
[исполняемый пример](../../examples/composition/README.md).

[Slot и композиции реализаций](../../examples/slot/README.md): контракт выбора,
retirement и явное пересоздание потребителей.
