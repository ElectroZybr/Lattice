#include <Lattice/tests/RuntimeFixture.hpp>
#include <Lattice/Tools/Tests.hpp>

#include <Lattice/Lattice.hpp>


namespace Lattice {

TEST(Package_Empty, RuntimeFixture,
    "Пустой пакет должен успешно загрузиться.")
{
    fixture.packageManager.scanDirectory(
        "Lattice/tests/TestPackages/Empty"
    );

    fixture.packageManager.checkCandidates();
    fixture.packageManager.loadCandidates();

    auto* package = fixture.packageManager.findCandidate("Empty");

    REQUIRE(package);
    REQUIRE(package->status == LoadStatus::Loaded);
}

TEST(Package_MissingDependency, RuntimeFixture,
    "Пакет с отсутствующей зависимостью не должен попасть в очередь загрузки.")
{
    fixture.packageManager.scanDirectory(
        "Lattice/tests/TestPackages/MissingDependency"
    );

    fixture.packageManager.checkCandidates();

    auto* package = fixture.packageManager.findCandidate("MissingDependency");

    REQUIRE(package);
    REQUIRE(package->status == LoadStatus::MissingDependency);
    REQUIRE(fixture.packageManager.queue().empty());
}

TEST(Package_DependencyOrder, RuntimeFixture,
    "Зависимость должна находиться в очереди загрузки раньше зависимого пакета.")
{
    fixture.packageManager.scanDirectory(
        "Lattice/tests/TestPackages/Provider"
    );

    fixture.packageManager.scanDirectory(
        "Lattice/tests/TestPackages/Consumer"
    );

    fixture.packageManager.checkCandidates();

    const auto& queue = fixture.packageManager.queue();

    REQUIRE(queue.size() == 2);

    REQUIRE(queue[0]->manifest.id == "Provider");
    REQUIRE(queue[1]->manifest.id == "Consumer");
}

TEST(Package_DependencyCycle, RuntimeFixture,
    "Циклическая зависимость должна быть обнаружена до регистрации пакетов.")
{
    fixture.packageManager.scanDirectory(
        "Lattice/tests/TestPackages/CycleA"
    );

    fixture.packageManager.scanDirectory(
        "Lattice/tests/TestPackages/CycleB"
    );

    fixture.packageManager.checkCandidates();

    auto* a = fixture.packageManager.findCandidate("CycleA");
    auto* b = fixture.packageManager.findCandidate("CycleB");

    REQUIRE(a);
    REQUIRE(b);

    REQUIRE(a->status == LoadStatus::DependencyCycle);
    REQUIRE(b->status == LoadStatus::DependencyCycle);

    REQUIRE(fixture.packageManager.queue().empty());
}

TEST(Package_Provider, RuntimeFixture,
    "Пакет, предоставляющий API или компонент, должен успешно загрузиться.")
{
    fixture.packageManager.scanDirectory(
        "Lattice/tests/TestPackages/Provider"
    );

    fixture.packageManager.checkCandidates();
    fixture.packageManager.loadCandidates();

    auto* package = fixture.packageManager.findCandidate("Provider");

    REQUIRE(package);
    REQUIRE(package->status == LoadStatus::Loaded);
    REQUIRE(package->library != nullptr);
}

TEST(Package_Consumer, RuntimeFixture,
    "Пакет с корректной зависимостью должен успешно загрузиться вместе с ней.")
{
    fixture.packageManager.scanDirectory(
        "Lattice/tests/TestPackages/Provider"
    );

    fixture.packageManager.scanDirectory(
        "Lattice/tests/TestPackages/Consumer"
    );

    fixture.packageManager.checkCandidates();
    fixture.packageManager.loadCandidates();

    auto* provider = fixture.packageManager.findCandidate("Provider");
    auto* consumer = fixture.packageManager.findCandidate("Consumer");

    REQUIRE(provider);
    REQUIRE(consumer);

    REQUIRE(provider->status == LoadStatus::Loaded);
    REQUIRE(consumer->status == LoadStatus::Loaded);

    REQUIRE(provider->library != nullptr);
    REQUIRE(consumer->library != nullptr);
}

TEST(Package_NoCompose, RuntimeFixture,
    "Пакет без compose должен завершиться ошибкой загрузки.")
{
    fixture.packageManager.scanDirectory(
        "Lattice/tests/TestPackages/NoCompose"
    );

    fixture.packageManager.checkCandidates();
    fixture.packageManager.loadCandidates();

    auto* package = fixture.packageManager.findCandidate("NoCompose");

    REQUIRE(package);
    REQUIRE(package->status == LoadStatus::Failed);
}

TEST(Package_CheckCandidatesTwice, RuntimeFixture,
    "Повторная проверка пакетов не должна дублировать очередь загрузки.")
{
    fixture.packageManager.scanDirectory(
        "Lattice/tests/TestPackages/Provider"
    );

    fixture.packageManager.scanDirectory(
        "Lattice/tests/TestPackages/Consumer"
    );

    fixture.packageManager.checkCandidates();

    const auto firstSize = fixture.packageManager.queue().size();

    fixture.packageManager.checkCandidates();

    const auto secondSize = fixture.packageManager.queue().size();

    REQUIRE(firstSize == secondSize);
}

TEST(Package_LoadEmptyOnlyOnce, RuntimeFixture,
    "Повторная загрузка уже загруженного пакета не должна создавать вторую библиотеку.")
{
    fixture.packageManager.scanDirectory(
        "Lattice/tests/TestPackages/Empty"
    );

    fixture.packageManager.checkCandidates();
    fixture.packageManager.loadCandidates();

    auto* package = fixture.packageManager.findCandidate("Empty");

    REQUIRE(package);
    REQUIRE(package->status == LoadStatus::Loaded);

    auto* library = package->library;

    fixture.packageManager.loadCandidates();

    REQUIRE(package->status == LoadStatus::Loaded);
    REQUIRE(package->library == library);
}

TEST(Package_DependencyLoad, RuntimeFixture,
    "Зависимость должна быть загружена до пакета, который от неё зависит.")
{
    fixture.packageManager.scanDirectory(
        "Lattice/tests/TestPackages/Provider"
    );

    fixture.packageManager.scanDirectory(
        "Lattice/tests/TestPackages/Consumer"
    );

    fixture.packageManager.checkCandidates();
    fixture.packageManager.loadCandidates();

    auto* provider = fixture.packageManager.findCandidate("Provider");
    auto* consumer = fixture.packageManager.findCandidate("Consumer");

    REQUIRE(provider);
    REQUIRE(consumer);

    REQUIRE(provider->status == LoadStatus::Loaded);
    REQUIRE(consumer->status == LoadStatus::Loaded);

    REQUIRE(provider->library != nullptr);
    REQUIRE(consumer->library != nullptr);

    REQUIRE(fixture.packageManager.queue()[0] == provider);
    REQUIRE(fixture.packageManager.queue()[1] == consumer);
}


TEST(Package_LibraryWithoutMetadataIsSkipped, RuntimeFixture,
    "Библиотека без lattice_package не считается пакетом.")
{
    fixture.packageManager.scanDirectory("Lattice/tests/TestPackages/NoMetadata");
    REQUIRE(fixture.packageManager.findCandidate("NoMetadata") == nullptr);
}

TEST(Package_MetadataIsTyped, RuntimeFixture,
    "Метаданные пакета читаются из скомпилированного объекта.")
{
    fixture.packageManager.scanDirectory("Lattice/tests/TestPackages/Consumer");
    auto* consumer = fixture.packageManager.findCandidate("Consumer");
    REQUIRE(consumer);
    REQUIRE(consumer->manifest.version == (Version{1, 0, 0}));
    REQUIRE(consumer->manifest.kernelApi == kernelApi);
    REQUIRE(consumer->manifest.dependencies.size() == 1);
    REQUIRE(consumer->manifest.dependencies[0].id == "Provider");
    REQUIRE(consumer->manifest.dependencies[0].requirement.contains(Version{1, 2, 0}));
    REQUIRE(!consumer->manifest.dependencies[0].requirement.contains(Version{0, 9, 0}));
}

}