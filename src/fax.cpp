#include "fax.h"
#include <map>
#include <sstream>
namespace pdf {
namespace {
using Codes = std::map<std::pair<int, int>, int>;
void append(Codes &table, const char *codes, int first, int step) {
    std::istringstream in(codes);
    std::string bits;
    for (int run = first; in >> bits; run += step) {
        int n = 0;
        for (char c : bits)
            n = n * 2 + c - '0';
        table[{int(bits.size()), n}] = run;
    }
}
const Codes &codes(bool black) {
    static const auto tables = [] {
        std::pair<Codes, Codes> t;
        append(t.first,
               "00110101 000111 0111 1000 1011 1100 1110 1111 10011 10100 00111 01000 001000 "
               "000011 110100 110101 101010 101011 0100111 0001100 0001000 0010111 0000011 0000100 "
               "0101000 0101011 0010011 0100100 0011000 00000010 00000011 00011010 00011011 "
               "00010010 00010011 00010100 00010101 00010110 00010111 00101000 00101001 00101010 "
               "00101011 00101100 00101101 00000100 00000101 00001010 00001011 01010010 01010011 "
               "01010100 01010101 00100100 00100101 01011000 01011001 01011010 01011011 01001010 "
               "01001011 00110010 00110011 00110100",
               0, 1);
        append(
            t.second,
            "0000110111 010 11 10 011 0011 0010 00011 000101 000100 0000100 0000101 0000111 "
            "00000100 00000111 000011000 0000010111 0000011000 0000001000 00001100111 00001101000 "
            "00001101100 00000110111 00000101000 00000010111 00000011000 000011001010 000011001011 "
            "000011001100 000011001101 000001101000 000001101001 000001101010 000001101011 "
            "000011010010 000011010011 000011010100 000011010101 000011010110 000011010111 "
            "000001101100 000001101101 000011011010 000011011011 000001010100 000001010101 "
            "000001010110 000001010111 000001100100 000001100101 000001010010 000001010011 "
            "000000100100 000000110111 000000111000 000000100111 000000101000 000001011000 "
            "000001011001 000000101011 000000101100 000001011010 000001100110 000001100111",
            0, 1);
        append(t.first,
               "11011 10010 010111 0110111 00110110 00110111 01100100 01100101 01101000 01100111 "
               "011001100 011001101 011010010 011010011 011010100 011010101 011010110 011010111 "
               "011011000 011011001 011011010 011011011 010011000 010011001 010011010 011000 "
               "010011011",
               64, 64);
        append(t.second,
               "0000001111 000011001000 000011001001 000001011011 000000110011 000000110100 "
               "000000110101 0000001101100 0000001101101 0000001001010 0000001001011 0000001001100 "
               "0000001001101 0000001110010 0000001110011 0000001110100 0000001110101 "
               "0000001110110 0000001110111 0000001010010 0000001010011 0000001010100 "
               "0000001010101 0000001011010 0000001011011 0000001100100 0000001100101",
               64, 64);
        for (Codes *c : {&t.first, &t.second})
            append(*c,
                   "00000001000 00000001100 00000001101 000000010010 000000010011 000000010100 "
                   "000000010101 000000010110 000000010111 000000011100 000000011101 000000011110 "
                   "000000011111",
                   1792, 64);
        return t;
    }();
    return black ? tables.second : tables.first;
}
} // namespace
Bytes decode_fax4(std::string_view data, int width, int height) {
    check(width > 0 && height > 0 && std::uint64_t(width) * height <= 64 * 1024 * 1024,
          "Dimensões fax inválidas");
    std::size_t bit = 0;
    auto read = [&] {
        check(bit / 8 < data.size(), "Fax truncado");
        int v = (static_cast<unsigned char>(data[bit / 8]) >> (7 - bit % 8)) & 1;
        ++bit;
        return v;
    };
    auto run = [&](bool black) {
        int total = 0;
        for (int i = 0; i <= width / 64 + 1; ++i) {
            int v = 0, n = -1;
            for (int l = 1; l <= 13; ++l) {
                v = v * 2 + read();
                auto it = codes(black).find({l, v});
                if (it != codes(black).end()) {
                    n = it->second;
                    break;
                }
            }
            check(n >= 0 && n <= width - total, "Código fax inválido");
            total += n;
            if (n < 64)
                return total;
        }
        throw std::runtime_error("Run fax excedido");
    };
    Bytes out(static_cast<std::size_t>(width) * height);
    std::vector<int> reference{width, width};
    for (int y = 0; y < height; ++y) {
        int a = 0;
        bool black = false, initial = true;
        std::vector<int> changes;
        auto paint = [&](int end) {
            check(end >= a && end <= width, "Transição fax inválida");
            if (black)
                std::fill(out.begin() + std::size_t(y) * width + a,
                          out.begin() + std::size_t(y) * width + end, 1);
            a = end;
        };
        for (int budget = 0; a < width; ++budget) {
            check(budget <= width * 4 + 10, "Linha fax excedida");
            std::size_t j = black ? 1 : 0;
            while (j + 1 < reference.size() && (initial ? reference[j] < a : reference[j] <= a))
                j += 2;
            int b1 = j < reference.size() ? reference[j] : width,
                b2 = j + 1 < reference.size() ? reference[j + 1] : width;
            int op = -99, v = 0;
            for (int len = 1; len <= 7; ++len) {
                v = v * 2 + read();
                if (len == 1 && v == 1) {
                    op = 0;
                    break;
                }
                if (len == 3 && (v == 2 || v == 3)) {
                    op = v == 2 ? -1 : 1;
                    break;
                }
                if (len == 3 && v == 1) {
                    op = 10;
                    break;
                }
                if (len == 4 && v == 1) {
                    op = 11;
                    break;
                }
                if (len == 6 && (v == 2 || v == 3)) {
                    op = v == 2 ? -2 : 2;
                    break;
                }
                if (len == 7 && (v == 2 || v == 3)) {
                    op = v == 2 ? -3 : 3;
                    break;
                }
            }
            check(op != -99, "Modo fax não suportado");
            if (op == 11) {
                check(b2 > a, "Pass fax sem avanço");
                paint(b2);
            } else if (op == 10) {
                int n = run(black);
                paint(a + n);
                changes.push_back(a);
                black = !black;
                n = run(black);
                paint(a + n);
                changes.push_back(a);
                black = !black;
            } else {
                paint(b1 + op);
                changes.push_back(a);
                black = !black;
            }
            initial = false;
        }
        changes.push_back(width);
        changes.push_back(width);
        reference = std::move(changes);
    }
    return out;
}
} // namespace pdf
