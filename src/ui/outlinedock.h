#pragma once

#include <QDockWidget>
#include <QHash>

class QTreeWidget;
class QTreeWidgetItem;

namespace md {
struct Block;
struct Document;
} // namespace md

// Sidebar listing the document's headings as a tree.
class OutlineDock : public QDockWidget
{
    Q_OBJECT
    // Qt's generated code needs the complete Block type for the signal below.
    Q_MOC_INCLUDE("model/document.h")
public:
    explicit OutlineDock(QWidget *parent = nullptr);

    void setDocument(const md::Document *doc);
    void setCurrent(const md::Block *heading);

signals:
    void headingActivated(const md::Block *heading);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void syncHighlight();

    const md::Block *m_current = nullptr;
    QTreeWidget *m_tree;
    QHash<const md::Block *, QTreeWidgetItem *> m_items;
};
