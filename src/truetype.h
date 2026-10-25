#pragma once
#include "common.h"
#include <map>

namespace pdf {
// Contornos simples/compostos TrueType. Não executa bytecode de hinting.
class TrueType {
    struct Vertex {
        double x, y;
        bool on;
    };
    using Outline = std::vector<std::vector<Vertex>>;
    Bytes bytes_;
    std::size_t glyf_ = 0, loca_ = 0, glyf_size_ = 0;
    int units_ = 0, count_ = 0;
    bool long_offsets_ = false;
    std::map<int, Path> cache_;
    unsigned u16(std::size_t p) const;
    int s16(std::size_t p) const;
    std::uint32_t u32(std::size_t p) const;
    Outline outline(int glyph, int depth) const;

  public:
    explicit TrueType(Bytes bytes);
    int units() const {
        return units_;
    }
    const Path &glyph(int id);
};
} // namespace pdf
