#pragma once

#include <QList>

class QPrinter;

namespace md {

struct Document;
class ImageSource;
class Layout;

// Chooses page break positions (document y coordinates, ascending, ending with
// the document height) so that no text line, image or table row is split.
QList<qreal> paginate(const Layout &layout, qreal pageHeight);

// Prints with a fixed light theme and wrapped code, whatever the screen shows.
void printDocument(QPrinter *printer, const Document &doc, ImageSource *images);

} // namespace md
