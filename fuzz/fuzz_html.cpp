#include "model/htmlsubset.h"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    const QString html = QString::fromUtf8(reinterpret_cast<const char *>(data), qsizetype(size));
    md::tokenizeHtml(html);
    return 0;
}
