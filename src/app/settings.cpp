#include "app/settings.h"

#include <QSettings>

#include <type_traits>

AppSettings &AppSettings::instance()
{
    static AppSettings settings;
    return settings;
}

AppSettings::AppSettings()
    : m_settings(load())
{ }

namespace {

// Every persisted setting with its key, in one place so that loading and
// saving cannot disagree. No group may be called "general": QSettings' INI
// format reserves that name and keys stored under it do not read back.
template <typename S, typename Visit> void forEachSetting(S &m, Visit &&visit)
{
    visit("view/limitWidth", m.limitWidth);
    visit("view/widthPixels", m.widthPixels);
    visit("view/wrapCode", m.wrapCode);
    visit("view/minimap", m.minimap);
    visit("view/animateImages", m.animateImages);
    visit("view/outline", m.outline);
    visit("view/zoom", m.zoom);
    visit("privacy/autoLoadRemoteImages", m.autoLoadRemoteImages);
    visit("files/openInTabs", m.openInTabs);
    visit("files/linksInNewTab", m.linksInNewTab);
    visit("files/liveReload", m.liveReload);
    visit("files/editorCommand", m.editorCommand);
}

} // namespace

Settings AppSettings::load()
{
    QSettings s;
    Settings m;
    forEachSetting(m, [&s](const char *key, auto &value) {
        value = s.value(QLatin1String(key), value).template value<std::decay_t<decltype(value)>>();
    });
    m.widthPixels = qBound(ViewLimits::MinTextWidth, m.widthPixels, ViewLimits::MaxTextWidth);
    m.zoom = qBound(ViewLimits::MinZoom, m.zoom, ViewLimits::MaxZoom);
    return m;
}

void AppSettings::set(const Settings &settings)
{
    m_settings = settings;
    QSettings s;
    forEachSetting(settings, [&s](const char *key, const auto &value) { s.setValue(QLatin1String(key), value); });
    emit changed();
}
