#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace pdf {
using Bytes = std::vector<std::uint8_t>;
inline void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
inline Bytes read_file(const std::string &path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    check(bool(f), "Não foi possível abrir: " + path);
    const auto size = f.tellg();
    check(size >= 0 && size <= 512 * 1024 * 1024, "Arquivo vazio/inválido ou maior que 512 MiB");
    Bytes bytes(static_cast<std::size_t>(size));
    f.seekg(0);
    check(bool(f.read(reinterpret_cast<char *>(bytes.data()), size)), "Falha ao ler: " + path);
    return bytes;
}
inline std::string_view view(const Bytes &b) {
    return {reinterpret_cast<const char *>(b.data()), b.size()};
}
struct Point {
    double x = 0, y = 0;
};
struct Matrix {
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
    Point operator()(Point p) const {
        return {a * p.x + c * p.y + e, b * p.x + d * p.y + f};
    }
    Matrix operator*(const Matrix &r) const {
        return {a * r.a + c * r.b, b * r.a + d * r.b,     a * r.c + c * r.d,
                b * r.c + d * r.d, a * r.e + c * r.f + e, b * r.e + d * r.f + f};
    }
};
using Contour = std::vector<Point>;
using Path = std::vector<Contour>;
struct Bitmap {
    int width = 0, height = 0;
    std::vector<std::uint32_t> pixels; // 0x00RRGGBB
    Bitmap() = default;
    Bitmap(int w, int h, std::uint32_t color = 0xffffff) : width(w), height(h) {
        check(w > 0 && h > 0 && static_cast<std::uint64_t>(w) * h <= 64 * 1024 * 1024,
              "Bitmap excede o limite de 64 milhões de pixels");
        pixels.assign(static_cast<std::size_t>(w) * h, color);
    }
    void save_ppm(const std::string &path) const {
        std::ofstream out(path, std::ios::binary);
        check(bool(out), "Não foi possível criar: " + path);
        out << "P6\n" << width << ' ' << height << "\n255\n";
        for (auto p : pixels) {
            const char rgb[] = {char(p >> 16), char(p >> 8), char(p)};
            out.write(rgb, 3);
        }
        check(bool(out), "Falha ao gravar imagem");
    }
};
} // namespace pdf
