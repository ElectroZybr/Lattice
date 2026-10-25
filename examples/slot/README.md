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
├── Slot<Renderer>:renderer          объект SoftwareRenderer на самом слоте
└── Viewport:viewport → renderer
```

После замены:

```text
demo
├── Palette:palette
├── Slot<Renderer>:renderer          объект HardwareRenderer на самом слоте → uploads
│   └── UploadQueue:uploads → palette
└── Viewport:viewport → renderer
```

`Palette` переживает замену. Очередь загрузки принадлежит только hardware-подветке.
`Viewport` содержит обычную C++ ссылку на конкретный renderer и пересоздаётся извне.

```cpp
auto composeSoftware(NodeBuild& run) {
    return run.construct<SoftwareRenderer, Renderer>(run.require<Palette>("palette"));
}

auto composeHardware(NodeBuild& run) {
    auto queue = run.emplace<UploadQueue>("uploads", run.require<Palette>("palette"));
    return run.construct<HardwareRenderer, Renderer>(queue);
}

constinit const Lattice::Implementation implementations[] = {
    Lattice::implementation<Renderer, composeSoftware>("Software"),
    Lattice::implementation<Renderer, composeHardware>("Hardware"),
};

auto renderer = root.addSlot<Renderer>("renderer");
renderer.select("Software");
root.emplace<Viewport>("viewport", renderer.ref());
```

Реализации объявляет пакет в `lattice_package.implementations`; пример не пакет, поэтому
`main.cpp` регистрирует их сам (`addImplementation`). Слот видит все реализации своего
API: с одной он заполняется сам при первой нужде, с двумя, как здесь, ждёт `select`
или параметра выбора `renderer` на родителе.

Тип объекта берётся из возвращаемого handle; Slot открывает его API автоматически.
`node.bp` слота хранит API, `node.choice` — выбранную реализацию; объект реализации
создаётся на самом узле слота (`construct<Impl, API>`), его дети — части реализации.
Композиция обязана вернуть объект, созданный `construct` на слоте, а его C++ тип —
реализовывать API. Функция принимает `NodeBuild&` и возвращает `ObjectBuild<T>` или
`Ref<T>`; возврат конкретного handle через `auto` позволяет вывести связь с API.

Имя реализации в registry — `Пакет::Имя`. Повторная регистрация того же имени другой
функцией отклоняется. Повторный выбор уже работающей реализации возвращает `true` и
сохраняет объекты.

## Замена и lifecycle

```cpp
if (!renderer.select("Hardware")) {
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
участника не выполнен, старые объекты остаются живы. `select` возвращает `false`,
не ждёт в цикле и не запускает новую композицию; выбор запоминается, и `maintain`
собирает новую реализацию, как только старая ушла.

Потребителей внешний код собирает заново с `renderer.ref()`: runtime их не
пересобирает. В примере это делает `useHardware` — после `maintain` его повторный
`select` видит уже выбранную реализацию и возвращает `true`.

Если цепочка зависимостей включает владельца самого слота, `select` отклоняет
замену до retirement: в этом случае внешнему коду нужно пересобрать владеющую ветку.

## Ссылки и ошибки

- `focus<Renderer>(handle)` выбирает существующий кандидат Context.
  `slot.select(name)` выбирает реализацию и создаёт её подветку.
- `Slot` и `Focus` видят текущий renderer. Их `ref()`, а также `require`, `find`,
  `children` и `collect` возвращают снимок конкретного объекта с его поколением.
  Старый снимок не превращается в ссылку на новую реализацию.
- Неудачная сборка удаляет новую частичную подветку, её экспорты, локальный Context
  и зависимости. Slot остаётся пустым; прежняя реализация не восстанавливается.
- Устаревший Slot после удаления и повторного использования NodeId отклоняет `select`.
- Имя, которое не реализация API слота, — ошибка со списком реализаций.
