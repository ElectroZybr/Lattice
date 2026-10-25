# Переиспользуемые библиотеки

Эти библиотеки не линкуют Lattice Runtime и не требуют дерева, пакетов runtime или запуска приложения.

Папка библиотеки, её папка кода, цель и пакет xmake называются одинаково. Тесты — в
`tests/` библиотеки, цель `<имя>.tests`.

| Библиотека | Цель | Пакет xmake | Заголовки |
| --- | --- | --- | --- |
| `lattice-tools/` | header-only | `lattice-tools` | `Lattice/tools/*.hpp`, `Lattice/kernel/Value.hpp` |
| `lattice-test/` | shared | `lattice-test` | `lattice-test/Tests.hpp`, `Results.hpp`, `Reports.hpp` |
| `lattice-test-runner/` | binary `ltest` | `lattice-test-runner` | CLI поиска и запуска тестов, интеграция через JSON |
| `lattice-logger/` | shared | `lattice-logger` | `lattice-logger/Logger.hpp`, `lattice-logger/LogScope.hpp` |
| `lattice-sysinfo/` | shared | `lattice-sysinfo` | `lattice-sysinfo/SystemInfo.hpp`, `lattice-sysinfo/Output.hpp` |
| `lbm-runner/` | shared | `lbm-runner` | `lbm-runner/Benchmarks.hpp`, `lbm-runner/Metrics.hpp` |
| `lbm-history/` | static | — | `lbm-history/Repository.hpp`, `lbm-history/Statistics.hpp` |
| `lbm/` | binary `lbm` | — (`xmake install-lbm`) | — |

Зависимости: lattice-test → lattice-tools; lattice-test-runner → lattice-test и lattice-logger; lattice-logger → lattice-tools; lattice-sysinfo → lattice-logger; lbm-runner →
lattice-sysinfo и lattice-logger; lbm-history → lbm-runner; `lbm` (запуск наборов, история в
git, сравнение, [проект](../docs/ru/articles/BmHistory.md)) → lbm-history. lattice-tools —
header-only утилиты и тип данных `Value` (зависит от GLM). lattice-logger хранит sinks,
настройки вывода и локальный для потока логгер в одной общей динамической библиотеке:
приложение и пакеты используют одно состояние.

Подключение из другого проекта:

```lua
set_languages("c++23")
add_repositories("lattice-local /path/to/Lattice")
add_requires("lattice-logger", "lbm-runner")
target("example")
    set_kind("binary")
    add_files("main.cpp")
    add_packages("lattice-logger", "lbm-runner")
```

```cpp
#include <lattice-logger/Logger.hpp>
#include <lattice-sysinfo/SystemInfo.hpp>
#include <lbm-runner/Benchmarks.hpp>

int main() {
    const auto build = Lattice::SystemInfo::collectBuild();
    Logger::info("example", "Executable: {}", build.executable);
}
```

Namespace существующих API (`Lattice::SystemInfo`, `Lattice::Benchmarks`) сохранён.
Загрузочные пакеты с `compose.cpp` для этих библиотек не нужны.

Проверка из корня репозитория:

```sh
xmake build -y lattice-test.tests lattice-logger.tests lattice-sysinfo.tests lbm-runner.tests
xmake run lattice-test.tests
xmake run lattice-logger.tests
xmake run lattice-sysinfo.tests
xmake run lbm-runner.tests
```

Эти цели используют существующие тесты утилит и тестовый runner, не линкуют runtime.
Тесты TextFormatter с runtime-параметрами остаются в `Lattice.tests`.
Пакет `lattice` использует отдельные установленные `lattice-logger` и
`lattice-sysinfo`; BmRunner подключают потребители бенчмарков явно.

Тестовая инфраструктура — [lattice-test](lattice-test/README.md); её исходники не входят в runtime.
