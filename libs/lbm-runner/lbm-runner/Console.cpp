#include "Console.hpp"
#include "Output.hpp"
#include <indicators/block_progress_bar.hpp>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <sstream>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace Lattice::Benchmarks::Output {
namespace {
bool terminal() {
#ifdef _WIN32
    const bool tty = _isatty(1) && _isatty(2);
#else
    const bool tty = isatty(1) && isatty(2);
#endif
    const char* term = std::getenv("TERM");
    return tty && (!term || std::string_view(term) != "dumb");
}
}

struct Console::State {
    Mode mode;
    size_t total, completed = 0, samples = 0;
    bool tty, finished = false, successful = false, previousLive;
    LogMode previousMode;
    LogSystem::ConsoleWriterId writer = 0;
    LogSystem::SinkId warnings = 0;
    std::mutex mutex;
    std::ostringstream buffer;
    indicators::BlockProgressBar bar;
    std::string name, detail;
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

    State(Mode mode, size_t total)
        : mode(mode), total(total), tty(terminal()), previousLive(liveState().enabled),
          previousMode(LogSystem::current().defaultMode()),
          bar(indicators::option::Stream{buffer}, indicators::option::BarWidth{20},
              indicators::option::MaxProgress{1000}, indicators::option::ShowPercentage{false}) {}

    void clear() { if (tty) std::cerr << "\r\033[2K" << std::flush; }
    void draw() {
        if (!tty || finished) return;
        const auto width = indicators::terminal_size().second;
        const size_t columns = width > 0 ? static_cast<size_t>(width) : 80;
        static constexpr std::array<std::string_view, 10> frames{
            "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"
        };
        const auto frame = successful ? std::string_view("✓") : frames[samples % frames.size()];
        const auto progress = successful ? 1000 : std::min<size_t>(999, completed * 1000 / total);
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - started).count();
        const auto time = std::format("{:02}:{:02}", elapsed / 60, elapsed % 60);
        const auto count = successful ? std::format("{}/{}", samples, samples) : std::to_string(samples);
        const auto counters = columns >= 70
            ? TextFormatter::format("<mut>benches </><light>{}/{}</><mut> · samples </><light>{}</><mut> · {}</>", completed, total, count, time)
            : TextFormatter::format("<mut>b </><light>{}/{}</><mut> s </><light>{}</><mut> {}</>", completed, total, count, time);
        const size_t overhead = unicode::display_width(counters.plain()) + 12;
        const size_t available = columns > overhead ? columns - overhead : 0;
        const size_t barWidth = std::min<size_t>(24, successful ? available : available / 2);
        const size_t used = overhead + barWidth;
        const size_t space = columns > used + 2 ? columns - used - 2 : 0;
        std::string label = successful ? "" : name + detail;
        // Preserve complete UTF-8 code points when shortening variant names.
        if (label.size() > space) {
            size_t cut = space > 3 ? space - 3 : 0;
            while (cut && (static_cast<unsigned char>(label[cut]) & 0xc0) == 0x80) --cut;
            label = cut ? label.substr(0, cut) + "..." : "";
        }
        bar.set_option(indicators::option::BarWidth{barWidth});
        buffer.str({}); buffer.clear();
        bar.set_progress(static_cast<float>(progress));
        auto text = buffer.str();
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
        if (!text.empty() && text.front() == '\r') text.erase(0, 1);
        auto line = TextFormatter::format("<{}>{} {}</> <light>{:>3}%</> ", successful ? "ok" : "a2", frame, text, progress / 10);
        line += counters;
        if (!label.empty()) line += TextFormatter::format(" <mut>{}</>", TextMarkup::escape(label));
        clear();
        std::cerr << line.render() << std::flush;
    }
};

Console::Console(Mode mode, size_t total) {
    if (mode == Mode::Text) return;
    state_ = std::make_unique<State>(mode, total);
    auto& s = *state_;
    liveState().enabled = false;
    Logger::setDefaultMode(LogMode::Verbose | LogMode::Gap);
    s.writer = LogSystem::setConsoleWriter([this](std::string_view text) {
        auto& s = *state_;
        std::lock_guard lock(s.mutex);
        if (s.mode == Mode::Progress) return;
        s.clear();
        std::cout << text << std::flush;
        s.draw();
    });
    if (mode == Mode::Progress) s.warnings = LogSystem::addSink([this](const LogEvent& event) {
        if (event.level != Level::Warning && event.level != Level::Error && event.level != Level::Exception) return;
        auto& s = *state_;
        std::lock_guard lock(s.mutex);
        s.clear();
        std::cerr << event.text.render() << '\n';
        s.draw();
    });
}
Console::~Console() {
    if (!state_) return;
    auto& s = *state_;
    if (s.warnings) LogSystem::removeSink(s.warnings);
    LogSystem::resetConsoleWriter(s.writer);
    Logger::setDefaultMode(s.previousMode);
    liveState().enabled = s.previousLive;
    if (!s.finished) s.clear();
}
void Console::start(std::string_view name) {
    if (!state_) return;
    auto& s = *state_;
    std::lock_guard lock(s.mutex);
    s.name = name; s.detail = " preparing/calibrating"; s.draw();
}
void Console::sample(const SampleResult& sample) {
    if (!state_) return;
    auto& s = *state_;
    std::lock_guard lock(s.mutex);
    ++s.samples;
    s.name = fullName(sample.group, sample.name);
    s.detail = std::format(" N={} {} {}/{}", sample.n, sample.stage, sample.sample,
                           sample.samples ? std::to_string(sample.samples) : "?");
    s.draw();
}
void Console::advance() {
    if (!state_) return;
    auto& s = *state_;
    std::lock_guard lock(s.mutex);
    ++s.completed;
    // Comparisons must succeed before displaying 100%.
    if (s.completed < s.total) s.draw();
}
void Console::finish() {
    if (!state_) return;
    auto& s = *state_;
    std::lock_guard lock(s.mutex);
    s.name = "completed"; s.detail.clear(); s.successful = true; s.draw(); s.finished = true;
    if (s.tty) std::cerr << '\n';
    else std::cout << TextFormatter::format("<ok>✓</> <mut>benches </><light>{}/{}</><mut> · samples </><light>{}/{}</>\n",
                                           s.completed, s.total, s.samples, s.samples).render();
}
}
