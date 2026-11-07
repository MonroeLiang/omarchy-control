#pragma once

#include <QLockFile>
#include <QObject>
#include <QThread>
#include <QUrl>
#include <QVariantMap>
#include <atomic>
#include <memory>

#include "changecoordinator.h"
#include "iconthemes.h"

// Copies one extracted icon pack into the user's icons folder. The source is only ever
// read, nothing from the pack is run, and the copy appears all at once or not at all: it
// is built in a staging folder beside the destination, checked, then renamed into place
// by an operation that refuses to replace anything.
class IconThemeImporter : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString themeId READ themeId NOTIFY changed)
    Q_PROPERTY(QString themeName READ themeName NOTIFY changed)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool usable READ usable NOTIFY changed)

public:
    struct Inspection {
        bool ok = false;
        QString error;
        QString source, id, name, destination;
        QStringList shadows;         // The same pack elsewhere, which this copy will win over.
        QStringList missingParents;  // Reported, and never downloaded.
        bool hidden = false;
    };

    IconThemeImporter(const Env &env, ChangeCoordinator *coordinator, QObject *parent = nullptr);
    ~IconThemeImporter() override;

    QString state() const { return m_state; }
    QString message() const { return m_message; }
    QString themeId() const { return m_inspection.id; }
    QString themeName() const { return m_inspection.name; }
    qreal progress() const { return m_progress.load(); }
    bool busy() const { return m_state == QLatin1String("copying"); }
    bool usable() const { return m_state == QLatin1String("imported") && !m_inspection.hidden; }

    Inspection inspect(const QString &folder) const;

    // A folder from the chooser or a drop: both go through the same checks. If the pack
    // would hide another copy of itself this waits for confirm(), and says why.
    Q_INVOKABLE void choose(const QUrl &folder);
    void choosePath(const QString &folder);
    Q_INVOKABLE void confirm();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void dismiss();
    // Lets the interface report a problem with what was dropped in the same place.
    Q_INVOKABLE void reject(const QString &message);

    // Staging folders a previous run left behind. One is only removed if it is unchanged
    // from what that run recorded; anything else is left there and reported.
    QStringList recoverStaging();

signals:
    void changed();
    void progressChanged();
    void finished(bool imported);

private:
    void setState(const QString &state, const QString &message);
    void start();
    void conclude(bool ok, bool cancelled, const QString &error);

    Env m_env;
    ChangeCoordinator *m_coordinator;
    QString m_state = QStringLiteral("idle");
    QString m_message;
    Inspection m_inspection;
    std::atomic<qreal> m_progress{0};
    std::shared_ptr<std::atomic_bool> m_cancel;
    QThread *m_worker = nullptr;
    std::unique_ptr<QLockFile> m_importLock;
};
