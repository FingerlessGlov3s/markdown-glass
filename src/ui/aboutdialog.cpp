#include "ui/aboutdialog.h"

#include "model/parser.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QFontDatabase>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QVBoxLayout>
#include <ksyntaxhighlighting_version.h>

namespace {
constexpr int PageMargin = 12;         // around the text of the About and Libraries tabs
constexpr int LibrariesWidth = 460;    // narrowest the wrapped library list gets
constexpr QSize InitialSize(560, 440); // room for the licence texts without scrolling sideways
} // namespace

// The resources live in a static library, so they must be registered by hand.
// Q_INIT_RESOURCE has to be used outside any namespace, hence "static" here.
static void initResources()
{
    Q_INIT_RESOURCE(resources);
}

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    initResources();
    setWindowTitle(tr("About Markdown Glass"));
    auto *root = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    root->addWidget(tabs);

    // About
    auto *about = new QLabel(
        tr("<h2>Markdown Glass %1</h2>"
           "<p>A simple markdown only viewer for Qt based desktop environments.</p>"
           "<p>Copyright (c) 2026 FingerlessGloves &lt;me@fingerlessgloves.me&gt;</p>"
           "<p>Released under the BSD 3-Clause licence.</p>"
           "<p><a href=\"https://github.com/FingerlessGlov3s/markdown-glass\">github.com/FingerlessGlov3s/markdown-glass</a></p>")
            .arg(QApplication::applicationVersion()),
        this);
    about->setOpenExternalLinks(true);
    about->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    about->setMargin(PageMargin);
    tabs->addTab(about, tr("About"));

    auto addText = [&](const QString &resource, const QString &title) {
        auto *text = new QPlainTextEdit(this);
        text->setReadOnly(true);
        text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        QFile file(resource);
        if (file.open(QIODevice::ReadOnly))
            text->setPlainText(QString::fromUtf8(file.readAll()));
        tabs->addTab(text, title);
        return text;
    };
    addText(QStringLiteral(":/licenses/markdown-glass.txt"), tr("Licence"))
        ->setObjectName(QStringLiteral("projectLicence"));

    // Libraries
    auto *libraries = new QLabel(
        tr("<p>Markdown Glass is built with these libraries:</p>"
           "<p><b>Qt %1</b><br>Application framework, used under the LGPL version 3.<br>"
           "<a href=\"https://www.qt.io\">qt.io</a></p>"
           "<p><b>KSyntaxHighlighting %2</b> (KDE Frameworks)<br>Syntax highlighting for code blocks. "
           "The library is MIT licensed; its syntax definitions carry their own licences.<br>"
           "<a href=\"https://invent.kde.org/frameworks/syntax-highlighting\">invent.kde.org/frameworks/syntax-highlighting</a></p>"
           "<p><b>cmark-gfm %3</b><br>GitHub's markdown parser, included in this program. "
           "BSD 2-Clause and MIT licensed; the full notices are on the next tab.<br>"
           "<a href=\"https://github.com/github/cmark-gfm\">github.com/github/cmark-gfm</a></p>")
            .arg(QString::fromLatin1(qVersion()), QStringLiteral(KSYNTAXHIGHLIGHTING_VERSION_STRING),
                 md::parserVersion()),
        this);
    libraries->setWordWrap(true);
    libraries->setOpenExternalLinks(true);
    libraries->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    libraries->setMargin(PageMargin);
    libraries->setMinimumWidth(LibrariesWidth);
    libraries->setMinimumHeight(libraries->heightForWidth(LibrariesWidth));
    tabs->addTab(libraries, tr("Libraries"));

    // Licence notices for code compiled into the binary
    addText(QStringLiteral(":/licenses/cmark-gfm.txt"), tr("cmark-gfm Licence"))
        ->setObjectName(QStringLiteral("cmarkLicence"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
    resize(InitialSize);
}
