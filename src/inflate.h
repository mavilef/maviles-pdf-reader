#pragma once
#include "common.h"
namespace pdf {
// RFC 1950/1951: zlib + DEFLATE, sem libz.
Bytes inflate(std::string_view input);
} // namespace pdf
