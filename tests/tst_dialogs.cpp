#include "model/parser.h"
#include "ui/aboutdialog.h"
#include "ui/settingsdialog.h"

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QSpinBox>
#include <QTest>

// The settings and About dialogs.
class TestDialogs : public QObject
{
    Q_OBJECT
private slots:
    void cleanup() { QApplication::closeAllWindows(); }

    void settingsDialogFitsItsText();
    void settingsDialogSavesEveryField();
    void aboutDialogShowsBundledLicence();
};

void TestDialogs::settingsDialogFitsItsText()
{
    SettingsDialog dialog;
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    const auto labels = dialog.findChildren<QLabel *>();
    for (const QLabel *label : labels) {
        if (label->wordWrap())
            QVERIFY2(label->height() >= label->heightForWidth(label->width()), "wrapped text is clipped");
    }
}

void TestDialogs::settingsDialogSavesEveryField()
{
    Settings before;
    before.outline = false; // not in the dialog: must survive untouched
    before.zoom = 1.7;
    AppSettings::instance().set(before);

    SettingsDialog dialog;
    // Every box the other way round, the width and editor changed, and the
    // other radio button.
    const auto boxes = dialog.findChildren<QCheckBox *>();
    QCOMPARE(boxes.size(), 7);
    for (QCheckBox *box : boxes)
        box->setChecked(!box->isChecked());
    auto *width = dialog.findChild<QSpinBox *>();
    QVERIFY(width);
    width->setValue(ViewLimits::MinTextWidth + 40);
    // The spin box has a line edit of its own; the editor field is the other one.
    QLineEdit *editor = nullptr;
    const auto edits = dialog.findChildren<QLineEdit *>();
    for (QLineEdit *edit : edits) {
        if (!qobject_cast<QSpinBox *>(edit->parentWidget()))
            editor = edit;
    }
    QVERIFY(editor);
    editor->setText(QStringLiteral("  kate %f  "));
    const auto radios = dialog.findChildren<QRadioButton *>();
    QCOMPARE(radios.size(), 2);
    for (QRadioButton *radio : radios) {
        if (!radio->isChecked())
            radio->setChecked(true);
    }
    dialog.accept();

    const Settings after = AppSettings::instance().get();
    QCOMPARE(after.limitWidth, !before.limitWidth);
    QCOMPARE(after.widthPixels, ViewLimits::MinTextWidth + 40);
    QCOMPARE(after.wrapCode, !before.wrapCode);
    QCOMPARE(after.minimap, !before.minimap);
    QCOMPARE(after.animateImages, !before.animateImages);
    QCOMPARE(after.linksInNewTab, !before.linksInNewTab);
    QCOMPARE(after.liveReload, !before.liveReload);
    QCOMPARE(after.autoLoadRemoteImages, !before.autoLoadRemoteImages);
    QCOMPARE(after.openInTabs, !before.openInTabs);
    QCOMPARE(after.editorCommand, QStringLiteral("kate %f"));
    QCOMPARE(after.outline, before.outline);
    QCOMPARE(after.zoom, before.zoom);

    // Cancelling changes nothing.
    SettingsDialog again;
    again.findChild<QSpinBox *>()->setValue(ViewLimits::MinTextWidth);
    again.reject();
    QCOMPARE(AppSettings::instance().get().widthPixels, ViewLimits::MinTextWidth + 40);
    AppSettings::instance().set(Settings());
}

void TestDialogs::aboutDialogShowsBundledLicence()
{
    AboutDialog dialog;
    const auto *own = dialog.findChild<QPlainTextEdit *>(QStringLiteral("projectLicence"));
    QVERIFY(own);
    QVERIFY(own->toPlainText().contains(QStringLiteral("FingerlessGloves")));
    QVERIFY(own->toPlainText().contains(QStringLiteral("endorse or promote")));
    const auto *notices = dialog.findChild<QPlainTextEdit *>(QStringLiteral("cmarkLicence"));
    QVERIFY(notices);
    QVERIFY(notices->toPlainText().contains(QStringLiteral("John MacFarlane")));
    QVERIFY(notices->toPlainText().contains(QStringLiteral("Redistributions in binary form")));

    // The library versions come from the libraries themselves.
    bool listed = false;
    const auto labels = dialog.findChildren<QLabel *>();
    for (const QLabel *label : labels)
        listed |= label->text().contains(QStringLiteral("cmark-gfm ") + md::parserVersion());
    QVERIFY(listed);
}

QTEST_MAIN(TestDialogs)
#include "tst_dialogs.moc"
