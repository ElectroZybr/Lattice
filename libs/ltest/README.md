# ltest

Приложение: находит тестовые бинарники, запускает каждый своим процессом и суммирует их
отчёты. Наборы — обычные цели на [lattice-test](../lattice-test/README.md).

```sh
xmake install-ltest                   # ~/.local/bin/ltest
ltest list                            # наборы
ltest list Boundaries                 # сценарии набора, без запуска
ltest run                             # всё, краткая сводка
ltest run Lattice 'Blueprints_*' --details
ltest run Tasks -v                    # живой ход тестов
ltest run '*' --json
```

`Suites` — поиск, процессы, таймауты и протокол; `main.cpp` — аргументы и вывод. Лог процесса
и отчёт живут во временном каталоге и удаляются после прогона; если набор не смог отчитаться
(ошибка загрузчика, падение), в сводку попадает конец его лога. `--logs <dir>` сохраняет их.

Проверки: `xmake build -y ltest.tests && xmake run ltest.tests`.
[Тесты и ltest](../../docs/ru/articles/Testing.md).
