#include <lattice-test/Tests.hpp>
#include <stdexcept>

namespace {
int constructed = 0, destroyed = 0, continued = 0;
struct Tracked : Lattice::Fixture {
    Tracked() { ++constructed; }
    ~Tracked() override { ++destroyed; }
};
struct Broken : Lattice::Fixture {
    Broken() { throw std::runtime_error("fixture constructor failure"); }
};
TEST(Passing, Tracked) { REQUIRE(true); CHECK(true, "optional message"); }
TEST(CheckContinues, Tracked) { CHECK(false, "expected CHECK failure"); ++continued; }
TEST(RequireStops, Tracked) { REQUIRE(false, "expected REQUIRE failure"); ++continued; }
TEST(ExceptionIsFailure, Tracked) { throw std::runtime_error("expected exception"); }
TEST(ConstructorIsFailure, Broken) {}
TEST(ContinuesAfterFailures, Tracked) { ++continued; }
}

// Expected failures exercise the runner itself; success means all were accounted for.
int main() {
    const auto failures = Lattice::Test::instance().runAll();
    return failures == 4 && constructed == 5 && destroyed == 5 && continued == 2 ? 0 : 1;
}
