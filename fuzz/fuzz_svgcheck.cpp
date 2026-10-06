#include "images/svgcheck.h"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    svgIsSelfContained(QByteArrayView(data, qsizetype(size)));
    return 0;
}
