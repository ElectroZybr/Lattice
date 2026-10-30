# Композиция, Context и пересборка

## Назначение

`compose(NodeBuild root)` описывает создание подветки, а не устройство предметных
классов. В одном `compose.cpp` могут находиться локальные функции сборки.
Пакет экспортирует одну точку входа `extern "C" void compose(NodeBuild)`.

```cpp
extern "C" void compose(NodeBuild root) {
    auto cpu = root.emplaceShared<Device>("cpu", "cpu");
    auto gpu = root.emplaceShared<Device>("gpu", "gpu");
    root.focus<Device>(cpu.ref());

    auto slow = root.branch("slow");
    composeRun(slow);
    auto fast = root.branch("fast");
    fast.focus<Device>(gpu.ref());
    composeRun(fast);
}
```

Локальные функции сборки принимают `NodeBuild&`: `void composeRun(NodeBuild& run)`.
Они используют builder на время вызова и не забирают его, поэтому `std::move` не нужен.

`Device` — обычный класс. Общие объекты создаются раньше потребителей. Типы и
публичные API регистрируются в месте `emplace`, без отдельного списка регистрации.
`emplace<Implementation, API>` явно открывает C++ базовый интерфейс для runtime.

## Метаданные пакета

Описание пакета объявляется в том же `compose.cpp`, типизированно:

```cpp
extern "C" constinit const Lattice::Package lattice_package{
    .id = "Shell",
    .version = {1, 0, 0},
    .dependencies = {{.id = "Actions", .min = {1, 0, 0}}, {.id = "Graphics"}},
};
```

`PackageManager` открывает библиотеки пакета (в каталоге пакета или на уровень ниже),
читает `lattice_package` и упорядочивает регистрацию по зависимостям. Библиотека без
метаданных пропускается. `kernelApi` по умолчанию — версия заголовков, с которыми
собран пакет. Метаданные — константные данные: каждый пакет экспортирует один и тот же
символ, и динамическая инициализация шла бы через символ ранее загруженного пакета.

Другой пакет вкладывается дочерней веткой: `root.compose("Graphics", "render")`.

## Context: кандидаты и выбор

Кандидаты — доступные для роли объекты. `emplaceShared` публикует их в Context.
Focus — отдельный явный target. Один кандидат без target разрешается автоматически;
несколько дают Ambiguous, отсутствие кандидатов — Missing. Произвольный первый
кандидат не выбирается.

`branch.focus<T>()` создаёт или использует scope именно этой ветки. Пустой scope
наследует родительские решения. Локальный target перекрывает родительский для
этой ветки и потомков, не меняя соседей.

```cpp
fast.focus<Device>(gpu.ref());                 // выбрать локальный target
fast.focus<Device>().reset();                  // убрать локальный target
```

`reset` не удаляет кандидатов. Возобновляется обычное разрешение: родительский
выбор, единственный кандидат либо ошибка неоднозначности.

Единственный кандидат закрепляется при первой нужде (`Focus`, `mount`, `require`
общего объекта, `focus` в `configure`): он становится target своего scope, и
появившийся позже второй кандидат выбор не меняет. Уход закреплённого кандидата
освобождает выбор. Неявное закрепление не уведомляет наблюдателей и не сохраняется
в конфигурацию.

Выбор роли открыт инструментам параметром на узле-владельце scope:
`device = gpu ∈ {cpu, gpu}`. Параметр ничего не хранит — чтение, запись (`set` или
`reset` для пустого значения) и варианты идут в Context. Он появляется у точки фокуса
(`focus<T>()`, `focus<T>(ref)` в композиции) и у scope ветки, где у роли два кандидата и
больше. Общие объекты (`emplaceShared`) — кандидаты корня, и там параметр сам не
заводится: глобальный выбор, который никто не делает, был бы шумом. Он появляется при
первом обращении к неоднозначной роли без фокуса (`ChoiceRequired`). Имя — короткое имя типа с маленькой буквы (`Graphics::Device` → `device`),
при занятом имени — `deviceFocus`; вариант — имя экземпляра, при совпадении
`Type:name`. В CLI выбор фиксируется командой `device gpu`. Кандидатные наборы и
явные targets наследуются ядром отдельно; правила реализованы в `NodeContext`.

`branch.focus<T>(ref)` выбирает существующий экземпляр (`NodeId`) для контекстной роли.
Слот выбирает реализацию API. В композиции объявляется только он сам —
`root.addSlot<Renderer>("renderer")`; варианты — реализации `Renderer`, которые пакеты
объявляют в `lattice_package.implementations`:

