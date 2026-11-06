#pragma once

#include <QStringList>

#include "env.h"

// The headless interface: `omarchy-control <command>`. Each command takes --json,
// exits 0 or 1, and writes errors to stderr. It shares the backend with the
// window, so validation and journaling cannot drift apart.
namespace Cli {

bool isCliCommand(const QString &command);
int run(const Env &env, const QStringList &arguments);

}  // namespace Cli
