#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include <BmRunner/Results.hpp>

#include "Repository.hpp"

namespace Lattice::BmHistory {

/// When a difference counts as a change.
struct Thresholds {
    double relative = 0.05;   // |ratio − 1| above this
    double alpha = 0.01;      // significance of the rank test
    double noisy = 0.10;      // coefficient of variation above which a series is noisy
};

double median(std::span<const double> values);
/// Coefficient of variation: standard deviation / mean (0 for fewer than two values).
double variation(std::span<const double> values);

/// Two-sided p-value of the Mann–Whitney U test: exact for small samples without ties,
/// normal approximation with the tie correction otherwise.
double mannWhitney(std::span<const double> a, std::span<const double> b);

/// Difference of B from A.
struct Shift {
    double ratio = 1;        // median(B) / median(A)
    double low = 1, high = 1;   // 95 % bootstrap interval of the ratio (= ratio without samples)
    double p = 1;            // rank test; 1 without samples
    bool noisy = false;      // a series varies more than Thresholds::noisy: the threshold widens
    bool significant = false;
};

/// From samples when both sides have at least two, else from the values alone (analysis
/// rows: fixed/per, N*) — then only the relative threshold decides.
Shift shift(double a, std::span<const double> samplesA, double b, std::span<const double> samplesB,
            const Thresholds& thresholds = {});

/// Higher is better for throughput (`.rate`, `bandwidth`), lower for everything else
/// (time, phases, fixed/per, error, crossover N).
bool higherIsBetter(std::string_view metric) noexcept;

struct Change {
    Benchmarks::ResultRow before;   // A
    Benchmarks::ResultRow after;    // B
    Shift shift;
    bool regression = false;        // significant and worse
    bool improvement = false;       // significant and better
};

/// Rows present in both tables, matched by key, with their shift; significant first,
/// then by the size of the change.
std::vector<Change> compareTables(const Benchmarks::ResultTable& a, const Benchmarks::ResultTable& b,
                                  const Thresholds& thresholds = {});

/// One point of a row's history: the value as measured by one run.
struct SeriesPoint {
    RunInfo run;
    double value = 0;
    std::vector<double> samples;
};

/// The history of one row (rowKey), only versions where it was measured anew.
std::vector<SeriesPoint> series(const std::vector<std::pair<RunInfo, Benchmarks::ResultTable>>& history,
                                std::string_view key);

/// A point where the series shifts: runs [0, index) differ from [index, …).
struct ChangePoint {
    size_t index = 0;
    Shift shift;     // window after against window before
};

/// Shifts between windows of `window` runs before and after each point (samples of a
/// window pooled), the strongest significant ones, at least a window apart.
std::vector<ChangePoint> changePoints(std::span<const SeriesPoint> points, const Thresholds& thresholds = {},
                                      size_t window = 3);

}
