#pragma once
#include "render.h"
namespace pdf {
void show_document(Document &document, std::size_t initial_page, bool smoke_test = false);
}
