#pragma once

#include <QDialog>

// About box: version, the libraries the application is built on, and the
// licence notices that must accompany the bundled parser.
class AboutDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AboutDialog(QWidget *parent = nullptr);
};
