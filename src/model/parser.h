#pragma once

#include "model/document.h"

#include <QByteArray>

#include <memory>

namespace md {

// Parses GitHub Flavored Markdown (UTF-8) into the document model.
std::unique_ptr<Document> parseMarkdown(const QByteArray &utf8);

// The version of the bundled cmark-gfm, for the About dialog.
QString parserVersion();

} // namespace md
