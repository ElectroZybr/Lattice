#include <lbm-runner/Main.hpp>
#include <lbm-runner/Benchmarks.hpp>
#include <lattice-logger/Logger.hpp>
#include <chrono>
#include <stdexcept>
#include <thread>

using namespace Lattice;
using namespace Lattice::Benchmarks;
struct Probe : Fixture { explicit Probe(size_t) {} };
class ProbeMetric : public MetricCapability<"Value", ProbeMetric> {
public:
    inline static constexpr auto number = defineMetric("number", Unit::Count, MetricFlags::Live);
    inline static constexpr auto Schema = defineSchema(number);
    Metrics end() override { return {.schema = schema(), .values = {42}}; }
    Metrics result() override { return end(); }
};
BENCH_GROUP(ConsoleProbe) {
    bench.config.sizes = {1};
    bench.config.target = std::chrono::microseconds{0};
    bench.analysis.clear();
    bench.stages.clear();
    bench.stages.add<ProbeMetric>().samples(3);
}
BENCH_GROUPED(ConsoleProbe, PassFirst, Probe) {
    Logger::message("probe diagnostic");
    bench.measure<Probe>([](Probe&) { std::this_thread::sleep_for(std::chrono::milliseconds(60)); return 1; });
}
BENCH_GROUPED(ConsoleProbe, PassSecond, Probe) {
    Logger::warning("probe", "probe warning");
    bench.measure<Probe>([](Probe&) { return 2; });
}
BENCH_GROUPED(ConsoleProbe, WarmupCount, Probe) {
    bench.stages.clear();
    bench.stages.add<Warmup>().samples(2);
    bench.stages.add<ProbeMetric>().samples(3);
    bench.measure<Probe>([](Probe&) { return 3; });
}
BENCH_GROUPED(ConsoleProbe, Fail, Probe) { throw std::runtime_error("probe failure"); }
int main(int argc, char** argv) { return Lattice::Benchmarks::main(argc, argv); }
