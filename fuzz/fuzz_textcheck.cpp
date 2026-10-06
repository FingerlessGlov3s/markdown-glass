#include "model/textcheck.h"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    md::checkText(QByteArrayView(data, qsizetype(size)));
    return 0;
}
