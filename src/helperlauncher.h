#pragma once

#include <QObject>

#include "capabilities.h"
#include "catalog.h"
#include "commandrunner.h"

// Starts specialist tools. A launch can only report that a tool opened; whether
// anything then changed is learned by reading state afterward.
class HelperLauncher : public QObject {
    Q_OBJECT

public:
    HelperLauncher(CommandRunner *runner, Capabilities *capabilities, QObject *parent = nullptr);

    struct Outcome {
        bool opened = false;
        QString message;
        QString navigatePage;
        QString navigateFocus;
    };
    using Callback = std::function<void(const Outcome &)>;

    void launch(const CatalogEntry &entry, Callback callback);

    // The exact argv a launch resolves to. Pure, so tests can inspect every entry
    // in the catalog without starting anything.
    static QStringList resolve(const QJsonObject &launch, const Env &env, bool hasUwsm);

private:
    CommandRunner *m_runner;
    Capabilities *m_capabilities;
};
