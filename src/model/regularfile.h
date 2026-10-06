#pragma once

#include <QFile>
#include <QString>

namespace md {

// Opens `file` for reading only if it is a regular file at the moment it is
// opened. Checking with QFileInfo first and opening afterwards leaves a gap
// in which the file can be swapped for a FIFO or a device, and opening a FIFO
// blocks until something writes to it. On failure returns false with a
// reason in *error.
bool openRegularFile(QFile &file, QString *error = nullptr);

} // namespace md
