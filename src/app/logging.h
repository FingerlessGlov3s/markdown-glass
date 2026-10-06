#pragma once

#include <QLoggingCategory>

// Diagnostics, off by default for debug messages. Enable with, for example:
//   QT_LOGGING_RULES="markdownglass.*.debug=true" markdown-glass README.md
Q_DECLARE_LOGGING_CATEGORY(lcFiles)
Q_DECLARE_LOGGING_CATEGORY(lcImages)
Q_DECLARE_LOGGING_CATEGORY(lcInstance)
Q_DECLARE_LOGGING_CATEGORY(lcEditor)
