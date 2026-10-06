#pragma once

#include "app/settings.h"
#include "view/documentview.h"

#include <QMainWindow>

class DocumentPane;
class OutlineDock;
class QAction;
class QIcon;
class QKeySequence;
class QMenu;
class QStackedWidget;
class QTabWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Opens a file in a new tab or in the current one. A file that is already
    // open is brought to the front instead.
    DocumentPane *openFile(const QString &path, const QString &anchor = QString(), OpenIn where = OpenIn::NewTab);
    bool hasDocuments() const;

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    enum class PrintMode { Direct, Preview };

    void createActions();
    void createFileMenu();
    void createEditMenu();
    void createViewMenu();
    void createGoMenu();
    void createSettingsAndHelpMenus();
    void createToolBar();
    QAction *addSettingToggle(QMenu *menu, const QIcon &icon, const QString &text, const QString &iconText,
                              const QKeySequence &shortcut, const QString &toolTip, bool Settings::*setting);
    DocumentPane *currentPane() const;
    DocumentPane *addPane();
    bool loadInto(DocumentPane *pane, const QString &path, const QString &anchor);
    void closeTab(int index);
    void currentChanged();
    void updateOutline();
    void updateActions();
    void applySettings();
    void openDialog();
    void print(PrintMode mode);
    void editCurrent();

    QStackedWidget *m_stack;
    QTabWidget *m_tabs;
    OutlineDock *m_outline;

    QAction *m_open = nullptr;
    QAction *m_back = nullptr;
    QAction *m_forward = nullptr;
    QAction *m_reload = nullptr;
    QAction *m_edit = nullptr;
    QAction *m_print = nullptr;
    QAction *m_printPreview = nullptr;
    QAction *m_closeTab = nullptr;
    QAction *m_copy = nullptr;
    QAction *m_selectAll = nullptr;
    QAction *m_find = nullptr;
    QAction *m_findNext = nullptr;
    QAction *m_findPrevious = nullptr;
    QAction *m_limitWidth = nullptr;
    QAction *m_wrapCode = nullptr;
    QAction *m_minimap = nullptr;
    QAction *m_showOutline = nullptr;
    QList<QPair<QAction *, bool Settings::*>> m_settingToggles;
};
