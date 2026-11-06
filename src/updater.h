#pragma once

#include <QObject>
#include <QVariantMap>
#include <functional>

#include "commandrunner.h"

// Finding and installing a newer release. Nothing here runs in the background: every
// request to GitHub starts with someone asking for it.
namespace Updater {

struct Release {
    QString version, tag, notes, pageUrl, packageName, packageUrl, sumsUrl;
    bool installable() const { return !packageUrl.isEmpty() && !sumsUrl.isEmpty(); }
};

struct Check {
    enum Outcome { Found, NoReleases, Failed };
    Outcome outcome = Failed;
    QString error;
    Release release;
};

QStringList checkCommand();
Check interpret(const CommandResult &result, const QString &architecture);
Check fetchBlocking(CommandRunner &runner);

// Versions are dotted numbers. Anything else is never newer, so a malformed tag can
// not start an install.
bool isNewer(const QString &candidate, const QString &current);
QString currentVersion();
// "omarchy-control 0.2.0-1" gives "0.2.0"; anything else gives nothing.
QString versionFromPacman(const QByteArray &output);
// Release notes arrive as markdown and are shown as plain text.
QString plainNotes(const QString &markdown);
// The hash listed for a file in a SHA256SUMS file, or nothing.
QString checksumFor(const QByteArray &sums, const QString &fileName);

// Downloads, verifies, and hands the package to pacman in this terminal, where sudo
// asks for the password: this process never sees it.
bool install(CommandRunner &runner, const Release &release, bool assumeYes, QString *error,
             const std::function<void(const QString &)> &progress = {});

}  // namespace Updater

// The About page's view of the above.
class UpdateProvider : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(QString notes READ notes NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool canInstall READ canInstall NOTIFY changed)
    Q_PROPERTY(bool installStarted READ installStarted NOTIFY changed)
    Q_PROPERTY(QVariantMap build READ build CONSTANT)

public:
    explicit UpdateProvider(CommandRunner *runner, QObject *parent = nullptr);

    // idle, checking, current, none, available, error, or restart.
    QString state() const { return m_state; }
    QString currentVersion() const { return Updater::currentVersion(); }
    QString latestVersion() const { return m_latest; }
    QString notes() const { return m_release.notes; }
    QString error() const { return m_error; }
    bool canInstall() const { return m_release.installable(); }
    bool installStarted() const { return m_installStarted; }
    QVariantMap build() const;

    Q_INVOKABLE void check();
    Q_INVOKABLE void install();
    Q_INVOKABLE void refreshInstalled();
    // True once the replacement is on its way, so the window can close.
    Q_INVOKABLE bool restart();

signals:
    void changed();
    void message(const QString &text, bool isError);

private:
    void setState(const QString &state);

    CommandRunner *m_runner;
    Updater::Release m_release;
    QString m_state = QStringLiteral("idle");
    QString m_latest, m_error;
    bool m_installStarted = false;
};
