#include <lattice-test/Tests.hpp>

int main() { return Lattice::Test::instance().runAll() == 0 ? 0 : 1; }
