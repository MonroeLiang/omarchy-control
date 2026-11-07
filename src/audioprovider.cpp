#include "audioprovider.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

#include "iconthemes.h"

static int volumePercent(const QJsonObject &volume) {
    int highest = 0;
    bool any = false;
    for (auto it = volume.begin(); it != volume.end(); ++it) {
        const int percent = it.value().toObject().value("value_percent").toString().remove('%').toInt();
        highest = qMax(highest, percent);
        any = true;
    }
    return any ? highest : -1;
}

QVariantMap AudioProvider::nodeNames(const QString &description, const QJsonObject &properties) {
    const QString card = properties.value("device.description").toString();
    QString port = properties.value("device.profile.description").toString();
    QString title = description, subtitle;
    const QString rest = card.isEmpty() || !description.startsWith(card) ? QString() : description.mid(card.size()).trimmed();
    if (rest.isEmpty()) {
        // One device, one port, as a Bluetooth headset is: its name is the name, and the
        // port says how it is being used.
        if (!port.isEmpty() && port != description)
            subtitle = port;
    } else if (!port.isEmpty()) {
        static const QRegularExpression channels(R"(\(([^()]+)\)\s*$)");
        static const QRegularExpression direction(R"(\s+(Output|Input)$)");
        static const QRegularExpression numbered(R"(\d)");
        static const QRegularExpression family(R"(^(.*\))\s+(\S.*)$)");
        port.remove(direction);
        // "Maker Chip (Series 3) HD Audio": the words after the model say what kind of
        // device it is, which a plain port like "Speaker" needs and "HDMI 1" does not.
        QString maker = card, kind;
        const auto split = family.match(card);
        if (split.hasMatch()) {
            maker = split.captured(1);
            kind = split.captured(2);
        }
        title = !kind.isEmpty() && !numbered.match(port).hasMatch() ? kind + " " + port : port;
        subtitle = maker;
        const auto layout = channels.match(rest);
        if (layout.hasMatch() && layout.captured(1) != port)
            subtitle += QString::fromUtf8(" · ") + layout.captured(1);
    }
    return {{"title", title}, {"subtitle", subtitle}};
}

static QString nodeKind(const QJsonObject &properties, const QString &description, bool source) {
    if (source)
        return QStringLiteral("mic");
    const QString hint = properties.value("device.icon_name").toString() + " " + properties.value("device.form_factor").toString();
    if (hint.contains("headphone") || hint.contains("headset"))
        return QStringLiteral("headphones");
    if (hint.contains("video-display") || description.contains("HDMI") || description.contains("DisplayPort"))
        return QStringLiteral("displays");
    if (properties.value("device.bus").toString() == QLatin1String("bluetooth"))
        return QStringLiteral("bluetooth");
    if (description.contains("Laptop", Qt::CaseInsensitive))
        return QStringLiteral("laptop");
    return QStringLiteral("speaker");
}

void AudioProvider::matchStreams(QVariantList &streams, const QVariantList &outputs) {
    for (auto &value : streams) {
        auto stream = value.toMap();
        const int sink = stream.value("sink").toInt();
        QString output;
        // pactl names a stream's sink by the same index it lists sinks with; older
        // PipeWire made that the object id, so either one counts.
        for (const auto &row : outputs)
            if (row.toMap().value("index").toInt() == sink)
                output = row.toMap().value("name").toString();
        if (output.isEmpty())
            for (const auto &row : outputs)
                if (row.toMap().value("objectId").toString() == QString::number(sink))
                    output = row.toMap().value("name").toString();
        stream["output"] = output;
        value = stream;
    }
}

QVariantList AudioProvider::parseNodes(const QByteArray &json, const QString &defaultName, bool sources) {
    QVariantList rows;
    for (const auto &value : QJsonDocument::fromJson(json).array()) {
        const auto node = value.toObject();
        const auto properties = node.value("properties").toObject();
        const QString name = node.value("name").toString();
        // Every sink has a monitor source; none of them is a microphone.
        if (sources && (properties.value("device.class").toString() == QLatin1String("monitor") || name.endsWith(".monitor") ||
                        !node.value("monitor_of_sink").toString().isEmpty()))
            continue;
        QVariantMap row;
        row["name"] = name;
        row["description"] = node.value("description").toString(name);
        row["index"] = node.value("index").toInt(-1);
        row["objectId"] = properties.value("object.id").toString();
        const auto names = nodeNames(row["description"].toString(), properties);
        row["title"] = names.value("title");
        row["subtitle"] = names.value("subtitle");
        row["kind"] = nodeKind(properties, row["description"].toString(), sources);
        row["volume"] = volumePercent(node.value("volume").toObject());
        row["mute"] = node.value("mute").toBool();
        row["state"] = node.value("state").toString();
        row["isDefault"] = name == defaultName;
        rows << row;
    }
    return rows;
}

