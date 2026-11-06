// Usage: screens <sandbox-root|--real> <output-dir> <page[:focus]>...
//
// --real reads the live machine, which only ever reads: it opens pages and takes
// pictures, and clicks nothing. Those images show personal data, so they stay local.
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>

#include "backend.h"
#include "env.h"
#include "gui.h"

int main(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_FORCE_STDERR_LOGGING", "1");
    QGuiApplication app(argc, argv);
    app.setApplicationName("omarchy-control");
    app.setApplicationVersion(APP_VERSION);
    const QStringList args = app.arguments();
    const QString root = args.value(1), output = args.value(2);
    const QStringList pages = args.mid(3);
    QDir().mkpath(output);
    const Env env = root == QLatin1String("--real") ? Env::system() : Env::sandbox(root, root + "/bin");
    QElapsedTimer started;
    started.start();

    return runGui(env, {}, [&](QQmlApplicationEngine &engine, Backend &) {
        qInfo("window ready in %lld ms", started.elapsed());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        auto *index = new int(0);
        auto *timer = new QTimer(window);
        timer->setInterval(900);
        QObject::connect(timer, &QTimer::timeout, window, [=] {
            static bool waiting = false;
            if (waiting) {
                const QString name = pages[*index].section(':', 0, 0) + (pages[*index].contains(':') ? "-" + pages[*index].section(':', 1).replace('.', '_').replace(':', '_') : "");
                window->grabWindow().save(output + "/" + name + ".png");
                ++*index;
                waiting = false;
                if (*index >= pages.size()) {
                    QCoreApplication::quit();
                    return;
                }
            }
            const QString spec = pages[*index];
            const QString page = spec.section(':', 0, 0), focus = spec.section(':', 1, 1), size = spec.section(':', 2, 2);
            if (!size.isEmpty()) {
                window->setWidth(size.section('x', 0, 0).toInt());
                window->setHeight(size.section('x', 1, 1).toInt());
            }
            QMetaObject::invokeMethod(window, "go", Q_ARG(QVariant, page), Q_ARG(QVariant, focus));
            waiting = true;
        });
        timer->start();
    });
}
