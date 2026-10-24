#include "Statistics.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <random>

namespace Lattice::BmHistory {

double median(std::span<const double> values) {
    if (values.empty())
        return std::nan("");
    std::vector<double> sorted(values.begin(), values.end());
    std::ranges::sort(sorted);
    const size_t n = sorted.size();
    return n % 2 ? sorted[n / 2] : 0.5 * (sorted[n / 2 - 1] + sorted[n / 2]);
}

double variation(std::span<const double> values) {
    if (values.size() < 2)
        return 0;
    const double mean = std::accumulate(values.begin(), values.end(), 0.0) / double(values.size());
    if (mean == 0)
        return 0;
    double squared = 0;
    for (const double value : values)
        squared += (value - mean) * (value - mean);
    return std::sqrt(squared / double(values.size() - 1)) / std::abs(mean);
}

namespace {

/// Number of arrangements of n1 + n2 ranks with U = u, for all u (no ties).
std::vector<double> exactDistribution(size_t n1, size_t n2) {
    // counts[i][j][u]: i from A, j from B; built incrementally over j for memory.
    const size_t maxU = n1 * n2;
    std::vector<std::vector<double>> previous(n1 + 1, std::vector<double>(maxU + 1, 0.0));
    for (size_t i = 0; i <= n1; ++i)
        previous[i][0] = 1;   // j = 0: only U = 0
    for (size_t j = 1; j <= n2; ++j) {
        std::vector<std::vector<double>> current(n1 + 1, std::vector<double>(maxU + 1, 0.0));
        current[0][0] = 1;
        for (size_t i = 1; i <= n1; ++i)
            for (size_t u = 0; u <= maxU; ++u) {
                // The largest value is from A (adds j to U) or from B.
                double count = previous[i][u];
                if (u >= j)
                    count += current[i - 1][u - j];
                current[i][u] = count;
            }
        previous = std::move(current);
    }
    return previous[n1];
}

}

double mannWhitney(std::span<const double> a, std::span<const double> b) {
    const size_t n1 = a.size(), n2 = b.size();
    if (n1 < 2 || n2 < 2)
        return 1;

    struct Item { double value; bool first; };
    std::vector<Item> all;
    for (const double value : a) all.push_back({value, true});
    for (const double value : b) all.push_back({value, false});
    std::ranges::sort(all, {}, &Item::value);

    // Ranks with ties averaged.
    double rankSum = 0, tieTerm = 0;
    bool ties = false;
    for (size_t i = 0; i < all.size();) {
        size_t j = i;
        while (j < all.size() && all[j].value == all[i].value)
            ++j;
        const double rank = 0.5 * double(i + 1 + j);
        const double t = double(j - i);
        if (t > 1) {
            ties = true;
            tieTerm += t * t * t - t;
        }
        for (size_t k = i; k < j; ++k)
            if (all[k].first)
                rankSum += rank;
        i = j;
    }
    const double u = rankSum - double(n1 * (n1 + 1)) / 2;
    const double mean = double(n1 * n2) / 2;

    if (!ties && n1 * n2 <= 400) {
        const auto counts = exactDistribution(n1, n2);
        const double total = std::accumulate(counts.begin(), counts.end(), 0.0);
        const double low = std::min(u, double(n1 * n2) - u);
        double tail = 0;
        for (size_t k = 0; double(k) <= low + 1e-9; ++k)
            tail += counts[k];
        return std::min(1.0, 2 * tail / total);
    }

    const double n = double(n1 + n2);
    const double variance = double(n1 * n2) / 12 * ((n + 1) - tieTerm / (n * (n - 1)));
    if (variance <= 0)
        return 1;
    const double z = (std::abs(u - mean) - 0.5) / std::sqrt(variance);
    return std::min(1.0, std::erfc(std::max(0.0, z) / std::sqrt(2.0)));
}

Shift shift(double a, std::span<const double> samplesA, double b, std::span<const double> samplesB,
            const Thresholds& thresholds) {
    Shift result;
    const bool sampled = samplesA.size() >= 2 && samplesB.size() >= 2;
    const double baseA = sampled ? median(samplesA) : a;
    const double baseB = sampled ? median(samplesB) : b;
    if (!std::isfinite(baseA) || !std::isfinite(baseB))
        return result;
    if (baseA == 0)
        result.ratio = baseB == 0 ? 1 : INFINITY;
    else
        result.ratio = baseB / baseA;
    result.low = result.high = result.ratio;

    double threshold = thresholds.relative;
    if (sampled) {
        const double noise = std::max(variation(samplesA), variation(samplesB));
        result.noisy = noise > thresholds.noisy;
        if (result.noisy)
            threshold = std::max(threshold, 2 * noise);
        result.p = mannWhitney(samplesA, samplesB);

        // Bootstrap of the ratio of medians; fixed seed: the same data, the same answer.
        std::mt19937 random(12345);
        std::vector<double> ratios;
        std::vector<double> x(samplesA.size()), y(samplesB.size());
        std::uniform_int_distribution<size_t> pickA(0, samplesA.size() - 1), pickB(0, samplesB.size() - 1);
        for (int round = 0; round < 1000; ++round) {
            for (auto& value : x) value = samplesA[pickA(random)];
            for (auto& value : y) value = samplesB[pickB(random)];
            const double ma = median(x);
            if (ma != 0)
                ratios.push_back(median(y) / ma);
        }
        if (!ratios.empty()) {
            std::ranges::sort(ratios);
            result.low = ratios[size_t(0.025 * double(ratios.size() - 1))];
            result.high = ratios[size_t(0.975 * double(ratios.size() - 1))];
        }
        result.significant = result.p < thresholds.alpha && std::abs(result.ratio - 1) > threshold &&
                             (result.low > 1 || result.high < 1);
    } else {
        result.significant = std::abs(result.ratio - 1) > threshold;
    }
    return result;
}

bool higherIsBetter(std::string_view metric) noexcept {
    return metric.ends_with(".rate") || metric.find("bandwidth") != std::string_view::npos;
}

std::vector<Change> compareTables(const Benchmarks::ResultTable& a, const Benchmarks::ResultTable& b,
                                  const Thresholds& thresholds) {
    std::map<std::string, const Benchmarks::ResultRow*> before;
    for (const auto& row : a.rows)
        before.emplace(Benchmarks::rowKey(row), &row);

    std::vector<Change> changes;
    for (const auto& row : b.rows) {
        const auto it = before.find(Benchmarks::rowKey(row));
        if (it == before.end())
            continue;
        const auto& old = *it->second;
        Change change{.before = old, .after = row,
                      .shift = shift(old.value, old.samples, row.value, row.samples, thresholds)};
        // Ошибка подгонки — качество модели, а не скорость.
        if (change.shift.significant && !row.metric.ends_with(".error") && row.metric != "error") {
            const bool worse = higherIsBetter(row.metric) ? change.shift.ratio < 1 : change.shift.ratio > 1;
            change.regression = worse;
            change.improvement = !worse;
        }
        changes.push_back(std::move(change));
    }
    std::ranges::stable_sort(changes, [](const Change& x, const Change& y) {
        if (x.shift.significant != y.shift.significant)
            return x.shift.significant;
        return std::abs(std::log(x.shift.ratio)) > std::abs(std::log(y.shift.ratio));
    });
    return changes;
}

std::vector<SeriesPoint> series(const std::vector<std::pair<RunInfo, Benchmarks::ResultTable>>& history,
                                std::string_view key) {
    std::vector<SeriesPoint> points;
    for (const auto& [run, table] : history) {
        const std::string id = run.trailer("Run");
        for (const auto& row : table.rows)
            if (Benchmarks::rowKey(row) == key && (id.empty() || row.measured == id)) {
                points.push_back({.run = run, .value = row.value, .samples = row.samples});
                break;
            }
    }
    return points;
}

std::vector<ChangePoint> changePoints(std::span<const SeriesPoint> points, const Thresholds& thresholds,
                                      size_t window) {
    if (window == 0 || points.size() < 2 * window)
        return {};

    auto pooled = [&](size_t begin, size_t end, double& value) {
        std::vector<double> samples, values;
        for (size_t i = begin; i < end; ++i) {
            values.push_back(points[i].value);
            samples.insert(samples.end(), points[i].samples.begin(), points[i].samples.end());
        }
        value = median(values);
        // Without samples, the values of the window's runs are the samples.
        return samples.empty() ? values : samples;
    };

    std::vector<ChangePoint> candidates;
    for (size_t index = window; index + window <= points.size(); ++index) {
        double before = 0, after = 0;
        const auto a = pooled(index - window, index, before);
        const auto b = pooled(index, index + window, after);
        const Shift result = shift(before, a, after, b, thresholds);
        if (result.significant)
            candidates.push_back({index, result});
    }
    // The strongest first; then drop those within a window of a stronger one.
    std::ranges::sort(candidates, [](const ChangePoint& x, const ChangePoint& y) {
        return std::abs(std::log(x.shift.ratio)) > std::abs(std::log(y.shift.ratio));
    });
    std::vector<ChangePoint> chosen;
    for (const auto& candidate : candidates)
        if (std::ranges::none_of(chosen, [&](const ChangePoint& c) {
                return (candidate.index > c.index ? candidate.index - c.index : c.index - candidate.index) < window;
            }))
            chosen.push_back(candidate);
    std::ranges::sort(chosen, {}, &ChangePoint::index);
    return chosen;
}

}