QVariantList AudioProvider::parseStreams(const QByteArray &json) {
    QVariantList rows;
    for (const auto &value : QJsonDocument::fromJson(json).array()) {
        const auto stream = value.toObject();
        const auto properties = stream.value("properties").toObject();
        // A DSP chain's own output is a sink input with no application name;
        // moving it would rewire the processing itself.
        const QString app = properties.value("application.name").toString();
        if (app.isEmpty() || app == QLatin1String("EasyEffects"))
            continue;
        QVariantMap row;
        row["index"] = stream.value("index").toInt();
        row["app"] = app;
        row["title"] = properties.value("media.name").toString();
        row["sink"] = stream.value("sink").toInt();
        row["iconName"] = properties.value("application.icon_name").toString();
        row["binary"] = properties.value("application.process.binary").toString();
        row["volume"] = volumePercent(stream.value("volume").toObject());
        rows << row;
    }
    return rows;
}

AudioProvider::AudioProvider(CommandRunner *runner, ChangeCoordinator *coordinator, QObject *parent)
    : QObject(parent), m_runner(runner), m_coordinator(coordinator) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(200);
    connect(&m_debounce, &QTimer::timeout, this, &AudioProvider::refresh);
}

AudioProvider::~AudioProvider() {
    setActive(false);
}

void AudioProvider::setActive(bool active) {
    if (active == (m_subscribe != nullptr))
        return;
    if (!active) {
        m_subscribe->disconnect();
        m_subscribe->kill();
        m_subscribe->waitForFinished(300);
        delete m_subscribe;
        m_subscribe = nullptr;
        return;
    }
    const QString pactl = m_runner->env().findExecutable("pactl");
    if (pactl.isEmpty())
        return;
    m_subscribe = new QProcess(this);
    m_subscribe->setProcessEnvironment(m_runner->env().process);
    connect(m_subscribe, &QProcess::readyReadStandardOutput, this, [this] {
        m_subscribe->readAllStandardOutput();
        m_debounce.start();
    });
    m_subscribe->start(pactl, {"subscribe"});
}

int AudioProvider::defaultOutputVolume() const {
    for (const auto &row : m_outputs)
        if (row.toMap().value("name").toString() == m_defaultOutputSink)
            return row.toMap().value("volume").toInt();
    for (const auto &row : m_outputs)
        if (row.toMap().value("isDefault").toBool())
            return row.toMap().value("volume").toInt();
    return -1;
}

void AudioProvider::refresh() {
    const int generation = ++m_generation;
    struct Pending {
        QByteArray sinks, sources, inputs;
        QString defaultSink, defaultSource;
        int remaining = 5;
        bool failed = false;
    };
    auto pending = std::make_shared<Pending>();
    auto finished = [this, pending, generation] {
        if (--pending->remaining > 0 || generation != m_generation)
            return;
        m_status = pending->failed ? "unavailable" : "ok";
        m_outputs = parseNodes(pending->sinks, pending->defaultSink, false);
        m_inputs = parseNodes(pending->sources, pending->defaultSource, true);
        m_streams = parseStreams(pending->inputs);
        matchStreams(m_streams, m_outputs);
        // The app's own icon from the desktop's icon theme, found once per app.
        const Env &env = m_runner->env();
        for (auto &value : m_streams) {
            auto stream = value.toMap();
            const QString key = stream.value("iconName").toString() + "|" + stream.value("binary").toString();
            if (!m_appIcons.contains(key)) {
                QStringList names;
                for (const QString &name : {stream.value("iconName").toString(), stream.value("binary").toString(), stream.value("app").toString().toLower()})
                    if (!name.isEmpty() && !name.contains('/'))
                        names << name;
                const QString path = names.isEmpty() ? QString() : IconThemes::findIcon(env, IconThemes::desktopDefault(env), names, 48);
                m_appIcons.insert(key, path.isEmpty() ? QString() : QUrl::fromLocalFile(path).toString());
            }
            stream["icon"] = m_appIcons.value(key);
            value = stream;
        }
        emit changed();
        // A DSP sink can front the real output, where loudness actually lives.
        m_runner->run({"omarchy", "audio", "output", "sink"}, CommandOptions{3000}, this, [this](const CommandResult &result) {
            if (result.ok() && m_defaultOutputSink != result.outText()) {
                m_defaultOutputSink = result.outText();
                emit changed();
            }
        });
    };
    auto fetch = [this, pending, finished](const QStringList &argv, std::function<void(const CommandResult &)> store) {
        m_runner->run(argv, CommandOptions{4000}, this, [pending, finished, store](const CommandResult &result) {
            if (!result.ok())
                pending->failed = true;
            store(result);
            finished();
        });
    };
    fetch({"pactl", "-f", "json", "list", "sinks"}, [pending](const CommandResult &r) { pending->sinks = r.out; });
    fetch({"pactl", "-f", "json", "list", "sources"}, [pending](const CommandResult &r) { pending->sources = r.out; });
    fetch({"pactl", "-f", "json", "list", "sink-inputs"}, [pending](const CommandResult &r) { pending->inputs = r.out; });
    fetch({"pactl", "get-default-sink"}, [pending](const CommandResult &r) { pending->defaultSink = r.outText(); });
    fetch({"pactl", "get-default-source"}, [pending](const CommandResult &r) { pending->defaultSource = r.outText(); });
}

