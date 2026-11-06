#include "networkprovider.h"

#include <QDateTime>
#include <QRegularExpression>

static const QStringList ManagerCall{"busctl", "call", "org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager", "org.freedesktop.NetworkManager"};
// Long enough to cover a slow connection attempt and still leave time to decide.
static constexpr int CheckpointSeconds = 60;

NetworkProvider::NetworkProvider(CommandRunner *runner, ChangeCoordinator *coordinator, QObject *parent)
    : QObject(parent), m_runner(runner), m_coordinator(coordinator) {
    m_keepTimer.setInterval(1000);
    connect(&m_keepTimer, &QTimer::timeout, this, [this] {
        m_keepSeconds = qMax<qint64>(0, (m_checkpointDeadline - QDateTime::currentMSecsSinceEpoch()) / 1000);
        if (m_keepSeconds <= 0) {
            // NetworkManager rolled back by itself at the same moment.
            m_keepTimer.stop();
            m_checkpoint.clear();
            emit message(tr("The previous connection was restored."), false);
            refresh();
        }
        emit keepChanged();
    });
}

// nmcli's terse mode escapes ':' and '\' inside values.
QStringList NetworkProvider::splitTerse(const QString &line) {
    QStringList fields;
    QString current;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line[i];
        if (c == '\\' && i + 1 < line.size()) {
            current += line[++i];
        } else if (c == ':') {
            fields << current;
            current.clear();
        } else {
            current += c;
        }
    }
    fields << current;
    return fields;
}

QVariantList NetworkProvider::parseNetworks(const QByteArray &text) {
    QHash<QString, QVariantMap> best;
    QStringList order;
    for (const auto &line : QString::fromUtf8(text).split('\n', Qt::SkipEmptyParts)) {
        const auto fields = splitTerse(line);
        if (fields.size() < 4 || fields[1].isEmpty())
            continue;
        QVariantMap row;
        row["active"] = fields[0].trimmed() == QLatin1String("*");
        row["ssid"] = fields[1];
        row["signal"] = fields[2].toInt();
        row["security"] = fields[3];
        row["secured"] = !fields[3].isEmpty() && fields[3] != QLatin1String("--");
        const auto existing = best.constFind(fields[1]);
        if (existing == best.constEnd())
            order << fields[1];
        // One row per name: the strongest access point, unless another is in use.
        if (existing == best.constEnd() || row["active"].toBool() || (!existing->value("active").toBool() && row["signal"].toInt() > existing->value("signal").toInt()))
            best.insert(fields[1], row);
    }
    QVariantList rows;
    for (const auto &ssid : order)
        rows << best.value(ssid);
    std::stable_sort(rows.begin(), rows.end(), [](const QVariant &a, const QVariant &b) {
        const auto ma = a.toMap(), mb = b.toMap();
        if (ma.value("active").toBool() != mb.value("active").toBool())
            return ma.value("active").toBool();
        return ma.value("signal").toInt() > mb.value("signal").toInt();
    });
    return rows;
}

QVariantList NetworkProvider::parseDevices(const QByteArray &text) {
    QVariantList rows;
    for (const auto &line : QString::fromUtf8(text).split('\n', Qt::SkipEmptyParts)) {
        const auto fields = splitTerse(line);
        if (fields.size() < 4)
            continue;
        const QString type = fields[1];
        if (type != QLatin1String("wifi") && type != QLatin1String("ethernet") && type != QLatin1String("tun") && type != QLatin1String("wireguard") && type != QLatin1String("vpn"))
            continue;
        QVariantMap row;
        row["device"] = fields[0];
        row["type"] = type;
        row["state"] = fields[2];
        row["connection"] = fields[3];
        row["connected"] = fields[2].startsWith("connected");
        rows << row;
    }
    return rows;
}

