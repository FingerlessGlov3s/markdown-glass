#include "app/render.h"

#include "app/app.h"
#include "highlight/highlighter.h"
#include "layout/layout.h"
#include "model/parser.h"
#include "model/regularfile.h"
#include "model/textcheck.h"

#include <QApplication>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QTextStream>
#include <QThreadPool>
#include <QTimer>
#include <QWidget>

using namespace md;

namespace {
// Breeze Dark, so --dark looks like the viewer on a dark Plasma desktop.
const QColor DarkBase(0x1b, 0x1e, 0x20);
const QColor DarkText(0xfc, 0xfc, 0xfc);
const QColor DarkLink(0x1d, 0x99, 0xf3);
// The largest image written, so a hostile document cannot demand an enormous
// one: a height limit, and an area limit for wide renders (200 MB as RGB32).
constexpr qreal MaxImageHeight = 30000;
constexpr qint64 MaxImagePixels = 50'000'000;
// As much of the file as is read, like the viewer's own limit.
constexpr qint64 MaxDocumentBytes = qint64(200) * 1024 * 1024;
// Long enough for the window to lay out and local images to load before
// the screenshot is taken.
constexpr int ScreenshotSettleMs = 700;
} // namespace

// Errors go straight to stderr: Qt's logging may go to the system journal
// instead when output is piped, and a script calling --render-png needs them.
int renderToPng(const QString &markdownPath, const QString &pngPath, const RenderOptions &options)
{
    const int width = qBound(RenderOptions::MinWidth, options.width, RenderOptions::MaxWidth);
    QFile file(markdownPath);
    QString reason;
    if (!openRegularFile(file, &reason)) {
        QTextStream(stderr) << "Cannot open " << markdownPath << ": " << reason << '\n';
        return 1;
    }
    const QByteArray markdown = file.read(MaxDocumentBytes);
    if (checkText(markdown) != NotText::No) {
        QTextStream(stderr) << markdownPath << " does not look like a text file\n";
        return 1;
    }
    const auto doc = parseMarkdown(markdown);

    QPalette palette = QApplication::palette();
    if (options.dark) {
        palette.setColor(QPalette::Base, DarkBase);
        palette.setColor(QPalette::Text, DarkText);
        palette.setColor(QPalette::Link, DarkLink);
    }
    const Theme theme = Theme::fromPalette(palette, 1.0);
    CodeHighlighter highlighter;
    highlighter.setDark(theme.dark);

    const qreal margin = theme.contentMargin();
    LayoutOptions layoutOptions;
    layoutOptions.width = qMax(theme.minContentWidth(), width - 2 * margin);
    layoutOptions.wrapCode = options.wrapCode;
    const Layout layout(*doc, theme, layoutOptions, &highlighter, nullptr);

    const qreal maxHeight = qMin(MaxImageHeight, qreal(MaxImagePixels) / width);
    const int height = int(qMin(layout.height() + 2 * margin, maxHeight));
    QImage image(width, qMax(1, height), QImage::Format_RGB32);
    image.fill(theme.background);
    QPainter p(&image);
    p.translate(margin, margin);
    PaintState state;
    state.preview = true; // no images are loaded here, but a PNG is never "looked at"
    layout.paint(p, QRectF(-margin, -margin, width, height), state);
    p.end();

    if (!image.save(pngPath, "PNG")) {
        QTextStream(stderr) << "Cannot write " << pngPath << '\n';
        return 1;
    }
    return 0;
}

int screenshotWindow(QApplication &app, const QStringList &files, const QString &output)
{
    // Exercises the real window without joining the single-instance channel.
    App instance;
    instance.open(files);
    QTimer::singleShot(ScreenshotSettleMs, &app, [output] {
        const QWidgetList widgets = QApplication::topLevelWidgets();
        for (QWidget *widget : widgets) {
            if (widget->inherits("QMainWindow"))
                widget->grab().save(output);
        }
        QApplication::quit();
    });
    return runUntilQuit(app);
}

int runUntilQuit(QApplication &app)
{
    const int status = app.exec();
    QThreadPool::globalInstance()->clear();
    QThreadPool::globalInstance()->waitForDone();
    return status;
}