```cpp
auto composeHardware(NodeBuild& slot) {               // объект — на самом слоте
    return slot.construct<HardwareRenderer, Renderer>(slot.require<Palette>("palette"));
}
constinit const Lattice::Implementation implementations[] = {
    Lattice::implementation<Renderer, composeHardware>("Hardware"),   // Render::Hardware
};
extern "C" constinit const Lattice::Package lattice_package{
    .id = "Render", .version = {1, 0, 0}, .implementations = implementations,
};
```

Новый слот пуст. Единственная реализация API заполняет его при первой нужде:
`slot.ref()`, `require`, `mount`, `focus.ref()`, конец композиции (`Builder`), `maintain`. Несколько реализаций — слот ждёт
явного выбора: `slot.select("Hardware")`, параметр выбора `renderer` на родителе слота
(`renderer Hardware` в CLI, конфигурация). Догруженный позже пакет с ещё одной
реализацией заполненный слот не меняет. Замена работающей реализации — retirement
старой и сборка новой: `select` возвращает `false`, пока старая уходит, а `maintain`
доводит замену сам.

**Невыбранное требует выбора лениво.** Пока к пустому слоту или неоднозначной роли
никто не обратился, это не ошибка. Обращение (`slot.ref()`, `require` слота, `mount`,
`focus.ref()`) бросает `ChoiceRequired`: чей параметр, как называется, из чего выбирать.
Самая внешняя композиция ветки его перехватывает: ветка остаётся пустым узлом и ждёт
(`ExportsView::pending()`). Выбор внутри ветки получает заместитель на её узле с
относительным именем (`renderer`, `view.renderer`); выбор снаружи (`device` корня) —
пишется в свой параметр. После записи `maintain` собирает ветку заново, а выбор,
сделанный в заместитель, становится записью в настоящий параметр слота или фокуса.

Контракт замены и пример —
[Slot и композиции реализаций](../../../examples/slot/README.md).
Это разные операции. `branch.focus<T>(ref)` принимает handle существующего кандидата. Проверяются runtime,
поколение NodeId, доступность объекта, совместимость с API и принадлежность к
видимому набору кандидатов. Нельзя выбрать retiring объект или объект другого runtime.
Выбор сам по себе не создаёт владение и не регистрирует зависимости потребителя.

## Focus, mount и снимки

```cpp
auto selected = run.focus<Device>();
auto device = run.mount<Device>();
auto renderer = run.emplace<Renderer>("renderer", device.ref());
```

`Focus<T>` — копируемый runtime handle решения. Его `get()` отражает последующие
изменения Context. `ref()` разрешает текущий объект в снимок `Ref<T>` и сообщает
ошибку при Missing/Ambiguous. Handle scope перестаёт быть валиден после удаления
ветки, даже если её NodeId затем используется повторно.

`mount` разрешает shared-объект и сохраняет зависимость ветки. Передача `ref()`
в `emplace` дополнительно связывает сам создаваемый объект с поставщиком.
Изменение Focus не меняет полученные раньше `Ref` и обычные C++ ссылки.

| Значение | После изменения Context/дерева |
| --- | --- |
| `Focus<T>` | Разрешает текущий выбор |
| `Ref<T>` / ссылка конструктора | Продолжает обозначать прежний экземпляр |
| `find<T>()` | Снимок найденного объекта или отсутствия |
| `children<T>()`, `collect<T>()` | Снимок набора на момент запроса |

Запросы не создают недостающих объектов. Зависимости снимков записываются при
передаче в `emplace`, у потребителя, и живут вместе с ним. Удаление поставщика
включает зависимых потребителей в retirement.

## configure: связывание с текущим деревом

Пакет может экспортировать рядом с `compose` необязательную функцию
`extern "C" void configure(NodeConfigure root)`; реализация слота передаёт её вторым
аргументом `implementation<API, Compose>(name, configure)`. Runtime вызывает её сразу после `compose` и затем лениво в
`maintain`, если:

* в поддереве ветки создан узел (или построен его объект) либо узел ушёл в retire /
  удалён;
* ушёл объект, найденный через `find`/`require`/`focus`/`owner`;
* изменилась роль Context, которую он прочитал через `focus`/`focusId`. Изменения других
  ролей его не будят: состав дерева и выбор в Context учитываются раздельно.

`NodeConfigure` только читает дерево (`self`, `children`, `collect`, `childNodes`,
`collectNodes`, `find`, `require`, `focus`, `owner`) и не создаёт узлы. Возвращаются
только активные построенные объекты. Результаты не записываются как зависимости:
уход найденного объекта переконфигурирует потребителя, а не завершает его.

