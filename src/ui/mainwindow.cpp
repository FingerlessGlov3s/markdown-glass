#include "ui/mainwindow.h"

#include "app/editor.h"
#include "app/links.h"
#include "app/settings.h"
#include "images/imageloader.h"
#include "print/printing.h"
#include "ui/aboutdialog.h"
#include "ui/documentpane.h"
#include "ui/outlinedock.h"
#include "ui/plaintext.h"
#include "ui/settingsdialog.h"
#include "view/documentview.h"

#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMimeData>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>

namespace {

const QSize DefaultWindowSize(1200, 860); // for a first run, before a geometry was saved
const QSize MinPreviewSize(1000, 800);    // print preview: room for a page at a readable scale

QIcon themed(const char *name, const char *fallback = nullptr)
{
    return QIcon::fromTheme(QLatin1String(name), fallback ? QIcon::fromTheme(QLatin1String(fallback)) : QIcon());
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_stack(new QStackedWidget(this))
    , m_tabs(new QTabWidget(this))
    , m_outline(new OutlineDock(this))
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAcceptDrops(true);
    setWindowTitle(tr("Markdown Glass"));
    setWindowIcon(themed("io.github.markdown_glass", "text-markdown"));

    auto *placeholder = new QLabel(tr("Open a markdown file with Ctrl+O, or drop one here."), this);
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setForegroundRole(QPalette::PlaceholderText);
    m_stack->addWidget(placeholder);
    m_stack->addWidget(m_tabs);
    setCentralWidget(m_stack);

    m_tabs->setDocumentMode(true);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setTabBarAutoHide(true);
    m_tabs->tabBar()->setElideMode(Qt::ElideMiddle);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::currentChanged);

    addDockWidget(Qt::LeftDockWidgetArea, m_outline);
    connect(m_outline, &OutlineDock::headingActivated, this, [this](const md::Block *heading) {
        if (DocumentPane *pane = currentPane())
            pane->view()->scrollToBlock(heading);
    });

    createActions();
    statusBar()->setSizeGripEnabled(true);

    QSettings geometry;
    if (!restoreGeometry(geometry.value("window/geometry").toByteArray()))
        resize(DefaultWindowSize);
    restoreState(geometry.value("window/state").toByteArray());

    connect(&AppSettings::instance(), &AppSettings::changed, this, &MainWindow::applySettings);
    applySettings();
    // The dock's own close button must update the persisted setting too.
    connect(m_outline, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (isVisible() && !isMinimized() && visible != AppSettings::instance().get().outline)
            AppSettings::instance().update([visible](Settings &s) { s.outline = visible; });
    });
    updateActions();
}

void MainWindow::createActions()
{
    createFileMenu();
    createEditMenu();
    createViewMenu();
    createGoMenu();
    createSettingsAndHelpMenus();
    createToolBar();
}

void MainWindow::createFileMenu()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    m_open =
        file->addAction(themed("document-open"), tr("&Open..."), QKeySequence::Open, this, &MainWindow::openDialog);
    m_reload = file->addAction(themed("view-refresh"), tr("&Reload"), QKeySequence::Refresh, this, [this] {
        if (DocumentPane *pane = currentPane())
            pane->reload();
    });
    m_edit = file->addAction(themed("document-edit"), tr("&Edit in Text Editor"), QKeySequence(Qt::CTRL | Qt::Key_E),
                             this, &MainWindow::editCurrent);
    m_edit->setIconText(tr("Edit"));
    m_edit->setToolTip(tr("Open this file in your text editor"));
    file->addSeparator();
    m_print = file->addAction(themed("document-print"), tr("&Print..."), QKeySequence::Print, this,
                              [this] { print(PrintMode::Direct); });
    m_printPreview = file->addAction(themed("document-print-preview"), tr("Print Pre&view..."), this,
                                     [this] { print(PrintMode::Preview); });
    file->addSeparator();
    m_closeTab =
        file->addAction(themed("tab-close"), tr("&Close Tab"), QKeySequence(Qt::CTRL | Qt::Key_W), this, [this] {
            if (m_tabs->count() > 0)
                closeTab(m_tabs->currentIndex());
            else
                close();
        });
    file->addAction(themed("application-exit"), tr("&Quit"), QKeySequence::Quit, qApp, &QApplication::closeAllWindows);
}

