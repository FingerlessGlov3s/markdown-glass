#include "ui/documentpane.h"

#include "images/imageloader.h"
#include "ui/findbar.h"
#include "view/documentview.h"
#include "view/minimap.h"

#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>

namespace {
// Editors often save in several steps (truncate, write, rename); reload once
// the file has been quiet this long.
constexpr int ReloadSettleMs = 150;
constexpr int BarIconSize = 16;
// The remote-image bar's padding: a little more before the icon than after the button.
constexpr int BarPadLeft = 10;
constexpr int BarPadRight = 6;
constexpr int BarPadVertical = 4;
} // namespace

DocumentPane::DocumentPane(QWidget *parent)
    : QWidget(parent)
    , m_view(new DocumentView(this))
    , m_minimap(new Minimap(m_view, this))
    , m_findBar(new FindBar(this))
    , m_remoteBar(new QFrame(this))
    , m_images(new ImageLoader(this))
    , m_watcher(new QFileSystemWatcher(this))
{
    m_view->setImageSource(m_images);

    // Remote image bar, shown like an email client's "images blocked" strip.
    m_remoteBar->setFrameShape(QFrame::StyledPanel);
    m_remoteBar->setAutoFillBackground(true);
    m_remoteBar->setBackgroundRole(QPalette::AlternateBase);
    auto *barLayout = new QHBoxLayout(m_remoteBar);
    barLayout->setContentsMargins(BarPadLeft, BarPadVertical, BarPadRight, BarPadVertical);
    auto *barIcon = new QLabel(m_remoteBar);
    barIcon->setPixmap(QIcon::fromTheme(QStringLiteral("security-medium")).pixmap(BarIconSize, BarIconSize));
    auto *barText = new QLabel(tr("Remote images in this document are blocked to protect your privacy."), m_remoteBar);
    auto *barButton = new QPushButton(tr("Load Remote Images"), m_remoteBar);
    barButton->setToolTip(tr("Fetch this document's images from the internet. "
                             "The servers hosting them will see your IP address."));
    barLayout->addWidget(barIcon);
    barLayout->addWidget(barText, 1);
    barLayout->addWidget(barButton);
    m_remoteBar->hide();
    connect(barButton, &QPushButton::clicked, this, [this] { m_images->setRemoteAllowed(true); });

    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    row->addWidget(m_view, 1);
    row->addWidget(m_minimap);

    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    column->addWidget(m_remoteBar);
    column->addLayout(row, 1);
    column->addWidget(m_findBar);
    m_findBar->hide();

    connect(m_images, &ImageLoader::changed, m_view, &DocumentView::relayout);
    connect(m_images, &ImageLoader::blockedChanged, this, &DocumentPane::updateRemoteBar);
    connect(m_images, &ImageLoader::frameChanged, m_view->viewport(), qOverload<>(&QWidget::update));
    connect(m_view, &DocumentView::linkActivated, this, &DocumentPane::activateLink);
    connect(m_view, &DocumentView::linkHovered, this, &DocumentPane::statusMessage);

    connect(m_findBar, &FindBar::queryChanged, this, &DocumentPane::runFind);
    connect(m_findBar, &FindBar::next, this, &DocumentPane::findNext);
    connect(m_findBar, &FindBar::closed, this, [this] {
        m_findBar->hide();
        m_view->clearFind();
        m_view->setFocus();
    });

    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(ReloadSettleMs);
    connect(&m_reloadTimer, &QTimer::timeout, this, &DocumentPane::fileEvent);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this] {
        m_fileChanged = true;
        m_reloadTimer.start();
    });
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, &m_reloadTimer, qOverload<>(&QTimer::start));

    applySettings(AppSettings::instance().get());
}

DocumentPane::~DocumentPane() = default;

const md::Document *DocumentPane::document() const
{
    return m_view->document();
}

QString DocumentPane::title() const
{
    return m_path.isEmpty() ? tr("Untitled") : QFileInfo(m_path).fileName();
}

void DocumentPane::applySettings(const Settings &s)
{
    m_view->setWidthLimit(s.limitWidth, s.widthPixels);
    m_view->setWrapCode(s.wrapCode);
    m_view->setZoom(s.zoom);
    m_minimap->setVisible(s.minimap);
    m_view->setVerticalScrollBarPolicy(s.minimap ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
    m_images->setAnimated(s.animateImages);
    m_liveReload = s.liveReload;
    m_autoLoadRemote = s.autoLoadRemoteImages;
    m_linksInNewTab = s.linksInNewTab;
    if (s.autoLoadRemoteImages)
        m_images->setRemoteAllowed(true);
}

void DocumentPane::updateRemoteBar()
{
    m_remoteBar->setVisible(m_images->hasBlocked());
}

void DocumentPane::goBack()
{
    navigate(m_back, m_forward);
}

void DocumentPane::goForward()
{
    navigate(m_forward, m_back);
}

// Moves one step through the history: the last entry of `from` is opened at
// its saved scroll position, and the current document is pushed onto `to`.
void DocumentPane::navigate(QList<HistoryEntry> &from, QList<HistoryEntry> &to)
{
    if (from.isEmpty())
        return;
    // The entry leaves the history only once its file is showing: declining
    // a question about it, or a passing error, must not lose the place.
    const HistoryEntry entry = from.last();
    const HistoryEntry here {m_path, m_view->verticalScrollBar()->value()};
    const LoadResult result = read(entry.path, false);
    if (result) {
        from.removeLast();
        to.append(here);
        m_view->verticalScrollBar()->setValue(entry.scroll);
    } else if (result.failed()) {
        emit statusMessage(result.message);
    }
    emit historyChanged();
}

// ------------------------------------------------------------------- find

void DocumentPane::showFind()
{
    const QString selection = m_view->selectedText();
    m_findBar->activate(selection.contains(u'\n') ? QString() : selection);
    runFind();
}

void DocumentPane::runFind()
{
    const int total = m_view->find(m_findBar->text(), m_findBar->caseSensitive());
    m_findBar->setResult(m_view->currentMatch(), total);
}

void DocumentPane::findNext(FindDirection direction)
{
    if (!m_findBar->isVisible()) {
        showFind();
        return;
    }
    m_view->findNext(direction);
    m_findBar->setResult(m_view->currentMatch(), m_view->matchCount());
}
