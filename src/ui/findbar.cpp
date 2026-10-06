#include "ui/findbar.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>

namespace {
constexpr int MaxQueryWidth = 360; // the search field does not stretch across a wide window
constexpr int BarMarginX = 6;      // around the bar's controls
constexpr int BarMarginY = 4;
} // namespace

FindBar::FindBar(QWidget *parent)
    : QWidget(parent)
    , m_edit(new QLineEdit(this))
    , m_case(new QToolButton(this))
    , m_count(new QLabel(this))
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(BarMarginX, BarMarginY, BarMarginX, BarMarginY);

    auto *close = new QToolButton(this);
    close->setIcon(QIcon::fromTheme(QStringLiteral("dialog-close")));
    close->setAutoRaise(true);
    close->setToolTip(tr("Close (Esc)"));

    m_edit->setPlaceholderText(tr("Find in document"));
    m_edit->setClearButtonEnabled(true);
    m_edit->setMaximumWidth(MaxQueryWidth);

    auto *previous = new QToolButton(this);
    previous->setIcon(QIcon::fromTheme(QStringLiteral("go-up")));
    previous->setAutoRaise(true);
    previous->setToolTip(tr("Previous match (Shift+Enter)"));
    auto *following = new QToolButton(this);
    following->setIcon(QIcon::fromTheme(QStringLiteral("go-down")));
    following->setAutoRaise(true);
    following->setToolTip(tr("Next match (Enter)"));

    m_case->setText(QStringLiteral("Aa"));
    m_case->setCheckable(true);
    m_case->setAutoRaise(true);
    m_case->setToolTip(tr("Match case"));

    layout->addWidget(close);
    layout->addWidget(m_edit, 1);
    layout->addWidget(previous);
    layout->addWidget(following);
    layout->addWidget(m_case);
    layout->addWidget(m_count);
    layout->addStretch(1);

    connect(close, &QToolButton::clicked, this, &FindBar::closed);
    connect(m_edit, &QLineEdit::textChanged, this, &FindBar::queryChanged);
    connect(m_case, &QToolButton::toggled, this, &FindBar::queryChanged);
    connect(previous, &QToolButton::clicked, this, [this] { emit next(FindDirection::Backward); });
    connect(following, &QToolButton::clicked, this, [this] { emit next(FindDirection::Forward); });
}

void FindBar::activate(const QString &initialText)
{
    show();
    if (!initialText.isEmpty())
        m_edit->setText(initialText);
    m_edit->setFocus();
    m_edit->selectAll();
}

QString FindBar::text() const
{
    return m_edit->text();
}

bool FindBar::caseSensitive() const
{
    return m_case->isChecked();
}

void FindBar::setResult(int current, int total)
{
    if (m_edit->text().isEmpty())
        m_count->clear();
    else if (total == 0)
        m_count->setText(tr("No matches"));
    else
        m_count->setText(tr("%1 of %2").arg(current + 1).arg(total));
}

void FindBar::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        emit closed();
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        emit next(event->modifiers() & Qt::ShiftModifier ? FindDirection::Backward : FindDirection::Forward);
    } else {
        QWidget::keyPressEvent(event);
    }
}
