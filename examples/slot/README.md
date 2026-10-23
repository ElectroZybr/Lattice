# Slot: выбор композиции реализации

Пример показывает две реализации `Renderer` с разными зависимостями. Все предметные
классы находятся в `model.hpp` и не включают Lattice. Вся сборка — в `compose.cpp`.
`main.cpp` запускает runtime и выполняет внешнюю последовательность замены.

```sh
xmake build Lattice.example.slot
xmake run Lattice.example.slot
```

Основные строки вывода:

```text
Initial renderer: software
Old renderer and viewport retiring
Replacement renderer: hardware; frame=32
```

## Одна роль, две подветки

```text
demo
├── Palette:palette
├── Slot<Renderer>:renderer
│   └── SoftwareRenderer:backend
└── Viewport:viewport → renderer/backend
```

После замены:

```text
demo
├── Palette:palette
├── Slot<Renderer>:renderer
│   ├── UploadQueue:uploads → palette
│   └── HardwareRenderer:backend → uploads
└── Viewport:viewport → renderer/backend
```

`Palette` переживает замену. Очередь загрузки принадлежит только hardware-подветке.
`Viewport` содержит обычную C++ ссылку на конкретный renderer и пересоздаётся извне.

```cpp
auto composeSoftware(NodeBuild& run) {
    return run.emplace<SoftwareRenderer>("backend", run.require<Palette>("palette"));
}

auto composeHardware(NodeBuild& run) {
    auto queue = run.emplace<UploadQueue>("uploads", run.require<Palette>("palette"));
    return run.emplace<HardwareRenderer>("backend", queue);
}

auto renderer = root.addSlot<Renderer>("renderer");
renderer.choice("SlotExample::Software", composeSoftware);
root.emplace<Viewport>("viewport", renderer.ref());
```

`choice(name, function)` регистрирует composition blueprint при первом использовании.
Тип объекта берётся из возвращаемого handle; Slot открывает его API автоматически.
`node.bp` слота хранит API, `node.object.bp` — выбранную композицию;
`node.object.ptr` пуст, объект реализации принадлежит дочерней ноде.
Отдельный класс композиции и список регистрации типов не нужны. Возвращаемый объект
должен принадлежать поддереву слота и этому runtime, а его C++ тип — реализовывать API.
Функция принимает `NodeBuild&` и возвращает `ObjectBuild<T>` или `Ref<T>`.
Если возвращаемый `Ref<API>` уже скрывает конкретный тип, API нужно открыть при
создании: `emplace<Implementation, API>(...)`. Возврат конкретного handle через
`auto`, как в примере, позволяет Slot вывести эту связь.
Захватывающие лямбды не сохраняются: здесь используется обычный указатель на функцию.

Имя идентифицирует вариант композиции в registry. Повторная регистрация того же
имени другой функцией/API отклоняется. Повторный выбор уже работающего blueprint
возвращает `true` и сохраняет объекты.

## Замена и lifecycle

```cpp
if (!renderer.choice("SlotExample::Hardware", composeHardware)) {
    // Прежняя подветка и потребители завершаются. Вернуться во внешний цикл.
    return false;
}
root.emplace<Viewport>("viewport", renderer.ref());
```

Первый вызов при замене существующей композиции начинает retirement старой подветки
и зависимых потребителей. Slot сохраняет NodeId и место в Context, но временно
не разрешает объект. Старые объекты и их экспорты скрываются; lifecycle callbacks
вызываются через обычный механизм `NodeOps`.

Внешний цикл вызывает `maintain()` / `collectRetired()`. Пока readiness хотя бы одного
участника не выполнен, старые объекты остаются живы. `choice` возвращает `false`,
не ждёт в цикле и не запускает новую композицию. Даже синхронное завершение требует
шага обслуживания runtime.

После фактического удаления внешний код повторяет `choice` с желаемым вариантом.
Теперь создаётся новая подветка, результат — `true`. Потребителей внешний код
собирает заново с `renderer.ref()`. Runtime не хранит отложенную команду и не
пересобирает потребителей автоматически. В примере это делает `useHardware`.

Если цепочка зависимостей включает владельца самого слота, `choice` отклоняет
замену до retirement: в этом случае внешнему коду нужно пересобрать владеющую ветку.

## Ссылки и ошибки

- `focus<Renderer>(handle)` выбирает существующий кандидат Context.
  `slot.choice(...)` выбирает blueprint и создаёт его подветку.
- `Slot` и `Focus` видят текущий renderer. Их `ref()`, а также `require`, `find`,
  `children` и `collect` возвращают снимок конкретного объекта с его поколением.
  Старый снимок не превращается в ссылку на новую реализацию.
- `collect<Renderer>()` не считает один объект дважды через слот и его backend.
- Неудачная сборка удаляет новую частичную подветку, её экспорты, локальный Context
  и зависимости. Slot остаётся пустым; прежняя реализация не восстанавливается.
- Устаревший Slot после удаления и повторного использования NodeId отклоняет `choice`.

Реализация слота всегда задаётся функцией композиции: `choice(name, function)`.
