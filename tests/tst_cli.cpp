#include "app/render.h"
#include "appsupport.h"
#include "testsupport.h"

#include <QImage>
#include <QTemporaryDir>
#include <QTest>

// The command line: --version, --help, --render-png and --screenshot, run
// against the real program as well as in-process.
class TestCli : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();

    void renderToPngOffscreen();
    void commandLine();
    void renderRefusesBinaryInput();

private:
    QTemporaryDir m_dir;
    QString m_a, m_b, m_long;
};

void TestCli::initTestCase()
{
    QVERIFY(m_dir.isValid());
    setUpWindowSuite(m_dir.path(), &m_a, &m_b, &m_long);
}

void TestCli::renderToPngOffscreen()
{
    const QString out = m_dir.filePath(QStringLiteral("out.png"));
    RenderOptions options;
    options.width = 700;
    QCOMPARE(renderToPng(m_a, out, options), 0);
    QImage light(out);
    QCOMPARE(light.width(), 700);
    options.dark = true;
    options.wrapCode = false;
    QCOMPARE(renderToPng(m_a, out, options), 0);
    QImage dark(out);
    QVERIFY(QColor(dark.pixel(2, 2)).lightness() < QColor(light.pixel(2, 2)).lightness());

    // The width is kept within sensible bounds.
    options.width = 50;
    QCOMPARE(renderToPng(m_a, out, options), 0);
    QCOMPARE(QImage(out).width(), RenderOptions::MinWidth);

    QCOMPARE(renderToPng(m_dir.filePath(QStringLiteral("missing.md")), out, options), 1);
    QCOMPARE(renderToPng(m_a, QStringLiteral("/nonexistent/dir/out.png"), options), 1);
}

void TestCli::commandLine()
{
    QByteArray output;
    QCOMPARE(runProgram({QStringLiteral("--version")}, &output), 0);
    QVERIFY(output.contains("markdown-glass"));
    QCOMPARE(runProgram({QStringLiteral("--help")}, &output), 0);
    QVERIFY(output.contains("--render-png"));

    const QString png = m_dir.filePath(QStringLiteral("cli.png"));
    QCOMPARE(runProgram({QStringLiteral("--render-png"), png, QStringLiteral("--width"), QStringLiteral("640"),
                         QStringLiteral("--dark"), QStringLiteral("--no-code-wrap"), m_a}),
             0);
    QCOMPARE(QImage(png).width(), 640);
    // --render-png takes exactly one file.
    QCOMPARE(runProgram({QStringLiteral("--render-png"), png, m_a, m_b}), 2);

    const QString shot = m_dir.filePath(QStringLiteral("window.png"));
    QCOMPARE(runProgram({QStringLiteral("--screenshot"), shot, m_a}), 0);
    QVERIFY(!QImage(shot).isNull());
}

void TestCli::renderRefusesBinaryInput()
{
    const QString binary =
        writeFile(m_dir.path(), QStringLiteral("data.bin"), QByteArray("\0\x01\x02\x03", 4).repeated(100));
    const QString png = m_dir.filePath(QStringLiteral("refused.png"));
    QCOMPARE(renderToPng(binary, png, RenderOptions()), 1);
    QVERIFY(!QFile::exists(png));
    QByteArray output;
    QCOMPARE(runProgram({QStringLiteral("--render-png"), png, binary}, &output), 1);
    QVERIFY(output.contains("does not look like a text file"));
    // Opening such a file normally asks first; tst_instance covers that path.
}

QTEST_MAIN(TestCli)
#include "tst_cli.moc"
