#pragma once

#include "view/documentview.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QToolButton;

class FindBar : public QWidget
{
    Q_OBJECT
public:
    explicit FindBar(QWidget *parent = nullptr);

    void activate(const QString &initialText = QString());
    QString text() const;
    bool caseSensitive() const;
    void setResult(int current, int total);

signals:
    void queryChanged();
    void next(FindDirection direction);
    void closed();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    QLineEdit *m_edit;
    QToolButton *m_case;
    QLabel *m_count;
};
