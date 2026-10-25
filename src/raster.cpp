#include "raster.h"

namespace pdf {
void Raster::paint(std::size_t p, std::uint32_t color, const Clip &clip, unsigned alpha) {
    if (clip.mask && !(*clip.mask)[p])
        return;
    if (clip.softmask)
        alpha = alpha * (*clip.softmask)[p] / 255;
    if (!alpha)
        return;
    const auto old = bitmap.pixels[p];
    unsigned out = 0;
    for (int shift : {0, 8, 16}) {
        unsigned c = (color >> shift) & 255, b = (old >> shift) & 255;
        if (clip.multiply)
            c = c * b / 255;
        out |= ((c * alpha + b * (255 - alpha) + 127) / 255) << shift;
    }
    bitmap.pixels[p] = out;
}
void Raster::fill(const Path &path, std::uint32_t color, const Clip &clip, bool even_odd) {
    double top = clip.bottom, bottom = clip.top;
    for (const auto &contour : path)
        for (const auto &p : contour) {
            check(std::isfinite(p.x) && std::isfinite(p.y), "Coordenada não finita");
            top = std::min(top, p.y);
            bottom = std::max(bottom, p.y);
        }
    const int y0 = static_cast<int>(std::clamp(std::floor(top), std::max(0.0, clip.top),
                                               std::min(double(bitmap.height), clip.bottom)));
    const int y1 = static_cast<int>(std::clamp(std::ceil(bottom), std::max(0.0, clip.top),
                                               std::min(double(bitmap.height), clip.bottom)));
    std::vector<std::pair<double, int>> crossings;
    for (int y = y0; y < y1; ++y) {
        crossings.clear();
        const double scan = y + 0.5;
        for (const auto &contour : path)
            for (std::size_t i = 0; i < contour.size(); ++i) {
                const Point a = contour[i], b = contour[(i + 1) % contour.size()];
                if ((a.y <= scan && b.y > scan) || (b.y <= scan && a.y > scan))
                    crossings.emplace_back(a.x + (scan - a.y) * (b.x - a.x) / (b.y - a.y),
                                           b.y > a.y ? 1 : -1);
            }
        std::sort(crossings.begin(), crossings.end());
        int winding = 0;
        double start = 0;
        for (const auto &crossing : crossings) {
            const bool was_inside = even_odd ? bool(winding & 1) : winding != 0;
            winding += crossing.second;
            const bool inside = even_odd ? bool(winding & 1) : winding != 0;
            if (!was_inside && inside)
                start = crossing.first;
            if (was_inside && !inside) {
                const int left =
                    static_cast<int>(std::clamp(std::ceil(start - 0.5), std::max(0.0, clip.left),
                                                std::min(double(bitmap.width), clip.right)));
                const int right = static_cast<int>(
                    std::clamp(std::ceil(crossing.first - 0.5), std::max(0.0, clip.left),
                               std::min(double(bitmap.width), clip.right)));
                for (int x = left; x < right; ++x) {
                    const auto p = y * bitmap.width + x;
                    paint(p, color, clip);
                }
            }
        }
    }
}
Clip Raster::intersect(const Path &path, const Clip &clip, bool even_odd) {
    Raster mask(bitmap.width, bitmap.height);
    Clip geometry = clip;
    geometry.softmask.reset();
    geometry.multiply = false;
    mask.fill(path, 0, geometry, even_odd);
    Clip result = clip;
    result.mask = std::make_shared<Bytes>(bitmap.pixels.size());
    for (std::size_t i = 0; i < bitmap.pixels.size(); ++i)
        (*result.mask)[i] = mask.bitmap.pixels[i] == 0;
    return result;
}
void Raster::stroke(const Path &path, double width, std::uint32_t color, const Clip &clip, int cap,
                    int join_style, double miter, const std::vector<double> &dash, double phase) {
    if (!dash.empty()) {
        auto pattern = dash;
        if (pattern.size() % 2)
            pattern.insert(pattern.end(), dash.begin(), dash.end());
        double sum = 0;
        for (double n : pattern) {
            check(std::isfinite(n) && n >= 0, "Traço inválido");
            sum += n;
        }
        check(sum > 0 && std::isfinite(sum) && std::isfinite(phase), "Padrão de traço inválido");
        Path pieces;
        int budget = 1000000;
        for (const auto &contour : path) {
            std::size_t part = 0;
            double off = std::fmod(std::fmod(phase, sum) + sum, sum);
            while (off >= pattern[part]) {
                off -= pattern[part];
                part = (part + 1) % pattern.size();
            }
            double remaining = pattern[part] - off;
            for (std::size_t i = 1; i < contour.size(); ++i) {
                auto a = contour[i - 1], b = contour[i];
                double len = std::hypot(b.x - a.x, b.y - a.y), pos = 0;
                while (pos < len) {
                    check(--budget > 0, "Traços excedidos");
                    if (remaining <= 0) {
                        part = (part + 1) % pattern.size();
                        remaining = pattern[part];
                        continue;
                    }
                    double step = std::min(remaining, len - pos);
                    Point first{a.x + (b.x - a.x) * pos / len, a.y + (b.y - a.y) * pos / len},
                        last{a.x + (b.x - a.x) * (pos + step) / len,
                             a.y + (b.y - a.y) * (pos + step) / len};
                    if (part % 2 == 0) {
                        if (pieces.empty() || pieces.back().back().x != first.x ||
                            pieces.back().back().y != first.y)
                            pieces.push_back({first});
                        pieces.back().push_back(last);
                    }
                    pos += step;
                    remaining -= step;
                }
            }
        }
        stroke(pieces, width, color, clip, cap, join_style, miter);
        return;
    }
    width = std::max(1.0, width);
    auto circle = [&](Point p) {
        Contour c;
        for (int i = 0; i < 32; ++i) {
            double a = i * 6.283185307179586 / 32;
            c.push_back({p.x + width / 2 * std::cos(a), p.y + width / 2 * std::sin(a)});
        }
        fill({c}, color, clip);
    };
    for (const auto &contour : path) {
        if (contour.size() < 2)
            continue;
        bool closed =
            contour.front().x == contour.back().x && contour.front().y == contour.back().y;
        if (cap == 1 && !closed) {
            circle(contour.front());
            circle(contour.back());
        }
        if (join_style == 1)
            for (std::size_t i = 1; i + 1 < contour.size(); ++i)
                circle(contour[i]);
    }
    for (const auto &contour : path)
        for (std::size_t i = 1; i < contour.size(); ++i) {
            auto a = contour[i - 1], b = contour[i];
            const double length = std::hypot(b.x - a.x, b.y - a.y);
            if (!length)
                continue;
            const double dx = -(b.y - a.y) * width / (2 * length),
                         dy = (b.x - a.x) * width / (2 * length);
            if (cap == 2 &&
                (contour.front().x != contour.back().x || contour.front().y != contour.back().y)) {
                const double ex = (b.x - a.x) * width / (2 * length),
                             ey = (b.y - a.y) * width / (2 * length);
                if (i == 1) {
                    a.x -= ex;
                    a.y -= ey;
                }
                if (i + 1 == contour.size()) {
                    b.x += ex;
                    b.y += ey;
                }
            }
            fill({{{a.x + dx, a.y + dy},
                   {b.x + dx, b.y + dy},
                   {b.x - dx, b.y - dy},
                   {a.x - dx, a.y - dy}}},
                 color, clip);
        }
    // Junções miter com limite 4; acima do limite, usa bevel.
    for (const auto &contour : path)
        for (std::size_t i = 1; i + 1 < contour.size(); ++i) {
            const auto a = contour[i - 1], p = contour[i], b = contour[i + 1];
            const double before = std::hypot(p.x - a.x, p.y - a.y),
                         after = std::hypot(b.x - p.x, b.y - p.y);
            if (before < 1e-12 || after < 1e-12)
                continue;
            const Point n1{-(p.y - a.y) / before, (p.x - a.x) / before},
                n2{-(b.y - p.y) / after, (b.x - p.x) / after};
            const double denominator = 1 + n1.x * n2.x + n1.y * n2.y, half = width / 2;
            for (double side : {-1.0, 1.0}) {
                Contour join{p, {p.x + side * half * n1.x, p.y + side * half * n1.y}};
                if (denominator > 1e-12 && join_style == 0) {
                    const Point delta{side * half * (n1.x + n2.x) / denominator,
                                      side * half * (n1.y + n2.y) / denominator};
                    if (std::hypot(delta.x, delta.y) <= half * miter)
                        join.push_back({p.x + delta.x, p.y + delta.y});
                }
                join.push_back({p.x + side * half * n2.x, p.y + side * half * n2.y});
                fill({join}, color, clip);
            }
        }
}
void Raster::image(const Bitmap &source, const Matrix &transform, const Clip &clip,
                   const Bytes &alpha) {
    // transform mapeia o quadrado PDF [0,1]², cujo eixo Y sobe.
    const Matrix m = transform * Matrix{1.0 / source.width, 0, 0, -1.0 / source.height, 0, 1};
    const double determinant = m.a * m.d - m.b * m.c;
    if (std::abs(determinant) < 1e-20)
        return;
    const Matrix inverse{m.d / determinant,
                         -m.b / determinant,
                         -m.c / determinant,
                         m.a / determinant,
                         (m.c * m.f - m.d * m.e) / determinant,
                         (m.b * m.e - m.a * m.f) / determinant};
    const Point corners[] = {transform({0, 0}), transform({1, 0}), transform({0, 1}),
                             transform({1, 1})};
    double x0 = clip.right, y0 = clip.bottom, x1 = clip.left, y1 = clip.top;
    for (auto p : corners) {
        check(std::isfinite(p.x) && std::isfinite(p.y), "Matriz de imagem inválida");
        x0 = std::min(x0, p.x);
        y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x);
        y1 = std::max(y1, p.y);
    }
    const int left = static_cast<int>(std::clamp(std::floor(x0), clip.left, clip.right));
    const int right = static_cast<int>(std::clamp(std::ceil(x1), clip.left, clip.right));
    const int top = static_cast<int>(std::clamp(std::floor(y0), clip.top, clip.bottom));
    const int bottom = static_cast<int>(std::clamp(std::ceil(y1), clip.top, clip.bottom));
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x) {
            auto p = inverse({x + 0.5, y + 0.5});
            if (p.x < 0 || p.y < 0 || p.x >= source.width || p.y >= source.height)
                continue;
            const auto at = static_cast<int>(p.y) * source.width + static_cast<int>(p.x);
            paint(y * bitmap.width + x, source.pixels[at], clip,
                  alpha.empty() ? 255 : alpha.at(at));
        }
}
Bitmap Raster::downsample() const {
    Bitmap out(bitmap.width / 2, bitmap.height / 2);
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            const auto p = (2 * y) * bitmap.width + 2 * x;
            unsigned r = 0, g = 0, b = 0;
            for (auto i : {p, p + 1, p + bitmap.width, p + bitmap.width + 1}) {
                const auto color = bitmap.pixels[i];
                r += (color >> 16) & 255;
                g += (color >> 8) & 255;
                b += color & 255;
            }
            out.pixels[y * out.width + x] = ((r / 4) << 16) | ((g / 4) << 8) | (b / 4);
        }
    return out;
}
} // namespace pdf
