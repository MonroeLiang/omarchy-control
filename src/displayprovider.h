#pragma once

#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVariantList>

#include "changecoordinator.h"
#include "commandrunner.h"

// Monitor arrangement. A layout is previewed under a separate watchdog process,
// armed before the first change, which restores the previous layout unless told
// to keep it. It survives this window crashing and tolerates a monitor being
// unplugged mid-preview.
//
// Editing stays locked until a recovery check has passed against the running
// Hyprland: the check moves one display a few pixels, confirms Hyprland took the
// change, and confirms the watchdog then restores it. Passing is recorded per
// Hyprland version.
class DisplayProvider : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList monitors READ monitors NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool editingUnlocked READ editingUnlocked NOTIFY changed)
    Q_PROPERTY(bool previewing READ previewing NOTIFY previewChanged)
    Q_PROPERTY(int previewSeconds READ previewSeconds NOTIFY previewChanged)
    Q_PROPERTY(QString checkState READ checkState NOTIFY changed)

public:
    DisplayProvider(CommandRunner *runner, ChangeCoordinator *coordinator, QObject *parent = nullptr);

    QVariantList monitors() const { return m_monitors; }
    QString status() const { return m_status; }
    bool editingUnlocked() const;
    bool previewing() const { return m_previewing; }
    int previewSeconds() const { return m_previewSeconds; }
    QString checkState() const { return m_checkState; }

    Q_INVOKABLE void refresh();
    // `layout` is a list of {output, mode, x, y, scale, transform, enabled}.
    Q_INVOKABLE QString validateLayout(const QVariantList &layout) const;
    Q_INVOKABLE void preview(const QVariantList &layout);
    Q_INVOKABLE void keep();
    Q_INVOKABLE void revert();
    Q_INVOKABLE void runRecoveryCheck();

    static QVariantList parseMonitors(const QByteArray &json);
    static QString monitorLua(const QJsonObject &monitor);
    static double cleanScale(double requested, int width, int height);
    static bool isSafeOutputName(const QString &name);

    // The body of the hidden `display-watchdog` subcommand: restores the
    // previous layout on timeout or request, then exits. Blocks until done.
    static int runWatchdog(const Env &env, const QString &planPath);

    // Used by `omarchy-control selftest display`.
    QString recoveryCheckPath() const;

signals:
    void changed();
    void previewChanged();
    void message(const QString &text, bool isError);
    void checkFinished(bool passed, const QString &detail);

private:
    void startPreview(const QVariantList &layout, int seconds, bool isCheck);
    void applyLayout(const QVariantList &layout, int index, std::function<void(bool, const QString &)> done);
    void waitForWatchdog(const QString &file, const QString &expected, int attempts, std::function<void(bool)> done);
    void finishCheck();
    void failStart(const QString &text, bool isCheck);
    QString runDirectory() const;
    QStringList layoutSignature() const;

    CommandRunner *m_runner;
    ChangeCoordinator *m_coordinator;
    QVariantList m_monitors;
    QVariantList m_pendingLayout;
    QString m_status = "loading";
    QString m_checkState = "idle";
    QString m_hyprlandVersion;
    bool m_previewing = false;
    bool m_isCheck = false;
    bool m_starting = false;
    bool m_checkMoved = false;
    QStringList m_checkOriginal;
    QHash<QString, QString> m_startFingerprints;
    int m_previewSeconds = 0;
    QString m_planPath, m_keepFile, m_revertFile, m_resultFile;
    QTimer m_countdown;
};
