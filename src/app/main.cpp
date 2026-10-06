#include "app/app.h"
#include "app/render.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QLoggingCategory>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    // Qt logs a warning for every font that lacks shaping tables for a script
    // it is asked about. It is noise for anyone running the viewer from a
    // terminal; QT_LOGGING_RULES="qt.text.font.db.warning=true" brings it back.
    QLoggingCategory::setFilterRules(QStringLiteral("qt.text.font.db.warning=false"));
    QApplication::setOrganizationName(QStringLiteral("markdown-glass"));
    QApplication::setApplicationName(QStringLiteral("markdown-glass"));
    QApplication::setDesktopFileName(QStringLiteral("io.github.markdown_glass"));
    QApplication::setApplicationDisplayName(QStringLiteral("Markdown Glass"));
    QApplication::setApplicationVersion(QStringLiteral(MDGLASS_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("A read-only markdown viewer"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Markdown files to open."),
                                 QStringLiteral("[files...]"));
    // Offscreen rendering, used by tests and container checks.
    const QCommandLineOption renderPng(QStringLiteral("render-png"),
                                       QStringLiteral("Render the file to a PNG and exit."), QStringLiteral("output"));
    const QCommandLineOption renderWidth(QStringLiteral("width"), QStringLiteral("Width in pixels for --render-png."),
                                         QStringLiteral("pixels"), QString::number(RenderOptions::DefaultWidth));
    const QCommandLineOption renderDark(QStringLiteral("dark"), QStringLiteral("Use a dark palette for --render-png."));
    const QCommandLineOption renderNoWrap(QStringLiteral("no-code-wrap"),
                                          QStringLiteral("Do not wrap code blocks for --render-png."));
    const QCommandLineOption screenshot(QStringLiteral("screenshot"),
                                        QStringLiteral("Open the file in a window, save a screenshot and exit."),
                                        QStringLiteral("output"));
    parser.addOptions({renderPng, renderWidth, renderDark, renderNoWrap, screenshot});
    parser.process(app);

    const QStringList files = parser.positionalArguments();
    if (parser.isSet(renderPng)) {
        if (files.size() != 1)
            parser.showHelp(2);
        RenderOptions options;
        options.width = parser.value(renderWidth).toInt();
        options.dark = parser.isSet(renderDark);
        options.wrapCode = !parser.isSet(renderNoWrap);
        return renderToPng(files.first(), parser.value(renderPng), options);
    }

    // Paths are made absolute here because a running instance has its own working directory.
    QStringList absolute;
    for (const QString &file : files)
        absolute.append(QFileInfo(file).absoluteFilePath());

    if (parser.isSet(screenshot))
        return screenshotWindow(app, absolute, parser.value(screenshot));

    if (App::sendToRunningInstance(absolute))
        return 0;

    App instance;
    instance.listen();
    // Nothing to show: every file was missing or the user declined to open it.
    if (!instance.open(absolute))
        return 1;
    return runUntilQuit(app);
}
