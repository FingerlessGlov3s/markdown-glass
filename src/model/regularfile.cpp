#include "model/regularfile.h"

#include <QCoreApplication>
#include <QtLogging> // qt_error_string, thread-safe like QFile's own messages

#include <cerrno>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace md {

bool openRegularFile(QFile &file, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
#ifdef Q_OS_UNIX
    // O_NONBLOCK so a FIFO cannot hold up the open; the type is then checked
    // on what was actually opened, not on the path.
    const int fd = ::open(QFile::encodeName(file.fileName()).constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY);
    if (fd < 0)
        return fail(qt_error_string(errno));
    struct stat st {};
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        ::close(fd);
        return fail(QCoreApplication::translate("RegularFile", "Not a regular file"));
    }
    if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::close(fd);
        return fail(file.errorString());
    }
    return true;
#else
    if (!file.open(QIODevice::ReadOnly))
        return fail(file.errorString());
    return true;
#endif
}

} // namespace md