void MainWindow::createEditMenu()
{
    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    m_copy = edit->addAction(themed("edit-copy"), tr("&Copy"), QKeySequence::Copy, this, [this] {
        if (DocumentPane *pane = currentPane())
            pane->view()->copy();
    });
    m_selectAll = edit->addAction(themed("edit-select-all"), tr("Select &All"), QKeySequence::SelectAll, this, [this] {
        if (DocumentPane *pane = currentPane())
            pane->view()->selectAll();
    });
    edit->addSeparator();
    m_find = edit->addAction(themed("edit-find"), tr("&Find..."), QKeySequence::Find, this, [this] {
        if (DocumentPane *pane = currentPane())
            pane->showFind();
    });
    m_findNext = edit->addAction(tr("Find &Next"), QKeySequence::FindNext, this, [this] {
        if (DocumentPane *pane = currentPane())
            pane->findNext(FindDirection::Forward);
    });
    m_findPrevious = edit->addAction(tr("Find &Previous"), QKeySequence::FindPrevious, this, [this] {
        if (DocumentPane *pane = currentPane())
            pane->findNext(FindDirection::Backward);
    });
}

void MainWindow::createViewMenu()
{
    QMenu *view = menuBar()->addMenu(tr("&View"));
    m_showOutline =
        addSettingToggle(view, themed("view-list-tree", "sidebar-expand-left"), tr("&Outline Sidebar"), tr("Outline"),
                         QKeySequence(Qt::Key_F9), tr("Show or hide the list of headings"), &Settings::outline);
    m_minimap =
        addSettingToggle(view, themed("view-preview", "document-preview"), tr("Document &Preview on Scrollbar"),
                         tr("Preview"), QKeySequence(Qt::CTRL | Qt::Key_M),
                         tr("Show a miniature of the whole document in place of the scrollbar"), &Settings::minimap);
    view->addSeparator();
    m_limitWidth = addSettingToggle(view, themed("zoom-fit-width", "view-split-left-right"), tr("&Limit Text Width"),
                                    tr("Limit Width"), QKeySequence(Qt::CTRL | Qt::Key_L),
                                    tr("Keep lines to a comfortable reading width instead of filling the window"),
                                    &Settings::limitWidth);
    m_wrapCode = addSettingToggle(view, themed("text-wrap"), tr("&Wrap Code Blocks"), tr("Wrap Code"),
                                  QKeySequence(Qt::ALT | Qt::Key_Z),
                                  tr("Wrap long code lines, or scroll each code block sideways"), &Settings::wrapCode);
    view->addSeparator();

    auto zoomBy = [](double factor) {
        AppSettings::instance().update([factor](Settings &s) {
            s.zoom = factor == 0 ? 1.0 : qBound(ViewLimits::MinZoom, s.zoom * factor, ViewLimits::MaxZoom);
        });
    };
    QAction *zoomIn =
        view->addAction(themed("zoom-in"), tr("Zoom &In"), this, [zoomBy] { zoomBy(ViewLimits::ZoomStep); });
    zoomIn->setShortcuts({QKeySequence::ZoomIn, QKeySequence(Qt::CTRL | Qt::Key_Equal)});
    view->addAction(themed("zoom-out"), tr("Zoom &Out"), QKeySequence::ZoomOut, this,
                    [zoomBy] { zoomBy(1 / ViewLimits::ZoomStep); });
    view->addAction(themed("zoom-original"), tr("&Actual Size"), QKeySequence(Qt::CTRL | Qt::Key_0), this,
                    [zoomBy] { zoomBy(0); });
    view->addSeparator();
    QAction *fullScreen = view->addAction(themed("view-fullscreen"), tr("&Full Screen"));
    fullScreen->setShortcut(QKeySequence::FullScreen);
    fullScreen->setCheckable(true);
    connect(fullScreen, &QAction::triggered, this, [this](bool on) { on ? showFullScreen() : showNormal(); });
}

// A checkable action that mirrors one on/off setting: toggling it changes the
// setting, and applySettings() keeps its check mark in step.
QAction *MainWindow::addSettingToggle(QMenu *menu, const QIcon &icon, const QString &text, const QString &iconText,
                                      const QKeySequence &shortcut, const QString &toolTip, bool Settings::*setting)
{
    QAction *action = menu->addAction(icon, text);
    action->setIconText(iconText);
    action->setShortcut(shortcut);
    action->setToolTip(toolTip);
    action->setCheckable(true);
    connect(action, &QAction::triggered, this,
            [setting](bool on) { AppSettings::instance().update([setting, on](Settings &s) { s.*setting = on; }); });
    m_settingToggles.append({action, setting});
    return action;
}

