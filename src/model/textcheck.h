#pragma once

#include <QByteArrayView>

namespace md {

// The UTF-8 byte order mark some editors write; it is not part of the text.
constexpr QByteArrayView Utf8Bom("\xEF\xBB\xBF");

// Why some bytes do not look like a text document.
enum class NotText {
    No,                // looks like text
    NulBytes,          // binary formats (images, PDFs, archives, 3D models) are full of them
    ControlCharacters, // many characters that never appear in written text
    InvalidUtf8,       // a lot of byte sequences that are not UTF-8
};

// Decides whether a file's contents are plausibly text (markdown in any
// encoding close to UTF-8) rather than binary data shown as gibberish. Only
// the start of the data is examined: binary formats give themselves away early.
NotText checkText(QByteArrayView data);

} // namespace md
