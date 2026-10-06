#pragma once

#include <QString>
#include <QUrl>

// Where a link in a document leads. Links come from untrusted files, so only
// the kinds listed here are ever acted on directly.
struct LinkTarget {
    enum Kind {
        Anchor,      // a heading in the current document
        Markdown,    // another markdown file, opened in the viewer
        Web,         // http/https, handed to the default browser
        Mail,        // mailto
        OtherFile,   // a local file or folder we will not launch
        Unsupported, // any other scheme
    };
    Kind kind = Unsupported;
    QString path;   // Markdown, OtherFile: absolute local path
    QString anchor; // Anchor, Markdown
    QUrl url;       // Web, Mail
};

LinkTarget resolveLink(const QString &href, const QString &baseDir);
bool isMarkdownFile(const QString &path);