void MainWindow::createGoMenu()
{
    QMenu *go = menuBar()->addMenu(tr("&Go"));
    m_back = go->addAction(themed("go-previous"), tr("&Back"), QKeySequence::Back, this, [this] {
        if (DocumentPane *pane = currentPane())
            pane->goBack();
    });
    m_forward = go->addAction(themed("go-next"), tr("&Forward"), QKeySequence::Forward, this, [this] {
        if (DocumentPane *pane = currentPane())
            pane->goForward();
    });
}

void MainWindow::createSettingsAndHelpMenus()
{
    QMenu *settings = menuBar()->addMenu(tr("&Settings"));
    settings->addAction(themed("configure"), tr("&Configure Markdown Glass..."), QKeySequence::Preferences, this,
                        [this] {
                            SettingsDialog dialog(this);
                            dialog.exec();
                        });
    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(themed("help-about"), tr("&About Markdown Glass"), this, [this] {
        AboutDialog dialog(this);
        dialog.exec();
    });
}

void MainWindow::createToolBar()
{
    QToolBar *bar = addToolBar(tr("Main Toolbar"));
    bar->setObjectName(QStringLiteral("mainToolBar"));
    bar->setMovable(false);
    bar->addAction(m_open);
    bar->addAction(m_back);
    bar->addAction(m_forward);
    bar->addSeparator();
    bar->addAction(m_showOutline);
    bar->addAction(m_limitWidth);
    bar->addAction(m_wrapCode);
    bar->addAction(m_minimap);
    bar->addSeparator();
    bar->addAction(m_find);
    bar->addAction(m_edit);
    bar->addAction(m_print);
}

DocumentPane *MainWindow::currentPane() const
{
    return qobject_cast<DocumentPane *>(m_tabs->currentWidget());
}

bool MainWindow::hasDocuments() const
{
    return m_tabs->count() > 0;
}

DocumentPane *MainWindow::addPane()
{
    auto *pane = new DocumentPane(m_tabs);
    connect(pane, &DocumentPane::titleChanged, this, [this, pane] {
        const int index = m_tabs->indexOf(pane);
        if (index >= 0) {
            m_tabs->setTabText(index, pane->title());
            m_tabs->setTabToolTip(index, plainToolTip(pane->path()));
        }
        if (pane == currentPane())
            setWindowTitle(pane->title());
    });
    connect(pane, &DocumentPane::documentChanged, this, [this, pane] {
        if (pane == currentPane())
            updateOutline();
    });
    connect(pane, &DocumentPane::historyChanged, this, &MainWindow::updateActions);
    connect(pane, &DocumentPane::statusMessage, this, [this](const QString &message) {
        if (message.isEmpty())
            statusBar()->clearMessage();
        else
            statusBar()->showMessage(message);
    });
    connect(pane, &DocumentPane::openInNewTab, this,
            [this](const QString &path, const QString &anchor) { openFile(path, anchor, OpenIn::NewTab); });
    auto track = [this, pane] {
        if (pane == currentPane())
            m_outline->setCurrent(pane->view()->currentHeading());
    };
    connect(pane->view()->verticalScrollBar(), &QScrollBar::valueChanged, this, track);
    connect(pane->view(), &DocumentView::layoutChanged, this, track);
    // Ctrl+wheel zoom in one view becomes the zoom for all of them.
    connect(pane->view(), &DocumentView::zoomChanged, this, [](qreal zoom) {
        if (!qFuzzyCompare(zoom, AppSettings::instance().get().zoom))
            AppSettings::instance().update([zoom](Settings &s) { s.zoom = zoom; });
    });
    return pane;
}

DocumentPane *MainWindow::openFile(const QString &path, const QString &anchor, OpenIn where)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    // Compared with links resolved, so a symbolic link to an open file
    // switches to its tab instead of opening the same file twice.
    const QString canonical = DocumentPane::canonicalise(absolute);
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto *pane = qobject_cast<DocumentPane *>(m_tabs->widget(i));
        if (pane && pane->canonicalPath() == canonical) {
            m_tabs->setCurrentIndex(i);
            if (!anchor.isEmpty())
                pane->view()->scrollToAnchor(anchor);
            return pane;
        }
    }

    if (where == OpenIn::CurrentTab) {
        if (DocumentPane *pane = currentPane())
            return loadInto(pane, absolute, anchor) ? pane : nullptr;
    }

    DocumentPane *pane = addPane();
    if (!loadInto(pane, absolute, anchor)) {
        delete pane;
        return nullptr;
    }
    const int index = m_tabs->addTab(pane, pane->title());
    m_tabs->setTabToolTip(index, plainToolTip(pane->path()));
    m_tabs->setCurrentIndex(index);
    m_stack->setCurrentWidget(m_tabs);
    pane->view()->setFocus();
    return pane;
}

