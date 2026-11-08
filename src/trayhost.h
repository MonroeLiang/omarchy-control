#pragma once

#include <QDBusServiceWatcher>
#include <QObject>

// Whether something on the desktop shows tray icons: Omarchy's bar does, through the
// StatusNotifierWatcher it registers. Only then can a closed window wait in the tray,
// since without a host there would be no way back to it. Offscreen runs (tests and the
// screenshot harness) never have one, so they cannot put an icon in the real bar.
class TrayHost : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
public:
    explicit TrayHost(QObject *parent = nullptr);
    bool available() const { return m_available; }

signals:
    void availableChanged();

private:
    void set(bool available);
    QDBusServiceWatcher m_watcher;
    bool m_available = false;
};
