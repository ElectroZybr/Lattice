# lattice-test

Тесты обычного C++ без Lattice Runtime.

- `Tests.hpp` — `TEST`, `REQUIRE`, `CHECK`, `SKIP`; реестр `Lattice::Test`, запуск в процессе и результаты (`Testing::RunResult`). В консоль не пишет.
- `Report.hpp` — результат в TSV-протокол ltest и обратно, JSON.
- `main/Main.cpp` — статическая `lattice-test-main`: стандартный `main` набора.

```lua
target("Example.tests")
    set_kind("binary")
    add_files("tests/*.cpp")
    add_packages("lattice-test")       -- в ядре: add_deps("lattice-test-main")
```

```cpp
#include <lattice-test/Tests.hpp>

TEST(Addition, Lattice::Fixture, "Сумма двух чисел") {
    REQUIRE(2 + 2 == 4);
    CHECK(3 + 3 == 6, "необязательное пояснение");
}
```

`main` не нужен: его даёт `lattice-test-main`, набор со своим `main` его не подтянет.
Фикстура создаётся на каждый сценарий и уничтожается после него. Проверки выполняются в
потоке теста; ошибки рабочих потоков передают в него результатом или исключением.

Запуск многих наборов — [ltest](../ltest/README.md); подробно — [Testing](../../docs/ru/articles/Testing.md).
Тесты библиотеки: `xmake build -y lattice-test.tests && xmake run lattice-test.tests`.
