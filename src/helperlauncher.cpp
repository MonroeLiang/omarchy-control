#include "helperlauncher.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>

HelperLauncher::HelperLauncher(CommandRunner *runner, Capabilities *capabilities, QObject *parent)
    : QObject(parent), m_runner(runner), m_capabilities(capabilities) {}

QStringList HelperLauncher::resolve(const QJsonObject &launch, const Env &env, bool hasUwsm) {
    const QString kind = launch.value("kind").toString();
    QStringList argv;
    if (kind == QLatin1String("terminal")) {
        // This wrapper interprets its argument as a command line, so only the
        // static strings in the reviewed catalog ever reach it.
        argv << "omarchy-launch-floating-terminal-with-presentation" << launch.value("command").toString();
    } else if (kind == QLatin1String("shell")) {
        argv << "bash" << "-c" << launch.value("command").toString();
    } else if (kind == QLatin1String("exec")) {
        for (const auto &part : launch.value("argv").toArray())
            argv << env.expand(part.toString());
    } else if (kind == QLatin1String("app")) {
        if (hasUwsm)
            argv << "uwsm-app" << "--";
        for (const auto &part : launch.value("argv").toArray())
            argv << part.toString();
    } else if (kind == QLatin1String("panel")) {
        argv << "omarchy" << "shell" << "shell" << launch.value("verb").toString("toggle") << launch.value("target").toString();
        if (launch.contains("arg"))
            argv << launch.value("arg").toString();
    } else if (kind == QLatin1String("config")) {
        argv << "omarchy" << "launch" << "config" << "editor" << env.expand(launch.value("file").toString());
    } else if (kind == QLatin1String("url")) {
        argv << "omarchy" << "launch" << "webapp" << launch.value("url").toString();
    }
    return argv;
}

void HelperLauncher::launch(const CatalogEntry &entry, Callback callback) {
    Outcome outcome;
    const auto tr = [](const char *text) { return QCoreApplication::translate("HelperLauncher", text); };
    const auto availability = m_capabilities->availability(entry);
    if (!availability.available) {
        outcome.message = availability.reason;
        callback(outcome);
        return;
    }
    const QString kind = entry.launch.value("kind").toString();
    const QString label = QCoreApplication::translate("catalog", entry.label.toUtf8().constData());
    if (kind == QLatin1String("page")) {
        outcome.opened = true;
        outcome.navigatePage = entry.launch.value("page").toString();
        outcome.navigateFocus = entry.launch.value("focus").toString();
        callback(outcome);
        return;
    }
    // Package operations cannot overlap, and pacman's own error arrives too late, in a
    // terminal, after someone has already started something.
    const QString text = entry.launch.value("command").toString() + " " + entry.launch.value("argv").toVariant().toStringList().join(' ');
    static const QRegularExpression packageWork(R"(omarchy-(install|remove|update|pkg|reinstall|tui-(install|remove)|webapp-(install|remove))|omarchy (install|remove|update|pkg))");
    if (packageWork.match(text).hasMatch() && QFileInfo::exists(m_runner->env().sys("/var/lib/pacman/db.lck"))) {
        outcome.message = tr("Another package operation is running. Try again when it finishes.");
        callback(outcome);
        return;
    }
    const QStringList argv = resolve(entry.launch, m_runner->env(), m_capabilities->hasCommand("uwsm-app"));
    if (argv.isEmpty()) {
        outcome.message = tr("%1 has no way to open on this version of Omarchy.").arg(label);
        callback(outcome);
        return;
    }
    if (kind == QLatin1String("panel")) {
        // IPC answers right away, so a stopped shell can be reported directly.
        m_runner->run(argv, CommandOptions{4000}, this, [callback, label, tr](const CommandResult &result) {
            Outcome out;
            out.opened = result.ok();
            out.message = result.ok() ? tr("Opened %1").arg(label)
                                      : tr("%1 could not open because the Omarchy shell did not answer.").arg(label);
            callback(out);
        });
        return;
    }
    QString error;
    outcome.opened = m_runner->launchDetached(argv, &error);
    outcome.message = outcome.opened ? tr("Opened %1").arg(label) : error;
    callback(outcome);
}
