#include <algorithm>
#include <cmath>
#include <limits>

#include <lbm-runner/Analysis.hpp>
#include <lbm-runner/BigO.hpp>

namespace Lattice::Benchmarks {

std::vector<AnalysisResult> Analysis::run(
    std::span<const PointResult> points
) const {
    std::vector<AnalysisResult> results;
    results.reserve(requests_.size());

    for (const AnalysisRequest& request : requests_) {
        switch (request.type) {
            case AnalysisType::Growth:
                results.push_back({
                    .type = request.type,
                    .x = request.x,
                    .y = request.y,
                    .values = analyzeGrowth(points, request)
                });
                break;
            case AnalysisType::Correlation:
                results.push_back({
                    .type = request.type,
                    .x = request.x,
                    .y = request.y,
                    .values = analyzeCorrelation(points, request)
                });
                break;
            case AnalysisType::Affine:
                results.push_back({
                    .type = request.type,
                    .x = request.x,
                    .y = request.y,
                    .values = analyzeAffine(points, request)
                });
                break;
            case AnalysisType::Phases: {
                // Набор фаз — из схемы последней точки (она самая полная): время на
                // итерацию каждой фазы, стена и остаток.
                if (points.empty())
                    break;
                for (const StageResult& stage : points.back().stages)
                    for (const CapabilityMetrics& capability : stage.capabilities) {
                        if (capability.capability != request.y.capability)
                            continue;
                        const auto schema = capability.metrics.schema;
                        for (size_t i = 0; i < schema.size(); ++i) {
                            if (schema[i].unit != Unit::Nanoseconds)
                                continue;
                            const AnalysisRequest affine{
                                .type = AnalysisType::Affine,
                                .x = request.x,
                                .y = {
                                    .source = ValueSource::Metric,
                                    .capability = capability.capability,
                                    .name = schema[i].name,
                                    .unit = schema[i].unit,
                                    .index = i
                                }
                            };
                            results.push_back({
                                .type = AnalysisType::Affine,
                                .x = affine.x,
                                .y = affine.y,
                                .values = analyzeAffine(points, affine)
                            });
                        }
                    }
                break;
            }
            }
    }

    return results;
}

double Analysis::resolve(
    const PointResult& point,
    ValueRef ref
) {
    if (ref.source == ValueSource::Parameter) {
        switch (ref.index) {
            case static_cast<size_t>(Parameter::N):
                return static_cast<double>(point.n);
        }

        return std::numeric_limits<double>::quiet_NaN();
    }

    const CapabilityMetrics* found = nullptr;

    for (const StageResult& stage : point.stages) {
        for (const CapabilityMetrics& capability : stage.capabilities) {
            if (capability.capability != ref.capability)
                continue;

            if (found)
                return std::numeric_limits<double>::quiet_NaN();

            found = &capability;
        }
    }

    if (!found)
        return std::numeric_limits<double>::quiet_NaN();

    // По имени, если оно есть: у динамических схем (Phases) индекс метрики может
    // отличаться между точками.
    const auto& metrics = found->metrics;
    if (!ref.name.empty()) {
        for (size_t i = 0; i < metrics.schema.size() && i < metrics.values.size(); ++i)
            if (metrics.schema[i].name == ref.name)
                return metrics.values[i];
        return std::numeric_limits<double>::quiet_NaN();
    }

    if (ref.index >= metrics.values.size())
        return std::numeric_limits<double>::quiet_NaN();

    return metrics.values[ref.index];
}

Unit Analysis::resolveUnit(
    std::span<const PointResult> points,
    ValueRef ref
) {
    if (!ref.name.empty())
        return ref.unit;

    if (ref.source == ValueSource::Parameter)
        return Unit::None;

    for (const PointResult& point : points) {
        for (const StageResult& stage : point.stages) {
            for (const CapabilityMetrics& capability : stage.capabilities) {
                if (
                    capability.capability == ref.capability &&
                    ref.index < capability.metrics.schema.size()
                ) {
                    return capability.metrics.schema[ref.index].unit;
                }
            }
        }
    }

    return Unit::None;
}

std::vector<AnalysisValue> Analysis::analyzeGrowth(
    std::span<const PointResult> points,
    const AnalysisRequest& request
) {
    std::vector<ComplexityPoint> values;
    values.reserve(points.size());

    for (const PointResult& point : points) {
        const double x = resolve(point, request.x);
        const double y = resolve(point, request.y);

        if (!std::isfinite(x) || !std::isfinite(y))
            continue;

        values.push_back({
            .x = x,
            .y = y
        });
    }

    const BigOResult result = analyze(values);

    if (result.complexity == Complexity::Unknown)
        return {};

    return {
        {
            .name = {},
            .value = std::string(complexityName(result.complexity))
        },
        {
            .name = "k",
            .value = result.coefficient,
            .unit = resolveUnit(points, request.y)
        },
        {
            .name = "error",
            .value = result.error * 100.0,
            .unit = Unit::Percent
        }
    };
}

AffineFit fitAffine(std::span<const ComplexityPoint> points) {
    // Минимум Σ ((fixed + per·x − y) / y)²: точки по N различаются на порядки, и обычные
    // МНК подогнали бы только самые большие N.
    double sw = 0, sx = 0, sxx = 0, sy = 0, sxy = 0;
    size_t used = 0;
    for (const ComplexityPoint& point : points) {
        if (!(point.y > 0.0) || !std::isfinite(point.x) || !std::isfinite(point.y))
            continue;
        const double w = 1.0 / (point.y * point.y);
        sw += w;
        sx += w * point.x;
        sxx += w * point.x * point.x;
        sy += w * point.y;
        sxy += w * point.x * point.y;
        ++used;
    }
    if (used < 2)
        return {};
    const double det = sw * sxx - sx * sx;
    if (!(std::abs(det) > 0.0))
        return {};

    AffineFit fit{
        .fixed = (sxx * sy - sx * sxy) / det,
        .per = (sw * sxy - sx * sy) / det,
        .valid = true
    };
    // Время не бывает отрицательным: при шуме у одной части — подгонка без неё.
    if (fit.fixed < 0.0) {
        fit.fixed = 0.0;
        fit.per = sxx > 0.0 ? sxy / sxx : 0.0;
    }
    if (fit.per < 0.0) {
        fit.per = 0.0;
        fit.fixed = sy / sw;
    }
    double squared = 0;
    for (const ComplexityPoint& point : points) {
        if (!(point.y > 0.0))
            continue;
        const double relative = (fit.fixed + fit.per * point.x - point.y) / point.y;
        squared += relative * relative;
    }
    fit.error = std::sqrt(squared / static_cast<double>(used));
    return fit;
}

std::vector<AnalysisValue> Analysis::analyzeAffine(
    std::span<const PointResult> points,
    const AnalysisRequest& request
) {
    std::vector<ComplexityPoint> values;
    values.reserve(points.size());

    for (const PointResult& point : points) {
        const double x = resolve(point, request.x);
        const double y = resolve(point, request.y);

        if (std::isfinite(x) && std::isfinite(y))
            values.push_back({.x = x, .y = y});
    }

    const AffineFit fit = fitAffine(values);
    if (!fit.valid)
        return {};

    const Unit unit = resolveUnit(points, request.y);
    return {
        {.name = "fixed", .value = fit.fixed, .unit = unit},
        {.name = "per", .value = fit.per, .unit = unit},
        {.name = "error", .value = fit.error * 100.0, .unit = Unit::Percent}
    };
}

std::vector<AnalysisValue> Analysis::analyzeCorrelation(
    std::span<const PointResult> points,
    const AnalysisRequest& request
) {
    std::vector<ComplexityPoint> values;
    values.reserve(points.size());

    for (const PointResult& point : points) {
        const double x = resolve(point, request.x);
        const double y = resolve(point, request.y);

        if (!std::isfinite(x) || !std::isfinite(y))
            continue;

        values.push_back({
            .x = x,
            .y = y
        });
    }

    if (values.size() < 2)
        return {};

    double meanX = 0;
    double meanY = 0;

    for (const ComplexityPoint& value : values) {
        meanX += value.x;
        meanY += value.y;
    }

    const double count = static_cast<double>(values.size());
    meanX /= count;
    meanY /= count;

    double covariance = 0;
    double varianceX = 0;
    double varianceY = 0;

    for (const ComplexityPoint& value : values) {
        const double x = value.x - meanX;
        const double y = value.y - meanY;

        covariance += x * y;
        varianceX += x * x;
        varianceY += y * y;
    }

    const double divisor = std::sqrt(varianceX * varianceY);

    if (!(divisor > 0.0) || !std::isfinite(divisor))
        return {};

    return {
        {
            .name = "r",
            .value = std::clamp(covariance / divisor, -1.0, 1.0),
            .unit = Unit::Ratio
        },
        {
            .name = "samples",
            .value = static_cast<uint64_t>(values.size()),
            .unit = Unit::Count
        }
    };
}

}
