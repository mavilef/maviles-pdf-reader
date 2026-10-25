#include "jpeg.h"
#include <array>

namespace pdf {
namespace {
constexpr int zigzag[] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
                          12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
                          35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
                          58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};
struct Huffman {
    std::array<int, 17> first{}, count{}, offset{};
    std::vector<unsigned> symbols;
    bool present = false;
};
struct Component {
    int id = 0, h = 0, v = 0, quant = 0, dc_table = 0, ac_table = 0, previous = 0, stride = 0;
    Bytes samples;
};
class Decoder {
    std::string_view data_;
    std::size_t pos_ = 0;
    unsigned buffer_ = 0, remaining_ = 0;
    std::array<std::array<int, 64>, 4> quant_{};
    std::array<bool, 4> quant_present_{};
    std::array<Huffman, 4> dc_, ac_;
    std::vector<Component> components_;
    int width_ = 0, height_ = 0, max_h_ = 0, max_v_ = 0, restart_ = 0;
    int adobe_ = -1;
    unsigned byte() {
        check(pos_ < data_.size(), "JPEG truncado");
        return static_cast<unsigned char>(data_[pos_++]);
    }
    unsigned word() {
        const auto a = byte();
        return a * 256 + byte();
    }
    unsigned bits(int n) {
        unsigned result = 0;
        for (int i = 0; i < n; ++i) {
            if (!remaining_) {
                buffer_ = byte();
                if (buffer_ == 255)
                    check(byte() == 0, "Marcador inesperado no stream JPEG");
                remaining_ = 8;
            }
            result = (result << 1) | ((buffer_ >> --remaining_) & 1);
        }
        return result;
    }
    int symbol(const Huffman &tree) {
        check(tree.present, "JPEG sem tabela Huffman");
        int code = 0;
        for (int i = 1; i <= 16; ++i) {
            code = code * 2 + bits(1);
            const int index = code - tree.first[i];
            if (index >= 0 && index < tree.count[i])
                return tree.symbols.at(tree.offset[i] + index);
        }
        throw std::runtime_error("Código Huffman JPEG inválido");
    }
    int signed_bits(int n) {
        check(n >= 0 && n <= 16, "Magnitude JPEG inválida");
        if (!n)
            return 0;
        const int value = bits(n);
        return value < (1 << (n - 1)) ? value - ((1 << n) - 1) : value;
    }
    void block(Component &c, int bx, int by) {
        check(quant_present_[c.quant], "JPEG sem quantização");
        std::array<double, 64> coeff{};
        const int category = symbol(dc_[c.dc_table]);
        check(category <= 11, "DC JPEG inválido");
        c.previous += signed_bits(category);
        check(std::abs(c.previous) <= 32768, "DC JPEG excedido");
        coeff[0] = c.previous * quant_[c.quant][0];
        bool only_dc = true;
        for (int k = 1; k < 64;) {
            const int s = symbol(ac_[c.ac_table]);
            if (s == 0)
                break;
            if (s == 0xf0) {
                k += 16;
                check(k <= 64, "Run JPEG excedido");
                continue;
            }
            const int run = s >> 4, size = s & 15;
            k += run;
            check(k < 64 && size > 0 && size <= 10, "AC JPEG inválido");
            coeff[zigzag[k]] = signed_bits(size) * quant_[c.quant][zigzag[k]];
            ++k;
            only_dc = false;
        }
        static const auto basis = [] {
            std::array<std::array<double, 8>, 8> m{};
            for (int x = 0; x < 8; ++x)
                for (int u = 0; u < 8; ++u)
                    m[x][u] = 0.5 * (u == 0 ? std::sqrt(0.5) : 1.0) *
                              std::cos((2 * x + 1) * u * 3.14159265358979323846 / 16);
            return m;
        }();
        std::array<double, 64> temp{};
        if (!only_dc)
            for (int v = 0; v < 8; ++v)
                for (int x = 0; x < 8; ++x)
                    for (int u = 0; u < 8; ++u)
                        temp[v * 8 + x] += basis[x][u] * coeff[v * 8 + u];
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                double value = only_dc ? coeff[0] / 8 : 0;
                if (!only_dc)
                    for (int v = 0; v < 8; ++v)
                        value += basis[y][v] * temp[v * 8 + x];
                c.samples.at((by * 8 + y) * c.stride + bx * 8 + x) =
                    static_cast<std::uint8_t>(std::clamp(std::lround(value + 128), 0l, 255l));
            }
    }
    Bitmap scan(std::size_t end) {
        check(width_ > 0 && height_ > 0, "JPEG sem SOF0");
        const int n = byte();
        check(n == static_cast<int>(components_.size()),
              "JPEG com scans separados ainda não suportado");
        std::vector<int> order;
        for (int i = 0; i < n; ++i) {
            const int id = byte(), tables = byte();
            auto it = std::find_if(components_.begin(), components_.end(),
                                   [&](const Component &c) { return c.id == id; });
            check(it != components_.end() && (tables >> 4) < 4 && (tables & 15) < 4,
                  "Componente JPEG inválido");
            it->dc_table = tables >> 4;
            it->ac_table = tables & 15;
            const int index = static_cast<int>(it - components_.begin());
            check(std::find(order.begin(), order.end(), index) == order.end(),
                  "Componente JPEG repetido");
            order.push_back(index);
        }
        check(byte() == 0 && byte() == 63 && byte() == 0 && pos_ == end, "JPEG não sequencial");
        const int cols = (width_ + max_h_ * 8 - 1) / (max_h_ * 8),
                  rows = (height_ + max_v_ * 8 - 1) / (max_v_ * 8);
        for (auto &c : components_) {
            c.stride = cols * c.h * 8;
            const auto size = static_cast<std::size_t>(c.stride) * rows * c.v * 8;
            check(size <= 64 * 1024 * 1024, "Componente JPEG grande demais");
            c.samples.resize(size);
        }
        int mcu = 0, restart_index = 0;
        for (int y = 0; y < rows; ++y)
            for (int x = 0; x < cols; ++x, ++mcu) {
                if (restart_ && mcu && mcu % restart_ == 0) {
                    remaining_ = 0;
                    check(byte() == 255, "Restart JPEG ausente");
                    unsigned marker;
                    do {
                        marker = byte();
                    } while (marker == 255);
                    check(marker == static_cast<unsigned>(0xd0 + (restart_index++ % 8)),
                          "Restart JPEG fora de ordem");
                    for (auto &c : components_)
                        c.previous = 0;
                }
                for (int i : order) {
                    auto &c = components_[i];
                    for (int vy = 0; vy < c.v; ++vy)
                        for (int hx = 0; hx < c.h; ++hx)
                            block(c, x * c.h + hx, y * c.v + vy);
                }
            }
        remaining_ = 0;
        check(byte() == 255, "Fim de imagem JPEG ausente");
        unsigned end_marker;
        do {
            end_marker = byte();
        } while (end_marker == 255);
        check(end_marker == 0xd9, "JPEG com múltiplos scans ou fim inválido");
        Bitmap output(width_, height_);
        auto sample = [&](int i, int x, int y) {
            const auto &c = components_[i];
            return double(c.samples[(y * c.v / max_v_) * c.stride + x * c.h / max_h_]);
        };
        auto channel = [](double v) {
            return static_cast<unsigned>(std::clamp(std::lround(v), 0l, 255l));
        };
        for (int y = 0; y < height_; ++y)
            for (int x = 0; x < width_; ++x) {
                const double a = sample(0, x, y);
                double r = a, g = a, b = a;
                if (components_.size() == 3) {
                    const double cb = sample(1, x, y), cr = sample(2, x, y);
                    if (adobe_ == 0 || components_[0].id == 'R') {
                        g = cb;
                        b = cr;
                    } else {
                        r = a + 1.402 * (cr - 128);
                        g = a - 0.344136 * (cb - 128) - 0.714136 * (cr - 128);
                        b = a + 1.772 * (cb - 128);
                    }
                }
                if (components_.size() == 4) {
                    const double c1 = sample(1, x, y), c2 = sample(2, x, y), k = sample(3, x, y);
                    if (adobe_ == 2) {
                        r = (255 - std::clamp(a + 1.402 * (c2 - 128), 0.0, 255.0)) * k / 255;
                        g = (255 - std::clamp(a - 0.344136 * (c1 - 128) - 0.714136 * (c2 - 128),
                                              0.0, 255.0)) *
                            k / 255;
                        b = (255 - std::clamp(a + 1.772 * (c1 - 128), 0.0, 255.0)) * k / 255;
                    } else if (adobe_ == 0) {
                        r = a * k / 255;
                        g = c1 * k / 255;
                        b = c2 * k / 255;
                    } else {
                        r = (255 - a) * (255 - k) / 255;
                        g = (255 - c1) * (255 - k) / 255;
                        b = (255 - c2) * (255 - k) / 255;
                    }
                }
                output.pixels[y * width_ + x] = (channel(r) << 16) | (channel(g) << 8) | channel(b);
            }
        return output;
    }

