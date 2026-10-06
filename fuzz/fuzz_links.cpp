#include "app/links.h"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    const QString href = QString::fromUtf8(reinterpret_cast<const char *>(data), qsizetype(size));
    resolveLink(href, QStringLiteral("/nonexistent/docs"));
    return 0;
}
