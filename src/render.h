#pragma once
#include "cff.h"
#include "pdf.h"
#include "truetype.h"
#include <memory>

namespace pdf {
class Renderer {
    struct Font {
        std::shared_ptr<TrueType> face;
        std::shared_ptr<CFF> cff;
        int codes[256]{};
        std::map<int, double> widths;
        Bytes cid_map;
        double default_width = 1000;
    };
    Document &document_;
    std::map<int, std::shared_ptr<TrueType>> faces_;
    std::map<int, Font> fonts_;
    std::map<int, std::shared_ptr<CFF>> cff_faces_;
    Font &font(const Value &ref);

  public:
    explicit Renderer(Document &document) : document_(document) {}
    Bitmap render(std::size_t page, double scale = 1.0);
};
} // namespace pdf
