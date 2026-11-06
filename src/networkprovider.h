#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>

#include "changecoordinator.h"
#include "commandrunner.h"

// Common connections. A switch that could cut the connection runs under a
// NetworkManager checkpoint, which restores the previous state by itself if the
// user does not keep the result, even when this app has died. Passwords are
// never collected here: joining a secured network goes to Omarchy's panel.
class NetworkProvider : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool wifiEnabled READ wifiEnabled NOTIFY changed)
    Q_PROPERTY(QString connectivity READ connectivity NOTIFY changed)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
    Q_PROPERTY(QVariantList networks READ networks NOTIFY changed)
    Q_PROPERTY(QVariantList saved READ saved NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(int keepSeconds READ keepSeconds NOTIFY keepChanged)
    Q_PROPERTY(bool awaitingKeep READ awaitingKeep NOTIFY keepChanged)

public:
    NetworkProvider(CommandRunner *runner, ChangeCoordinator *coordinator, QObject *parent = nullptr);

    bool wifiEnabled() const { return m_wifiEnabled; }
    QString connectivity() const { return m_connectivity; }
    QVariantList devices() const { return m_devices; }
    QVariantList networks() const { return m_networks; }
    QVariantList saved() const { return m_saved; }
    QString status() const { return m_status; }
    bool busy() const { return m_busy; }
    int keepSeconds() const { return m_keepSeconds; }
    bool awaitingKeep() const { return !m_checkpoint.isEmpty() && m_keepSeconds > 0; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void rescan();
    Q_INVOKABLE void setWifiEnabled(bool on);
    Q_INVOKABLE void activate(const QString &uuid, const QString &name);
    Q_INVOKABLE void deactivate(const QString &device);
    Q_INVOKABLE void keepConnection();
    Q_INVOKABLE void revertConnection();

    static QStringList splitTerse(const QString &line);
    static QVariantList parseNetworks(const QByteArray &text);
    static QVariantList parseDevices(const QByteArray &text);
    static QVariantList parseSaved(const QByteArray &text);

signals:
    void changed();
    void keepChanged();
    void message(const QString &text, bool isError);

private:
    CommandRunner *m_runner;
    ChangeCoordinator *m_coordinator;
    bool m_wifiEnabled = false;
    QString m_connectivity;
    QVariantList m_devices, m_networks, m_saved;
    QString m_status = "loading";
    bool m_busy = false;
    QString m_checkpoint;
    qint64 m_checkpointDeadline = 0;
    int m_keepSeconds = 0;
    QTimer m_keepTimer;
    int m_generation = 0;
};
