#pragma once
#include "pdf.h"
namespace pdf {
struct PDFImage {
    Bitmap bitmap;
    Bytes alpha;
};
PDFImage decode_image(Document &document, const Value &ref, const Value &resources,
                      std::uint32_t stencil = 0, int depth = 0);
} // namespace pdf
