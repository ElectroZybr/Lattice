#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace Example {

// Ordinary library code: no Lattice headers, base classes or runtime handles.
struct Particle {
    double position = 0.0;
    double velocity = 2.0;
};

struct ParticleStorage {
    std::vector<Particle> particles{Particle{}};
};

struct ComputeDevice {
    explicit ComputeDevice(std::string name) : name(std::move(name)) {}
    std::string name;
    std::size_t submissions = 0;

    template<class Work>
    void submit(Work&& work) {
        std::forward<Work>(work)();
        ++submissions;
    }

};

struct ForceField {
    explicit ForceField(double acceleration) : acceleration(acceleration) {}
    double acceleration;
};

struct Sample {
    std::string run;
    double position;
};

struct Metrics {
    std::vector<Sample> samples;
};

class Integrator {
public:
    Integrator(ParticleStorage& storage, ComputeDevice& device, ForceField& field, double dt)
        : dt(dt), storage_(storage), device_(device), field_(field) {}

    void step() {
        device_.submit([this] {
            for (auto& particle : storage_.particles) {
                particle.velocity += field_.acceleration * dt;
                particle.position += particle.velocity * dt;
            }
        });
    }

    const ComputeDevice& device() const { return device_; }
    const ForceField& field() const { return field_; }
    double dt;

private:
    ParticleStorage& storage_;
    ComputeDevice& device_;
    ForceField& field_;
};

class Sampler {
public:
    Sampler(std::string name, ParticleStorage& storage, Metrics& metrics)
        : name_(std::move(name)), storage_(storage), metrics_(metrics) {}

    void record() {
        metrics_.samples.push_back({name_, storage_.particles.front().position});
    }
    const Metrics& metrics() const { return metrics_; }

private:
    std::string name_;
    ParticleStorage& storage_;
    Metrics& metrics_;
};

class Simulation {
public:
    Simulation(Integrator& integrator, Sampler& sampler)
        : integrator_(integrator), sampler_(sampler) {}
    void step() {
        integrator_.step();
        sampler_.record();
    }

private:
    Integrator& integrator_;
    Sampler& sampler_;
};

struct Logger {
    void write(const std::string&) {}
};

class Dashboard {
public:
    Dashboard(std::vector<Sampler*> samplers, Logger* logger)
        : samplers_(std::move(samplers)), logger_(logger) {}
    std::size_t sources() const { return samplers_.size(); }
    bool hasLogger() const { return logger_ != nullptr; }

private:
    std::vector<Sampler*> samplers_;
    Logger* logger_;
};

}
