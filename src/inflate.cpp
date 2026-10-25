#include "inflate.h"
#include <array>

namespace pdf {
namespace {
class Bits {
    std::string_view input_;
    std::size_t bit_ = 0;

  public:
    explicit Bits(std::string_view input) : input_(input) {}
    unsigned get(unsigned n) {
        check(n <= 24 && bit_ + n <= input_.size() * 8, "DEFLATE truncado");
        unsigned value = 0;
        for (unsigned i = 0; i < n; ++i, ++bit_)
            value |= ((static_cast<unsigned char>(input_[bit_ / 8]) >> (bit_ % 8)) & 1u) << i;
        return value;
    }
    void align() {
        bit_ = (bit_ + 7) & ~std::size_t(7);
    }
};
struct Huffman {
    std::array<int, 16> count{};
    std::vector<int> symbols;
    explicit Huffman(const std::vector<int> &lengths) {
        for (int n : lengths) {
            check(n >= 0 && n <= 15, "Código Huffman inválido");
            ++count[n];
        }
        int available = 1;
        for (int n = 1; n <= 15; ++n) {
            available = available * 2 - count[n];
            check(available >= 0, "Árvore Huffman excedida");
        }
        std::array<int, 16> offset{};
        for (int n = 1; n < 15; ++n)
            offset[n + 1] = offset[n] + count[n];
        symbols.resize(lengths.size());
        for (std::size_t s = 0; s < lengths.size(); ++s)
            if (lengths[s])
                symbols[offset[lengths[s]]++] = static_cast<int>(s);
    }
    int decode(Bits &bits) const {
        int code = 0, first = 0, index = 0;
        for (int n = 1; n <= 15; ++n) {
            code = (code << 1) | bits.get(1);
            if (code - first < count[n])
                return symbols.at(index + code - first);
            index += count[n];
            first = (first + count[n]) << 1;
        }
        throw std::runtime_error("Código DEFLATE inválido");
    }
};
} // namespace
Bytes inflate(std::string_view input) {
    check(input.size() >= 6, "Stream zlib truncado");
    const auto cmf = static_cast<unsigned char>(input[0]);
    const auto flg = static_cast<unsigned char>(input[1]);
    check((cmf & 15) == 8 && (cmf >> 4) <= 7 && (cmf * 256 + flg) % 31 == 0 && !(flg & 32),
          "Cabeçalho zlib não suportado");
    Bits bits(input.substr(2, input.size() - 6));
    Bytes out;
    constexpr std::size_t limit = 64 * 1024 * 1024;
    auto append = [&](unsigned value) {
        check(out.size() < limit, "Stream descomprimido excede 64 MiB");
        out.push_back(static_cast<std::uint8_t>(value));
    };
    bool last;
    do {
        last = bits.get(1);
        const unsigned type = bits.get(2);
        if (type == 0) {
            bits.align();
            const unsigned n = bits.get(16), inverse = bits.get(16);
            check((n ^ inverse) == 65535, "Bloco DEFLATE inválido");
            for (unsigned i = 0; i < n; ++i)
                append(bits.get(8));
            continue;
        }
        check(type != 3, "Tipo de bloco DEFLATE reservado");
        std::vector<int> lit, dist;
        if (type == 1) {
            lit.resize(288);
            dist.assign(32, 5);
            for (int i = 0; i < 288; ++i)
                lit[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
        } else {
            const int nl = bits.get(5) + 257, nd = bits.get(5) + 1, nc = bits.get(4) + 4;
            check(nl <= 286, "Número inválido de códigos DEFLATE");
            constexpr int order[] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                     11, 4,  12, 3, 13, 2, 14, 1, 15};
            std::vector<int> sizes(19);
            for (int i = 0; i < nc; ++i)
                sizes[order[i]] = bits.get(3);
            const Huffman tree(sizes);
            std::vector<int> lengths;
            while (static_cast<int>(lengths.size()) < nl + nd) {
                const int symbol = tree.decode(bits);
                if (symbol < 16)
                    lengths.push_back(symbol);
                else {
                    check(symbol != 16 || !lengths.empty(),
                          "Repetição DEFLATE sem código anterior");
                    const int value = symbol == 16 ? lengths.back() : 0;
                    const int n = symbol == 16   ? bits.get(2) + 3
                                  : symbol == 17 ? bits.get(3) + 3
                                                 : bits.get(7) + 11;
                    check(lengths.size() + n <= static_cast<std::size_t>(nl + nd),
                          "Repetição DEFLATE excedida");
                    lengths.insert(lengths.end(), n, value);
                }
            }
            lit.assign(lengths.begin(), lengths.begin() + nl);
            dist.assign(lengths.begin() + nl, lengths.end());
        }
        check(lit[256] != 0, "DEFLATE sem fim de bloco");
        const Huffman literals(lit), distances(dist);
        constexpr int length_base[] = {3,  4,  5,  6,   7,   8,   9,   10,  11, 13,
                                       15, 17, 19, 23,  27,  31,  35,  43,  51, 59,
                                       67, 83, 99, 115, 131, 163, 195, 227, 258};
        constexpr int length_bits[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                       2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        constexpr int distance_base[] = {
            1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
            193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        constexpr int distance_bits[] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                         6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            const int symbol = literals.decode(bits);
            if (symbol == 256)
                break;
            if (symbol < 256) {
                append(symbol);
                continue;
            }
            check(symbol <= 285, "Comprimento DEFLATE inválido");
            const int n = length_base[symbol - 257] + bits.get(length_bits[symbol - 257]);
            const int d = distances.decode(bits);
            check(d < 30, "Distância DEFLATE inválida");
            const std::size_t distance = distance_base[d] + bits.get(distance_bits[d]);
            check(distance <= out.size(), "Referência DEFLATE fora do stream");
            for (int i = 0; i < n; ++i)
                append(out[out.size() - distance]);
        }
    } while (!last);
    std::uint32_t a = 1, b = 0, expected = 0;
    for (auto byte : out) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    for (std::size_t i = input.size() - 4; i < input.size(); ++i)
        expected = (expected << 8) | static_cast<unsigned char>(input[i]);
    check((b << 16 | a) == expected, "Checksum zlib incorreto");
    return out;
}
} // namespace pdf
