#pragma once
#include "common.h"
#include <memory>
namespace pdf {
struct Clip {
    double left, top, right, bottom;
    std::shared_ptr<Bytes> mask, softmask;
    bool multiply = false;
    Clip(double l = 0, double t = 0, double r = 0, double b = 0)
        : left(l), top(t), right(r), bottom(b) {}
};
class Raster {
  public:
    Bitmap bitmap;
    void paint(std::size_t p, std::uint32_t color, const Clip &clip, unsigned alpha = 255);
    explicit Raster(int width, int height) : bitmap(width, height) {}
    void fill(const Path &path, std::uint32_t color, const Clip &clip, bool even_odd = false);
    void stroke(const Path &path, double width, std::uint32_t color, const Clip &clip, int cap = 0,
                int join = 0, double miter = 4, const std::vector<double> &dash = {},
                double phase = 0);
    Clip intersect(const Path &path, const Clip &clip, bool even_odd);
    void image(const Bitmap &image, const Matrix &transform, const Clip &clip,
               const Bytes &alpha = {});
    Bitmap downsample() const;
};
} // namespace pdf
