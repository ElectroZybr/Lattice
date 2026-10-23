# Композиция, зависимости и общие ресурсы

Пример содержит две симуляции, общие ресурсы и агрегатор результатов.
Реализация в `model.hpp` не включает Lattice. Единственная точка входа пакета
находится в `compose.cpp`:

```cpp
extern "C" void compose(NodeBuild root);
```

## Сборка и запуск

Из корня репозитория с xmake и C++23 компилятором:

```sh
xmake f --lattice_build_extras=y
xmake build Lattice.example.composition
xmake run Lattice.example.composition
```

Ожидаемые строки среди сообщений runtime:

```text
Devices: slow=cpu, fast=gpu; shared field and metrics
Slow position: 2.62500
Fast position: 1.15625
Focus changed to cpu; existing fast still uses gpu
Dashboard snapshot retired with slow branch
Fast after removing slow: 2.65625
Submissions cpu/gpu: 4/5; samples: 9
```

Пакет собирается как shared library. `main.cpp` демонстрирует инфраструктуру
загрузки, изменения параметров и удаления веток; в приложении её предоставляет
Lattice Runtime. `lattice_package` в `compose.cpp` задаёт идентификатор `Example`. Загрузчик регистрирует
`compose` как фабрику подветки и вызывает её при создании экземпляра, а не при
загрузке библиотеки. Для каждого экземпляра создаются собственные объекты.

## Структура и выбор объектов

```text
Example:demo                         структурный корень
├── ComputeDevice:cpu                shared; наследуется slow
├── ComputeDevice:gpu                shared; локальный выбор fast
├── ForceField:field                 shared
├── Metrics:metrics                  shared
├── slow
│   ├── ParticleStorage:particles
│   ├── Integrator:integrator
│   ├── Sampler:sampler
│   └── Simulation:simulation
├── fast
│   ├── ParticleStorage:particles
│   ├── Integrator:integrator
│   ├── Sampler:sampler
│   └── Simulation:simulation
└── Dashboard:dashboard              снимок обоих Sampler; Logger отсутствует
```

Типы регистрируются при первом `emplace`. Отдельные списки регистрации и классы
композиции не нужны. `branch` возвращает временный builder. Локальная функция `composeRun(NodeBuild& run, double dt)`
использует его по ссылке на время вызова:

```cpp
auto slow = root.branch("slow");
auto fast = root.branch("fast");
composeRun(slow, 0.25);
composeRun(fast, 0.125);
```

Необработанная ошибка из `compose` откатывает создаваемую ветку пакета. Отдельный
вызов `branch` не является вложенной транзакцией. Builders используются только
во время композиции.

## Одно выражение — создание и зависимости

```cpp
auto storage = run.emplace<ParticleStorage>("particles");
auto device = run.mount<ComputeDevice>();
auto field = run.mount<ForceField>();

auto integrator = run.emplace<Integrator>(
    "integrator", storage, device.ref(), field.ref(), dt);
```

`emplace` принимает handles, сохраняет зависимости нового узла и передаёт
конструктору обычные C++ ссылки. Реализация остаётся такой:

```cpp
Integrator(ParticleStorage&, ComputeDevice&, ForceField&, double dt);
```

Ручные `dependsOn` не нужны. Обычные аргументы, включая move-only значения,
передаются конструктору напрямую. Сырые указатели и разыменованные ссылки также
остаются обычными аргументами, но теряют идентичность узла: для отслеживания связей
передавайте сам handle, а не `*handle` или `handle.get()`.

Handles проверяются при внедрении: runtime, идентичность узла и доступность
объекта должны совпадать. Удаление и повторное использование NodeId не превращает
старый handle в ссылку на новый экземпляр.

## Поиск от ветки

Область поиска задаётся самим builder:

```cpp
run.emplace<NeedsDevice>("render", root.require<Device>("device"));
run.emplace<Dashboard>("dashboard", root.collect<Sampler>(), root.find<Logger>("logger"));
```

| Аргумент | Результат для обычного конструктора |
| --- | --- |
| Handle уже созданного объекта | `T&` |
| `branch.require<T>(name)` | Обязательный `T&` |
| `branch.find<T>(name)` | Необязательный `T*`, включая `nullptr` |
| `branch.children<T>()` | `std::vector<T*>` непосредственных детей |
| `branch.collect<T>()` | `std::vector<T*>` из поддерева |
| `branch.mount<T>().ref()` | `T&` выбранного shared-объекта |

`require`/`find` используют обычный семантический поиск Lattice: начиная с детей
текущей ветки и далее в родительских областях. Имя экземпляра учитывается; без
аргумента используется имя по умолчанию. `collect` следует семантике поддерева,
включая сам узел, если на нём находится подходящий объект.

Запросы читают уже созданные доступные объекты и не создают зависимости. Связи записываются у потребителя при передаче результата
в `emplace`, а не у ветки, через которую выполнялся поиск.

`find` и коллекции — снимки. Появление нового объекта не обновляет существующий
`T*` или `std::vector<T*>`. Каждая непустая ссылка из снимка становится отдельной
зависимостью потребителя. Отсутствующий optional и пустая коллекция связей не создают.
Отсутствующая обязательная зависимость и неоднозначность приводят к ошибке.

## Предоставляемые API

Обычное C++ наследование можно явно открыть runtime прямо в месте создания:

```cpp
root.emplace<MeshRenderer, Renderer>("mesh", mesh);
root.emplace<TextRenderer, Renderer>("text", font);
run.emplace<RenderSystem>("render", root.collect<Renderer>());
```

