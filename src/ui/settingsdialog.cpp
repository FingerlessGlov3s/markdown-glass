#include "ui/settingsdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
constexpr int WidthStep = 20;           // pixels per click of the width spin box
constexpr qreal NoteFontScale = 0.9;    // the privacy note, a step smaller than the options
constexpr int NarrowestNoteWidth = 420; // pixels; see privacyGroup()
} // namespace

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Configure Markdown Glass"));
    const Settings s = AppSettings::instance().get();
    auto *root = new QVBoxLayout(this);
    root->addWidget(readingGroup(s));
    root->addWidget(filesGroup(s));
    root->addWidget(privacyGroup(s));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    root->addWidget(buttons);
}

QGroupBox *SettingsDialog::readingGroup(const Settings &s)
{
    auto *reading = new QGroupBox(tr("Reading"), this);
    auto *readingLayout = new QVBoxLayout(reading);
    auto *widthRow = new QHBoxLayout;
    m_limitWidth = new QCheckBox(tr("Limit the text width to"), reading);
    m_limitWidth->setChecked(s.limitWidth);
    m_widthPixels = new QSpinBox(reading);
    m_widthPixels->setRange(ViewLimits::MinTextWidth, ViewLimits::MaxTextWidth);
    m_widthPixels->setSingleStep(WidthStep);
    m_widthPixels->setSuffix(tr(" px"));
    m_widthPixels->setValue(s.widthPixels);
    m_widthPixels->setEnabled(s.limitWidth);
    connect(m_limitWidth, &QCheckBox::toggled, m_widthPixels, &QWidget::setEnabled);
    widthRow->addWidget(m_limitWidth);
    widthRow->addWidget(m_widthPixels);
    widthRow->addStretch(1);
    readingLayout->addLayout(widthRow);
    m_wrapCode = new QCheckBox(tr("Wrap long lines in code blocks"), reading);
    m_wrapCode->setToolTip(tr("When off, each code block gets its own horizontal scrollbar."));
    m_wrapCode->setChecked(s.wrapCode);
    readingLayout->addWidget(m_wrapCode);
    m_minimap = new QCheckBox(tr("Show the document preview in place of the scrollbar"), reading);
    m_minimap->setChecked(s.minimap);
    readingLayout->addWidget(m_minimap);
    m_animateImages = new QCheckBox(tr("Play animated images"), reading);
    m_animateImages->setToolTip(tr("When off, animated GIF and WebP images show only their first frame."));
    m_animateImages->setChecked(s.animateImages);
    readingLayout->addWidget(m_animateImages);
    return reading;
}

QGroupBox *SettingsDialog::filesGroup(const Settings &s)
{
    auto *files = new QGroupBox(tr("Files"), this);
    auto *filesLayout = new QVBoxLayout(files);
    filesLayout->addWidget(new QLabel(tr("When a file is opened from the file manager or command line:"), files));
    m_openWindow = new QRadioButton(tr("Open it in a new window"), files);
    m_openTab = new QRadioButton(tr("Open it as a tab in the current window"), files);
    (s.openInTabs ? m_openTab : m_openWindow)->setChecked(true);
    filesLayout->addWidget(m_openWindow);
    filesLayout->addWidget(m_openTab);
    m_linksInNewTab = new QCheckBox(tr("Open links to other markdown files in a new tab"), files);
    m_linksInNewTab->setToolTip(tr("When off, the linked file replaces the current one and Back returns to it. "
                                   "Ctrl+click always opens a new tab."));
    m_linksInNewTab->setChecked(s.linksInNewTab);
    filesLayout->addWidget(m_linksInNewTab);
    m_liveReload = new QCheckBox(tr("Reload automatically when the file changes on disk"), files);
    m_liveReload->setChecked(s.liveReload);
    filesLayout->addWidget(m_liveReload);
    auto *editorRow = new QHBoxLayout;
    editorRow->addWidget(new QLabel(tr("Editor for the Edit button:"), files));
    m_editorCommand = new QLineEdit(s.editorCommand, files);
    m_editorCommand->setPlaceholderText(tr("System default text editor"));
    m_editorCommand->setClearButtonEnabled(true);
    m_editorCommand->setToolTip(tr("Leave empty to use the text editor chosen in System Settings. "
                                   "Otherwise enter a command, for example: code\n"
                                   "The file is added at the end, or wherever %f appears."));
    editorRow->addWidget(m_editorCommand, 1);
    filesLayout->addLayout(editorRow);
    return files;
}

QGroupBox *SettingsDialog::privacyGroup(const Settings &s)
{
    auto *privacy = new QGroupBox(tr("Privacy"), this);
    auto *privacyLayout = new QVBoxLayout(privacy);
    m_remoteImages = new QCheckBox(tr("Load remote images automatically"), privacy);
    m_remoteImages->setChecked(s.autoLoadRemoteImages);
    privacyLayout->addWidget(m_remoteImages);
    auto *disclaimer =
        new QLabel(tr("Loading an image from the internet tells the server hosting it your IP address and "
                      "that you opened the document, and can be used to track you. When this is off, remote "
                      "images are blocked until you choose \"Load remote images\" for a document."),
                   privacy);
    disclaimer->setWordWrap(true);
    QFont small = disclaimer->font();
    small.setPointSizeF(small.pointSizeF() * NoteFontScale);
    disclaimer->setFont(small);
    disclaimer->setForegroundRole(QPalette::PlaceholderText);
    // A top-level window does not grow for a wrapped label's height, so
    // reserve the height the text needs at the narrowest width it can get.
    disclaimer->setMinimumWidth(NarrowestNoteWidth);
    disclaimer->setMinimumHeight(disclaimer->heightForWidth(NarrowestNoteWidth));
    privacyLayout->addWidget(disclaimer);
    return privacy;
}

void SettingsDialog::accept()
{
    Settings s = AppSettings::instance().get();
    s.limitWidth = m_limitWidth->isChecked();
    s.widthPixels = m_widthPixels->value();
    s.wrapCode = m_wrapCode->isChecked();
    s.minimap = m_minimap->isChecked();
    s.animateImages = m_animateImages->isChecked();
    s.linksInNewTab = m_linksInNewTab->isChecked();
    s.liveReload = m_liveReload->isChecked();
    s.editorCommand = m_editorCommand->text().trimmed();
    s.autoLoadRemoteImages = m_remoteImages->isChecked();
    s.openInTabs = m_openTab->isChecked();
    AppSettings::instance().set(s);
    QDialog::accept();
}