`maintain` вызывает `configure` до удаления retiring-узлов, а удаление вне `maintain`
(замена слота, откат) переконфигурирует сразу — объекты не держат удалённый узел.
Неоднозначный `focus` — ошибка, как и в `compose`.

## Конвейеры

Последовательность работы (шаг симуляции, кадр) — дерево узлов `PipelineAPI`
(`Lattice/kernel/PipelineAPI.hpp`, только заголовок). `Pipeline` выполняет детей своего
узла по порядку дерева; сам он тоже `PipelineAPI`, поэтому группы вкладываются.
`CallStage<T>` делает стадией метод обычного объекта — реализация о конвейере не знает.

```cpp
extern "C" void compose(NodeBuild root) {
    auto integrator = root.emplace<Verlet>("integrator", atoms);
    root.emplace<Lattice::CallStage<Verlet>, Lattice::PipelineAPI>("predict", integrator, &Verlet::predict);
    auto forces = root.branch("forces");
    forces.emplace<Lattice::CallStage<Walls>, Lattice::PipelineAPI>("walls", walls, &Walls::addForces);
    forces.construct<Lattice::Pipeline, Lattice::PipelineAPI>();
    root.emplace<Lattice::CallStage<Verlet>, Lattice::PipelineAPI>("correct", integrator, &Verlet::correct);
    root.construct<Lattice::Pipeline, Lattice::PipelineAPI>();
}

extern "C" void configure(NodeConfigure root) {
    Lattice::configurePipelines(root);
}
```

* Порядок стадий — порядок детей: композиция создаёт их в нужном порядке. Новый узел
  встаёт в конец после `maintain`, retire стадии убирает её до удаления.
* `setStages` ждёт конца текущего `run()`: конвейер может выполняться в потоке сервиса.

## Поток исполнения экспортов

Состояние объекта, которым владеет другой поток (симуляция в своём сервисе), нельзя
читать и писать из потоков потребителей экспортов (CLI, роутер, сериализатор). Поток
назначается один раз, до экспортов объекта:

```cpp
auto commands = root.emplace<Tasks::Queue>("commands");
auto verlet = root.emplace<Verlet, IntegratorAPI>("integrator", atoms);
verlet.executeOn(commands);
verlet.param("dt", &Verlet::dt);          // обычный параметр
```

Дальше чтение, запись и вызов экспортов объекта — включая таблицы (`exportTable`) —
идут в поток-владелец. Чтение, вызов и свой сеттер параметра — через `commands->call(work)`:
работа выполняется там, вызывающий ждёт её. Простая запись проверяется (тип, правила) в
потоке вызывающего и передаётся через `commands->post(work)` без ожидания, если у очереди
есть `post`: результат уже известен, а очередь сохраняет порядок, так что следующее чтение,
вызов и сохранение видят новое значение. Отложенные записи доходят до объекта до его
уничтожения (`Exports::settle`). Объект зависит от узла очереди. `executeOn` после
экспортов объекта — ошибка; параметры выбора, которые ядро ставит на узел само, не в
счёт — они идут в свой источник. Очередь — `ObjectBuild` или `Ref` (`slot.require<Queue>`).

## Ребёнок из реализации каталога

`Builder::add(parent, implementation, name)` собирает реализацию из `lattice_package.implementations`
обычным ребёнком: узел — объект реализации под её API (как на слоте), тип объекта — тот же, что
у ребёнка из `compose`. Такие дети одного типа — части (свет сцены), а не кандидаты выбора
Context. Так конфигурация создаёт детей той же функцией сборки, что и код пакета.

## Пересборка: явная последовательность

Чтобы применить новый выбор к уже работающей композиции:

1. Из внешнего события изменить Context в сохраняемом родительском scope.
2. Запросить удаление старой ветки через `Builder::del`.
3. Дождаться фактического удаления: `maintain` / `collectRetired` опрашивают readiness.
4. Вызвать `Builder::add` с прежним composition blueprint и именем экземпляра.
5. Новая композиция разрешит новый Context и получит новые снимки.

```cpp
// Код внешнего управляющего слоя; parentId принадлежит сохраняемой ветке.
NodeBuild{parentId, nodes}.focus<Device>(nextDevice);
nodes.builder.del(parentId, oldBranchId);

// На последующих итерациях внешнего цикла:
nodes.ops.maintain();
if (!nodes.registry.get(oldBranchId)) {
    auto replacement = nodes.builder.add(parentId, consumerBlueprint, "consumer");
    // Сохранить replacement и завершить операцию пересборки.
}
```

Управляющий слой должен отслеживать конкретный экземпляр, а не продолжать
использовать старые handles после удаления. Новая ветка может получить прежний
NodeId, но её поколение и объекты будут другими.

