# Переиспользуемые библиотеки

Эти библиотеки не линкуют Lattice Runtime и не требуют дерева, пакетов runtime или запуска приложения.

| Исходники | Цель xmake | Пакет xmake | Заголовки |
| --- | --- | --- | --- |
| `Tools/` | `Lattice.Tools` (header-only) | `lattice-tools` | `Lattice/Tools/*.hpp`, `Lattice/Kernel/Value.hpp` |
| `Logger/` | `Logger` (shared) | `lattice-logger` | `Logger/Logger.hpp`, `Logger/LogScope.hpp` |
| `SysInfo/` | `SysInfo` (shared) | `lattice-sysinfo` | `SysInfo/SystemInfo.hpp`, `SysInfo/Output.hpp` |
| `BmRunner/` | `BmRunner` (shared) | `lattice-bmrunner` | `BmRunner/Benchmarks.hpp`, `BmRunner/Metrics.hpp` |

Зависимости: Logger → Tools; SysInfo → Logger (форматированный вывод);
BmRunner → SysInfo и Logger. Tools содержит header-only утилиты и тип данных Value
с зависимостью от GLM; старые публичные пути этих SDK-заголовков сохранены.
Logger хранит sinks, настройки вывода и локальный для потока логгер в одной общей
динамической библиотеке: приложение и плагины используют одно состояние.

Подключение из другого проекта:

```lua
set_languages("c++23")
add_repositories("lattice-local /path/to/Lattice")
add_requires("lattice-logger", "lattice-bmrunner")
target("example")
    set_kind("binary")
    add_files("main.cpp")
    add_packages("lattice-logger", "lattice-bmrunner")
```

```cpp
#include <Logger/Logger.hpp>
#include <SysInfo/SystemInfo.hpp>
#include <BmRunner/Benchmarks.hpp>

int main() {
    const auto build = Lattice::SystemInfo::collectBuild();
    Logger::info("example", "Executable: {}", build.executable);
}
```

Namespace существующих API (`Lattice::SystemInfo`, `Lattice::Benchmarks`) сохранён.
Загрузочные пакеты с `compose.cpp` для этих библиотек не нужны.

Проверка из корня репозитория:

```sh
xmake build Logger.tests SysInfo.tests BmRunner.tests
xmake run Logger.tests
xmake run SysInfo.tests
xmake run BmRunner.tests
```

Эти цели используют существующие тесты утилит и тестовый runner, не линкуют runtime.
Тесты TextFormatter с runtime-параметрами остаются в `Lattice.tests`.
Пакет `lattice` использует отдельные установленные `lattice-logger` и
`lattice-sysinfo`; BmRunner подключают потребители бенчмарков явно.