QVariantList NetworkProvider::parseSaved(const QByteArray &text) {
    QVariantList rows;
    for (const auto &line : QString::fromUtf8(text).split('\n', Qt::SkipEmptyParts)) {
        const auto fields = splitTerse(line);
        if (fields.size() < 4)
            continue;
        const QString type = fields[2];
        if (type == QLatin1String("loopback") || type == QLatin1String("bridge") || type == QLatin1String("tun"))
            continue;
        QVariantMap row;
        row["name"] = fields[0];
        row["uuid"] = fields[1];
        row["type"] = type;
        row["active"] = fields[3] == QLatin1String("yes");
        rows << row;
    }
    return rows;
}

void NetworkProvider::refresh() {
    const int generation = ++m_generation;
    m_runner->run({"nmcli", "-t", "-f", "WIFI,CONNECTIVITY", "general"}, CommandOptions{4000}, this, [this, generation](const CommandResult &result) {
        if (generation != m_generation)
            return;
        if (!result.ok()) {
            m_status = "unavailable";
            emit changed();
            return;
        }
        const auto fields = splitTerse(result.outText());
        m_wifiEnabled = fields.value(0) == QLatin1String("enabled");
        m_connectivity = fields.value(1);
        m_status = "ok";
        emit changed();
    });
    m_runner->run({"nmcli", "-t", "-f", "DEVICE,TYPE,STATE,CONNECTION", "device"}, CommandOptions{4000}, this, [this](const CommandResult &result) {
        m_devices = parseDevices(result.out);
        emit changed();
    });
    m_runner->run({"nmcli", "-t", "-f", "NAME,UUID,TYPE,ACTIVE", "connection", "show"}, CommandOptions{4000}, this, [this](const CommandResult &result) {
        m_saved = parseSaved(result.out);
        emit changed();
    });
    // No rescan here: scanning takes seconds and can briefly disturb the radio,
    // so it only happens when someone asks.
    m_runner->run({"nmcli", "-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY", "device", "wifi", "list", "--rescan", "no"}, CommandOptions{4000}, this, [this](const CommandResult &result) {
        m_networks = parseNetworks(result.out);
        emit changed();
    });
}

void NetworkProvider::rescan() {
    m_busy = true;
    emit changed();
    CommandOptions options;
    options.timeoutMs = 15000;
    m_runner->run({"nmcli", "device", "wifi", "rescan"}, options, this, [this](const CommandResult &) {
        m_busy = false;
        refresh();
    });
}

void NetworkProvider::setWifiEnabled(bool on) {
    ChangeCoordinator::Request request;
    request.settingId = "network.wifi";
    request.label = tr("Wi-Fi");
    request.before = m_wifiEnabled ? "true" : "false";
    request.after = on ? "true" : "false";
    request.apply = [this, on](ChangeCoordinator::Done done) {
        CommandOptions options;
        options.mutation = true;
        m_runner->run({"nmcli", "radio", "wifi", on ? "on" : "off"}, options, this, [done](const CommandResult &result) { done(result.ok(), result.describe("nmcli")); });
    };
    request.verify = [this, on](ChangeCoordinator::Done done) {
        m_runner->run({"nmcli", "-t", "-f", "WIFI", "general"}, CommandOptions{4000}, this, [this, on, done](const CommandResult &result) {
            // A failed query proves nothing, in either direction.
            done(result.ok() && (result.outText() == QLatin1String("enabled")) == on, tr("Wi-Fi did not change state."));
        });
    };
    m_coordinator->submit(std::move(request), [this](const Transaction &tx) {
        if (tx.phase != QLatin1String("committed"))
            emit message(tx.message, true);
        refresh();
    });
}

