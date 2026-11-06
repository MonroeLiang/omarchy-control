#pragma once

#include <QObject>
#include <QVariantList>

#include "changecoordinator.h"
#include "commandrunner.h"

// Background services and login items. System-level actions go through systemctl
// so that the desktop's own polkit agent asks for authorization; the app itself
// stays unprivileged.
class ServicesProvider : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList userServices READ userServices NOTIFY changed)
    Q_PROPERTY(QVariantList systemServices READ systemServices NOTIFY changed)
    Q_PROPERTY(QVariantList startupApps READ startupApps NOTIFY changed)
    Q_PROPERTY(int failedCount READ failedCount NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)

public:
    ServicesProvider(CommandRunner *runner, ChangeCoordinator *coordinator, QObject *parent = nullptr);

    QVariantList userServices() const { return m_user; }
    QVariantList systemServices() const { return m_system; }
    QVariantList startupApps() const { return m_startup; }
    int failedCount() const;
    QString status() const { return m_status; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void serviceAction(const QString &unit, const QString &action, bool system);
    Q_INVOKABLE void setStartupEnabled(const QString &id, bool enabled);

    static bool isSafeUnit(const QString &unit);
    static QVariantList mergeUnits(const QByteArray &units, const QByteArray &files);
    static QVariantList readStartup(const Env &env);
    static QByteArray withHidden(const QByteArray &entry, bool hidden);

signals:
    void changed();
    void message(const QString &text, bool isError);

private:
    void loadScope(bool system);

    CommandRunner *m_runner;
    ChangeCoordinator *m_coordinator;
    QVariantList m_user, m_system, m_startup;
    QString m_status = "loading";
};
