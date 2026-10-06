#include "model/parser.h"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    md::parseMarkdown(QByteArray(reinterpret_cast<const char *>(data), qsizetype(size)));
    return 0;
}