`del` лишь начинает retirement. Пока старый экземпляр ещё жив, `add` той же
идентичности отклоняется. Не надо блокировать runtime ожиданием readiness:
продолжайте обслуживать внешний цикл и завершите шаг позже.

Смена Focus не вызывает автоматическую пересборку и не меняет уже внедрённые
аргументы.

Реакция на смену Context — подписка в композиции: `focus.onChange(owner, callback)`.
Ядро помечает изменённые роли (`set`, `reset`, кандидаты, удаление целей) и вызывает
наблюдателей лениво, в `maintain`, по одному разу на роль; подписка живёт, пока жив
`owner`. В runtime `focus.set(ref)` выбирает объект: видимого кандидата или объект из
поддерева владельца фокуса. Политики отслеживания изменений остаются во
внешнем управляющем слое; отдельного DI-менеджера или планировщика здесь нет.

Ошибка нового `add` откатывает новую частично построенную ветку. Уже удалённая
старая ветка не восстанавливается. Транзакционной замены работающих веток нет.
Если локальная ветка собиралась обычной функцией через `branch()`, управляющий
код после удаления снова вызывает эту функцию и заново задаёт её локальные выборы.
Выбор, который должен пережить замену, хранится в родительском scope.

## Экспорты и lifecycle

```cpp
integrator.param("dt", &Integrator::dt);
simulation.action("step", &Simulation::step);
worker.onRetire(&Worker::requestStop);
worker.readyToDestroy(&Worker::stopped);
```

Экспорт принадлежит самому объекту: `(NodeId owner, name)`. Callback остановки
вызывается один раз; readiness удерживает retirement-набор до завершения работ.
Обычные деструкторы освобождают ресурсы. Подробности и ограничения builders — в
[документации примера](../../../examples/composition/README.md).

## Сервисы

Долгий цикл объявляется в композиции, реализация не наследует `ServiceAPI`:

```cpp
auto loop = root.emplace<SimulationLoop>("loop", simulation);
loop.service(&SimulationLoop::run, &SimulationLoop::requestStop);
```

`run` принимает `T&` или `(T&, const std::function<void()>& idle)`; `stop` — `T&`.
Сервисная роль хранится в lifecycle самого объекта, без дополнительного узла.
Runtime ищет отмеченные объекты в поддереве стартовой ветки: ноль — нет сервиса, один — запуск, несколько — ошибка.
Режим worker/host задаётся `lattice.toml`. `idle` в host-режиме вызывает `maintain`,
в worker-режиме ничего не делает. Retirement вызывает `stop` и ждёт выхода из `run`.

## Экспорты объекта

```cpp
terminal.param("prompt", terminal->style().prompt);     // значение внутри объекта
cli.globalAction("cd", &CLI::changeDirectory);          // void(ActionContext&, std::string)
Lattice::exportTable(atoms);                            // view / addRow для Table
```

Метод может принимать `ActionContext&` первым параметром: остальные параметры становятся
аргументами действия. `globalAction` виден из корня runtime, но принадлежит объекту и
снимается при его retirement. `Table` — обычный контракт: сам ничего не регистрирует.

## Правила и свой сеттер параметра

`param(...)` возвращает `ParamBuild` (приводится к `ExportId`): к нему добавляются правила.
Своему сеттеру передаётся значение, прошедшее тип и правила; он применяет его или
отказывает с подробностями:

```cpp
integrator.param("dt", &Verlet::dt).range(1e-6, 0.1);                 // концы входят, NaN — нет
mesh.param("display", &MeshObject::display).choices({"shaded", "wireframe"});
label.param("name", &Label::name).maxLength(64);
space.param("shape", &Domain::text, [](Domain& domain, const std::string& text) -> Lattice::ParamResult {
    auto shape = Boundaries::parseShape(text);
    if (!shape)
        return Lattice::reject(Lattice::ParamError::Invalid, shape.error().message,
                               shape.error().begin, shape.error().end, shape.error().expected);
    domain.setShape(std::move(*shape), text);
    return {};
});
```

* Запись: тип → правила → сеттер или присваивание → уведомление. Любой путь записи, включая
  сырой `ResolvedExport::set`, проходит проверку; отказ не меняет поле и не уведомляет.
* `ParamResult` — один указатель: при успехе ничего не создаётся. Отказ `ParamRejection` —
  код (`Type`, `Range`, `Choice`, `Length`, `Invalid`), сообщение, участок строкового ввода
  `[begin, end)` и ожидаемое на этом месте.
