#include "trayhost.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QGuiApplication>

static const QString watcherService = QStringLiteral("org.kde.StatusNotifierWatcher");

TrayHost::TrayHost(QObject *parent) : QObject(parent) {
    if (QGuiApplication::platformName() == QLatin1String("offscreen"))
        return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    m_watcher.setConnection(bus);
    m_watcher.addWatchedService(watcherService);
    m_watcher.setWatchMode(QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration);
    // A bar that restarts brings a new watcher, which knows nothing of the icon, so the
    // icon is dropped and made again to register with it.
    connect(&m_watcher, &QDBusServiceWatcher::serviceRegistered, this, [this] {
        set(false);
        set(true);
    });
    connect(&m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this] { set(false); });
    m_available = bus.interface() && bus.interface()->isServiceRegistered(watcherService);
}

void TrayHost::set(bool available) {
    if (m_available == available)
        return;
    m_available = available;
    emit availableChanged();
}
