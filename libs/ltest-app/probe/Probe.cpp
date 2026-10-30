// A suite for ltest's own tests: every outcome a suite can have.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include <lattice-test/Tests.hpp>

TEST(Probe_Pass, Lattice::Fixture) { std::printf("native stdout\n"); std::fprintf(stderr, "native stderr\n"); }
TEST(Probe_Fail, Lattice::Fixture) { REQUIRE(false, "expected probe failure"); }
TEST(Probe_Skip, Lattice::Fixture) { SKIP("adapter absent"); }
TEST(Probe_Crash, Lattice::Fixture) { std::fprintf(stderr, "about to abort\n"); std::abort(); }
TEST(Probe_Timeout, Lattice::Fixture) { std::this_thread::sleep_for(std::chrono::seconds(30)); }