API регистрируются автоматически, а приведения генерируются из публичных
однозначных C++ баз. RTTI не используется. Без указания API регистрируется только
конкретный тип: автоматического перечисления всех C++ баз здесь нет.
`emplaceShared<Concrete, API>` публикует кандидата для конкретного типа и указанных API.

## Shared-ресурсы и граф

`emplaceShared` публикует объект как контекстного кандидата. `mount` выбирает
существующего кандидата и сохраняет зависимость ветки от поставщика. Один кандидат
разрешается автоматически; несколько без явного focus дают ошибку неоднозначности.
Конструктор, получивший `mount.ref()`, также получает собственную связь с поставщиком.

У каждой симуляции получается граф:

```text
Simulation ──→ Integrator ──→ ParticleStorage
     │              ├──────→ ComputeDevice  (shared)
     │              └──────→ ForceField     (shared)
     └───────→ Sampler ─────→ ParticleStorage
                     └─────→ Metrics        (shared)
Dashboard ──→ slow/Sampler, fast/Sampler
```

Владение определяет дерево. Зависимость не передаёт владение и не создаёт
`shared_ptr`. Порядок сборки явный: сначала поставщики, затем потребители.

Связи конструкторов живут вместе с потребителем. Удаление поставщика
через runtime retirement включает его транзитивных потребителей. Физическое
уничтожение учитывает зависимости, даже если потребитель расположен в другой
ветке владения. Замена реализации слота завершает внешних потребителей и
возвращает `false`, пока они не удалены; потребителей затем собирают заново.
Если потребитель — владелец самого слота, замена отклоняется.

## Экспорты принадлежат объекту

`emplace` возвращает временный move-only `ObjectBuild<T>`. Настройка выполняется
через него, а runtime код получает обычный `Ref<T>` через `ref()`:

```cpp
integrator.param("dt", &Integrator::dt);
simulation.action("step", &Simulation::step);
auto runtimeSimulation = simulation.ref();
```

Владельцем экспорта является узел самого объекта. Идентичность `dt` теперь
`(integrator.id(), "dt")`; параметр ссылается на реальное поле интегратора.
Методы действий могут иметь аргументы и быть `const`. При retirement объекта
его экспорты снимаются до вызова lifecycle callbacks и уничтожения.

`NodeBuild`, `ObjectBuild<T>` и `MountBuild<T>` нельзя копировать. Перемещение
отзывает исходный builder; попытки использовать перемещённый, удалённый или
retiring builder отклоняются. Builders предназначены только для локальной сборки,
их нельзя сохранять в реализациях для последующей runtime мутации. `Ref<T>`
копируется, но не предоставляет методов регистрации экспортов или lifecycle.

## Внешний lifecycle

Реализация сама не обязана знать соглашения Lattice. При сборке можно подключить
её обычные методы или внешние функции:

```cpp
auto device = root.emplaceShared<ComputeDevice>("compute");
device.onRetire(&ComputeDevice::requestStop);
device.readyToDestroy(&ComputeDevice::stopped);
```

`onRetire` вызывается один раз для экземпляра, после скрытия retirement-ветки из
поиска и снятия экспортов. В нём запрашивается остановка фоновой работы.
`readyToDestroy` принимает `const T&` и сообщает, завершилась ли она. Пока ответ
отрицательный, runtime сохраняет весь retirement-набор, включая зависимости.
После готовности вызываются обычные деструкторы, которые освобождают ресурсы.

Можно передать лямбды `[](T& object) { ... }` и `[](const T& object) { return ...; }`.
Callbacks принадлежат конкретному экземпляру, а не всем объектам его blueprint.
Их очистка и повторное использование NodeId не переносят lifecycle на новый объект.
Без явно заданных hooks обычному классу дополнительные методы не вызываются.

Обычный путь `retireBranch` / `collectRetired` не блокирует ожидание готовности.
Прямое инфраструктурное уничтожение, в том числе откат неудачной композиции,
также вызывает остановку и синхронно ждёт явно заданный readiness predicate.
Для такого cleanup предикат должен получать прогресс независимо от нового
вызова композиции. В синхронном примере devices не требуют hooks; приложение закрывает ветку
через обычный retirement-путь.

## Что проверяет пример

Приложение проверяет локальный выбор CPU/GPU, общность поля сил и метрик,
независимость хранилищ, результаты интегрирования и изменение только `fast/dt`. После удаления
`slow` завершается также `Dashboard`, поскольку его снимок содержит `slow/Sampler`.
Ветка `fast` продолжает работу; CPU сохраняет четыре отправки, GPU — пять, общие метрики — девять измерений. Снимок не перестраивается автоматически.

`extern "C"` фиксирует имя символа. `NodeBuild` остаётся C++ API; пакет и runtime
собираются совместимыми toolchain и SDK.

## Focus и явная пересборка

```cpp
root.focus<ComputeDevice>(cpu.ref());
auto fast = root.branch("fast");
fast.focus<ComputeDevice>(gpu.ref());
composeRun(fast, 0.125);
```

`slow` наследует CPU, а `fast` локально выбирает GPU. После сборки пример меняет
Focus `fast` на CPU: новые разрешения возвращают CPU, но уже работающий интегратор
по-прежнему использует GPU. Focus не переписывает constructor-ссылки.

Чтобы применить выбор к существующему потребителю, внешний управляющий код
завершает старую ветку, дожидается lifecycle и собирает новую. Выбор хранится в
сохраняемом родительском scope либо задаётся заново при сборке локального scope.
Автоматического rebuild и восстановления старой ветки при ошибке нового build нет.

Подробный контракт, карта исходников и сценарий с Builder:
[Context и пересборка](../../Docs/ru/Kernel/Composition.md).
