#pragma once

#include <QStringList>
#include <functional>

#include "env.h"

class Backend;
class QQmlApplicationEngine;

// Starts the window. `ready` lets the screenshot harness drive it offscreen.
int runGui(const Env &env, const QStringList &arguments, std::function<void(QQmlApplicationEngine &, Backend &)> ready = nullptr);
