# lattice-test

Тесты обычного C++ без Lattice Runtime. Зависимость — `lattice-logger` и его заголовочные
утилиты `lattice-tools`. Один реестр `Lattice::Test` в общей библиотеке; `Lattice::Fixture`
остаётся общим контрактом тестов и бенчмарков в `lattice-tools`.

```lua
add_requires("lattice-test")
target("Example.tests")
    set_kind("binary")
    add_files("tests/*.cpp")
    add_packages("lattice-test")
```

```cpp
#include <lattice-test/Tests.hpp>

TEST(Addition, Lattice::Fixture, "Сумма двух чисел") {
    REQUIRE(2 + 2 == 4);
    CHECK(3 + 3 == 6, "необязательное пояснение");
}

int main() { return Lattice::Test::instance().runAll() == 0 ? 0 : 1; }
```

`TEST` регистрирует сценарий автоматически. Фикстура создаётся для каждого сценария и
уничтожается после него; свой тип наследуется от `Lattice::Fixture`. `REQUIRE` прерывает
сценарий исключением, `CHECK` отмечает ошибку и продолжает. Оба выводят выражение, файл,
строку и необязательное сообщение. Исключение из сценария или конструктора фикстуры
считается ошибкой, следующие сценарии продолжают выполняться. `runAll()` возвращает число
неудачных сценариев. Проверки выполняются в потоке runner; ошибки рабочих потоков передают
в него результатом или исключением.

Тесты библиотеки: `xmake build -y lattice-test.tests && xmake run lattice-test.tests`.
Прогон намеренно создаёт ошибочные сценарии и проверяет их число и очистку фикстур.
