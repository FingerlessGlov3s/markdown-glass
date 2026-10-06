#pragma once

#include <QByteArrayView>

// True if an SVG can be drawn without reading anything outside itself: every
// href and CSS url() points into the document or at an inline data: image
// (itself checked the same way when it is an SVG), there is no style sheet
// import or entity declaration, and the XML is well formed. QtSvg would
// otherwise load referenced local files, so anything it might follow that is
// not clearly internal is refused. QtCore only, so it can be fuzzed.
bool svgIsSelfContained(QByteArrayView svg);
