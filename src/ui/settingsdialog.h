#pragma once

#include "app/settings.h"

#include <QDialog>

class QCheckBox;
class QGroupBox;
class QLineEdit;
class QRadioButton;
class QSpinBox;

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    void accept() override;

private:
    QGroupBox *readingGroup(const Settings &s);
    QGroupBox *filesGroup(const Settings &s);
    QGroupBox *privacyGroup(const Settings &s);

    QCheckBox *m_limitWidth = nullptr;
    QSpinBox *m_widthPixels = nullptr;
    QCheckBox *m_wrapCode = nullptr;
    QCheckBox *m_minimap = nullptr;
    QCheckBox *m_animateImages = nullptr;
    QCheckBox *m_linksInNewTab = nullptr;
    QCheckBox *m_liveReload = nullptr;
    QLineEdit *m_editorCommand = nullptr;
    QCheckBox *m_remoteImages = nullptr;
    QRadioButton *m_openWindow = nullptr;
    QRadioButton *m_openTab = nullptr;
};
