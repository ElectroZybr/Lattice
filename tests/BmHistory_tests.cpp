#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <string>

#include <BmHistory/Git.hpp>
#include <BmHistory/Machine.hpp>
#include <BmHistory/Repository.hpp>
#include <BmHistory/Statistics.hpp>
#include <Lattice/Tools/Tests.hpp>

namespace Lattice {

namespace {

using namespace BmHistory;
namespace fs = std::filesystem;

/// A fresh folder for a repository, removed with the object.
struct TempFolder {
    fs::path path;
    explicit TempFolder(std::string_view name)
        : path(fs::temp_directory_path() / std::format("bmhistory-{}-{}", name, std::random_device{}())) {
        fs::remove_all(path);
    }
    ~TempFolder() { fs::remove_all(path); }
};

std::vector<double> noisy(double center, double spread, size_t count, std::mt19937& random) {
    std::normal_distribution<double> noise(0.0, spread);
    std::vector<double> values;
    for (size_t i = 0; i < count; ++i)
        values.push_back(center * (1 + noise(random)));
    return values;
}

Benchmarks::ResultRow row(std::string bench, std::string n, std::vector<double> samples) {
    const double value = median(samples);
    return {.group = "G", .bench = std::move(bench), .implementation = "-", .params = "-", .variant = "-", .n = std::move(n),
            .stage = "Time", .metric = "Time.median", .unit = "ns", .value = value, .samples = std::move(samples), .measured = "-"};
}

SaveRequest request(std::vector<Benchmarks::ResultRow> rows) {
    return {.machine = "test-machine", .machineDescription = "id = \"test-machine\"\n", .suite = "Demo.bench",
            .table = {.rows = std::move(rows)}, .trailers = {{"Code", "Demo@abc1234"}, {"Build", "release"}}};
}

}

TEST(BmHistory_RankTestAndShift, Fixture,
    "Статистика должна отличать сдвиг от шума: точный тест рангов, порог, шумные ряды.") {
    const std::vector<double> low{1, 2, 3, 4, 5}, high{6, 7, 8, 9, 10};
    // Полностью разделённые выборки 5 и 5: p = 2 / C(10, 5).
    REQUIRE(std::abs(mannWhitney(low, high) - 2.0 / 252.0) < 1e-12);
    REQUIRE(mannWhitney(low, low) > 0.5);

    std::mt19937 random(7);
    const auto a = noisy(100, 0.01, 10, random), b = noisy(120, 0.01, 10, random);
    const Shift slower = shift(median(a), a, median(b), b);
    REQUIRE(slower.significant && slower.ratio > 1.15 && slower.low > 1.1);

    // 3 % — ниже порога 5 %.
    const auto c = noisy(103, 0.005, 10, random);
    REQUIRE(!shift(median(a), a, median(c), c).significant);
    // Шумные ряды (20 %): 15 % — не деградация.
    const auto d = noisy(100, 0.2, 10, random), e = noisy(115, 0.2, 10, random);
    const Shift wide = shift(median(d), d, median(e), e);
    REQUIRE(wide.noisy && !wide.significant);
    // Без семплов решает только порог.
    REQUIRE(shift(100, {}, 110, {}).significant && !shift(100, {}, 103, {}).significant);
    REQUIRE(Lattice::Benchmarks::higherIsBetter({.metric = "Phases.Transfer.In.rate", .unit = "B/s"}));
    REQUIRE(!Lattice::Benchmarks::higherIsBetter({.metric = "Time.median", .unit = "ns"}));
}

TEST(BmHistory_SavesAndMergesRuns, Fixture,
    "Репозиторий результатов должен сохранять запуски, сливать частичные и хранить baseline.") {
    TempFolder folder("save");
    auto repository = Repository::open(folder.path);
    std::mt19937 random(1);

    const auto first = repository.save(request({row("A", "1000", noisy(100, 0.01, 5, random)),
                                                row("B", "1000", noisy(50, 0.01, 5, random))}));
    REQUIRE(first.trailer("Run") == "r0001" && first.trailer("Machine") == "test-machine");
    REQUIRE(first.all("Code").size() == 1 && first.trailer("Build") == "release");
    REQUIRE(fs::exists(folder.path / "test-machine" / "Demo.bench.tsv"));
    REQUIRE(fs::exists(folder.path / "test-machine" / "machine.toml"));

    // Частичный запуск: только A.
    repository.save(request({row("A", "1000", noisy(130, 0.01, 5, random))}));
    const auto table = repository.load("HEAD", "test-machine", "Demo.bench");
    REQUIRE(table && table->rows.size() == 2);
    for (const auto& saved : table->rows)
        REQUIRE(saved.measured == (saved.bench == "A" ? "r0002" : "r0001"));

    REQUIRE(repository.runs().size() == 2 && repository.runs("test-machine", "Demo.bench").size() == 2);
    REQUIRE(repository.runs("other").empty());
    REQUIRE(repository.resolve("r0001") == first.commit);

    // Сравнение: A замедлился, B не перемеряли и не изменился.
    const auto old = repository.load("r0001", "test-machine", "Demo.bench");
    const auto changes = compareTables(*old, *table);
    REQUIRE(changes.size() == 2 && changes[0].after.bench == "A" && changes[0].regression);
    REQUIRE(!changes[1].shift.significant);

    repository.setBaseline("release", "r0001");
    REQUIRE(repository.baselines().size() == 1 && repository.baselines()[0].second == first.commit);
    REQUIRE(repository.resolve("release") == first.commit);
    REQUIRE(repository.load("baseline/release", "test-machine", "Demo.bench")->rows.size() == 2);
    repository.removeBaseline("release");
    REQUIRE(repository.baselines().empty());

    // Повторное открытие — тот же репозиторий.
    REQUIRE(Repository::open(folder.path).runs().size() == 2);
}

TEST(BmHistory_FindsTheShiftingRun, Fixture,
    "По истории должна находиться точка сдвига на верном запуске, а шум — не давать сдвигов.") {
    TempFolder folder("history");
    auto repository = Repository::open(folder.path);
    std::mt19937 random(3);
    for (int run = 0; run < 8; ++run) {
        const double center = run < 4 ? 100 : 125;   // деградация с пятого запуска
        repository.save(request({row("A", "1000", noisy(center, 0.01, 5, random)),
                                 row("B", "1000", noisy(50, 0.01, 5, random))}));
    }
    const auto history = repository.history("test-machine", "Demo.bench");
    REQUIRE(history.size() == 8);

    const std::string keyA = Benchmarks::rowKey(row("A", "1000", {}));
    const auto pointsA = series(history, keyA);
    REQUIRE(pointsA.size() == 8);
    const auto shifts = changePoints(pointsA);
    REQUIRE(shifts.size() == 1 && shifts[0].index == 4);
    REQUIRE(pointsA[shifts[0].index].run.trailer("Run") == "r0005" && shifts[0].shift.ratio > 1.2);

    const auto pointsB = series(history, Benchmarks::rowKey(row("B", "1000", {})));
    REQUIRE(changePoints(pointsB).empty());
}

TEST(BmHistory_MachineAndCode, Fixture,
    "Отпечаток машины должен быть стабильным, состояние кода — читаться из git.") {
    const auto machine = currentMachine();
    REQUIRE(!machine.id.empty() && machine.id == currentMachine().id);
    REQUIRE(machine.description.find("[memory]") != std::string::npos);

    TempFolder folder("code");
    fs::create_directories(folder.path);
    REQUIRE(!codeState(folder.path));
    Git git(folder.path);
    git.run({"init", "--quiet"});
    git.run({"config", "user.email", "t@t"});
    git.run({"config", "user.name", "t"});
    { std::ofstream(folder.path / "file.txt") << "one"; }
    git.run({"add", "file.txt"});
    git.run({"commit", "--quiet", "-m", "first"});
    auto state = codeState(folder.path / "file.txt");
    REQUIRE(state && state->commit.size() >= 7 && !state->dirty);
    { std::ofstream(folder.path / "file.txt") << "two"; }
    REQUIRE(codeState(folder.path)->dirty);

    bool failed = false;
    try {
        git.run({"no-such-command"});
    } catch (const ExceptionBase&) {
        failed = true;
    }
    REQUIRE(failed);
}

}
