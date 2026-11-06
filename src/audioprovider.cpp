#include "audioprovider.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

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
        row["objectId"] = properties.value("object.id").toString();
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
    m_coordinator->submit(std::move(request), [this, description](const Transaction &tx) {
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
    m_runner->run({"pactl", "move-sink-input", QString::number(index), sink}, options, this, [this](const CommandResult &result) {
        if (!result.ok())
            emit message(result.describe("pactl"), true);
        refresh();
    });
}