// Loads a file into a pane, reporting a failure to the user. Returns false
// when nothing was loaded, whether it failed or the user declined.
bool MainWindow::loadInto(DocumentPane *pane, const QString &path, const QString &anchor)
{
    const LoadResult result = pane->load(path, anchor);
    if (result.failed())
        showPlainWarning(this, tr("Cannot Open File"), result.message);
    return bool(result);
}

void MainWindow::closeTab(int index)
{
    QWidget *pane = m_tabs->widget(index);
    m_tabs->removeTab(index);
    delete pane;
    if (m_tabs->count() == 0) {
        m_stack->setCurrentIndex(0);
        setWindowTitle(tr("Markdown Glass"));
    }
}

void MainWindow::currentChanged()
{
    if (DocumentPane *pane = currentPane())
        setWindowTitle(pane->title());
    statusBar()->clearMessage();
    updateOutline();
    updateActions();
}

void MainWindow::updateOutline()
{
    DocumentPane *pane = currentPane();
    m_outline->setDocument(pane ? pane->document() : nullptr);
    if (pane)
        m_outline->setCurrent(pane->view()->currentHeading());
}

void MainWindow::updateActions()
{
    DocumentPane *pane = currentPane();
    const bool has = pane != nullptr;
    for (QAction *action :
         {m_reload, m_edit, m_print, m_printPreview, m_copy, m_selectAll, m_find, m_findNext, m_findPrevious})
        action->setEnabled(has);
    m_back->setEnabled(has && pane->canGoBack());
    m_forward->setEnabled(has && pane->canGoForward());
}

void MainWindow::applySettings()
{
    const Settings &s = AppSettings::instance().get();
    for (const auto &[action, setting] : std::as_const(m_settingToggles))
        action->setChecked(s.*setting);
    m_outline->setVisible(s.outline);
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto *pane = qobject_cast<DocumentPane *>(m_tabs->widget(i)))
            pane->applySettings(s);
    }
}

void MainWindow::openDialog()
{
    DocumentPane *pane = currentPane();
    const QString dir = pane ? QFileInfo(pane->path()).absolutePath() : QString();
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Open Markdown File"), dir,
        tr("Markdown files (*.md *.markdown *.mdown *.mkd *.mkdn *.mdwn);;Text files (*.txt);;All files (*)"));
    for (const QString &file : files)
        openFile(file);
}

void MainWindow::editCurrent()
{
    DocumentPane *pane = currentPane();
    if (!pane || pane->path().isEmpty())
        return;
    QString error;
    if (!openInEditor(pane->path(), AppSettings::instance().get().editorCommand, &error))
        showPlainWarning(this, tr("Cannot Open Editor"), error);
}

void MainWindow::print(PrintMode mode)
{
    DocumentPane *pane = currentPane();
    if (!pane || !pane->document())
        return;
    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(pane->title());
    if (mode == PrintMode::Preview) {
        // A parentless window, so it gets its own taskbar entry and cannot
        // get lost behind the main window; exec() still keeps it modal.
        QPrintPreviewDialog dialog(&printer, nullptr, Qt::Window);
        dialog.setWindowTitle(tr("Print Preview - %1").arg(pane->title()));
        dialog.setWindowIcon(windowIcon());
        dialog.resize(size().expandedTo(MinPreviewSize));
        connect(&dialog, &QPrintPreviewDialog::paintRequested, this, [pane](QPrinter *p) {
            if (pane->document())
                md::printDocument(p, *pane->document(), pane->images());
        });
        dialog.exec();
        return;
    }
    QPrintDialog dialog(&printer, this);
    if (dialog.exec() == QDialog::Accepted)
        md::printDocument(&printer, *pane->document(), pane->images());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QSettings geometry;
    geometry.setValue("window/geometry", saveGeometry());
    geometry.setValue("window/state", saveState());
    QMainWindow::closeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl &url : urls) {
        if (url.isLocalFile())
            openFile(url.toLocalFile());
    }
    event->acceptProposedAction();
}
