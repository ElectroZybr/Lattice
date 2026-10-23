#pragma once
#include <string_view>

namespace SlotExample {
// Ordinary application/library code: no Lattice headers or constructors.
struct Palette { int colors = 16; };
struct Renderer {
    virtual ~Renderer() = default;
    virtual std::string_view backend() const = 0;
    virtual int draw() = 0;
};
struct SoftwareRenderer : Renderer {
    Palette& palette;
    explicit SoftwareRenderer(Palette& palette) : palette(palette) {}
    std::string_view backend() const override { return "software"; }
    int draw() override { return palette.colors; }
};
struct UploadQueue {
    Palette& palette;
    explicit UploadQueue(Palette& palette) : palette(palette) {}
};
struct HardwareRenderer : Renderer {
    UploadQueue& queue;
    explicit HardwareRenderer(UploadQueue& queue) : queue(queue) {}
    std::string_view backend() const override { return "hardware"; }
    int draw() override { return queue.palette.colors * 2; }
};
struct Viewport {
    Renderer& renderer;
    explicit Viewport(Renderer& renderer) : renderer(renderer) {}
    int frame() { return renderer.draw(); }
};
}
