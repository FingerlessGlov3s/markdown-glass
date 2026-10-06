#pragma once

#include <QRectF>
#include <QSizeF>
#include <QString>

class QPainter;

namespace md {

// Supplies decoded images to layout and painting. ImageLoader is the real
// implementation; layout and printing only see this interface.
class ImageSource
{
public:
    enum State { Ready, Pending, Blocked, Failed };
    virtual ~ImageSource() = default;

    // Returns the image's state. Not a plain getter: the first request for a
    // source starts loading it (a worker thread for local files, a network
    // fetch for remote ones if allowed), so building a layout triggers I/O.
    // When a load finishes, the implementation asks for a new layout.
    virtual State request(const QString &src) = 0;
    // Size in device-independent pixels; only meaningful when Ready.
    virtual QSizeF naturalSize(const QString &src) = 0;
    // A preview (minimap, print, PNG) shows the image without anyone watching
    // it, so it does not count as the image being on screen for animation.
    virtual void paint(QPainter &painter, const QString &src, const QRectF &target, bool preview) = 0;
};

} // namespace md
