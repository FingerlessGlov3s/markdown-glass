#pragma once

#include <QString>
#include <QStringList>

class QApplication;

// The commands that make a picture and exit, used by tests and for checking
// rendering in a container: --render-png draws the document alone, without a
// window; --screenshot opens the real window and grabs it.

struct RenderOptions {
    int width = DefaultWidth; // of the image, in pixels; clamped to the range below
    bool dark = false;        // a dark palette, as on a dark desktop
    bool wrapCode = true;     // wrap long code lines rather than cut them

    static constexpr int DefaultWidth = 1000;
    static constexpr int MinWidth = 200;
    static constexpr int MaxWidth = 8000;
};

// Returns a process exit code.
int renderToPng(const QString &markdownPath, const QString &pngPath, const RenderOptions &options);

// Opens `files` in a window, saves a screenshot of it to `output` once it has
// settled, and quits. Returns the process exit code.
int screenshotWindow(QApplication &app, const QStringList &files, const QString &output);

// Runs the event loop until the application quits and returns its status,
// after draining the image decoders still on the thread pool: one running
// when the application object is torn down could touch plugins already
// unloaded.
int runUntilQuit(QApplication &app);
