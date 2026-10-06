#pragma once

#include <QCache>
#include <QImage>
#include <QWidget>

class DocumentView;

// A scaled-down preview of the whole document that stands in for the vertical
// scrollbar: the highlighted box is the visible region and can be dragged.
class Minimap : public QWidget
{
    Q_OBJECT
public:
    explicit Minimap(DocumentView *view, QWidget *parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct Geometry {
        qreal scale = 0.1; // document pixels to minimap pixels
        qreal offset = 0;  // minimap pixels scrolled off the top
        qreal boxTop = 0;
        qreal boxHeight = 0;
        qreal boxSlope = 0; // boxTop per pixel of document scroll
    };
    // Not geometry(): QWidget already has one, meaning the widget's own rectangle.
    Geometry geometryNow() const;
    QImage tile(int index, const Geometry &g);
    void invalidate();
    void scrollTo(qreal boxTop);

    DocumentView *m_view;
    QCache<int, QImage> m_tiles;
    bool m_dragging = false;
    bool m_hovered = false;
    qreal m_grab = 0;
};
