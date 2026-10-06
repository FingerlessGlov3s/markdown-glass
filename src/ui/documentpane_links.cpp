#include "ui/documentpane.h"

// DocumentPane: following links. Links come from untrusted documents, so only
// anchors and markdown files are opened directly; web and mail links go to
// the desktop's handlers, and anything else is only described in a dialog.

#include "app/links.h"
#include "ui/plaintext.h"
#include "view/documentview.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>

namespace {
// Enough to recognise a link; a document can make one any length.
constexpr qsizetype MaxShownLinkLength = 500;
} // namespace

void DocumentPane::activateLink(const QString &href, OpenIn where)
{
    const bool newTab = where == OpenIn::NewTab;
    const LinkTarget target = resolveLink(href, QFileInfo(m_path).absolutePath());
    switch (target.kind) {
    case LinkTarget::Anchor:
        if (!m_view->scrollToAnchor(target.anchor))
            emit statusMessage(tr("No heading matches #%1").arg(target.anchor));
        break;
    case LinkTarget::Markdown:
        if (!newTab && canonicalise(target.path) == m_canonicalPath) {
            m_view->scrollToAnchor(target.anchor);
        } else if (newTab || m_linksInNewTab) {
            emit openInNewTab(target.path, target.anchor);
        } else {
            const LoadResult result = load(target.path, target.anchor);
            if (result.failed())
                showPlainWarning(window(), tr("Cannot Open Link"), result.message);
        }
        break;
    case LinkTarget::Web:
    case LinkTarget::Mail:
        QDesktopServices::openUrl(target.url);
        break;
    case LinkTarget::OtherFile: {
        // Never launch arbitrary local files from a document: show where the
        // link leads and offer the containing folder instead.
        const QFileInfo info(target.path);
        QMessageBox box(QMessageBox::Information, tr("Local File Link"),
                        tr("This link points to a local file that Markdown Glass does not open:"), QMessageBox::Cancel,
                        window());
        setPlainInformativeText(box, target.path);
        QPushButton *folder = nullptr;
        if (info.exists())
            folder = box.addButton(tr("Open Containing Folder"), QMessageBox::AcceptRole);
        else
            box.setText(tr("This link points to a local file that does not exist:"));
        box.exec();
        if (folder && box.clickedButton() == folder) {
            QDesktopServices::openUrl(
                QUrl::fromLocalFile(info.isDir() ? info.absoluteFilePath() : info.absolutePath()));
        }
        break;
    }
    case LinkTarget::Unsupported: {
        QMessageBox box(QMessageBox::Information, tr("Unsupported Link"),
                        tr("Markdown Glass does not open this kind of link:"), QMessageBox::Cancel, window());
        setPlainInformativeText(box, href.left(MaxShownLinkLength));
        QPushButton *copy = box.addButton(tr("Copy Link"), QMessageBox::AcceptRole);
        box.exec();
        if (box.clickedButton() == copy)
            QApplication::clipboard()->setText(href);
        break;
    }
    }
}
