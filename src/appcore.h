#pragma once

#include "capabilities.h"
#include "catalog.h"
#include "changecoordinator.h"
#include "commandrunner.h"
#include "compat.h"
#include "env.h"
#include "iconthemeprovider.h"
#include "launchereditor.h"
#include "settingsengine.h"

// The reviewed backend, shared by the window and the headless CLI so both apply
// the same validation, journal, and verification.
struct AppCore {
    Env env;
    Catalog catalog;
    CommandRunner runner;
    Capabilities capabilities;
    ChangeCoordinator coordinator;
    Compat compat;
    IconThemeProvider icons;
    LauncherEditor launchers;
    SettingsEngine engine;

    explicit AppCore(const Env &environment, const QString &catalogDirectory = QStringLiteral(":/data"))
        : env(environment),
          catalog(catalogDirectory),
          runner(env),
          capabilities(&runner),
          coordinator(env),
          compat(env, catalogDirectory),
          icons(&runner, &coordinator),
          launchers(env, &coordinator, &icons),
          engine(&catalog, &runner, &capabilities, &coordinator, &compat) {
        engine.setIconThemes(&icons);
        engine.setLaunchers(&launchers);
    }
};
