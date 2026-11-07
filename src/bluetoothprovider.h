#pragma once

#include <QObject>
#include <QVariantList>

#include "changecoordinator.h"
#include "commandrunner.h"

// Common Bluetooth controls. Pairing needs an agent to answer PIN prompts, which
// Omarchy's panel provides, so pairing stays there.
class BluetoothProvider : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool powered READ powered NOTIFY changed)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    // What is under way per device address ("connect", "disconnect", "forget"), and the
    // last failure per address until that device is acted on again.
    Q_PROPERTY(QVariantMap pending READ pending NOTIFY changed)
    Q_PROPERTY(QVariantMap failures READ failures NOTIFY changed)

public:
    BluetoothProvider(CommandRunner *runner, ChangeCoordinator *coordinator, QObject *parent = nullptr);

    bool powered() const { return m_powered; }
    QVariantList devices() const { return m_devices; }
    QString status() const { return m_status; }
    bool busy() const { return m_busy; }
    QVariantMap pending() const { return m_pending; }
    QVariantMap failures() const { return m_failures; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setPowered(bool on);
    Q_INVOKABLE void connectDevice(const QString &address);
    Q_INVOKABLE void disconnectDevice(const QString &address);
    Q_INVOKABLE void forgetDevice(const QString &address);

    static QVariantMap parseInfo(const QByteArray &text, const QString &address);
    static bool isValidAddress(const QString &address);

signals:
    void changed();
    void message(const QString &text, bool isError);

private:
    void deviceAction(const QString &action, const QString &address, std::function<bool(const QVariantMap &)> reached, const QString &done);

    CommandRunner *m_runner;
    ChangeCoordinator *m_coordinator;
    bool m_powered = false;
    bool m_busy = false;
    QVariantList m_devices;
    QVariantMap m_pending, m_failures;
    QString m_status = "loading";
    int m_generation = 0;
};
