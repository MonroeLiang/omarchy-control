#pragma once

#include <QList>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

#include "changecoordinator.h"
#include "commandrunner.h"
#include "iconthemes.h"

// Which icon pack the desktop uses, and keeping it that way. On the reviewed Omarchy the
// one setting that matters is GSettings: GTK reads it, and Qt follows through the gtk3
// platform theme Omarchy sets. Config files are only brought along when they already
// name an icon theme, so nothing is created for a toolkit that is not in use.
//
// "follow-theme" and "pack:<id>" are the values, so no pack's name can pass for following
// the desktop theme, whatever its folder is called.
class IconThemeProvider : public QObject {
    Q_OBJECT

public:
    struct Consumer {
        QString id, label, path, group, key, value;
        QString blocked;  // Why it cannot be written, when it cannot.
        bool present = true;  // The file is there. A missing one is created.
        bool named = true;    // The file names an icon theme. A missing key is added.
    };

    struct State {
        bool known = false;
        QString error;
        QString effective;
        bool managed = false;
        bool unreadablePolicy = false;
        bool fixed = false;
        QString selected;
        QString desktopDefault;
        bool selectedInstalled = true;
        bool effectiveInstalled = true;
        QString hook;  // absent, owned, edited
        bool suspended = false;
        QList<Consumer> consumers;

        QString value() const;
        QString reason() const;
        bool conflict() const;
        QStringList disagreeing() const;
        QVariantMap toVariant() const;
    };

    using StateCallback = std::function<void(const State &)>;
    using ChoicesCallback = std::function<void(const QVariantList &)>;
    using Finished = ChangeCoordinator::Finished;

    static constexpr const char *FollowTheme = "follow-theme";
    static constexpr const char *PackPrefix = "pack:";

    IconThemeProvider(CommandRunner *runner, ChangeCoordinator *coordinator, QObject *parent = nullptr);

    void readState(StateCallback callback);
    void readChoices(ChoicesCallback callback);

    // What a change will touch, for its plan and its journal entry.
    QStringList plannedFiles() const;
    QStringList plannedEffects() const;
    // The bare folder name is accepted too, which is what someone types at a terminal.
    static QString normalizeChoice(const QString &value);

    void submit(const QString &choice, const QString &kind, const QString &label, Finished finished);
    void undo(const Transaction &original, Finished finished);

    // Run by the owned hook after a theme change, with no window open. Resolves everything
    // when it runs, so an older switch's hook cannot bring back an older theme's pack.
    // `forTheme` is the theme the hook ran for. If another has replaced it since, that
    // switch has a hook of its own still to come, so this one has nothing to do.
    int reapply(QString *message, const QString &forTheme = {});
    // For uninstalling: takes away what this app added, and leaves the icons as they are.
    int removeIntegration(QString *message);

    // The samples for a pack's tile: where each one's picture is, and whether the pack
    // drew it or inherited it. Only called for tiles on screen.
    QVariantMap samples(const QString &id) const;
    QVariantList listJson(QStringList *diagnostics) const;

    QString policyPath() const;
    QString hookPath() const;
    QString runtimeStatePath() const;
    static QByteArray hookScript();
    QString postUpdateHookPath() const;
    static QByteArray postUpdateHookScript();

    // The launcher icon layer (iconlayer.h). It follows the saved choice: the fixed pack,
    // or what the desktop theme names. With no choice saved there is nothing to follow.
    QString layerPack(QString *why = nullptr) const;
    // Builds, refreshes, or removes the layer to match the switch and the choice. Only
    // reads files and holds its own lock, so it is safe on a worker thread.
    int syncLayer(bool ifStale, QString *message = nullptr) const;
    QVariantMap layerState() const;
    static QString layerReason(const QVariantMap &state);
    int retireIconic(QString *message) const;

    // Removing a pack the user installed: it goes to the Trash, and undo brings it back.
    // What is in use, or what another pack needs, stays.
    struct Removal {
        bool ok = false;
        QString reason, name, path;
    };
    Removal planRemoval(const QString &id) const;
    void removePack(const QString &id, Finished finished);
    void undoRemoval(const Transaction &original, Finished finished);
    int restoreIconic(QString *message) const;

private:
    struct Policy {
        bool exists = false, valid = false, fixed = false;
        QString theme;
    };
    struct Reapplied {
        int conflicts = 0;
        bool suspended = false;
        QString lastApplied;
    };

    Policy loadPolicy() const;
    QByteArray policyBytes(const QString &choice) const;
    QString hookState() const;
    QList<Consumer> consumers() const;
    Reapplied loadReapplied() const;
    void saveReapplied(const Reapplied &state) const;
    // `from`, when given, limits this to files still holding what this app last wrote, so
    // an automatic run never overwrites a hand edit. A deliberate choice passes nothing.
    bool patchConsumers(const QString &target, QString *error, const QString &from = {}) const;
    bool consumersMatch(const QString &target, QString *which) const;

    void readEffective(std::function<void(bool ok, const QString &value, const QString &error)> callback);
    // The value read before the change lock could be taken may be out of date by the time it
    // is held, and going ahead from it would record the wrong way back.
    void confirmUnchanged(const QString &expected, std::function<void()> proceed, ChangeCoordinator::Done done);
    void settle(const QString &target);
    void readUnset(std::function<void(bool unset)> callback);
    void writeEffective(const QString &value, bool unset, ChangeCoordinator::Done done);
    void verifyEffective(const QString &expected, int attempts, ChangeCoordinator::Done done);
    void restoreEffective(const QString &ours, const QString &value, bool unset, ChangeCoordinator::Done done);
    Transaction failure(const QString &label, const QString &message) const;

    CommandRunner *m_runner;
    ChangeCoordinator *m_coordinator;
};
