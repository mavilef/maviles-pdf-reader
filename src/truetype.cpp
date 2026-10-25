#include "truetype.h"

namespace pdf {
unsigned TrueType::u16(std::size_t p) const {
    check(p < bytes_.size() && bytes_.size() - p >= 2, "Fonte TrueType truncada");
    return bytes_[p] * 256u + bytes_[p + 1];
}
int TrueType::s16(std::size_t p) const {
    return static_cast<std::int16_t>(u16(p));
}
std::uint32_t TrueType::u32(std::size_t p) const {
    return u16(p) * 65536u + u16(p + 2);
}
TrueType::TrueType(Bytes bytes) : bytes_(std::move(bytes)) {
    check(u32(0) == 0x10000, "Formato de fonte não suportado (esperado TrueType)");
    std::map<std::string, std::pair<std::size_t, std::size_t>> tables;
    const unsigned n = u16(4);
    check(12 + n * 16 <= bytes_.size(), "Diretório TrueType inválido");
    for (unsigned i = 0; i < n; ++i) {
        const auto p = 12 + i * 16;
        const auto off = u32(p + 8), length = u32(p + 12);
        check(off <= bytes_.size() && length <= bytes_.size() - off,
              "Tabela TrueType fora da fonte");
        tables[std::string(view(bytes_).substr(p, 4))] = {off, length};
    }
    for (const auto *tag : {"head", "maxp", "glyf", "loca"})
        check(tables.count(tag), std::string("Fonte sem tabela ") + tag);
    units_ = u16(tables.at("head").first + 18);
    check(units_ >= 16 && units_ <= 16384, "Escala TrueType inválida");
    long_offsets_ = s16(tables.at("head").first + 50) == 1;
    count_ = u16(tables.at("maxp").first + 4);
    glyf_ = tables.at("glyf").first;
    glyf_size_ = tables.at("glyf").second;
    loca_ = tables.at("loca").first;
    check(tables.at("loca").second >=
              static_cast<std::size_t>(count_ + 1) * (long_offsets_ ? 4 : 2),
          "Tabela loca truncada");
}
TrueType::Outline TrueType::outline(int glyph, int depth) const {
    check(depth < 24 && glyph >= 0 && glyph < count_, "Glifo inválido ou composto circular");
    auto offset = [&](int id) -> std::size_t {
        return long_offsets_ ? u32(loca_ + id * 4) : u16(loca_ + id * 2) * 2;
    };
    const auto start = offset(glyph), end = offset(glyph + 1);
    check(start <= end && end <= glyf_size_, "Offset de glifo inválido");
    if (start == end)
        return {};
    auto p = glyf_ + start;
    const auto limit = glyf_ + end;
    auto byte = [&]() {
        check(p < limit, "Glifo truncado");
        return bytes_[p++];
    };
    auto word = [&]() {
        const unsigned a = byte();
        return a * 256u + byte();
    };
    auto signed_word = [&]() { return static_cast<std::int16_t>(word()); };
    const int contours = signed_word();
    for (int i = 0; i < 8; ++i)
        byte(); // bounding box
    Outline result;
    if (contours >= 0) {
        check(contours <= 4096, "Contornos de glifo excedidos");
        std::vector<unsigned> ends;
        for (int i = 0; i < contours; ++i) {
            const unsigned v = word();
            check(ends.empty() || v > ends.back(), "Contornos TrueType inválidos");
            ends.push_back(v);
        }
        const auto instructions = word();
        check(instructions <= limit - p, "Instruções TrueType truncadas");
        p += instructions;
        const unsigned n = ends.empty() ? 0 : ends.back() + 1;
        std::vector<unsigned> flags;
        while (flags.size() < n) {
            const unsigned f = byte();
            const unsigned repeat = (f & 8) ? byte() : 0;
            check(flags.size() + repeat + 1 <= n, "Flags TrueType inválidos");
            flags.insert(flags.end(), repeat + 1, f);
        }
        std::vector<Vertex> points(n);
        int x = 0, y = 0;
        for (unsigned i = 0; i < n; ++i) {
            const unsigned f = flags[i];
            x += (f & 2) ? ((f & 16) ? int(byte()) : -int(byte())) : ((f & 16) ? 0 : signed_word());
            points[i] = {double(x), 0, bool(f & 1)};
        }
        for (unsigned i = 0; i < n; ++i) {
            const unsigned f = flags[i];
            y += (f & 4) ? ((f & 32) ? int(byte()) : -int(byte())) : ((f & 32) ? 0 : signed_word());
            points[i].y = y;
        }
        unsigned begin = 0;
        for (auto stop : ends) {
            result.emplace_back(points.begin() + begin, points.begin() + stop + 1);
            begin = stop + 1;
        }
    } else {
        unsigned flags;
        int components = 0;
        do {
            check(++components <= 256, "Componentes TrueType excedidos");
            flags = word();
            const int id = word();
            int arg1, arg2;
            if (flags & 1) {
                arg1 = word();
                arg2 = word();
                if (flags & 2) {
                    arg1 = static_cast<std::int16_t>(arg1);
                    arg2 = static_cast<std::int16_t>(arg2);
                }
            } else {
                arg1 = byte();
                arg2 = byte();
                if (flags & 2) {
                    arg1 = static_cast<std::int8_t>(arg1);
                    arg2 = static_cast<std::int8_t>(arg2);
                }
            }
            Matrix m;
            auto fraction = [&]() { return signed_word() / 16384.0; };
            if (flags & 8)
                m.a = m.d = fraction();
            else if (flags & 64) {
                m.a = fraction();
                m.d = fraction();
            } else if (flags & 128) {
                m.a = fraction();
                m.b = fraction();
                m.c = fraction();
                m.d = fraction();
            }
            auto part = outline(id, depth + 1);
            for (auto &contour : part)
                for (auto &v : contour) {
                    const auto q = m({v.x, v.y});
                    v.x = q.x;
                    v.y = q.y;
                }
            Point translation;
            if (flags & 2) {
                translation = {double(arg1), double(arg2)};
                if (flags & 0x800)
                    translation = m(translation);
            } else {
                auto point_at = [](const Outline &o, int index) {
                    for (const auto &c : o) {
                        if (index < static_cast<int>(c.size()))
                            return Point{c[index].x, c[index].y};
                        index -= static_cast<int>(c.size());
                    }
                    throw std::runtime_error("Ponto de glifo composto inválido");
                };
                const auto parent = point_at(result, arg1), child = point_at(part, arg2);
                translation = {parent.x - child.x, parent.y - child.y};
            }
            for (auto &contour : part) {
                for (auto &v : contour) {
                    v.x += translation.x;
                    v.y += translation.y;
                }
                result.push_back(std::move(contour));
            }
        } while (flags & 32);
    }
    return result;
}
const Path &TrueType::glyph(int id) {
    const auto found = cache_.find(id);
    if (found != cache_.end())
        return found->second;
    Path path;
    for (const auto &contour : outline(id, 0)) {
        if (contour.empty())
            continue;
        std::vector<Vertex> expanded;
        for (std::size_t i = 0; i < contour.size(); ++i) {
            auto p = contour[i], q = contour[(i + 1) % contour.size()];
            expanded.push_back(p);
            if (!p.on && !q.on)
                expanded.push_back({(p.x + q.x) / 2, (p.y + q.y) / 2, true});
        }
        auto first =
            std::find_if(expanded.begin(), expanded.end(), [](const Vertex &p) { return p.on; });
        check(first != expanded.end(), "Contorno TrueType sem ponto inicial");
        std::rotate(expanded.begin(), first, expanded.end());
        Contour points{{expanded[0].x, expanded[0].y}};
        for (std::size_t i = 1; i <= expanded.size();) {
            const auto v = expanded[i % expanded.size()];
            if (v.on) {
                points.push_back({v.x, v.y});
                ++i;
            } else {
                const auto end = expanded[(i + 1) % expanded.size()];
                const auto start = points.back();
                for (int step = 1; step <= 8; ++step) {
                    const double t = step / 8.0, s = 1 - t;
                    points.push_back({s * s * start.x + 2 * s * t * v.x + t * t * end.x,
                                      s * s * start.y + 2 * s * t * v.y + t * t * end.y});
                }
                i += 2;
            }
        }
        path.push_back(std::move(points));
    }
    return cache_.emplace(id, std::move(path)).first->second;
}
} // namespace pdf