void NetworkProvider::activate(const QString &uuid, const QString &name) {
    static const QRegularExpression valid("^[0-9a-fA-F-]{36}$");
    if (!valid.match(uuid).hasMatch()) {
        emit message(tr("That connection is no longer available."), true);
        return;
    }
    // NetworkManager refuses overlapping checkpoints, and the old one's rollback would
    // still fire underneath the new connection.
    if (m_busy || !m_checkpoint.isEmpty()) {
        emit message(tr("Finish with the current connection change first."), true);
        return;
    }
    m_busy = true;
    emit changed();
    auto start = [this, uuid, name] {
        CommandOptions options;
        options.mutation = true;
        options.timeoutMs = 30000;
        m_runner->run({"nmcli", "connection", "up", "uuid", uuid}, options, this, [this, name](const CommandResult &result) {
            m_busy = false;
            emit changed();
            if (!result.ok()) {
                emit message(tr("Could not connect to %1: %2").arg(name, result.describe("nmcli")), true);
                if (!m_checkpoint.isEmpty())
                    revertConnection();
                refresh();
                return;
            }
            if (m_checkpoint.isEmpty())
                emit message(tr("Connected to %1").arg(name), false);
            else {
                // The rollback timer started with the checkpoint, not with this line, so
                // the countdown shows what is really left.
                m_keepSeconds = qMax<qint64>(1, (m_checkpointDeadline - QDateTime::currentMSecsSinceEpoch()) / 1000);
                m_keepTimer.start();
                emit keepChanged();
            }
            refresh();
        });
    };
    // A checkpoint is protection, not a prerequisite: where polkit declines one,
    // the switch still goes ahead, as nmcli alone would.
    const qint64 created = QDateTime::currentMSecsSinceEpoch();
    m_runner->run(ManagerCall + QStringList{"CheckpointCreate", "aouu", "0", QString::number(CheckpointSeconds), "0"}, CommandOptions{5000}, this, [this, start, created](const CommandResult &result) {
        static const QRegularExpression path("\"(/org/freedesktop/NetworkManager/Checkpoint/[0-9]+)\"");
        const auto match = path.match(QString::fromUtf8(result.out));
        m_checkpoint = result.ok() && match.hasMatch() ? match.captured(1) : QString();
        m_checkpointDeadline = created + CheckpointSeconds * 1000LL;
        start();
    });
}

void NetworkProvider::keepConnection() {
    m_keepTimer.stop();
    m_keepSeconds = 0;
    const QString checkpoint = m_checkpoint;
    m_checkpoint.clear();
    emit keepChanged();
    if (checkpoint.isEmpty())
        return;
    CommandOptions options;
    options.mutation = true;
    m_runner->run(ManagerCall + QStringList{"CheckpointDestroy", "o", checkpoint}, options, this, [this](const CommandResult &result) {
        // If this fails, the checkpoint has probably already rolled back, so saying
        // "kept" would be false.
        if (result.ok())
            emit message(tr("Connection kept"), false);
        else
            emit message(tr("The connection may already have gone back to the previous one. Check it below."), true);
        refresh();
    });
}

void NetworkProvider::revertConnection() {
    m_keepTimer.stop();
    m_keepSeconds = 0;
    const QString checkpoint = m_checkpoint;
    m_checkpoint.clear();
    emit keepChanged();
    if (checkpoint.isEmpty())
        return;
    CommandOptions options;
    options.mutation = true;
    options.timeoutMs = 20000;
    m_runner->run(ManagerCall + QStringList{"CheckpointRollback", "o", checkpoint}, options, this, [this](const CommandResult &result) {
        if (!result.ok())
            emit message(tr("Restoring the previous connection failed: %1").arg(result.describe("NetworkManager")), true);
        else
            emit message(tr("The previous connection was restored."), false);
        refresh();
    });
}

void NetworkProvider::deactivate(const QString &device) {
    static const QRegularExpression valid("^[A-Za-z0-9._-]+$");
    if (!valid.match(device).hasMatch())
        return;
    CommandOptions options;
    options.mutation = true;
    m_runner->run({"nmcli", "device", "disconnect", device}, options, this, [this](const CommandResult &result) {
        if (!result.ok())
            emit message(result.describe("nmcli"), true);
        refresh();
    });
}
