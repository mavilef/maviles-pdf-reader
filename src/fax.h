#pragma once
#include "common.h"
namespace pdf {
// ITU T.6 (Group 4); um byte por pixel, 0 branco / 1 preto.
Bytes decode_fax4(std::string_view data, int width, int height);
} // namespace pdf