  public:
    explicit Decoder(std::string_view data) : data_(data) {}
    Bitmap run() {
        check(word() == 0xffd8, "Imagem não é JPEG");
        for (;;) {
            check(byte() == 255, "Marcador JPEG inválido");
            unsigned marker;
            do {
                marker = byte();
            } while (marker == 255);
            check(marker != 0xd9, "JPEG sem imagem");
            const unsigned length = word();
            check(length >= 2 && length - 2 <= data_.size() - pos_, "Segmento JPEG truncado");
            const auto end = pos_ + length - 2;
            if (marker == 0xc0) {
                check(byte() == 8, "JPEG com precisão diferente de 8 bits");
                height_ = word();
                width_ = word();
                const int n = byte();
                check((n == 1 || n == 3 || n == 4) && width_ > 0 && height_ > 0,
                      "Componentes JPEG ou dimensões não suportadas");
                check(static_cast<std::uint64_t>(width_) * height_ <= 64 * 1024 * 1024,
                      "JPEG grande demais");
                check(components_.empty(), "JPEG com múltiplos frames");
                for (int i = 0; i < n; ++i) {
                    Component c;
                    c.id = byte();
                    const int sampling = byte();
                    c.h = sampling >> 4;
                    c.v = sampling & 15;
                    c.quant = byte();
                    check(c.h >= 1 && c.h <= 4 && c.v >= 1 && c.v <= 4 && c.quant < 4,
                          "Amostragem JPEG inválida");
                    max_h_ = std::max(max_h_, c.h);
                    max_v_ = std::max(max_v_, c.v);
                    components_.push_back(std::move(c));
                }
            } else if (marker == 0xdb) {
                while (pos_ < end) {
                    const unsigned info = byte(), precision = info >> 4, id = info & 15;
                    check(precision <= 1 && id < 4, "Quantização JPEG inválida");
                    for (int i = 0; i < 64; ++i)
                        quant_[id][zigzag[i]] = precision ? word() : byte();
                    quant_present_[id] = true;
                }
            } else if (marker == 0xc4) {
                while (pos_ < end) {
                    const unsigned info = byte(), kind = info >> 4, id = info & 15;
                    check(kind <= 1 && id < 4, "Tabela Huffman JPEG inválida");
                    auto &tree = kind ? ac_[id] : dc_[id];
                    tree = Huffman{};
                    int code = 0, offset = 0;
                    for (int i = 1; i <= 16; ++i) {
                        tree.count[i] = byte();
                        tree.first[i] = code;
                        tree.offset[i] = offset;
                        code += tree.count[i];
                        offset += tree.count[i];
                        check(code <= (1 << i), "Árvore JPEG excedida");
                        code *= 2;
                    }
                    check(offset <= 256, "Tabela JPEG grande demais");
                    for (int i = 0; i < offset; ++i)
                        tree.symbols.push_back(byte());
                    tree.present = true;
                }
            } else if (marker == 0xdd)
                restart_ = word();
            else if (marker == 0xda)
                return scan(end);
            else if (marker == 0xee && length >= 14 && data_.substr(pos_, 5) == "Adobe")
                adobe_ = static_cast<unsigned char>(data_[pos_ + 11]);
            else
                check(!(marker >= 0xc1 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 &&
                        marker != 0xcc),
                      "JPEG progressivo/não baseline ainda não suportado");
            check(pos_ <= end, "Segmento JPEG excedido");
            pos_ = end;
        }
    }
};
} // namespace
Bitmap decode_jpeg(std::string_view bytes) {
    return Decoder(bytes).run();
}
} // namespace pdf