void AudioProvider::setDefault(const QString &name, bool input) {
    const QVariantList &rows = input ? m_inputs : m_outputs;
    QString objectId, description, before;
    for (const auto &row : rows) {
        const auto map = row.toMap();
        if (map.value("name").toString() == name) {
            objectId = map.value("objectId").toString();
            description = map.value("description").toString();
        }
        if (map.value("isDefault").toBool())
            before = map.value("name").toString();
    }
    if (objectId.isEmpty()) {
        emit message(tr("That device is no longer available."), true);
        refresh();
        return;
    }
    const QStringList argv{"omarchy", "audio", input ? "input" : "output", "set", "default", objectId, name};
    const QStringList query{"pactl", input ? "get-default-source" : "get-default-sink"};

    ChangeCoordinator::Request request;
    request.settingId = input ? "sound.input" : "sound.output";
    request.label = input ? tr("Microphone") : tr("Sound output");
    request.before = before;
    request.after = name;
    request.apply = [this, argv](ChangeCoordinator::Done done) {
        CommandOptions options;
        options.mutation = true;
        m_runner->run(argv, options, this, [done, argv](const CommandResult &result) { done(result.ok(), result.describe(argv.value(0))); });
    };
    // Omarchy's setter ignores individual failures, so only the new default
    // proves that the device actually became active.
    request.verify = [this, query, name](ChangeCoordinator::Done done) {
        m_runner->run(query, CommandOptions{3000}, this, [this, done, name](const CommandResult &result) {
            done(result.outText() == name, tr("The device did not become the default."));
        });
    };
    m_pendingDefault = name;
    emit changed();
    m_coordinator->submit(std::move(request), [this, description](const Transaction &tx) {
        m_pendingDefault.clear();
        if (tx.phase == QLatin1String("committed"))
            emit message(tr("Now using %1").arg(description), false);
        else
            emit message(tx.message, true);
        refresh();
    });
}

void AudioProvider::setDefaultOutput(const QString &name) { setDefault(name, false); }
void AudioProvider::setDefaultInput(const QString &name) { setDefault(name, true); }

QString AudioProvider::volumeTarget(const QString &name) const {
    for (const auto &row : m_outputs) {
        const auto map = row.toMap();
        if (map.value("name").toString() == name && map.value("isDefault").toBool() && !m_defaultOutputSink.isEmpty())
            return m_defaultOutputSink;
    }
    return name;
}

void AudioProvider::setVolume(const QString &name, int percent) {
    const QStringList argv{"pactl", "set-sink-volume", volumeTarget(name), QString::number(qBound(0, percent, 150)) + "%"};
    CommandOptions options;
    options.mutation = true;
    m_runner->run(argv, options, this, [this](const CommandResult &result) {
        if (!result.ok())
            emit message(result.describe("pactl"), true);
        refresh();
    });
}

void AudioProvider::setInputVolume(const QString &name, int percent) {
    const QStringList argv{"pactl", "set-source-volume", name, QString::number(qBound(0, percent, 150)) + "%"};
    CommandOptions options;
    options.mutation = true;
    m_runner->run(argv, options, this, [this](const CommandResult &result) {
        if (!result.ok())
            emit message(result.describe("pactl"), true);
        refresh();
    });
}

void AudioProvider::setMute(const QString &name, bool mute) {
    CommandOptions options;
    options.mutation = true;
    m_runner->run({"pactl", "set-sink-mute", volumeTarget(name), mute ? "1" : "0"}, options, this, [this](const CommandResult &result) {
        if (!result.ok())
            emit message(result.describe("pactl"), true);
        refresh();
    });
}

void AudioProvider::setInputMute(const QString &name, bool mute) {
    CommandOptions options;
    options.mutation = true;
    m_runner->run({"pactl", "set-source-mute", name, mute ? "1" : "0"}, options, this, [this](const CommandResult &result) {
        if (!result.ok())
            emit message(result.describe("pactl"), true);
        refresh();
    });
}

void AudioProvider::moveStream(int index, const QString &sink) {
    CommandOptions options;
    options.mutation = true;
    m_runner->run({"pactl", "move-sink-input", QString::number(index), sink}, options, this, [this, index, sink](const CommandResult &result) {
        if (!result.ok()) {
            emit message(result.describe("pactl"), true);
            refresh();
            return;
        }
        // Where the stream really went, asked afterwards: an app can refuse a move or
        // be closed in between.
        m_runner->run({"pactl", "-f", "json", "list", "sink-inputs"}, CommandOptions{4000}, this, [this, index, sink](const CommandResult &check) {
            QVariantList streams = parseStreams(check.out);
            matchStreams(streams, m_outputs);
            bool found = false, moved = false;
            for (const auto &value : streams)
                if (value.toMap().value("index").toInt() == index) {
                    found = true;
                    moved = value.toMap().value("output").toString() == sink;
                }
            if (check.ok() && found && !moved)
                emit message(tr("The app did not move to that output."), true);
            else if (check.ok() && !found)
                emit message(tr("That app stopped playing before it could move."), true);
            refresh();
        });
    });
}
