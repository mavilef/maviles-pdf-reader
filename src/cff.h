#pragma once
#include "common.h"
#include <map>
namespace pdf {
// CFF1 / Type 2, contornos sem hinting, como o motor TrueType.
class CFF {
    Bytes bytes_;
    std::vector<std::string_view> chars_, global_, local_;
    std::map<std::string, int> names_;
    std::map<int, Path> cache_;
    int encoding_[256]{};
    Matrix matrix_{1, 0, 0, 1, 0, 0};
    std::vector<std::string_view> index(std::size_t &pos) const;

  public:
    explicit CFF(Bytes bytes);
    int named(const std::string &name) const;
    int encoded(int code) const {
        return encoding_[code];
    }
    const Path &glyph(int id);
    static std::string standard_name(int code, bool winansi = false);
};
} // namespace pdf
