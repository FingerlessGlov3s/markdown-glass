#include "highlight/highlighter.h"

#include "model/document.h"

#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/State>
#include <KSyntaxHighlighting/Theme>

namespace md {

namespace {

// Limits that keep hostile or enormous code blocks from stalling the UI.
constexpr int MaxHighlightChars = 400'000;
constexpr int MaxHighlightLineLength = 4'000;

// Loading the syntax index is the expensive part, so it is created on first
// use and shared; documents without fenced languages never pay for it.
KSyntaxHighlighting::Repository &repository()
{
    static KSyntaxHighlighting::Repository repo;
    return repo;
}

KSyntaxHighlighting::Definition definitionFor(const QString &language)
{
    if (language.isEmpty())
        return {};

    static const QHash<QString, QString> aliases = {
        {QStringLiteral("sh"), QStringLiteral("Bash")},
        {QStringLiteral("shell"), QStringLiteral("Bash")},
        {QStringLiteral("console"), QStringLiteral("Bash")},
        {QStringLiteral("zsh"), QStringLiteral("Zsh")},
        {QStringLiteral("js"), QStringLiteral("JavaScript")},
        {QStringLiteral("jsx"), QStringLiteral("JavaScript React (JSX)")},
        {QStringLiteral("ts"), QStringLiteral("TypeScript")},
        {QStringLiteral("tsx"), QStringLiteral("TypeScript React (TSX)")},
        {QStringLiteral("py"), QStringLiteral("Python")},
        {QStringLiteral("python3"), QStringLiteral("Python")},
        {QStringLiteral("rb"), QStringLiteral("Ruby")},
        {QStringLiteral("rs"), QStringLiteral("Rust")},
        {QStringLiteral("golang"), QStringLiteral("Go")},
        {QStringLiteral("yml"), QStringLiteral("YAML")},
        {QStringLiteral("cpp"), QStringLiteral("C++")},
        {QStringLiteral("cxx"), QStringLiteral("C++")},
        {QStringLiteral("cs"), QStringLiteral("C#")},
        {QStringLiteral("csharp"), QStringLiteral("C#")},
        {QStringLiteral("objc"), QStringLiteral("Objective-C")},
        {QStringLiteral("docker"), QStringLiteral("Dockerfile")},
        {QStringLiteral("make"), QStringLiteral("Makefile")},
        {QStringLiteral("md"), QStringLiteral("Markdown")},
        {QStringLiteral("patch"), QStringLiteral("Diff")},
        {QStringLiteral("ps1"), QStringLiteral("PowerShell")},
        {QStringLiteral("pwsh"), QStringLiteral("PowerShell")},
        {QStringLiteral("kt"), QStringLiteral("Kotlin")},
        {QStringLiteral("tex"), QStringLiteral("LaTeX")},
        {QStringLiteral("conf"), QStringLiteral("INI Files")},
        {QStringLiteral("ini"), QStringLiteral("INI Files")},
    };

    auto &repo = repository();
    auto def = repo.definitionForName(aliases.value(language, language));
    if (!def.isValid())
        def = repo.definitionForFileName(QStringLiteral("x.") + language);
    return def;
}

} // namespace

class CodeHighlighter::Impl : public KSyntaxHighlighting::AbstractHighlighter
{
public:
    HighlightedLines run(const QString &code)
    {
        HighlightedLines lines;
        KSyntaxHighlighting::State state;
        // Must split exactly as layout does, so the ranges line up with lines.
        for (const QStringView line : codeLines(code)) {
            m_current.clear();
            if (line.size() <= MaxHighlightLineLength)
                state = highlightLine(line, state);
            lines.append(m_current);
        }
        return lines;
    }

protected:
    void applyFormat(int offset, int length, const KSyntaxHighlighting::Format &format) override
    {
        if (length <= 0 || format.isDefaultTextStyle(theme()))
            return;
        HighlightRange r;
        r.start = offset;
        r.length = length;
        if (format.hasTextColor(theme())) {
            r.color = format.textColor(theme()).rgb();
            r.hasColor = true;
        }
        r.bold = format.isBold(theme());
        r.italic = format.isItalic(theme());
        r.underline = format.isUnderline(theme());
        m_current.append(r);
    }

private:
    QList<HighlightRange> m_current;
};

CodeHighlighter::CodeHighlighter() = default;
CodeHighlighter::~CodeHighlighter() = default;

void CodeHighlighter::setDark(bool dark)
{
    if (m_dark != dark) {
        m_dark = dark;
        clear();
    }
}

void CodeHighlighter::clear()
{
    m_cache.clear();
}

const HighlightedLines *CodeHighlighter::highlight(const Block &block)
{
    if (block.language.isEmpty() || block.code.size() > MaxHighlightChars)
        return nullptr;
    if (const auto it = m_cache.constFind(&block); it != m_cache.constEnd())
        return it->isEmpty() ? nullptr : &*it;

    const auto def = definitionFor(block.language);
    if (!def.isValid()) {
        m_cache.insert(&block, {});
        return nullptr;
    }
    if (!m_impl)
        m_impl = std::make_unique<Impl>();
    m_impl->setDefinition(def);
    m_impl->setTheme(repository().defaultTheme(m_dark ? KSyntaxHighlighting::Repository::DarkTheme
                                                      : KSyntaxHighlighting::Repository::LightTheme));
    return &*m_cache.insert(&block, m_impl->run(block.code));
}

} // namespace md
