#include "ui/outlinedock.h"

#include "model/document.h"
#include "ui/plaintext.h"

#include <QApplication>
#include <QHeaderView>
#include <QMouseEvent>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QTreeWidget>

Q_DECLARE_METATYPE(const md::Block *)

namespace {

constexpr int LevelIndent = 14;  // per heading level
constexpr int ElideReserve = 12; // width kept free beyond the focus frame, so the style never elides again

// Shortens over-long titles before the style sees them. Left to the style,
// Breeze drops its left margin on rows whose text does not fit, so elided and
// unelided rows would start at different positions.
class OutlineDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        const QStyle *style = option->widget ? option->widget->style() : QApplication::style();
        const int margins =
            2 * (style->pixelMetric(QStyle::PM_FocusFrameHMargin, option, option->widget) + 1) + ElideReserve;
        option->text =
            option->fontMetrics.elidedText(option->text, Qt::ElideRight, qMax(0, option->rect.width() - margins));
        option->textElideMode = Qt::ElideNone;
    }
};

} // namespace

OutlineDock::OutlineDock(QWidget *parent)
    : QDockWidget(tr("Outline"), parent)
    , m_tree(new QTreeWidget(this))
{
    setObjectName(QStringLiteral("outlineDock"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);

    m_tree->setHeaderHidden(true);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setItemDelegate(new OutlineDelegate(m_tree));
    m_tree->setIndentation(LevelIndent);
    setWidget(m_tree);
    m_tree->viewport()->installEventFilter(this);

    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) {
        emit headingActivated(item->data(0, Qt::UserRole).value<const md::Block *>());
    });
    connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        emit headingActivated(item->data(0, Qt::UserRole).value<const md::Block *>());
    });
}

void OutlineDock::setDocument(const md::Document *doc)
{
    m_tree->clear();
    m_items.clear();
    m_current = nullptr;
    if (!doc)
        return;

    // Each entry nests under the closest preceding heading of a lower level.
    QList<QPair<int, QTreeWidgetItem *>> stack;
    for (const md::OutlineEntry &entry : doc->outline) {
        while (!stack.isEmpty() && stack.last().first >= entry.level)
            stack.removeLast();
        auto *item = stack.isEmpty() ? new QTreeWidgetItem(m_tree) : new QTreeWidgetItem(stack.last().second);
        item->setText(0, entry.title);
        item->setToolTip(0, plainToolTip(entry.title));
        item->setData(0, Qt::UserRole, QVariant::fromValue(entry.block));
        m_items.insert(entry.block, item);
        stack.append({entry.level, item});
    }
    m_tree->expandAll();
}

void OutlineDock::setCurrent(const md::Block *heading)
{
    m_current = heading;
    syncHighlight();
}

void OutlineDock::syncHighlight()
{
    QTreeWidgetItem *item = m_items.value(m_current);
    if (item == m_tree->currentItem())
        return;
    const QSignalBlocker blocker(m_tree);
    m_tree->setCurrentItem(item);
    if (item)
        m_tree->scrollToItem(item);
}

// The highlight always shows where the document is. Dragging across the list
// with the button held must not move it, and a press that does not end in a
// click (released over another row) snaps it back.
bool OutlineDock::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_tree->viewport()) {
        if (event->type() == QEvent::MouseMove && static_cast<QMouseEvent *>(event)->buttons() != Qt::NoButton)
            return true;
        if (event->type() == QEvent::MouseButtonRelease)
            QTimer::singleShot(0, this, &OutlineDock::syncHighlight);
    }
    return QDockWidget::eventFilter(watched, event);
}
