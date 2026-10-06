#pragma once

#include <QHash>
#include <QList>
#include <QString>

namespace md {

struct HtmlToken {
    enum Kind { Text, Open, Close };
    Kind kind = Text;
    QString name;                  // lowercase tag name (Open/Close)
    QHash<QString, QString> attrs; // lowercase names, entity-decoded values (Open)
    QString text;                  // entity-decoded text (Text)
};

// Tolerant tokenizer for untrusted HTML fragments. Never fails: malformed
// markup degrades to text. Comments, declarations and the contents of
// <script>/<style> are discarded.
QList<HtmlToken> tokenizeHtml(QStringView html);

QString decodeHtmlEntities(QStringView text);

} // namespace md
