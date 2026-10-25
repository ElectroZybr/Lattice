#include <Lattice/tools/Tests.hpp>

int main() { return Lattice::Test::instance().runAll() == 0 ? 0 : 1; }
