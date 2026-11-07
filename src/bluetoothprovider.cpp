#include "bluetoothprovider.h"

#include <QRegularExpression>
#include <QTimer>

BluetoothProvider::BluetoothProvider(CommandRunner *runner, ChangeCoordinator *coordinator, QObject *parent)
    : QObject(parent), m_runner(runner), m_coordinator(coordinator) {}

bool BluetoothProvider::isValidAddress(const QString &address) {
    static const QRegularExpression valid("^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$");
    return valid.match(address).hasMatch();
}

QVariantMap BluetoothProvider::parseInfo(const QByteArray &text, const QString &address) {
    QVariantMap device;
    device["address"] = address;
    device["connected"] = false;
    device["paired"] = false;
    device["battery"] = -1;
    for (const auto &raw : QString::fromUtf8(text).split('\n')) {
        const QString line = raw.trimmed();
        const int colon = line.indexOf(':');
        if (colon < 0)
            continue;
        const QString key = line.left(colon);
        const QString value = line.mid(colon + 1).trimmed();
        if (key == QLatin1String("Name") || (key == QLatin1String("Alias") && !device.contains("name")))
            device["name"] = value;
        else if (key == QLatin1String("Connected"))
            device["connected"] = value == QLatin1String("yes");
        else if (key == QLatin1String("Paired"))
            device["paired"] = value == QLatin1String("yes");
        else if (key == QLatin1String("Icon"))
            device["kind"] = value;
        else if (key == QLatin1String("Battery Percentage")) {
            static const QRegularExpression percent(R"(\((\d+)\))");
            const auto match = percent.match(value);
            if (match.hasMatch())
                device["battery"] = match.captured(1).toInt();
        }
    }
    if (!device.contains("name"))
        device["name"] = address;
    return device;
}

void BluetoothProvider::refresh() {
    const int generation = ++m_generation;
    m_runner->run({"omarchy", "bluetooth", "power", "is-on"}, CommandOptions{5000}, this, [this, generation](const CommandResult &power) {
        if (generation != m_generation)
            return;
        if (power.failure != CommandResult::None) {
            m_status = "unavailable";
            emit changed();
            return;
        }
        m_powered = power.exitCode == 0;
        m_runner->run({"bluetoothctl", "devices"}, CommandOptions{5000}, this, [this, generation](const CommandResult &list) {
            if (generation != m_generation)
                return;
            QStringList addresses;
            static const QRegularExpression device("^Device ([0-9A-Fa-f:]{17})");
            for (const auto &line : QString::fromUtf8(list.out).split('\n')) {
                const auto match = device.match(line);
                if (match.hasMatch())
                    addresses << match.captured(1);
            }
            m_status = "ok";
            if (addresses.isEmpty()) {
                m_devices.clear();
                emit changed();
                return;
            }
            auto results = std::make_shared<QHash<QString, QVariantMap>>();
            auto remaining = std::make_shared<int>(addresses.size());
            for (const auto &address : addresses) {
                m_runner->run({"bluetoothctl", "info", address}, CommandOptions{5000}, this, [this, generation, address, addresses, results, remaining](const CommandResult &info) {
                    results->insert(address, parseInfo(info.out, address));
                    if (--*remaining > 0 || generation != m_generation)
                        return;
                    QVariantList rows;
                    for (const auto &a : addresses)
                        rows << results->value(a);
                    m_devices = rows;
                    for (const auto &key : m_failures.keys())
                        if (!addresses.contains(key))
                            m_failures.remove(key);
                    emit changed();
                });
            }
        });
    });
}

void BluetoothProvider::setPowered(bool on) {
    ChangeCoordinator::Request request;
    request.settingId = "bluetooth.power";
    request.label = tr("Bluetooth");
    request.before = m_powered ? "true" : "false";
    request.after = on ? "true" : "false";
    request.apply = [this, on](ChangeCoordinator::Done done) {
        CommandOptions options;
        options.mutation = true;
        m_runner->run({"omarchy", "bluetooth", "power", on ? "on" : "off"}, options, this, [done](const CommandResult &result) {
            done(result.ok(), result.describe("Bluetooth"));
        });
    };
    request.verify = [this, on](ChangeCoordinator::Done done) {
        m_runner->run({"omarchy", "bluetooth", "power", "is-on"}, CommandOptions{5000}, this, [this, on, done](const CommandResult &result) {
            done((result.exitCode == 0) == on, tr("Bluetooth did not change state."));
        });
    };
    m_busy = true;
    emit changed();
    m_coordinator->submit(std::move(request), [this](const Transaction &tx) {
        m_busy = false;
        if (tx.phase != QLatin1String("committed"))
            emit message(tx.message, true);
        refresh();
    });
}

void BluetoothProvider::deviceAction(const QString &action, const QString &address, std::function<bool(const QVariantMap &)> reached, const QString &doneText) {
    if (!isValidAddress(address)) {
        emit message(tr("That is not a valid device address."), true);
        return;
    }
    ChangeCoordinator::Request request;
    request.settingId = "bluetooth.device";
    request.label = tr("Bluetooth device");
    request.after = action;
    request.apply = [this, action, address](ChangeCoordinator::Done done) {
        CommandOptions options;
        options.mutation = true;
        options.timeoutMs = 45000;
        m_runner->run({"omarchy", "bluetooth", "device", action, address}, options, this, [done](const CommandResult &result) {
            done(result.ok(), result.describe("Bluetooth"));
        });
    };
    // Omarchy's device command ignores bluetoothctl's exit status, so success is
    // only ever established by asking BlueZ afterward.
    request.verify = [this, address, reached](ChangeCoordinator::Done done) {
        m_runner->run({"bluetoothctl", "info", address}, CommandOptions{5000}, this, [this, address, reached, done](const CommandResult &info) {
            done(reached(parseInfo(info.out, address)), tr("The device did not respond. Check that it is on and in range."));
        });
    };
    m_busy = true;
    m_pending.insert(address, action);
    m_failures.remove(address);
    emit changed();
    m_coordinator->submit(std::move(request), [this, doneText, address](const Transaction &tx) {
        m_busy = false;
        m_pending.remove(address);
        const bool ok = tx.phase == QLatin1String("committed");
        if (!ok)
            m_failures.insert(address, tx.message);
        emit message(ok ? doneText : tx.message, !ok);
        refresh();
    });
}

void BluetoothProvider::connectDevice(const QString &address) {
    deviceAction("connect", address, [](const QVariantMap &d) { return d.value("connected").toBool(); }, tr("Connected"));
}

void BluetoothProvider::disconnectDevice(const QString &address) {
    deviceAction("disconnect", address, [](const QVariantMap &d) { return !d.value("connected").toBool(); }, tr("Disconnected"));
}

void BluetoothProvider::forgetDevice(const QString &address) {
    deviceAction("forget", address, [](const QVariantMap &d) { return !d.value("paired").toBool(); }, tr("Device forgotten"));
}
