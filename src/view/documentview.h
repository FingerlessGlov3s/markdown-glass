#pragma once

#include "layout/layout.h"
#include "view/limits.h"

#include <QAbstractScrollArea>
#include <QBasicTimer>
#include <QSet>

#include <memory>

class QScrollBar;

namespace md {
class CodeHighlighter;
}

// Which way findNext() moves through the matches.
enum class FindDirection { Forward, Backward };

// Where a link or file opens: in the tab that is showing now, or a new one.
enum class OpenIn { CurrentTab, NewTab };

// Read-only, natively painted view of a markdown document.
class DocumentView : public QAbstractScrollArea
{
    Q_OBJECT
public:
    explicit DocumentView(QWidget *parent = nullptr);
    ~DocumentView() override;

    // keepPosition restores the scroll position by source line (for live reload).
    void setDocument(std::unique_ptr<md::Document> doc, bool keepPosition = false);
    const md::Document *document() const { return m_doc.get(); }
    void setImageSource(md::ImageSource *images);

    void setWidthLimit(bool enabled, int pixels);
    void setWrapCode(bool wrap);
    void setZoom(qreal zoom);
    qreal zoom() const { return m_zoom; }

    void scrollToBlock(const md::Block *block);
    bool scrollToAnchor(const QString &anchor);
    // The last heading at or above the top of the viewport.
    const md::Block *currentHeading() const;

    bool hasSelection() const;
    QString selectedText() const;
    void copy();
    void selectAll();

    // Returns the number of matches; the first one at or after the viewport becomes current.
    int find(const QString &text, bool caseSensitive);
    void findNext(FindDirection direction);
    int matchCount() const { return int(m_matches.size()); }
    int currentMatch() const { return m_currentMatch; }
    void clearFind();

    // For the minimap (and tests).
    // Named to avoid hiding QWidget::layout(), which is something else entirely.
    const md::Layout *documentLayout() const { return m_layout.get(); }
    md::ImageSource *imageSource() const { return m_images; }
    qreal documentHeight() const; // layout height plus vertical padding
    qreal verticalPadding() const;
    // Document coordinates to viewport pixels, the inverse of toDocument();
    // tests use it to aim synthetic mouse events at laid-out text.
    QPoint toViewport(const QPointF &docPoint) const;

    // Rebuilds the layout, e.g. after images finish loading.
    void relayout();

signals:
    void linkActivated(const QString &href, OpenIn where);
    void linkHovered(const QString &href);
    void layoutChanged();
    void zoomChanged(qreal zoom);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void changeEvent(QEvent *event) override;
    void timerEvent(QTimerEvent *event) override;

private:
    struct Cursor {
        int box = -1;
        int pos = 0;
        bool operator==(const Cursor &o) const = default;
        bool operator<(const Cursor &o) const { return box != o.box ? box < o.box : pos < o.pos; }
    };

    // What the pointer is over that can be copied with one click.
    struct CopyTarget {
        enum Kind { None, CodeBlock, InlineCode };
        Kind kind = None;
        const md::Block *block = nullptr; // CodeBlock
        int box = -1;                     // InlineCode
        int span = -1;
        QRectF button; // document coordinates
        bool operator==(const CopyTarget &o) const = default;
    };

    void rebuildTheme();
    void doLayout(const md::Block *anchorBlock, qreal anchorDelta);
    void updateScrollRange();
    void updateFrameBars();
    void updateHover(const QPoint &viewportPos);
    void setSelectionEnd(const QPoint &viewportPos);
    void runFind();
    void showMatch(int index);
    void performCopy(const CopyTarget &target);
    CopyTarget copyTargetAt(const QPointF &docPoint, const md::Hit &hit) const;
    void paintCopyButton(QPainter &p, const CopyTarget &target) const;
    md::PaintState paintState() const;
    int frameAt(const QPointF &docPoint) const;

    QPointF toDocument(const QPoint &viewportPos) const;
    qreal scrollY() const;

    std::unique_ptr<md::Document> m_doc;
    std::unique_ptr<md::Layout> m_layout;
    std::unique_ptr<md::CodeHighlighter> m_highlighter;
    md::ImageSource *m_images = nullptr;
    md::Theme m_theme;
    QSet<const md::Block *> m_toggled;

    bool m_limitWidth = true;
    int m_limitPixels = ViewLimits::DefaultTextWidth;
    bool m_wrapCode = true;
    qreal m_zoom = 1.0;
    qreal m_contentX = 0;

    Cursor m_anchor;
    Cursor m_cursor;
    bool m_selecting = false;
    bool m_pressMoved = false;
    QPoint m_pressPos;
    QString m_pressLink;
    const md::Block *m_pressToggle = nullptr;
    QBasicTimer m_autoScroll;
    QPoint m_lastMousePos;

    int m_hoverBox = -1;
    int m_hoverLink = -1;
    QString m_hoverHref;
    CopyTarget m_copyTarget;
    CopyTarget m_copied;
    QBasicTimer m_copiedTimer;

    QString m_findText;
    bool m_findCaseSensitive = false;
    std::vector<md::FindMatch> m_matches;
    int m_currentMatch = -1;

    QList<QScrollBar *> m_frameBars;
    QBasicTimer m_resizeTimer;
    qint64 m_lastLayoutMs = 0;
};