* Сеттер — метод `ParamResult (T::*)(const Field&)` или вызываемое `(T&, const Field&)`;
  `void` не принимается, чтобы отказ не был тихим. С `executeOn` он выполняется в потоке
  владельца, а отказ по правилам туда не заходит.
* `ExportsView::rules(id)` отдаёт правила инструментам (help, GUI): диапазон, выбор,
  длину и признак своего сеттера.
* Параметр без правил и потока остаётся указателем на поле: ни памяти, ни проверок сверх
  приведения типа. Цена записи — в бенче `Lattice.bench Exports`.

`root.exports()` и `root.tree()` передают runtime views инфраструктурным объектам
(маршрутизация ввода, терминалы) обычными аргументами конструктора.

## Проверки

- `tests/SlotComposition_tests.cpp` — вывод API, снимки, retirement потребителей,
  ожидание readiness, откат сборки и проверка владельца/поколения.
- `tests/Focus_tests.cpp` — локальный выбор, наследование, reset, типы и валидность
  handles, неизменность внедрённых ссылок, ожидание lifecycle и ошибки пересборки.
- `tests/Composition_tests.cpp` — конструкторные связи, снимки коллекций, экспорты,
  внешний lifecycle и move-only capabilities.
- `tests/NodeContext_tests.cpp` — низкоуровневые роли и разрешение scopes.
- `tests/Service_tests.cpp` — сервис обычного объекта, idle/host, retirement и откат.
- `tests/Table_tests.cpp` — `Table` без экспортов и `exportTable` в композиции.
- `tests/ParamRules_tests.cpp` — правила, отказы, свой сеттер, поток владельца, запись
  без ожидания и её доставка до уничтожения объекта.

В [примере](../../../examples/composition/README.md) `slow` наследует CPU, `fast`
выбирает GPU. Затем Focus ветки `fast` меняется на CPU, но уже созданный интегратор
продолжает работать с GPU. Это намеренное поведение до явной пересборки.

## Явные изменения параметров

`Exports::set(id, value)` и `ExportsView::set(resolved, value)` записывают параметр,
возвращают `ParamResult` и после принятой записи уведомляют `observeParameters(callback)`
событием `ParamChange::Set`.
`Exports::reset(id)` / `ExportsView::reset(resolved)` уведомляют об удалении
переопределения (`Reset`), не меняя поле. При снятии параметра приходит `Remove`.
Подписка снимается через `unobserveParameters(token)` до уничтожения наблюдателя.
Ядро не хранит исходные значения и пользовательский слой; их политика принадлежит пакетам.

Слоям конфигурации нужно знать и о том, что собрано позже: `NodeOps::observeBuilds(callback)`
вызывается в конце сборки (`Builder::add`) и `maintain`, вне композиции, когда появились новые
узлы или экспорты (слот при замене реализации сохраняет узел, новая реализация приносит новые
экспорты). Сборка внутри callback сообщается после его возврата. `NodeRegistry::lastGeneration()`
и `Exports::size()` растут монотонно: по ним наблюдатель отличает новое от виденного.

Прямые записи в поля и raw-setter `ResolvedExport::set(object, value)` не уведомляют
наблюдателей (правила raw-setter проверяет так же): этот путь используется для временного ввода, например накопления
движения камеры. CLI и инспекторы используют явный `ExportsView::set`, чтобы
Serialization мог сохранить заданное пользователем значение независимо от дальнейшей
работы объекта. `reset` не восстанавливает исходное значение: оно появится при
новой композиции ветки без переопределения.

Корневой объект композиции создаётся через `root.construct<T>(args...)`: он сохраняет идентичность пакета, владеет дочерними узлами и может объявляться сервисом. Объект уничтожается до принадлежащих ему детей, после остановки сервиса.

`construct` работает и на структурной ветке из `branch(name)`: узел ветки становится
узлом объекта, а дети, созданные до него, остаются его детьми. Так объект получает
зависимости от собственных детей (вьюпорт и его камера):

```cpp
auto viewport = root.branch("viewport");
auto camera = viewport.emplace<Camera>("camera");
viewport.construct<TestViewport, Viewport>(camera);
viewport.addSlot<Controller>("controller");         // единственная реализация — FreeCamera
```

Вложенная композиция находит объект, которому принадлежит, запросом
`owner<T>()`: снимок ближайшего предка, чей объект предоставляет `T`. Это не зависит
от имён, поэтому одинаковых веток может быть сколько угодно.

`obj.onMaintain(callback)` вызывает `callback(T&)` на host в каждом `maintain`, пока объект активен. Hook снимается вместе с lifecycle экземпляра; retiring-узлы не вызываются. Вложенный `maintain` не запускает hooks повторно. Используется CLI для исполнения принятых строк на host.
