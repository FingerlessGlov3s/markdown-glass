#pragma once

#include "view/limits.h"

#include <QObject>
#include <QString>

struct Settings {
    bool limitWidth = true;
    int widthPixels = ViewLimits::DefaultTextWidth;
    bool wrapCode = true;
    bool minimap = true;
    bool animateImages = true;
    bool outline = true;
    bool autoLoadRemoteImages = false;
    bool openInTabs = false;    // files opened from outside: tab in the current window, or a new window
    bool linksInNewTab = false; // links to other markdown files: new tab, or replace the current document
    bool liveReload = true;
    QString editorCommand; // for the Edit button; empty means the system default text editor
    double zoom = 1.0;
};

// Process-wide settings, persisted with QSettings.
class AppSettings : public QObject
{
    Q_OBJECT
public:
    static AppSettings &instance();

    const Settings &get() const { return m_settings; }
    // Reads the stored values; done once at startup, exposed for tests.
    static Settings load();
    void set(const Settings &settings);
    // Changes some fields and persists the result:
    // update([](Settings &s) { s.zoom = 1.5; });
    template <typename Fn> void update(Fn &&change)
    {
        Settings s = m_settings;
        change(s);
        set(s);
    }

signals:
    void changed();

private:
    AppSettings();
    Settings m_settings;
};
