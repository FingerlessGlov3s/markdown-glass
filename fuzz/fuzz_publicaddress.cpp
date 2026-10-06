#include "images/publicaddress.h"

#include <QHostAddress>
#include <QString>

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    const QString text = QString::fromUtf8(reinterpret_cast<const char *>(data), qsizetype(size));
    isPublicAddress(QHostAddress(text));
    return 0;
}
