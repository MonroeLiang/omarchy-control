#pragma once

#include <QObject>
#include <QVariantList>

#include "changecoordinator.h"
#include "commandrunner.h"

// Keyboard shortcuts as Hyprland reports them, plus editing of the bindings this
// app owns. A collision is reported, never silently replaced, and the shortcut
// that opens this window's way back (the launcher) needs an explicit decision.
class ShortcutsProvider : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList binds READ binds NOTIFY changed)
    Q_PROPERTY(QVariantList ownedBinds READ ownedBinds NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)

public:
    ShortcutsProvider(CommandRunner *runner, ChangeCoordinator *coordinator, QObject *parent = nullptr);

    QVariantList binds() const { return m_binds; }
    QVariantList ownedBinds() const;
    QString status() const { return m_status; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantMap checkKeys(const QString &keys) const;
    Q_INVOKABLE void addBinding(const QString &keys, const QString &description, const QString &command, bool replaceExisting);
    Q_INVOKABLE void removeBinding(const QString &keys);

    static QVariantList parseBinds(const QByteArray &json);
    static QString modsToText(int modmask);
    static bool isValidKeys(const QString &keys);
    static QString canonicalKeys(const QString &keys);

signals:
    void changed();
    void message(const QString &text, bool isError);

private:
    void submit(const QString &label, const QString &after, std::function<bool(class HyprModule &, QString *)> edit,
                std::function<bool(const QVariantList &)> verified, const QString &successText);

    CommandRunner *m_runner;
    ChangeCoordinator *m_coordinator;
    QVariantList m_binds;
    QString m_status = "loading";
};
