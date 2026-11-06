// Every test runs against a temporary root with fake commands first on PATH. No
// test can reach the real desktop: the sandbox Env is the only way in.
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QLockFile>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <unistd.h>

#ifndef Q_MOC_RUN
#include "fake_scripts.h"
#endif

#include "appcore.h"
#include "audioprovider.h"
#include "backend.h"
#include "bluetoothprovider.h"
#include "catalog.h"
#include "changecoordinator.h"
#include "cli.h"
#include "commandrunner.h"
#include "compat.h"
#include "windowrulesprovider.h"
#include <QAccessible>
#include <QSysInfo>
#include "coverage.h"
#include "displayprovider.h"
#include "env.h"
#include "fileops.h"
#include "helperlauncher.h"
#include "hypromodule.h"
#include "iconprovider.h"
#include "networkprovider.h"
#include "pluginsprovider.h"
#include "servicesprovider.h"
#include "settingsengine.h"
#include "shortcutsprovider.h"
#include "systemprovider.h"
#include "theme.h"
#include "translator.h"

namespace {

void writeFile(const QString &path, const QByteArray &contents) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(contents);
}

QByteArray readAll(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// A throwaway machine: a temp root, an Env pointing into it, and helpers for
// writing the fake commands a test needs.
struct Sandbox {
    QTemporaryDir dir;
    Env env;
    Sandbox() { env = Env::sandbox(dir.path(), dir.path() + "/bin"); }
    QString bin(const QString &name) const { return dir.path() + "/bin/" + name; }
    QString log() const { return dir.path() + "/commands.log"; }
    void command(const QString &name, const QByteArray &body) const {
        const QByteArray script = "#!/bin/bash\necho \"" + name.toUtf8() + " $*\" >> '" + log().toUtf8() + "'\n" + body + "\n";
        writeFile(bin(name), script);
        QFile::setPermissions(bin(name), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }
    QStringList calls() const {
        return QString::fromUtf8(readAll(log())).split('\n', Qt::SkipEmptyParts);
    }
    void minimalHyprland(const QString &extra = {}) const {
        command("hyprctl", (kScript0 + dir.path().toUtf8() + kScript1 + extra.toUtf8()).trimmed());
        writeFile(env.configHome + "/hypr/hyprland.lua", "dofile(\"x\")\nrequire(\"default.hypr.omarchy\")\nrequire(\"hypr.monitors\")\nrequire(\"hypr.autostart\")\nrequire(\"default.hypr.toggles\")\n-- custom\nrequire(\"hypr.mine\")\n");
    }
    void omarchy(const QByteArray &body) const { command("omarchy", body); }
};

Transaction applySetting(SettingsEngine &engine, const QString &id, const QString &value) {
    Transaction result;
    bool done = false;
    engine.apply(id, value, [&](const Transaction &tx) { result = tx; done = true; });
    QTest::qWaitFor([&] { return done; }, 15000);
    return result;
}

ReadResult readSetting(SettingsEngine &engine, const QString &id) {
    ReadResult result;
    bool done = false;
    engine.read(id, [&](const ReadResult &r) { result = r; done = true; });
    QTest::qWaitFor([&] { return done; }, 8000);
    return result;
}

Transaction undoTx(SettingsEngine &engine, const QString &id) {
    Transaction result;
    bool done = false;
    engine.undo(id, [&](const Transaction &tx) { result = tx; done = true; });
    QTest::qWaitFor([&] { return done; }, 15000);
    return result;
}

QStringList argvOf(const QJsonObject &action) {
    QStringList out;
    for (const auto &part : action.value("argv").toArray())
        out << part.toString();
    return out;
}

}  // namespace

class OmarchyControlTests : public QObject {
    Q_OBJECT

private slots:
    // ---- File editing ---------------------------------------------------------
    void jsonSetChangesOnlyTheTargetBytes() {
        QByteArray doc = "{\n  // keep me\n  \"zeta\": 1,\n  \"idle\": { \"lock\": 300, /* note */ \"screensaver\": 150 },\n  \"alpha\": [1, 2,]\n}\n";
        QVERIFY(FileOps::jsonSet(&doc, {"idle", "lock"}, 600));
        QCOMPARE(doc, QByteArray("{\n  // keep me\n  \"zeta\": 1,\n  \"idle\": { \"lock\": 600, /* note */ \"screensaver\": 150 },\n  \"alpha\": [1, 2,]\n}\n"));
    }
    void jsonSetCreatesMissingPathAndKeepsOrder() {
        QByteArray doc = "{\n  \"version\": 1,\n  \"plugins\": []\n}\n";
        QVERIFY(FileOps::jsonSet(&doc, {"idle", "lock"}, 60));
        const auto object = QJsonDocument::fromJson(doc).object();
        QCOMPARE(object.value("idle").toObject().value("lock").toInt(), 60);
        QVERIFY(doc.indexOf("version") < doc.indexOf("plugins"));
        QVERIFY(doc.indexOf("plugins") < doc.indexOf("idle"));
    }
    void jsonSetKeepsCommentsInAnEmptyObject() {
        QByteArray doc = "{\n  // Extend the menu.\n  // \"personal\": {},\n}\n";
        QVERIFY(FileOps::jsonSet(&doc, {"omarchy-control"}, QJsonObject{{"label", "Control"}}));
        QVERIFY(doc.contains("// Extend the menu."));
        QVERIFY(doc.contains("// \"personal\": {},"));
        QCOMPARE(FileOps::jsonGet(doc, {"omarchy-control", "label"}).toString(), QString("Control"));
    }
    void jsonRemoveKeepsTheRest() {
        QByteArray doc = "{\n  \"a\": 1,\n  \"b\": 2,\n  \"c\": 3\n}\n";
        QVERIFY(FileOps::jsonRemove(&doc, {"b"}));
        const auto object = QJsonDocument::fromJson(doc).object();
        QCOMPARE(object.size(), 2);
        QCOMPARE(object.value("c").toInt(), 3);
        QByteArray last = "{\n  \"a\": 1,\n  \"b\": 2\n}\n";
        QVERIFY(FileOps::jsonRemove(&last, {"b"}));
        QVERIFY(!QJsonDocument::fromJson(last).isNull());
        QCOMPARE(QJsonDocument::fromJson(last).object().size(), 1);
    }
    void jsonSetRefusesToReplaceANonObjectParent() {
        QByteArray doc = "{\"idle\": 5}";
        QString error;
        QVERIFY(!FileOps::jsonSet(&doc, {"idle", "lock"}, 1, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(doc, QByteArray("{\"idle\": 5}"));
    }
    void jsonSetRejectsCorruptDocuments() {
        QByteArray doc = "{\"a\": ";
        QString error;
        QVERIFY(!FileOps::jsonSet(&doc, {"a"}, 1, &error));
        QVERIFY(!error.isEmpty());
    }
    void jsonStringEscaping() {
        QByteArray doc = "{}";
        QVERIFY(FileOps::jsonSet(&doc, {"name"}, QStringLiteral("a\"b\\c\nd")));
        QCOMPARE(FileOps::jsonGet(doc, {"name"}).toString(), QStringLiteral("a\"b\\c\nd"));
    }
    void tomlReadsSectionsAndComments() {
        const QByteArray doc = "# top\n[bar]\nsize = 3 # keep\n\n[font]\nbase-size = 12 # size\nname = \"Mono # One\"\n";
        QCOMPARE(FileOps::tomlGet(doc, "font", "base-size"), QString("12"));
        QCOMPARE(FileOps::tomlGet(doc, "bar", "size"), QString("3"));
        QCOMPARE(FileOps::tomlGet(doc, "font", "name"), QString("Mono # One"));
        QVERIFY(FileOps::tomlGet(doc, "font", "missing").isEmpty());
    }
    void atomicWriteFollowsSymlinksAndKeepsMode() {
        QTemporaryDir dir;
        const QString target = dir.filePath("real/config.json"), link = dir.filePath("link.json");
        writeFile(target, "old");
        QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
        QVERIFY(QFile::link(target, link));
        QVERIFY(FileOps::atomicWrite(link, "new"));
        QVERIFY(QFileInfo(link).isSymLink());
        QCOMPARE(readAll(target), QByteArray("new"));
        QCOMPARE(QFile::permissions(target) & (QFile::ReadGroup | QFile::ReadOther), QFile::Permissions());
    }
    void atomicWriteFailureLeavesTheOriginal() {
        if (geteuid() == 0)
            QSKIP("root ignores directory permissions");
        QTemporaryDir dir;
        const QString target = dir.filePath("sub/file");
        writeFile(target, "old");
        QFile::setPermissions(dir.filePath("sub"), QFile::ReadOwner | QFile::ExeOwner);
        QString error;
        QVERIFY(!FileOps::atomicWrite(target, "new", &error));
        QVERIFY(!error.isEmpty());
        QFile::setPermissions(dir.filePath("sub"), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QCOMPARE(readAll(target), QByteArray("old"));
    }

    // ---- Process handling -----------------------------------------------------
    void runnerPassesArgumentsWithoutAShell() {
        Sandbox s;
        s.command("echoargs", "for a in \"$@\"; do echo \"[$a]\"; done");
        CommandRunner runner(s.env);
        const auto result = runner.runBlocking({"echoargs", "two words", "$(touch /tmp/oc-pwned)", "semi;colon"});
        QVERIFY(result.ok());
        QCOMPARE(QString::fromUtf8(result.out), QString("[two words]\n[$(touch /tmp/oc-pwned)]\n[semi;colon]\n"));
        QVERIFY(!QFile::exists("/tmp/oc-pwned"));
    }
    void runnerReportsMissingCommands() {
        Sandbox s;
        CommandRunner runner(s.env);
        const auto result = runner.runBlocking({"definitely-not-installed-xyz"});
        QCOMPARE(result.failure, CommandResult::NotFound);
        QVERIFY(result.describe("x").contains("not installed"));
    }
    void runnerTimesOut() {
        Sandbox s;
        s.command("slow", "sleep 20");
        CommandRunner runner(s.env);
        CommandOptions options;
        options.timeoutMs = 150;
        QElapsedTimer timer;
        timer.start();
        const auto result = runner.runBlocking({"slow"}, options);
        QCOMPARE(result.failure, CommandResult::Timeout);
        QVERIFY(timer.elapsed() < 5000);
    }
    void runnerBoundsOutput() {
        Sandbox s;
        s.command("loud", "head -c 200000 /dev/zero | tr '\\0' x");
        CommandRunner runner(s.env);
        CommandOptions options;
        options.maxOutputBytes = 1000;
        const auto result = runner.runBlocking({"loud"}, options);
        QVERIFY(result.out.size() <= 1000);
        QVERIFY(result.truncated);
    }
    void runnerReportsExitFailures() {
        Sandbox s;
        s.command("nope", "echo 'it broke' >&2; exit 3");
        CommandRunner runner(s.env);
        const auto result = runner.runBlocking({"nope"});
        QVERIFY(!result.ok());
        QCOMPARE(result.exitCode, 3);
        QCOMPARE(result.describe("nope"), QString("it broke"));
    }
    void runnerNeverBlocksTheEventLoop() {
        Sandbox s;
        s.command("slow", "sleep 0.5");
        CommandRunner runner(s.env);
        bool finished = false;
        QElapsedTimer timer;
        timer.start();
        runner.run({"slow"}, nullptr, [&](const CommandResult &) { finished = true; });
        QVERIFY(timer.elapsed() < 100);
        QVERIFY(!finished);
        QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
    }
    void runnerDropsCallbackWhenContextIsGone() {
        Sandbox s;
        s.command("slow", "sleep 0.2");
        CommandRunner runner(s.env);
        bool called = false;
        {
            QObject context;
            runner.run({"slow"}, &context, [&](const CommandResult &) { called = true; });
        }
        QTest::qWait(500);
        QVERIFY(!called);
    }

    // ---- Theme and language ---------------------------------------------------
    void themeReadsThePalette() {
        Sandbox s;
        writeFile(s.env.stateHome + "/omarchy/current/theme/colors.toml", "mode = \"light\"\naccent = \"#112233\"\nbackground = \"#fafafa\"\nforeground = \"#101010\"\n");
        writeFile(s.env.configHome + "/omarchy/shell.toml", "[font]\nbase-size = 16\n");
        Theme theme(s.env);
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.window(), QString("#fafafa"));
        QVERIFY(!theme.dark());
        QCOMPARE(theme.accentForeground(), QString("white"));
        QCOMPARE(theme.fontSize(), 16);
    }
    void themeFallsBackWithoutOmarchy() {
        Sandbox s;
        Theme theme(s.env);
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QVERIFY(theme.dark());
        QCOMPARE(theme.fontSize(), 13);
        QVERIFY(theme.animate());
    }
    void themeFollowsReplacementAndMotion() {
        Sandbox s;
        const QString current = s.env.stateHome + "/omarchy/current";
        writeFile(current + "/theme/colors.toml", "accent = \"#111111\"\n");
        Theme theme(s.env);
        QCOMPARE(theme.accent(), QString("#111111"));
        // A theme switch replaces the directory rather than editing the file.
        QVERIFY(QDir(current + "/theme").removeRecursively());
        writeFile(current + "/theme/colors.toml", "accent = \"#222222\"\n");
        QTRY_COMPARE_WITH_TIMEOUT(theme.accent(), QString("#222222"), 3000);
        writeFile(s.env.stateHome + "/omarchy/toggles/hypr/no-animations.lua", "x");
        QTRY_VERIFY_WITH_TIMEOUT(!theme.animate(), 3000);
    }
    void themeFollowsHyprlandRounding() {
        Sandbox s;
        s.command("hyprctl", "echo '{\"int\":6}'");
        Theme theme(s.env);
        QTRY_COMPARE_WITH_TIMEOUT(theme.radius(), 6, 3000);
    }
    void translatorFallsBackPerString() {
        QTemporaryDir dir;
        writeFile(dir.filePath("language"), "pt-BR\n");
        writeFile(dir.filePath("pt.json"), R"({"Save": "Guardar", "Open": "Abrir"})");
        writeFile(dir.filePath("pt-BR.json"), R"({"Save": "Salvar"})");
        Translator translator(dir.filePath("language"), dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("x", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("x", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("x", "Quit").isNull());
    }
    void translatorFollowsTheFileLive() {
        QTemporaryDir dir;
        writeFile(dir.filePath("da.json"), R"({"Save": "Gem"})");
        Translator translator(dir.filePath("language"), dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QSignalSpy spy(&translator, &Translator::changed);
        writeFile(dir.filePath("language"), "da\n");
        QTRY_COMPARE_WITH_TIMEOUT(translator.language(), QString("da"), 3000);
        QCOMPARE(translator.translate("x", "Save"), QString("Gem"));
        QVERIFY(spy.count() >= 1);
    }
    void translatorPlurals() {
        QCOMPARE(Translator::pluralCategory("ru", 21), QString("one"));
        QCOMPARE(Translator::pluralCategory("ru", 25), QString("many"));
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
    }
    void shippedCatalogsKeepPlaceholders() {
        // A translation that drops %1 shows the wrong text with no error anywhere.
        const QDir dir(PROJECT_ROOT "/src/i18n");
        static const QRegularExpression placeholder("%[0-9n]");
        for (const auto &name : dir.entryList({"*.json"})) {
            const auto object = QJsonDocument::fromJson(readAll(dir.filePath(name))).object();
            QVERIFY2(!object.isEmpty(), qPrintable(name));
            for (auto it = object.begin(); it != object.end(); ++it) {
                QSet<QString> expected;
                auto keys = placeholder.globalMatch(it.key());
                while (keys.hasNext())
                    expected << keys.next().captured();
                const auto check = [&](const QString &value) {
                    if (it.key().contains("%n") && !value.contains("%n"))
                        expected.remove("%n");   // Singular forms may spell the count out.
                    for (const auto &p : expected)
                        QVERIFY2(value.contains(p), qPrintable(name + ": " + it.key() + " -> " + value));
                };
                if (it.value().isString()) {
                    check(it.value().toString());
                } else {
                    const auto forms = it.value().toObject();
                    QVERIFY2(forms.contains("other"), qPrintable(name + ": " + it.key()));
                    check(forms.value("other").toString());
                }
            }
        }
    }

    // ---- Owned Lua module -----------------------------------------------------
    void luaStringsCannotEscape() {
        const QString evil = "x\")\nos.execute(\"touch /tmp/pwned\")\n--";
        const QString lua = HyprModule::luaString(evil);
        QVERIFY(!lua.contains('\n'));
        QVERIFY(lua.startsWith('"') && lua.endsWith('"'));
        QVERIFY(lua.contains("\\\""));
        QCOMPARE(HyprModule::luaString(QString("a") + QChar(0x01) + "b"), QString("\"ab\""));
    }
    void moduleRendersOptionsMonitorsAndBinds() {
        Sandbox s;
        HyprModule module(s.env);
        module.setOption({"input", "repeat_rate"}, 31);
        module.setOption({"input", "touchpad", "tap-to-click"}, false);
        module.setOption({"decoration", "dim_strength"}, 0.25);
        module.setMonitors(QJsonArray{QJsonObject{{"output", "DP-1"}, {"mode", "2560x1440@60.00"}, {"position", "0x0"}, {"scale", 1.25}, {"transform", 1}}, QJsonObject{{"output", "eDP-1"}, {"disabled", true}}});
        module.setBinds(QJsonArray{QJsonObject{{"keys", "SUPER + ALT + N"}, {"description", "Note"}, {"command", "notes --new"}, {"replaces", true}}});
        const QString lua = QString::fromUtf8(module.render());
        QVERIFY(lua.contains("repeat_rate = 31"));
        QVERIFY(lua.contains("[\"tap-to-click\"] = false"));
        QVERIFY(lua.contains("dim_strength = 0.25"));
        QVERIFY(lua.contains("hl.monitor({ output = \"DP-1\", mode = \"2560x1440@60.00\", position = \"0x0\", scale = 1.25, transform = 1 })"));
        QVERIFY(lua.contains("hl.monitor({ output = \"eDP-1\", disabled = true })"));
        QVERIFY(lua.contains("hl.unbind(\"SUPER + ALT + N\")"));
        QVERIFY(lua.contains("o.bind(\"SUPER + ALT + N\", \"Note\", \"notes --new\")"));
    }
    void moduleRoundTripsAndDetectsHandEdits() {
        Sandbox s;
        HyprModule module(s.env);
        QCOMPARE(module.state(), HyprModule::Absent);
        module.setOption({"input", "repeat_rate"}, 31);
        QVERIFY(module.save());
        QCOMPARE(module.state(), HyprModule::Ours);
        HyprModule reread(s.env);
        QVERIFY(reread.load());
        QCOMPARE(reread.option({"input", "repeat_rate"}).toInt(), 31);
        QVERIFY(reread.hasOption({"input", "repeat_rate"}));

        writeFile(module.path(), readAll(module.path()) + "-- my own line\n");
        QCOMPARE(module.state(), HyprModule::ModifiedByUser);
        HyprModule blocked(s.env);
        QString error;
        QVERIFY(!blocked.load(&error));
        QVERIFY(!error.isEmpty());
    }
    void moduleRemovalPrunesEmptyTables() {
        Sandbox s;
        HyprModule module(s.env);
        module.setOption({"input", "touchpad", "drag_3fg"}, 1);
        module.removeOption({"input", "touchpad", "drag_3fg"});
        QVERIFY(module.options().isEmpty());
        QVERIFY(!module.render().contains("hl.config"));
    }
    void importGoesAfterUserFilesAndBeforeToggles() {
        Sandbox s;
        s.minimalHyprland();
        HyprModule module(s.env);
        QVERIFY(!module.importInstalled());
        QVERIFY(module.installImport());
        QVERIFY(QFile::exists(module.path()));
        const QByteArray lua = readAll(module.hyprlandPath());
        const int autostart = lua.indexOf("require(\"hypr.autostart\")");
        const int ours = lua.indexOf("require(\"hypr.omarchy-control\")");
        const int toggles = lua.indexOf("require(\"default.hypr.toggles\")");
        const int custom = lua.indexOf("require(\"hypr.mine\")");
        QVERIFY(autostart < ours && ours < toggles && toggles < custom);
        QVERIFY(lua.contains("Managed by Omarchy Control"));
        // Installing twice must not stack up duplicates.
        QVERIFY(module.installImport());
        QCOMPARE(readAll(module.hyprlandPath()).count("hypr.omarchy-control"), qsizetype(1));
        QVERIFY(module.removeImport());
        QVERIFY(!module.importInstalled());
        QVERIFY(readAll(module.hyprlandPath()).contains("hypr.mine"));
    }
    void importRefusesACustomLayout() {
        Sandbox s;
        writeFile(s.env.configHome + "/hypr/hyprland.lua", "-- everything hand written\nhl.config({})\n");
        const QByteArray before = readAll(s.env.configHome + "/hypr/hyprland.lua");
        HyprModule module(s.env);
        QVERIFY(!module.canInstallImport());
        QString error;
        QVERIFY(!module.installImport(&error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(readAll(s.env.configHome + "/hypr/hyprland.lua"), before);
    }

    // ---- Change coordinator ---------------------------------------------------
    void coordinatorCommitsAndJournals() {
        Sandbox s;
        ChangeCoordinator coordinator(s.env);
        ChangeCoordinator::Request request;
        request.settingId = "x";
        request.label = "X";
        request.after = "1";
        request.recovery = "auto-undo";
        request.apply = [](ChangeCoordinator::Done done) { done(true, {}); };
        request.verify = [](ChangeCoordinator::Done done) { done(true, {}); };
        Transaction result;
        coordinator.submit(request, [&](const Transaction &tx) { result = tx; });
        QTRY_COMPARE(result.phase, QString("committed"));
        QVERIFY(result.undoable());
        QCOMPARE(coordinator.history().first().id, result.id);
        // Every phase reached the disk, so a crash at any point leaves a trace.
        const QString journal = QString::fromUtf8(readAll(coordinator.journalPath()));
        for (const char *phase : {"intent", "applying", "applied", "committed"})
            QVERIFY2(journal.contains(QString("\"phase\":\"%1\"").arg(phase)), phase);
        QCOMPARE(QFile::permissions(coordinator.journalPath()) & (QFile::ReadOther | QFile::ReadGroup), QFile::Permissions());
    }
    void coordinatorRestoresFilesOnFailedVerify() {
        Sandbox s;
        const QString file = s.env.configHome + "/thing.json";
        writeFile(file, "original");
        ChangeCoordinator coordinator(s.env);
        ChangeCoordinator::Request request;
        request.settingId = "x";
        request.label = "X";
        request.files << file;
        request.apply = [file](ChangeCoordinator::Done done) { done(FileOps::atomicWrite(file, "changed"), {}); };
        request.verify = [](ChangeCoordinator::Done done) { done(false, "did not take"); };
        Transaction result;
        coordinator.submit(request, [&](const Transaction &tx) { result = tx; });
        QTRY_COMPARE(result.phase, QString("rolled-back"));
        QCOMPARE(readAll(file), QByteArray("original"));
        QVERIFY(result.message.contains("did not take"));
        QVERIFY(!result.undoable());
    }
    void coordinatorRemovesAFileItCreated() {
        Sandbox s;
        const QString file = s.env.configHome + "/new.json";
        ChangeCoordinator coordinator(s.env);
        ChangeCoordinator::Request request;
        request.files << file;
        request.apply = [file](ChangeCoordinator::Done done) { done(FileOps::atomicWrite(file, "x"), {}); };
        request.verify = [](ChangeCoordinator::Done done) { done(false, "no"); };
        Transaction result;
        coordinator.submit(request, [&](const Transaction &tx) { result = tx; });
        QTRY_COMPARE(result.phase, QString("rolled-back"));
        QVERIFY(!QFile::exists(file));
    }
    void coordinatorReportsPartialWhenNothingCanBeRestored() {
        Sandbox s;
        ChangeCoordinator coordinator(s.env);
        ChangeCoordinator::Request request;
        request.apply = [](ChangeCoordinator::Done done) { done(false, "broke"); };
        Transaction result;
        coordinator.submit(request, [&](const Transaction &tx) { result = tx; });
        QTRY_COMPARE(result.phase, QString("partial"));
        QVERIFY(result.message.contains("partly"));
    }
    void coordinatorRefusesExternallyEditedFiles() {
        Sandbox s;
        const QString file = s.env.configHome + "/c.json";
        writeFile(file, "v1");
        const QString planned = FileOps::fingerprint(file);
        writeFile(file, "someone else");
        ChangeCoordinator coordinator(s.env);
        bool applied = false;
        ChangeCoordinator::Request request;
        request.expectedFingerprints.insert(file, planned);
        request.files << file;
        request.apply = [&](ChangeCoordinator::Done done) { applied = true; done(true, {}); };
        Transaction result;
        coordinator.submit(request, [&](const Transaction &tx) { result = tx; });
        QTRY_COMPARE(result.phase, QString("failed"));
        QVERIFY(!applied);
        QCOMPARE(readAll(file), QByteArray("someone else"));
    }
    void coordinatorSerializesConflictingChanges() {
        Sandbox s;
        ChangeCoordinator coordinator(s.env);
        QStringList order;
        for (int i = 0; i < 3; ++i) {
            ChangeCoordinator::Request request;
            request.settingId = QString::number(i);
            request.apply = [&order, i](ChangeCoordinator::Done done) {
                order << QString("begin%1").arg(i);
                QTimer::singleShot(30, [&order, i, done] { order << QString("end%1").arg(i); done(true, {}); });
            };
            coordinator.submit(request, nullptr);
        }
        QTRY_COMPARE(order.size(), 6);
        QCOMPARE(order, (QStringList{"begin0", "end0", "begin1", "end1", "begin2", "end2"}));
    }
    void coordinatorRefusesWhileAnotherInstanceHoldsTheLock() {
        Sandbox s;
        ChangeCoordinator coordinator(s.env);
        QProcess holder;
        // A real second process holding the lock, as another window would.
        QLockFile other(s.env.appStateDir + "/change.lock");
        QVERIFY(other.tryLock(0));
        // Same-process QLockFile also blocks, which models the other instance.
        bool applied = false;
        ChangeCoordinator::Request request;
        request.apply = [&](ChangeCoordinator::Done done) { applied = true; done(true, {}); };
        Transaction result;
        coordinator.submit(request, [&](const Transaction &tx) { result = tx; });
        QTRY_COMPARE_WITH_TIMEOUT(result.phase, QString("failed"), 5000);
        QVERIFY(!applied);
        QVERIFY(result.message.contains("Another"));
    }
    void historyRedactsSecrets() {
        QCOMPARE(ChangeCoordinator::redact("password=hunter2 ok"), QString("password=[redacted] ok"));
        QVERIFY(!ChangeCoordinator::redact("api_key: abc123").contains("abc123"));
        QVERIFY(!ChangeCoordinator::redact("nmcli con modify x wifi-sec.psk secretvalue").contains("secretvalue"));
        QCOMPARE(ChangeCoordinator::redact("nothing here"), QString("nothing here"));
    }

    // Crash at each journal phase, by writing the journal the crash would leave.
    void recoveryNeverOverwritesWhatItCannotAttribute() {
        // Writes are atomic, so this content is not half of the interrupted change. It
        // is an edit someone made after the crash, and it has to survive.
        Sandbox s;
        const QString file = s.env.configHome + "/shell.json";
        writeFile(file, "an edit made after the crash");
        ChangeCoordinator coordinator(s.env);
        const QString backup = coordinator.backupDirectory() + "/tx1/0";
        writeFile(backup, "original");
        Transaction tx;
        tx.id = "tx1";
        tx.settingId = "power.idle.lock";
        tx.label = "Lock after";
        tx.recovery = "auto-undo";
        tx.phase = "applying";
        tx.files.insert(file, QJsonObject{{"before", FileOps::fingerprintBytes("original")}, {"backup", backup}});
        writeFile(coordinator.journalPath(), QJsonDocument(tx.toJson()).toJson(QJsonDocument::Compact) + "\n");
        const QStringList messages = coordinator.recoverInterrupted();
        QCOMPARE(messages.size(), 1);
        QCOMPARE(readAll(file), QByteArray("an edit made after the crash"));
        QCOMPARE(coordinator.transaction("tx1").phase, QString("interrupted"));
        // The way back is offered in words, and the backup is still there to use.
        QVERIFY(coordinator.transaction("tx1").message.contains(backup));
        QVERIFY(QFile::exists(backup));
        // Running it again changes nothing and reports nothing.
        QVERIFY(coordinator.recoverInterrupted().isEmpty());
    }
    void recoveryKnowsWhenNothingChanged() {
        Sandbox s;
        const QString file = s.env.configHome + "/shell.json";
        writeFile(file, "same");
        ChangeCoordinator coordinator(s.env);
        Transaction tx;
        tx.id = "tx2";
        tx.label = "X";
        tx.phase = "intent";
        tx.files.insert(file, QJsonObject{{"before", FileOps::fingerprint(file)}});
        writeFile(coordinator.journalPath(), QJsonDocument(tx.toJson()).toJson(QJsonDocument::Compact) + "\n");
        coordinator.recoverInterrupted();
        QCOMPARE(coordinator.transaction("tx2").phase, QString("not-applied"));
    }
    void recoveryKeepsACompletedWrite() {
        Sandbox s;
        const QString file = s.env.configHome + "/shell.json";
        writeFile(file, "new value");
        ChangeCoordinator coordinator(s.env);
        Transaction tx;
        tx.id = "tx3";
        tx.label = "X";
        tx.phase = "applied";
        tx.files.insert(file, QJsonObject{{"before", FileOps::fingerprintBytes("old")}, {"after", FileOps::fingerprint(file)}});
        writeFile(coordinator.journalPath(), QJsonDocument(tx.toJson()).toJson(QJsonDocument::Compact) + "\n");
        coordinator.recoverInterrupted();
        QCOMPARE(coordinator.transaction("tx3").phase, QString("interrupted"));
        QCOMPARE(readAll(file), QByteArray("new value"));
    }
    void recoveryHandlesTransactionsWithoutFiles() {
        Sandbox s;
        ChangeCoordinator coordinator(s.env);
        Transaction tx;
        tx.id = "tx4";
        tx.label = "X";
        tx.phase = "applying";
        writeFile(coordinator.journalPath(), QJsonDocument(tx.toJson()).toJson(QJsonDocument::Compact) + "\n");
        QCOMPARE(coordinator.recoverInterrupted().size(), 1);
        QCOMPARE(coordinator.transaction("tx4").phase, QString("interrupted"));
    }
    void recoveryWaitsForARunningInstance() {
        Sandbox s;
        ChangeCoordinator coordinator(s.env);
        Transaction tx;
        tx.id = "tx5";
        tx.phase = "applying";
        writeFile(coordinator.journalPath(), QJsonDocument(tx.toJson()).toJson(QJsonDocument::Compact) + "\n");
        QLockFile other(s.env.appStateDir + "/change.lock");
        QVERIFY(other.tryLock(0));
        QVERIFY(coordinator.recoverInterrupted().isEmpty());
        QCOMPARE(coordinator.transaction("tx5").phase, QString("applying"));
    }
    void journalSurvivesATruncatedLastLine() {
        Sandbox s;
        ChangeCoordinator coordinator(s.env);
        Transaction tx;
        tx.id = "good";
        tx.phase = "committed";
        writeFile(coordinator.journalPath(), QJsonDocument(tx.toJson()).toJson(QJsonDocument::Compact) + "\n{\"id\":\"bad\",\"pha");
        QCOMPARE(coordinator.history().size(), 1);
        QCOMPARE(coordinator.history().first().id, QString("good"));
    }

    // ---- Settings engine ------------------------------------------------------
    void idleTimeoutFullPath() {
        // The plan's first work package: read, stage, scoped shell.json change,
        // verify, history, undo, with unknown keys and order preserved.
        Sandbox s;
        s.omarchy("exit 0");
        const QString config = s.env.configHome + "/omarchy/shell.json";
        const QByteArray original = "{\n  \"zzz\": {\"custom\": true},\n  \"idle\": {\n    \"lock\": 300,\n    \"screensaver\": 150\n  },\n  \"aaa\": 1\n}\n";
        writeFile(config, original);
        AppCore core(s.env);

        ReadResult current = readSetting(core.engine, "power.idle.lock");
        QCOMPARE(current.status, QString("ok"));
        QCOMPARE(current.value, QString("300"));

        const Transaction tx = applySetting(core.engine, "power.idle.lock", "600");
        QCOMPARE(tx.phase, QString("committed"));
        QCOMPARE(tx.before, QString("300"));
        QVERIFY(tx.undoable());
        const QByteArray changed = readAll(config);
        QCOMPARE(changed, QByteArray(original).replace("300", "600"));
        QVERIFY(s.calls().join('\n').contains("omarchy shell -q shell reloadConfig"));
        QCOMPARE(readSetting(core.engine, "power.idle.lock").value, QString("600"));

        const Transaction undone = undoTx(core.engine, tx.id);
        QCOMPARE(undone.phase, QString("committed"));
        QCOMPARE(readAll(config), original);
        QCOMPARE(core.coordinator.transaction(tx.id).undoneBy, undone.id);
        QVERIFY(!core.coordinator.transaction(tx.id).undoable());
    }
    void undoRefusesToOverwriteALaterEdit() {
        Sandbox s;
        s.omarchy("exit 0");
        const QString config = s.env.configHome + "/omarchy/shell.json";
        writeFile(config, "{\"idle\":{\"lock\":300}}");
        AppCore core(s.env);
        const Transaction tx = applySetting(core.engine, "power.idle.lock", "600");
        QCOMPARE(tx.phase, QString("committed"));
        writeFile(config, "{\"idle\":{\"lock\":900}}");
        const Transaction undone = undoTx(core.engine, tx.id);
        QCOMPARE(undone.phase, QString("failed"));
        QVERIFY(undone.message.contains("changed after"));
        QCOMPARE(readAll(config), QByteArray("{\"idle\":{\"lock\":900}}"));
    }
    void missingShellJsonStartsFromDefaults() {
        Sandbox s;
        s.omarchy("exit 0");
        writeFile(s.env.omarchyPath + "/config/omarchy/shell.json", "{\"version\":1,\"idle\":{\"lock\":300,\"screensaver\":150},\"bar\":{\"position\":\"top\"}}");
        AppCore core(s.env);
        QCOMPARE(readSetting(core.engine, "power.idle.lock").value, QString("300"));
        const Transaction tx = applySetting(core.engine, "power.idle.lock", "60");
        QCOMPARE(tx.phase, QString("committed"));
        const auto written = QJsonDocument::fromJson(readAll(s.env.configHome + "/omarchy/shell.json")).object();
        QCOMPARE(written.value("bar").toObject().value("position").toString(), QString("top"));
        QCOMPARE(written.value("idle").toObject().value("lock").toInt(), 60);
    }
    void corruptConfigIsLeftAlone() {
        Sandbox s;
        s.omarchy("exit 0");
        const QString config = s.env.configHome + "/omarchy/shell.json";
        writeFile(config, "{ this is not json");
        AppCore core(s.env);
        const Transaction tx = applySetting(core.engine, "power.idle.lock", "60");
        QVERIFY(tx.phase != QLatin1String("committed"));
        QCOMPARE(readAll(config), QByteArray("{ this is not json"));
    }
    void planValidates_data() {
        QTest::addColumn<QString>("id");
        QTest::addColumn<QString>("value");
        QTest::addColumn<bool>("ok");
        QTest::newRow("in range") << "appearance.textsize" << "16" << true;
        QTest::newRow("too large") << "appearance.textsize" << "99" << false;
        QTest::newRow("not a number") << "appearance.textsize" << "big" << false;
        QTest::newRow("float") << "input.sensitivity" << "0.5" << true;
        QTest::newRow("float nan") << "input.sensitivity" << "nan" << false;
        QTest::newRow("float out of range") << "input.sensitivity" << "5" << false;
        QTest::newRow("toggle") << "power.stayawake" << "true" << true;
        QTest::newRow("toggle junk") << "power.stayawake" << "maybe" << false;
        QTest::newRow("choice") << "appearance.corners" << "sharp" << true;
        QTest::newRow("choice junk") << "appearance.corners" << "; rm -rf /" << false;
        QTest::newRow("custom allowed") << "power.idle.lock" << "1234" << true;
        QTest::newRow("pattern refuses") << "power.idle.lock" << "12; reboot" << false;
        QTest::newRow("control chars") << "system.weather" << "a\nb" << false;
        QTest::newRow("status row") << "system.version" << "9" << false;
        QTest::newRow("unknown") << "no.such.setting" << "1" << false;
        QTest::newRow("scale flag") << "displays.scale" << "-h" << false;
        QTest::newRow("scale word") << "displays.scale" << "up" << false;
        QTest::newRow("scale empty") << "displays.scale" << "" << false;
        QTest::newRow("scale") << "displays.scale" << "1.25" << true;
        QTest::newRow("dns choice") << "network.dns" << "Cloudflare" << true;
        QTest::newRow("dns typed") << "network.dns" << "9.9.9.9" << false;
        QTest::newRow("dns custom") << "network.dns" << "Custom" << false;
    }
    void planValidates() {
        QFETCH(QString, id);
        QFETCH(QString, value);
        QFETCH(bool, ok);
        Sandbox s;
        s.minimalHyprland();
        writeFile(s.env.sys("/sys/class/net/wlan0/wireless/x"), "");
        AppCore core(s.env);
        QCOMPARE(core.engine.plan(id, value).ok, ok);
    }
    void liveChoiceListsAreEnforced() {
        Sandbox s;
        s.omarchy("case \"$*\" in 'font list') echo 'Good Font';; esac");
        AppCore core(s.env);
        const QVariantList good{QVariantMap{{"value", "Good Font"}, {"label", "Good Font"}}};
        QVERIFY(core.engine.plan("appearance.font", "Good Font", good).ok);
        // omarchy-font-set feeds this into sed, so an unlisted value must not pass.
        QVERIFY(!core.engine.plan("appearance.font", "x/;s/.*/pwned/;", good).ok);
        // No list means no way to check, which also means no.
        QVERIFY(!core.engine.plan("appearance.font", "Good Font", {}).ok);
    }
    void commandWriteVerifiesByReadingBack() {
        Sandbox s;
        s.omarchy("state=\"$HOME/corners\"; case \"$*\" in 'hyprland corners') cat \"$state\" 2>/dev/null || echo theme;; 'hyprland corners '*) echo \"$3\" > \"$state\";; esac");
        s.command("hyprctl", "echo '{}'");
        AppCore core(s.env);
        const Transaction tx = applySetting(core.engine, "appearance.corners", "sharp");
        QCOMPARE(tx.phase, QString("committed"));
        QCOMPARE(readSetting(core.engine, "appearance.corners").value, QString("sharp"));
    }
    void exitZeroWithNoEffectIsNotSuccess() {
        // Many Omarchy setters swallow failures, so a clean exit proves nothing.
        Sandbox s;
        s.omarchy("case \"$*\" in 'hyprland corners') echo theme;; esac");
        s.command("hyprctl", "echo '{}'");
        AppCore core(s.env);
        const Transaction tx = applySetting(core.engine, "appearance.corners", "sharp");
        QVERIFY(tx.phase != QLatin1String("committed"));
        QVERIFY(!tx.message.isEmpty());
    }
    void failedCommandReportsItsError() {
        Sandbox s;
        s.omarchy("case \"$*\" in 'hyprland corners') echo theme;; *) echo 'denied' >&2; exit 4;; esac");
        s.command("hyprctl", "echo '{}'");
        AppCore core(s.env);
        const Transaction tx = applySetting(core.engine, "appearance.corners", "sharp");
        QVERIFY(tx.phase != QLatin1String("committed"));
        QVERIFY(tx.message.contains("denied"));
    }
    void toggleOnlyCommandsOnlyRunWhenNeeded() {
        Sandbox s;
        s.omarchy("f=\"$HOME/.local/state/omarchy/toggles/screensaver-off\"; case \"$*\" in 'toggle screensaver') if [ -f \"$f\" ]; then rm \"$f\"; else mkdir -p \"$(dirname \"$f\")\"; touch \"$f\"; fi;; esac");
        AppCore core(s.env);
        // Already in the requested state: running a toggle would flip it the wrong way.
        const Transaction same = applySetting(core.engine, "power.screensaver", "true");
        QCOMPARE(same.phase, QString("committed"));
        QVERIFY(!s.calls().join('\n').contains("toggle screensaver"));
        QVERIFY(!QFile::exists(s.env.stateHome + "/omarchy/toggles/screensaver-off"));
        const Transaction flipped = applySetting(core.engine, "power.screensaver", "false");
        QCOMPARE(flipped.phase, QString("committed"));
        QVERIFY(QFile::exists(s.env.stateHome + "/omarchy/toggles/screensaver-off"));
    }
    void aNonzeroExitAfterTheWorkIsDoneIsNotFailure() {
        // Found on real scripts: they end by sending a notification, which fails when
        // the shell is down, so they exit 1 although the setting changed.
        Sandbox s;
        s.omarchy("f=\"$HOME/c\"; case \"$*\" in 'hyprland corners') cat \"$f\" 2>/dev/null || echo theme;; 'hyprland corners '*) echo \"$3\" > \"$f\"; exit 1;; esac");
        AppCore core(s.env);
        const Transaction tx = applySetting(core.engine, "appearance.corners", "sharp");
        QCOMPARE(tx.phase, QString("committed"));
        QCOMPARE(readSetting(core.engine, "appearance.corners").value, QString("sharp"));
        QCOMPARE(undoTx(core.engine, tx.id).phase, QString("committed"));
        QCOMPARE(readSetting(core.engine, "appearance.corners").value, QString("theme"));
    }
    void barToggleIsNotInverted() {
        // `omarchy toggle bar on` hides the bar, so "show" must send off.
        Sandbox s;
        s.omarchy("f=\"$HOME/.local/state/omarchy/toggles/bar-off\"; case \"$*\" in 'toggle bar off') rm -f \"$f\";; 'toggle bar on') mkdir -p \"$(dirname \"$f\")\"; touch \"$f\";; esac");
        AppCore core(s.env);
        QCOMPARE(readSetting(core.engine, "desktop.bar.visible").value, QString("true"));
        QCOMPARE(applySetting(core.engine, "desktop.bar.visible", "false").phase, QString("committed"));
        QVERIFY(s.calls().join('\n').contains("omarchy toggle bar on"));
        QCOMPARE(readSetting(core.engine, "desktop.bar.visible").value, QString("false"));
    }
    void defaultAppSettersOnlyOfferInstalledApps() {
        // A default setter installs whatever is missing; that must stay a separate,
        // explicit action.
        Sandbox s;
        s.command("firefox", "exit 0");
        s.omarchy("exit 0");
        AppCore core(s.env);
        QVariantList choices;
        bool done = false;
        core.engine.readChoices("apps.default.browser", [&](const QVariantList &c) { choices = c; done = true; });
        QTRY_VERIFY(done);
        QStringList values;
        for (const auto &choice : choices)
            values << choice.toMap().value("value").toString();
        QCOMPARE(values, QStringList{"firefox"});
    }
    void unavailableHardwareIsDistinct() {
        Sandbox s;
        AppCore core(s.env);
        const auto *entry = core.catalog.find("input.tp_natural");
        QVERIFY(entry);
        const auto availability = core.capabilities.availability(*entry);
        QVERIFY(!availability.available);
        QCOMPARE(availability.kind, QString("hardware"));
        QVERIFY(availability.reason.contains("touchpad"));
        QCOMPARE(readSetting(core.engine, "input.tp_natural").status, QString("unavailable"));
    }
    void missingToolsAreDistinct() {
        Sandbox s;
        AppCore core(s.env);
        const auto *entry = core.catalog.find("helper.printer");
        QVERIFY(entry);
        const auto availability = core.capabilities.availability(*entry);
        QVERIFY(!availability.available);
        QCOMPARE(availability.kind, QString("tool"));
    }
    void shellDownIsReportedNotHidden() {
        Sandbox s;
        s.omarchy("exit 1");
        AppCore core(s.env);
        core.capabilities.scanServicesBlocking();
        QVERIFY(!core.capabilities.has("shell"));
        const auto availability = core.capabilities.availability(*core.catalog.find("desktop.dnd"));
        QVERIFY(!availability.available);
        QCOMPARE(availability.kind, QString("service"));
        QVERIFY(availability.reason.contains("shell"));
    }
    void readReportsUnknownRatherThanStaleValue() {
        Sandbox s;
        s.omarchy("echo oops >&2; exit 1");
        AppCore core(s.env);
        const ReadResult result = readSetting(core.engine, "appearance.corners");
        QCOMPARE(result.status, QString("unknown"));
        QVERIFY(result.value.isEmpty());
        QVERIFY(!result.reason.isEmpty());
    }
    void hyprOptionParsing() {
        bool ok = false;
        QCOMPARE(SettingsEngine::parseHyprOption(R"({"option":"x","int":40,"set":true})", &ok), QString("40"));
        QVERIFY(ok);
        QCOMPARE(SettingsEngine::parseHyprOption(R"({"bool":false})", &ok), QString("false"));
        QCOMPARE(SettingsEngine::parseHyprOption(R"({"float":0.400000})", &ok), QString("0.4"));
        QCOMPARE(SettingsEngine::parseHyprOption(R"({"float":0.000000})", &ok), QString("0"));
        QCOMPARE(SettingsEngine::parseHyprOption(R"({"str":"[[EMPTY]]"})", &ok), QString());
        QCOMPARE(SettingsEngine::parseHyprOption(R"({"css":"5 5 5 5"})", &ok), QString("5"));
        SettingsEngine::parseHyprOption("garbage", &ok);
        QVERIFY(!ok);
    }
    void hyprOverrideFullLifecycle() {
        Sandbox s;
        s.minimalHyprland();
        const QString option = s.dir.path() + "/hypr-input_repeat_rate";
        // The fake compositor picks the value up from the owned module on reload.
        s.command("hyprctl", (kScript2));
        const QByteArray stock = readAll(s.env.configHome + "/hypr/hyprland.lua");
        AppCore core(s.env);
        core.capabilities.scanServicesBlocking();
        QCOMPARE(readSetting(core.engine, "input.repeat_rate").value, QString("40"));
        QVERIFY(!readSetting(core.engine, "input.repeat_rate").isOverride);

        const ChangePlan plan = core.engine.plan("input.repeat_rate", "31");
        QVERIFY(plan.ok);
        QVERIFY(plan.needsOwnership);
        QVERIFY(plan.confirmText.contains("hyprland.lua"));

        const Transaction tx = applySetting(core.engine, "input.repeat_rate", "31");
        QCOMPARE(tx.phase, QString("committed"));
        const ReadResult read = readSetting(core.engine, "input.repeat_rate");
        QCOMPARE(read.value, QString("31"));
        QVERIFY(read.isOverride);
        QVERIFY(readAll(s.env.configHome + "/hypr/omarchy-control.lua").contains("repeat_rate = 31"));
        // Second change needs no new ownership decision.
        QVERIFY(!core.engine.plan("input.repeat_rate", "33").needsOwnership);

        // "Use default" removes the override and reads the inherited value again.
        Transaction reset;
        bool done = false;
        core.engine.reset("input.repeat_rate", [&](const Transaction &t) { reset = t; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 8000);
        QCOMPARE(reset.phase, QString("committed"));
        QCOMPARE(readSetting(core.engine, "input.repeat_rate").value, QString("40"));
        QVERIFY(!readAll(s.env.configHome + "/hypr/omarchy-control.lua").contains("repeat_rate"));
        QVERIFY(readAll(s.env.configHome + "/hypr/hyprland.lua").contains("hypr.mine"));
        Q_UNUSED(stock);
    }
    void hyprOverrideRollsBackOnNewConfigError() {
        Sandbox s;
        s.minimalHyprland();
        s.command("hyprctl", kScript3);
        const QByteArray stock = readAll(s.env.configHome + "/hypr/hyprland.lua");
        AppCore core(s.env);
        core.capabilities.scanServicesBlocking();
        const Transaction tx = applySetting(core.engine, "input.repeat_rate", "31");
        QCOMPARE(tx.phase, QString("rolled-back"));
        QVERIFY(tx.message.contains("config error"));
        QVERIFY(!QFile::exists(s.env.configHome + "/hypr/omarchy-control.lua"));
        QCOMPARE(readAll(s.env.configHome + "/hypr/hyprland.lua"), stock);
    }
    void hyprOverrideToleratesExistingErrors() {
        Sandbox s;
        s.minimalHyprland();
        s.command("hyprctl", kScript4);
        AppCore core(s.env);
        core.capabilities.scanServicesBlocking();
        QCOMPARE(applySetting(core.engine, "input.repeat_rate", "31").phase, QString("committed"));
    }
    void hyprOverrideExplainsWhenCustomConfigWins() {
        Sandbox s;
        s.minimalHyprland();
        s.command("hyprctl", kScript5);
        AppCore core(s.env);
        core.capabilities.scanServicesBlocking();
        const Transaction tx = applySetting(core.engine, "input.repeat_rate", "31");
        QCOMPARE(tx.phase, QString("rolled-back"));
        QVERIFY(tx.message.contains("Another part"));
        QVERIFY(!QFile::exists(s.env.configHome + "/hypr/omarchy-control.lua"));
    }
    void handEditedModulePausesChanges() {
        Sandbox s;
        s.minimalHyprland();
        HyprModule module(s.env);
        module.setOption({"input", "repeat_rate"}, 31);
        QVERIFY(module.save());
        QVERIFY(module.installImport());
        const QByteArray edited = readAll(module.path()) + "-- mine\n";
        writeFile(module.path(), edited);
        AppCore core(s.env);
        core.capabilities.scanServicesBlocking();
        const ChangePlan plan = core.engine.plan("input.repeat_rate", "33");
        QVERIFY(!plan.ok);
        QVERIFY(plan.error.contains("by hand"));
        QCOMPARE(readSetting(core.engine, "input.repeat_rate").status, QString("custom"));
        QCOMPARE(readAll(module.path()), edited);
    }
    void customLayoutGetsAdvancedRouteNotARewrite() {
        Sandbox s;
        writeFile(s.env.configHome + "/hypr/hyprland.lua", "-- entirely my own\n");
        s.command("hyprctl", "echo '{\"int\":40}'");
        AppCore core(s.env);
        const ChangePlan plan = core.engine.plan("input.repeat_rate", "33");
        QVERIFY(!plan.ok);
        QVERIFY(plan.error.contains("stock layout"));
    }

    // ---- Catalog invariants ---------------------------------------------------
    void luaOptionKeysUseHyprlandsLuaSpelling() {
        // Found on real Hyprland: getoption says tap-to-click, but the Lua key is
        // tap_to_click, and the dashed form is a config error.
        Catalog catalog;
        for (const auto &e : catalog.entries()) {
            if (e.write.value("kind").toString() != QLatin1String("hypr"))
                continue;
            for (const auto &part : e.write.value("path").toArray())
                QVERIFY2(!part.toString().contains('-'), qPrintable(e.id));
        }
    }
    void catalogIsStructurallySound() {
        Catalog catalog;
        QVERIFY(catalog.entries().size() > 400);
        QSet<QString> ids;
        QSet<QString> pages;
        for (const auto &page : catalog.pages())
            pages << page.id;
        QCOMPARE(catalog.pages().size(), 12);
        static const QSet<QString> types{"toggle", "choice", "int", "float", "slider", "text", "status", "action"};
        static const QSet<QString> modes{"native", "omarchy-panel", "omarchy-wizard", "external-helper", "advanced-config"};
        static const QSet<QString> recoveries{"auto-undo", "best-effort", "no-undo"};
        static const QRegularExpression domain("^C[0-9]{2}$");
        for (const auto &e : catalog.entries()) {
            const QByteArray name = e.id.toUtf8();
            QVERIFY2(!ids.contains(e.id), name);
            ids << e.id;
            QVERIFY2(pages.contains(e.page), name);
            QVERIFY2(types.contains(e.type), name);
            QVERIFY2(modes.contains(e.integration), name);
            QVERIFY2(recoveries.contains(e.recovery), name);
            QVERIFY2(domain.match(e.domain).hasMatch(), name);
            QVERIFY2(!e.label.trimmed().isEmpty(), name);
            QVERIFY2(!e.section.isEmpty(), name);
            if (e.isSetting())
                QVERIFY2(!e.read.isEmpty(), name);
            if (e.isAction())
                QVERIFY2(!e.launch.isEmpty(), name);
        }
    }
    void everyPageHasEverySection() {
        Catalog catalog;
        for (const auto &e : catalog.entries()) {
            const auto *page = catalog.page(e.page);
            QVERIFY(page);
            QVERIFY2(page->sections.contains(e.section), qPrintable(e.id + " -> " + e.section));
        }
    }
    void onlyValuePlaceholdersReachCommands() {
        Catalog catalog;
        static const QRegularExpression placeholder("\\{[a-z]+\\}");
        for (const auto &e : catalog.entries()) {
            for (const auto &action : {e.write, e.read}) {
                for (const auto &arg : argvOf(action)) {
                    auto matches = placeholder.globalMatch(arg);
                    while (matches.hasNext())
                        QVERIFY2(matches.next().captured() == QLatin1String("{value}"), qPrintable(e.id + ": " + arg));
                }
            }
            // The launch path has no substitution at all: every route is static.
            const QString launchText = QString::fromUtf8(QJsonDocument(e.launch).toJson());
            QVERIFY2(!launchText.contains("{value}"), qPrintable(e.id));
        }
    }
    void noCatalogCommandRunsAShellWithAValue() {
        Catalog catalog;
        for (const auto &e : catalog.entries()) {
            const QStringList argv = argvOf(e.write);
            QVERIFY2(!argv.contains("bash") && !argv.contains("sh") && !argv.contains("-c"), qPrintable(e.id));
            // Only an argument, never part of a command string, carries a value.
            for (const auto &arg : argv)
                // Superwhisper takes JSON, so its strings arrive wrapped in quotes, and a
                // pattern or choice list keeps quotes out of the value.
                if (arg.contains("{value}"))
                    QVERIFY2(arg == QLatin1String("{value}") || arg == QLatin1String("{value}%") || (arg == QLatin1String("\"{value}\"") && (!e.pattern.isEmpty() || !e.allowCustomValue)), qPrintable(e.id + ": " + arg));
        }
    }
    void noPrivilegeOrSecretRoutes() {
        Catalog catalog;
        for (const auto &e : catalog.entries()) {
            const QString text = QString::fromUtf8(QJsonDocument(e.write).toJson()) + QString::fromUtf8(QJsonDocument(e.read).toJson());
            QVERIFY2(!text.contains("sudo") && !text.contains("pkexec"), qPrintable(e.id));
            const QString launch = QString::fromUtf8(QJsonDocument(e.launch).toJson());
            // Printing a Wi-Fi password is deliberately not reachable from here.
            QVERIFY2(!launch.contains("network-password") && !launch.contains("\"network\",\n\"password\""), qPrintable(e.id));
            static const QRegularExpression sudo("(^|[\\s\";])sudo\\s");
            // The one exception: UFW has no other route, and these run visibly in a
            // terminal, where sudo asks for the password itself.
            QVERIFY2(!sudo.match(launch).hasMatch() || e.id.startsWith("firewall."), qPrintable(e.id));
        }
    }
    void destructiveRoutesAlwaysAsk() {
        Catalog catalog;
        static const QRegularExpression destructive("omarchy-(remove|reinstall|refresh|system-factory|setup-security-.*--remove)");
        for (const auto &e : catalog.entries()) {
            if (!e.isAction())
                continue;
            const QString command = e.launch.value("command").toString();
            const bool isRefreshApplications = command.contains("refresh-applications");
            if (e.id.startsWith("remove.") || e.id == "setup.reset" || (destructive.match(command).hasMatch() && !isRefreshApplications)) {
                QVERIFY2(e.confirm == QLatin1String("destructive") || e.confirm == QLatin1String("confirm"), qPrintable(e.id));
                QVERIFY2(!e.confirmText.isEmpty(), qPrintable(e.id));
            }
        }
    }
    void agentAndDictationAreNotPlainSetters() {
        // `omarchy default agent` starts the agent; it must say so before it runs.
        Catalog catalog;
        const auto *claude = catalog.find("setup.default.agent.claude");
        QVERIFY(claude);
        QVERIFY(claude->isAction());
        QVERIFY(!claude->confirm.isEmpty());
        QVERIFY(claude->confirmText.contains("starts"));
        QVERIFY(!catalog.find("apps.default.agent")->isEditable());
        QVERIFY(!catalog.find("apps.default.dictation")->isEditable());
    }
    void helperLaunchesResolveToArgv() {
        Sandbox s;
        QCOMPARE(HelperLauncher::resolve(QJsonObject{{"kind", "terminal"}, {"command", "omarchy-update"}}, s.env, true),
                 (QStringList{"omarchy-launch-floating-terminal-with-presentation", "omarchy-update"}));
        QCOMPARE(HelperLauncher::resolve(QJsonObject{{"kind", "panel"}, {"target", "omarchy.network"}, {"verb", "toggle"}}, s.env, true),
                 (QStringList{"omarchy", "shell", "shell", "toggle", "omarchy.network"}));
        QCOMPARE(HelperLauncher::resolve(QJsonObject{{"kind", "app"}, {"argv", QJsonArray{"gnome-disks"}}}, s.env, true),
                 (QStringList{"uwsm-app", "--", "gnome-disks"}));
        QCOMPARE(HelperLauncher::resolve(QJsonObject{{"kind", "app"}, {"argv", QJsonArray{"gnome-disks"}}}, s.env, false), QStringList{"gnome-disks"});
        QCOMPARE(HelperLauncher::resolve(QJsonObject{{"kind", "config"}, {"file", "~/.config/hypr/input.lua"}}, s.env, true),
                 (QStringList{"omarchy", "launch", "config", "editor", s.env.home + "/.config/hypr/input.lua"}));
    }
    void launchReportsOpenedNotChanged() {
        Sandbox s;
        s.omarchy("exit 0");
        AppCore core(s.env);
        core.capabilities.scanServicesBlocking();
        HelperLauncher launcher(&core.runner, &core.capabilities);
        HelperLauncher::Outcome outcome;
        bool done = false;
        launcher.launch(*core.catalog.find("panel.network"), [&](const HelperLauncher::Outcome &o) { outcome = o; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(outcome.opened);
        QVERIFY(outcome.message.startsWith("Opened"));
        QVERIFY(core.coordinator.history().isEmpty());
    }
    void panelFailureIsReported() {
        Sandbox s;
        s.omarchy("case \"$*\" in 'shell shell ping') exit 0;; *) exit 1;; esac");
        AppCore core(s.env);
        core.capabilities.scanServicesBlocking();
        HelperLauncher launcher(&core.runner, &core.capabilities);
        HelperLauncher::Outcome outcome;
        bool done = false;
        launcher.launch(*core.catalog.find("panel.network"), [&](const HelperLauncher::Outcome &o) { outcome = o; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(!outcome.opened);
        QVERIFY(outcome.message.contains("shell"));
    }
    void pageRoutesPointAtRealDestinations() {
        Catalog catalog;
        for (const auto &e : catalog.entries()) {
            if (e.launch.value("kind").toString() != QLatin1String("page"))
                continue;
            const auto *page = catalog.page(e.launch.value("page").toString());
            QVERIFY2(page, qPrintable(e.id));
            const QString focus = e.launch.value("focus").toString();
            if (focus.startsWith("block."))
                QVERIFY2(page->blocks.contains(focus.mid(6)), qPrintable(e.id + " -> " + focus));
            else if (!focus.isEmpty())
                QVERIFY2(catalog.find(focus), qPrintable(e.id + " -> " + focus));
            QVERIFY2(QFile::exists(":/Block" + (focus.isEmpty() ? QString("Audio") : focus.mid(6, 1).toUpper() + focus.mid(7)) + ".qml") || !focus.startsWith("block."), qPrintable(focus));
        }
    }

    // ---- Search ---------------------------------------------------------------
    void searchLandsOnTheRightSetting_data() {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QString>("expected");
        QTest::newRow("sleeping") << "stop sleeping" << "power.stayawake";
        QTest::newRow("text") << "make text bigger" << "appearance.textsize";
        QTest::newRow("mic") << "change microphone" << "native.audio.input";
        QTest::newRow("login") << "start an app at login" << "native.startup";
        QTest::newRow("wallpaper") << "wallpaper" << "appearance.background";
        QTest::newRow("dnd") << "do not disturb" << "desktop.dnd";
        QTest::newRow("printers") << "printer" << "helper.printer";
        QTest::newRow("resolution") << "resolution" << "native.displays";
        QTest::newRow("volume") << "volume" << "native.audio.output";
        QTest::newRow("config filename") << "hyprsunset.conf" << "setup.config.hyprsunset";
        QTest::newRow("helper name") << "nmtui" << "helper.nmtui";
        QTest::newRow("shortcuts") << "keyboard shortcuts" << "native.shortcuts";
        QTest::newRow("snapshots") << "snapshot" << "snapshot.create";
        QTest::newRow("case") << "BLUETOOTH DEVICES" << "native.bluetooth";
    }
    void searchLandsOnTheRightSetting() {
        QFETCH(QString, query);
        QFETCH(QString, expected);
        Catalog catalog;
        const auto hits = catalog.search(query, 5);
        QVERIFY2(!hits.isEmpty(), qPrintable(query));
        QStringList ids;
        for (const auto &hit : hits)
            ids << hit.entry->id;
        QVERIFY2(ids.mid(0, 2).contains(expected), qPrintable(query + " -> " + ids.join(", ")));
    }
    void searchIsFastAndSideEffectFree() {
        Sandbox s;
        s.omarchy("touch \"$HOME/ran\"");
        AppCore core(s.env);
        QElapsedTimer timer;
        timer.start();
        for (const char *q : {"a", "theme", "stop sleeping", "install", "remove steam", "xyz nothing"})
            core.catalog.search(q);
        const auto perQuery = timer.elapsed() / 6;
        QVERIFY2(perQuery < 100, qPrintable(QString("%1 ms per query").arg(perQuery)));
        QVERIFY(!QFile::exists(s.env.home + "/ran"));
    }
    void searchEmptyQueryFindsNothing() {
        Catalog catalog;
        QVERIFY(catalog.search("").isEmpty());
        QVERIFY(catalog.search("   ").isEmpty());
        QVERIFY(catalog.search("zzzqqq").isEmpty());
    }

    // ---- Coverage -------------------------------------------------------------
    void everyDiscoveredEntryHasADestination() {
        // The release definition: zero unclassified first-party entries.
        const auto inventory = QJsonDocument::fromJson(readAll(PROJECT_ROOT "/docs/inventory-2026-11-05.json")).object();
        QVERIFY(!inventory.isEmpty());
        Catalog catalog;
        const auto result = Coverage::reconcile(catalog, inventory);
        QVERIFY2(result.unclassifiedCommands.isEmpty(), qPrintable(result.unclassifiedCommands.join(", ")));
        QVERIFY2(result.unclassifiedMenu.isEmpty(), qPrintable(result.unclassifiedMenu.join(", ")));
        QCOMPARE(result.claimedCommands + result.excludedCommands, 433);
        QVERIFY(result.claimedMenu > 300);
    }
    void aNewCommandFailsCoverage() {
        Catalog catalog;
        const QJsonArray commands{QJsonObject{{"route", "omarchy brand new feature"}, {"binary", "omarchy-brand-new-feature"}, {"hidden", false}}};
        const auto result = Coverage::reconcileLive(catalog, commands);
        QCOMPARE(result.unclassifiedCommands, QStringList{"omarchy brand new feature"});
        const QJsonArray hidden{QJsonObject{{"route", "omarchy brand new feature"}, {"binary", "x"}, {"hidden", true}}};
        QVERIFY(Coverage::reconcileLive(catalog, hidden).unclassifiedCommands.isEmpty());
    }
    void everyExclusionHasAReason() {
        Catalog catalog;
        for (const auto &group : {"commands", "menu"})
            for (const auto &value : catalog.exclusions().value(QLatin1String(group)).toArray()) {
                QVERIFY(!value.toObject().value("reason").toString().isEmpty());
                QVERIFY(!value.toObject().value("pattern").toString().isEmpty());
            }
    }
    void ledgerHasEveryRequiredField() {
        Catalog catalog;
        const auto rows = Coverage::ledger(catalog);
        QCOMPARE(rows.size(), catalog.entries().size());
        for (const auto &value : rows) {
            const auto row = value.toObject();
            for (const char *field : {"id", "domain", "source_references", "searchable_labels", "applicable_capability", "read_method", "integration_mode", "write_or_launch_method", "apply_restart_semantics", "recovery_class", "supported_versions", "implementation_status", "validation_evidence", "known_limitation"})
                QVERIFY2(row.contains(QLatin1String(field)), field);
            // Nothing claims "verified" without recorded evidence.
            QCOMPARE(row.value("implementation_status").toString(), QString("implemented"));
        }
    }
    void wildcards() {
        QVERIFY(Coverage::wildcardMatch("omarchy hw *", "omarchy hw laptop"));
        QVERIFY(!Coverage::wildcardMatch("omarchy hw *", "omarchy hwx"));
        QVERIFY(Coverage::wildcardMatch("a.b", "a.b"));
        QVERIFY(!Coverage::wildcardMatch("a.b", "axb"));
    }

    // ---- Device parsers -------------------------------------------------------
    void audioParsesAndFiltersMonitors() {
        const QByteArray sources = R"([{"name":"mic","description":"Mic","mute":true,"state":"IDLE","volume":{"mono":{"value_percent":"80%"}},"properties":{"object.id":"44"}},{"name":"x.monitor","description":"Monitor of x","volume":{},"properties":{"device.class":"monitor"}}])";
        const auto rows = AudioProvider::parseNodes(sources, "mic", true);
        QCOMPARE(rows.size(), 1);
        const auto mic = rows.first().toMap();
        QCOMPARE(mic.value("volume").toInt(), 80);
        QVERIFY(mic.value("isDefault").toBool());
        QVERIFY(mic.value("mute").toBool());
        QCOMPARE(mic.value("objectId").toString(), QString("44"));
    }
    void audioKeepsDspChainsOutOfTheStreamList() {
        const QByteArray inputs = R"([{"index":1,"sink":2,"volume":{},"properties":{"application.name":"Player"}},{"index":2,"sink":3,"volume":{},"properties":{}},{"index":3,"sink":3,"volume":{},"properties":{"application.name":"EasyEffects"}}])";
        const auto rows = AudioProvider::parseStreams(inputs);
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().toMap().value("app").toString(), QString("Player"));
    }
    void audioDefaultMustActuallyChange() {
        Sandbox s;
        // Omarchy's setter exits 0 even when nothing happened.
        s.omarchy("exit 0");
        s.command("pactl", kScript6);
        ChangeCoordinator coordinator(s.env);
        CommandRunner runner(s.env);
        AudioProvider audio(&runner, &coordinator);
        QSignalSpy messages(&audio, &AudioProvider::message);
        audio.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(audio.outputs().size(), 2, 5000);
        audio.setDefaultOutput("b");
        QTRY_VERIFY_WITH_TIMEOUT(messages.count() >= 1, 5000);
        QVERIFY(messages.first().at(1).toBool());
        QVERIFY(coordinator.history().first().phase != QLatin1String("committed"));
    }
    void bluetoothInfoParsing() {
        const auto device = BluetoothProvider::parseInfo("Device AA:BB:CC:DD:EE:01 (public)\n\tName: Phones\n\tAlias: Phones\n\tIcon: audio-headset\n\tPaired: yes\n\tConnected: no\n\tBattery Percentage: 0x4b (75)\n", "AA:BB:CC:DD:EE:01");
        QCOMPARE(device.value("name").toString(), QString("Phones"));
        QVERIFY(device.value("paired").toBool());
        QVERIFY(!device.value("connected").toBool());
        QCOMPARE(device.value("battery").toInt(), 75);
        QVERIFY(BluetoothProvider::isValidAddress("AA:BB:CC:DD:EE:01"));
        QVERIFY(!BluetoothProvider::isValidAddress("AA:BB; rm -rf /"));
    }
    void bluetoothDeviceRemovalIsVerified() {
        Sandbox s;
        s.omarchy("exit 0");
        s.command("bluetoothctl", "echo 'Connected: no'");
        ChangeCoordinator coordinator(s.env);
        CommandRunner runner(s.env);
        BluetoothProvider bluetooth(&runner, &coordinator);
        QSignalSpy messages(&bluetooth, &BluetoothProvider::message);
        bluetooth.connectDevice("AA:BB:CC:DD:EE:01");
        QTRY_VERIFY_WITH_TIMEOUT(messages.count() >= 1, 5000);
        QVERIFY(messages.first().at(1).toBool());
        QVERIFY(messages.first().at(0).toString().contains("did not respond"));
    }
    void networkTerseEscapes() {
        QCOMPARE(NetworkProvider::splitTerse(R"(a\:b:c\\d:e)"), (QStringList{"a:b", "c\\d", "e"}));
    }
    void networkDeduplicatesAccessPoints() {
        const auto rows = NetworkProvider::parseNetworks(" :Cafe:30:WPA2\n :Cafe:70:WPA2\n*:Home:40:WPA3\n :Open:20:\n ::10:WPA2\n");
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows[0].toMap().value("ssid").toString(), QString("Home"));
        QCOMPARE(rows[1].toMap().value("signal").toInt(), 70);
        QVERIFY(!rows[2].toMap().value("secured").toBool());
    }
    void networkDevicesIgnoreInternals() {
        const auto rows = NetworkProvider::parseDevices("wlan0:wifi:connected:Home\nlo:loopback:connected (externally):lo\np2p:wifi-p2p:disconnected:\ntailscale0:tun:connected (externally):ts\n");
        QCOMPARE(rows.size(), 2);
    }
    void networkSwitchUsesACheckpointAndRollsBack() {
        Sandbox s;
        s.command("busctl", kScript7);
        s.command("nmcli", "exit 0");
        ChangeCoordinator coordinator(s.env);
        CommandRunner runner(s.env);
        NetworkProvider network(&runner, &coordinator);
        network.activate("11111111-1111-1111-1111-111111111111", "Cafe");
        QTRY_VERIFY_WITH_TIMEOUT(network.awaitingKeep(), 5000);
        const QString calls = s.calls().join('\n');
        QVERIFY(calls.indexOf("CheckpointCreate") < calls.indexOf("connection up"));
        network.revertConnection();
        QTRY_VERIFY_WITH_TIMEOUT(s.calls().join('\n').contains("CheckpointRollback o /org/freedesktop/NetworkManager/Checkpoint/7"), 5000);
        QVERIFY(!network.awaitingKeep());
    }
    void networkKeepDestroysTheCheckpoint() {
        Sandbox s;
        s.command("busctl", kScript8);
        s.command("nmcli", "exit 0");
        ChangeCoordinator coordinator(s.env);
        CommandRunner runner(s.env);
        NetworkProvider network(&runner, &coordinator);
        network.activate("11111111-1111-1111-1111-111111111111", "Cafe");
        QTRY_VERIFY_WITH_TIMEOUT(network.awaitingKeep(), 5000);
        network.keepConnection();
        QTRY_VERIFY_WITH_TIMEOUT(s.calls().join('\n').contains("CheckpointDestroy"), 5000);
        QVERIFY(!s.calls().join('\n').contains("CheckpointRollback"));
    }
    void networkFailedSwitchRollsBackImmediately() {
        Sandbox s;
        s.command("busctl", kScript9);
        s.command("nmcli", kScript10);
        ChangeCoordinator coordinator(s.env);
        CommandRunner runner(s.env);
        NetworkProvider network(&runner, &coordinator);
        network.activate("11111111-1111-1111-1111-111111111111", "Cafe");
        QTRY_VERIFY_WITH_TIMEOUT(s.calls().join('\n').contains("CheckpointRollback"), 5000);
    }
    void networkWorksWithoutACheckpoint() {
        Sandbox s;
        s.command("busctl", "echo denied >&2; exit 1");
        s.command("nmcli", "exit 0");
        ChangeCoordinator coordinator(s.env);
        CommandRunner runner(s.env);
        NetworkProvider network(&runner, &coordinator);
        QSignalSpy messages(&network, &NetworkProvider::message);
        network.activate("11111111-1111-1111-1111-111111111111", "Cafe");
        QTRY_VERIFY_WITH_TIMEOUT(messages.count() >= 1, 5000);
        QVERIFY(!network.awaitingKeep());
        QVERIFY(!messages.first().at(1).toBool());
    }
    void networkRefusesBadIds() {
        Sandbox s;
        s.command("nmcli", "exit 0");
        ChangeCoordinator coordinator(s.env);
        CommandRunner runner(s.env);
        NetworkProvider network(&runner, &coordinator);
        QSignalSpy messages(&network, &NetworkProvider::message);
        network.activate("--help", "x");
        QCOMPARE(messages.count(), 1);
        QVERIFY(s.calls().isEmpty());
    }
    void serviceUnitsMergeAndSort() {
        const auto rows = ServicesProvider::mergeUnits(
            R"([{"unit":"b.service","active":"active","description":"B"},{"unit":"a.service","active":"failed","description":"A"},{"unit":"t.timer","active":"active"}])",
            R"([{"unit_file":"b.service","state":"enabled"},{"unit_file":"c.service","state":"disabled"},{"unit_file":"g@.service","state":"disabled"},{"unit_file":"s.service","state":"static"}])");
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows[0].toMap().value("unit").toString(), QString("a.service"));
        QVERIFY(rows[0].toMap().value("failed").toBool());
        QCOMPARE(rows[1].toMap().value("enabled").toString(), QString("enabled"));
        QCOMPARE(rows[2].toMap().value("unit").toString(), QString("c.service"));
    }
    void serviceActionsAreConstrained() {
        QVERIFY(ServicesProvider::isSafeUnit("foo-bar@x.service"));
        QVERIFY(!ServicesProvider::isSafeUnit("foo.timer"));
        QVERIFY(!ServicesProvider::isSafeUnit("--now.service; reboot"));
        Sandbox s;
        s.command("systemctl", "echo active");
        ChangeCoordinator coordinator(s.env);
        CommandRunner runner(s.env);
        ServicesProvider services(&runner, &coordinator);
        QSignalSpy messages(&services, &ServicesProvider::message);
        services.serviceAction("foo.service", "mask", false);
        QCOMPARE(messages.count(), 1);
        services.serviceAction("bad unit", "start", false);
        QCOMPARE(messages.count(), 2);
        QVERIFY(s.calls().isEmpty());
    }
    void serviceStartIsVerified() {
        Sandbox s;
        s.command("systemctl", kScript11);
        ChangeCoordinator coordinator(s.env);
        CommandRunner runner(s.env);
        ServicesProvider services(&runner, &coordinator);
        QSignalSpy messages(&services, &ServicesProvider::message);
        services.serviceAction("foo.service", "start", false);
        QTRY_VERIFY_WITH_TIMEOUT(messages.count() >= 1, 5000);
        QVERIFY(messages.first().at(1).toBool());
        QVERIFY(messages.first().at(0).toString().contains("did not start"));
        QVERIFY(s.calls().join('\n').contains("systemctl --user start foo.service"));
    }
    void startupMasksWithoutTouchingTheSystemEntry() {
        Sandbox s;
        const QString system = s.env.sys("/etc/xdg/autostart/agent.desktop");
        writeFile(system, "[Desktop Entry]\nName=Agent\nExec=agent\n");
        ChangeCoordinator coordinator(s.env);
        CommandRunner runner(s.env);
        ServicesProvider services(&runner, &coordinator);
        auto rows = ServicesProvider::readStartup(s.env);
        QCOMPARE(rows.size(), 1);
        QVERIFY(rows.first().toMap().value("enabled").toBool());
        services.setStartupEnabled("agent.desktop", false);
        QTRY_VERIFY_WITH_TIMEOUT(!coordinator.history().isEmpty() && coordinator.history().first().finished(), 5000);
        QCOMPARE(coordinator.history().first().phase, QString("committed"));
        rows = ServicesProvider::readStartup(s.env);
        QVERIFY(!rows.first().toMap().value("enabled").toBool());
        QCOMPARE(readAll(system), QByteArray("[Desktop Entry]\nName=Agent\nExec=agent\n"));
        QVERIFY(readAll(s.env.configHome + "/autostart/agent.desktop").contains("Hidden=true"));
    }
    void startupRefusesPathTraversal() {
        Sandbox s;
        ChangeCoordinator coordinator(s.env);
        CommandRunner runner(s.env);
        ServicesProvider services(&runner, &coordinator);
        QSignalSpy messages(&services, &ServicesProvider::message);
        services.setStartupEnabled("../../evil.desktop", false);
        QCOMPARE(messages.count(), 1);
        QVERIFY(coordinator.history().isEmpty());
    }
    void shortcutModifiersAndKeys() {
        QCOMPARE(ShortcutsProvider::modsToText(64 + 1), QString("SUPER + SHIFT"));
        QCOMPARE(ShortcutsProvider::modsToText(0), QString());
        QCOMPARE(ShortcutsProvider::canonicalKeys("shift + super + f"), QString("SUPER + SHIFT + F"));
        QVERIFY(ShortcutsProvider::isValidKeys("SUPER + F"));
        QVERIFY(!ShortcutsProvider::isValidKeys("F"));
        QVERIFY(!ShortcutsProvider::isValidKeys("SUPER + \"); os.exit()"));
        const auto rows = ShortcutsProvider::parseBinds(R"([{"modmask":64,"key":"space","description":"Launcher"},{"modmask":0,"key":"","catch_all":true}])");
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().toMap().value("keys").toString(), QString("SUPER + SPACE"));
    }
    void shortcutCollisionsAreNeverSilent() {
        Sandbox s;
        s.minimalHyprland();
        writeFile(s.dir.path() + "/hypr-binds", R"([{"modmask":64,"key":"SPACE","description":"Launcher"},{"modmask":64,"key":"E","description":"Files"}])");
        ChangeCoordinator coordinator(s.env);
        CommandRunner runner(s.env);
        ShortcutsProvider shortcuts(&runner, &coordinator);
        QSignalSpy changed(&shortcuts, &ShortcutsProvider::changed);
        shortcuts.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 1, 5000);
        const auto check = shortcuts.checkKeys("super + e");
        QCOMPARE(check.value("conflicts").toStringList(), QStringList{"Files"});
        QVERIFY(!check.value("critical").toBool());
        QVERIFY(shortcuts.checkKeys("SUPER + SPACE").value("critical").toBool());
        QVERIFY(shortcuts.checkKeys("SUPER + Y").value("conflicts").toStringList().isEmpty());
        QSignalSpy messages(&shortcuts, &ShortcutsProvider::message);
        shortcuts.addBinding("SUPER + E", "Mine", "mine", false);
        QCOMPARE(messages.count(), 1);
        QVERIFY(messages.first().at(1).toBool());
        QVERIFY(!QFile::exists(s.env.configHome + "/hypr/omarchy-control.lua"));
    }
    void shortcutAddIsVerifiedAndRemovable() {
        Sandbox s;
        s.minimalHyprland();
        // Binds appear once the module holds one.
        s.command("hyprctl", kScript12);
        ChangeCoordinator coordinator(s.env);
        CommandRunner runner(s.env);
        ShortcutsProvider shortcuts(&runner, &coordinator);
        QSignalSpy messages(&shortcuts, &ShortcutsProvider::message);
        shortcuts.addBinding("super + y", "Mine", "run \"quoted\" thing", false);
        QTRY_VERIFY_WITH_TIMEOUT(messages.count() >= 1, 5000);
        QVERIFY2(!messages.first().at(1).toBool(), qPrintable(messages.first().at(0).toString()));
        const QByteArray lua = readAll(s.env.configHome + "/hypr/omarchy-control.lua");
        QVERIFY(lua.contains("o.bind(\"SUPER + Y\", \"Mine\", \"run \\\"quoted\\\" thing\")"));
        QCOMPARE(shortcuts.ownedBinds().size(), 1);
        shortcuts.removeBinding("SUPER + Y");
        QTRY_VERIFY_WITH_TIMEOUT(messages.count() >= 2, 5000);
        QCOMPARE(shortcuts.ownedBinds().size(), 0);
    }
    void shortcutFailureRestoresTheModule() {
        Sandbox s;
        s.minimalHyprland();
        s.command("hyprctl", "exit 0");
        const QByteArray stock = readAll(s.env.configHome + "/hypr/hyprland.lua");
        ChangeCoordinator coordinator(s.env);
        CommandRunner runner(s.env);
        ShortcutsProvider shortcuts(&runner, &coordinator);
        QSignalSpy messages(&shortcuts, &ShortcutsProvider::message);
        shortcuts.addBinding("SUPER + Y", "Mine", "mine", false);
        QTRY_VERIFY_WITH_TIMEOUT(messages.count() >= 1, 5000);
        QVERIFY(messages.first().at(1).toBool());
        QVERIFY(!QFile::exists(s.env.configHome + "/hypr/omarchy-control.lua"));
        QCOMPARE(readAll(s.env.configHome + "/hypr/hyprland.lua"), stock);
    }
    void pluginParsingAndManifestFallback() {
        Sandbox s;
        writeFile(s.env.omarchyPath + "/shell/plugins/a/manifest.json", R"({"id":"omarchy.a","name":"A","kinds":["panel"]})");
        writeFile(s.env.omarchyPath + "/shell/plugins/bar/x.manifest.json", R"({"id":"omarchy.x","name":"X","kinds":["bar-widget"]})");
        writeFile(s.env.configHome + "/omarchy/plugins/mine/manifest.json", R"({"id":"me.mine","name":"Mine"})");
        const auto rows = PluginsProvider::scanManifests(s.env);
        QCOMPARE(rows.size(), 3);
        for (const auto &row : rows)
            QVERIFY(row.toMap().value("unknownState").toBool());
        QVERIFY(PluginsProvider::isSafeId("omarchy.clock"));
        QVERIFY(!PluginsProvider::isSafeId("--force"));
        QVERIFY(!PluginsProvider::isSafeId("a b"));
    }
    void barLayoutPreservesInstanceData() {
        const auto layout = PluginsProvider::parseLayout(R"({"bar":{"layout":{"left":[{"id":"a"},{"id":"b","format":"x"}],"tray":[]}}})");
        QCOMPARE(layout.value("left").toList().size(), 2);
        QCOMPARE(layout.value("left").toList().at(1).toString(), QString("b"));
        QVERIFY(layout.value("center").toList().isEmpty());
    }
    void pluginToggleIsVerifiedThroughTheShell() {
        Sandbox s;
        s.omarchy(kScript13);
        ChangeCoordinator coordinator(s.env);
        CommandRunner runner(s.env);
        PluginsProvider plugins(&runner, &coordinator);
        QSignalSpy messages(&plugins, &PluginsProvider::message);
        plugins.setEnabled("omarchy.a", true);
        QTRY_VERIFY_WITH_TIMEOUT(messages.count() >= 1, 5000);
        QVERIFY(messages.first().at(1).toBool());
        QVERIFY(s.calls().join('\n').contains("omarchy plugin enable omarchy.a"));
    }
    void storageParsingSkipsPseudoFilesystems() {
        const auto rows = SystemProvider::parseDf("Filesystem Type 1B Used Avail Use% Mounted\n/dev/a btrfs 100 42 58 42% /\ntmpfs tmpfs 10 1 9 10% /run\n/dev/b ext4 100 95 5 95% /my disk\n");
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows[1].toMap().value("mount").toString(), QString("/my disk"));
        QCOMPARE(rows[1].toMap().value("percent").toInt(), 95);
    }
    void diagnosticsAreRedacted() {
        const QString report = SystemProvider::buildDiagnostics("wifi password=hunter2\ntoken: abc", {{"shell", true}}, {{"omarchy", "4.5"}}, {});
        QVERIFY(!report.contains("hunter2"));
        QVERIFY(!report.contains("abc"));
        QVERIFY(report.contains("4.5"));
    }
    void desktopEntriesParse() {
        Sandbox s;
        writeFile(s.env.sys("/usr/share/applications/a.desktop"), "[Desktop Entry]\nType=Application\nName=Alpha\nName[da]=Alfa\nMimeType=text/plain;\n[Desktop Action x]\nName=Other\n");
        writeFile(s.env.sys("/usr/share/applications/hidden.desktop"), "[Desktop Entry]\nType=Application\nName=Hidden\nNoDisplay=true\n");
        writeFile(s.env.sys("/usr/share/applications/link.desktop"), "[Desktop Entry]\nType=Link\nName=Link\n");
        const auto rows = SystemProvider::readApps(s.env);
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows[0].toMap().value("name").toString(), QString("Alpha"));
        QVERIFY(rows[1].toMap().value("hidden").toBool());
    }

    // ---- Displays -------------------------------------------------------------
    void aDisplaysCurrentModeIsAlwaysValid() {
        // Found on a real nested output, which lists no modes at all.
        Sandbox s;
        s.minimalHyprland();
        writeFile(s.dir.path() + "/hypr-monitors", R"([{"name":"WAYLAND-1","width":1280,"height":720,"refreshRate":60,"x":0,"y":0,"scale":1,"transform":0,"disabled":false,"availableModes":[]}])");
        ChangeCoordinator coordinator(s.env);
        CommandRunner runner(s.env);
        DisplayProvider displays(&runner, &coordinator);
        displays.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(displays.monitors().size(), 1, 5000);
        QVERIFY(displays.validateLayout(displays.monitors()).isEmpty());
    }
    void textSizeFallsBackToTheStockDefault() {
        Sandbox s;
        AppCore core(s.env);
        const ReadResult result = readSetting(core.engine, "appearance.textsize");
        QCOMPARE(result.status, QString("ok"));
        QCOMPARE(result.value, QString("12"));
    }
    void cleanScalesMatchHyprlandRules() {
        QCOMPARE(DisplayProvider::cleanScale(1.25, 2880, 1800), 1.25);
        QCOMPARE(DisplayProvider::cleanScale(1, 1920, 1080), 1.0);
        const double rounded = DisplayProvider::cleanScale(1.3, 1920, 1080);
        QVERIFY(rounded >= 1.3);
        QCOMPARE(fmod(1920 * 120 / (rounded * 120), 1.0), 0.0);
    }
    void monitorLuaRefusesNothingUnsafeReaches() {
        QVERIFY(DisplayProvider::isSafeOutputName("DP-1"));
        QVERIFY(!DisplayProvider::isSafeOutputName("DP\"); os.execute(\"x"));
        QCOMPARE(DisplayProvider::monitorLua(QJsonObject{{"output", "DP-1"}, {"mode", "1920x1080@60.00"}, {"position", "0x0"}, {"scale", 1.5}, {"transform", 0}}),
                 QString("hl.monitor({ output = \"DP-1\", mode = \"1920x1080@60.00\", position = \"0x0\", scale = 1.5 })"));
        QVERIFY(DisplayProvider::monitorLua(QJsonObject{{"output", "DP-1"}, {"disabled", true}}).contains("disabled = true"));
    }
    void monitorParsing() {
        const auto rows = DisplayProvider::parseMonitors(R"([{"name":"eDP-1","width":2880,"height":1800,"refreshRate":120.001,"x":0,"y":0,"scale":1.25,"transform":0,"disabled":false,"availableModes":["2880x1800@120.00Hz"]}])");
        QCOMPARE(rows.size(), 1);
        const auto m = rows.first().toMap();
        QCOMPARE(m.value("mode").toString(), QString("2880x1800@120.00"));
        QCOMPARE(m.value("modes").toStringList(), QStringList{"2880x1800@120.00"});
        QVERIFY(m.value("internal").toBool());
    }
    void layoutValidationKeepsOneDisplayOn() {
        Sandbox s;
        monitorFixture(s, twoMonitors());
        ChangeCoordinator coordinator(s.env);
        CommandRunner runner(s.env);
        DisplayProvider displays(&runner, &coordinator);
        displays.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(displays.monitors().size(), 3, 5000);
        auto layout = displays.monitors();
        QVERIFY(displays.validateLayout(layout).isEmpty());
        auto off = layout;
        for (auto &row : off) {
            auto m = row.toMap();
            m["enabled"] = false;
            row = m;
        }
        QVERIFY(displays.validateLayout(off).contains("At least one"));
        auto badMode = layout;
        auto first = badMode[0].toMap();
        first["mode"] = "9999x9999@1.00";
        badMode[0] = first;
        QVERIFY(displays.validateLayout(badMode).contains("does not offer"));
        auto gone = layout;
        auto ghost = gone[0].toMap();
        ghost["output"] = "HDMI-9";
        gone[0] = ghost;
        QVERIFY(displays.validateLayout(gone).contains("no longer connected"));
    }
    void editingStaysLockedWithoutARecoveryCheck() {
        Sandbox s;
        monitorFixture(s, twoMonitors());
        ChangeCoordinator coordinator(s.env);
        CommandRunner runner(s.env);
        DisplayProvider displays(&runner, &coordinator);
        displays.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(displays.monitors().size(), 3, 5000);
        QVERIFY(!displays.editingUnlocked());
        QSignalSpy messages(&displays, &DisplayProvider::message);
        displays.preview(displays.monitors());
        QCOMPARE(messages.count(), 1);
        QVERIFY(!QFile::exists(s.dir.path() + "/hypr-eval"));
        // A pass recorded for another Hyprland version does not carry over.
        writeFile(displays.recoveryCheckPath(), R"({"passed":true,"hyprland":"0.1.0"})");
        QVERIFY(!displays.editingUnlocked());
        writeFile(displays.recoveryCheckPath(), R"({"passed":true,"hyprland":"0.56.2"})");
        QTRY_VERIFY_WITH_TIMEOUT(displays.editingUnlocked(), 3000);
    }
    void watchdogRestoresOnTimeout() {
        Sandbox s;
        monitorFixture(s, twoMonitors());
        const QString plan = watchdogPlan(s, 1);
        QCOMPARE(DisplayProvider::runWatchdog(s.env, plan), 0);
        QCOMPARE(readAll(s.dir.path() + "/result").trimmed(), QByteArray("restored"));
        const QByteArray evals = readAll(s.dir.path() + "/hypr-eval");
        QVERIFY(evals.contains("output = \"eDP-1\""));
        QVERIFY(evals.contains("output = \"DP-1\""));
        // Enabled outputs come back first, so there is never a moment with none.
        QVERIFY(evals.indexOf("eDP-1") < evals.indexOf("DP-2"));
        QVERIFY(evals.contains("DP-2\", disabled = true"));
    }
    void watchdogHonorsKeep() {
        Sandbox s;
        monitorFixture(s, twoMonitors());
        const QString plan = watchdogPlan(s, 30);
        writeFile(s.dir.path() + "/keep", "");
        QCOMPARE(DisplayProvider::runWatchdog(s.env, plan), 0);
        QCOMPARE(readAll(s.dir.path() + "/result").trimmed(), QByteArray("kept"));
        QVERIFY(!QFile::exists(s.dir.path() + "/hypr-eval"));
    }
    void watchdogRestoresOnRequest() {
        Sandbox s;
        monitorFixture(s, twoMonitors());
        const QString plan = watchdogPlan(s, 30);
        writeFile(s.dir.path() + "/revert", "");
        QElapsedTimer timer;
        timer.start();
        QCOMPARE(DisplayProvider::runWatchdog(s.env, plan), 0);
        QVERIFY(timer.elapsed() < 5000);
        QCOMPARE(readAll(s.dir.path() + "/result").trimmed(), QByteArray("restored"));
    }
    void watchdogSkipsUnpluggedOutputs() {
        Sandbox s;
        // DP-1 went away during the preview.
        monitorFixture(s, R"([{"name":"eDP-1","width":1,"height":1,"refreshRate":60,"x":0,"y":0,"scale":1,"transform":0,"availableModes":[]},{"name":"DP-2","disabled":true,"width":1,"height":1,"refreshRate":60,"availableModes":[]}])");
        const QString plan = watchdogPlan(s, 0);
        QCOMPARE(DisplayProvider::runWatchdog(s.env, plan), 0);
        const QByteArray evals = readAll(s.dir.path() + "/hypr-eval");
        QVERIFY(evals.contains("eDP-1"));
        QVERIFY(!evals.contains("\"DP-1\""));
    }
    void watchdogNeverLeavesEveryOutputOff() {
        Sandbox s;
        // Every output it remembered is gone; whatever is left must come on.
        monitorFixture(s, R"([{"name":"HDMI-7","width":1,"height":1,"refreshRate":60,"availableModes":[]}])");
        const QString plan = watchdogPlan(s, 0);
        QCOMPARE(DisplayProvider::runWatchdog(s.env, plan), 0);
        const QByteArray evals = readAll(s.dir.path() + "/hypr-eval");
        QVERIFY(evals.contains("HDMI-7"));
        QVERIFY(evals.contains("preferred"));
    }
    void watchdogReportsARefusedRestore() {
        Sandbox s;
        s.command("hyprctl", kScript14);
        const QString plan = watchdogPlan(s, 0);
        QCOMPARE(DisplayProvider::runWatchdog(s.env, plan), 0);
        QCOMPARE(readAll(s.dir.path() + "/result").trimmed(), QByteArray("restore-failed"));
    }
    void watchdogSurvivesTheWindowDying() {
        // The real scenario: start the watchdog as its own process, kill the parent
        // that would have kept it, and confirm the layout still comes back.
        Sandbox s;
        monitorFixture(s, twoMonitors());
        const QString plan = watchdogPlan(s, 1);
        QProcess parent;
        parent.setProcessEnvironment(s.env.process);
        // setsid gives the watchdog its own session, as startDetached does in the app.
        parent.start("bash", {"-c", QString("setsid '%1' --watchdog '%2' '%3' >/dev/null 2>&1 & sleep 30").arg(QCoreApplication::applicationFilePath(), s.dir.path(), plan)});
        QVERIFY(parent.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(readAll(s.dir.path() + "/result").contains("armed"), 8000);
        parent.kill();
        parent.waitForFinished(2000);
        QTRY_VERIFY_WITH_TIMEOUT(readAll(s.dir.path() + "/result").contains("restored"), 8000);
        QVERIFY(readAll(s.dir.path() + "/hypr-eval").contains("eDP-1"));
    }
    void previewArmsRecoveryBeforeTheFirstChange() {
        Sandbox s;
        monitorFixture(s, twoMonitors());
        ChangeCoordinator coordinator(s.env);
        CommandRunner runner(s.env);
        // A self binary that cannot run means recovery cannot be armed.
        Env broken = s.env;
        broken.selfBinary = "/nonexistent/omarchy-control";
        CommandRunner brokenRunner(broken);
        DisplayProvider displays(&brokenRunner, &coordinator);
        displays.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(displays.monitors().size(), 3, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!displays.status().isEmpty(), 1000);
        writeFile(displays.recoveryCheckPath(), R"({"passed":true,"hyprland":"0.56.2"})");
        QTRY_VERIFY_WITH_TIMEOUT(displays.editingUnlocked(), 3000);
        QSignalSpy messages(&displays, &DisplayProvider::message);
        displays.preview(displays.monitors());
        QTRY_VERIFY_WITH_TIMEOUT(messages.count() >= 1, 8000);
        QVERIFY(messages.first().at(0).toString().contains("nothing was changed"));
        QVERIFY(!QFile::exists(s.dir.path() + "/hypr-eval"));
    }

    // ---- Regressions from the adversarial review ------------------------------
    void unreadableFilesStopTheChangeAndSurvive() {
        if (geteuid() == 0)
            QSKIP("root can read everything");
        Sandbox s;
        const QString file = s.env.configHome + "/locked.json";
        writeFile(file, "precious");
        QFile::setPermissions(file, QFile::Permissions());
        QCOMPARE(FileOps::fingerprint(file), QString("unreadable"));
        ChangeCoordinator coordinator(s.env);
        bool applied = false;
        ChangeCoordinator::Request request;
        request.files << file;
        request.apply = [&](ChangeCoordinator::Done done) { applied = true; done(false, "x"); };
        Transaction result;
        coordinator.submit(request, [&](const Transaction &tx) { result = tx; });
        QTRY_COMPARE(result.phase, QString("failed"));
        QVERIFY(!applied);
        QVERIFY(QFile::exists(file));
        QFile::setPermissions(file, QFile::ReadOwner | QFile::WriteOwner);
        QCOMPARE(readAll(file), QByteArray("precious"));
    }
    void noBackupMeansNoChange() {
        if (geteuid() == 0)
            QSKIP("root ignores directory permissions");
        Sandbox s;
        const QString file = s.env.configHome + "/c.json";
        writeFile(file, "original");
        ChangeCoordinator coordinator(s.env);
        QFile::setPermissions(coordinator.backupDirectory(), QFile::ReadOwner | QFile::ExeOwner);
        bool applied = false;
        ChangeCoordinator::Request request;
        request.files << file;
        request.apply = [&](ChangeCoordinator::Done done) { applied = true; done(true, {}); };
        Transaction result;
        coordinator.submit(request, [&](const Transaction &tx) { result = tx; });
        QTRY_COMPARE(result.phase, QString("failed"));
        QFile::setPermissions(coordinator.backupDirectory(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QVERIFY(!applied);
        QVERIFY(result.message.contains("backup"));
    }
    void failedVerifyKeepsAnEditMadeMeanwhile() {
        Sandbox s;
        const QString file = s.env.configHome + "/c.json";
        writeFile(file, "original");
        ChangeCoordinator coordinator(s.env);
        ChangeCoordinator::Request request;
        request.files << file;
        request.apply = [file](ChangeCoordinator::Done done) { done(FileOps::atomicWrite(file, "ours"), {}); };
        request.verify = [file](ChangeCoordinator::Done done) {
            FileOps::atomicWrite(file, "someone else's edit");
            done(false, "did not take");
        };
        Transaction result;
        coordinator.submit(request, [&](const Transaction &tx) { result = tx; });
        QTRY_COMPARE(result.phase, QString("partial"));
        QCOMPARE(readAll(file), QByteArray("someone else's edit"));
        QVERIFY(result.message.contains("edited"));
    }
    void appendingAfterATruncatedLineKeepsTheRecord() {
        Sandbox s;
        ChangeCoordinator coordinator(s.env);
        Transaction tx;
        tx.id = "stuck";
        tx.label = "X";
        tx.phase = "applying";
        writeFile(coordinator.journalPath(), QJsonDocument(tx.toJson()).toJson(QJsonDocument::Compact) + "\n{\"id\":\"bad\",\"pha");
        QCOMPARE(coordinator.recoverInterrupted().size(), 1);
        QCOMPARE(coordinator.transaction("stuck").phase, QString("interrupted"));
        // Resolved means resolved: a later launch must not recover it a second time.
        QVERIFY(coordinator.recoverInterrupted().isEmpty());
    }
    void pruningKeepsAnUnfinishedChangesBackup() {
        Sandbox s;
        ChangeCoordinator coordinator(s.env);
        QByteArray journal;
        Transaction old;
        old.id = "stuck";
        old.phase = "applying";
        journal += QJsonDocument(old.toJson()).toJson(QJsonDocument::Compact) + "\n";
        for (int i = 0; i < 40; ++i) {
            Transaction tx;
            tx.id = QString("done%1").arg(i);
            tx.phase = "committed";
            journal += QJsonDocument(tx.toJson()).toJson(QJsonDocument::Compact) + "\n";
        }
        writeFile(coordinator.journalPath(), journal);
        writeFile(coordinator.backupDirectory() + "/stuck/0", "x");
        writeFile(coordinator.backupDirectory() + "/done0/0", "x");
        ChangeCoordinator::Request request;
        request.apply = [](ChangeCoordinator::Done done) { done(true, {}); };
        request.verify = [](ChangeCoordinator::Done done) { done(true, {}); };
        bool finished = false;
        coordinator.submit(request, [&](const Transaction &) { finished = true; });
        QTRY_VERIFY(finished);
        QVERIFY(QFile::exists(coordinator.backupDirectory() + "/stuck/0"));
        QVERIFY(!QFile::exists(coordinator.backupDirectory() + "/done0/0"));
    }
    void lockRefusalDoesNotPrune() {
        Sandbox s;
        ChangeCoordinator coordinator(s.env);
        writeFile(coordinator.backupDirectory() + "/in-progress-elsewhere/0", "x");
        QLockFile other(s.env.appStateDir + "/change.lock");
        QVERIFY(other.tryLock(0));
        ChangeCoordinator::Request request;
        request.apply = [](ChangeCoordinator::Done done) { done(true, {}); };
        Transaction result;
        coordinator.submit(request, [&](const Transaction &tx) { result = tx; });
        QTRY_COMPARE_WITH_TIMEOUT(result.phase, QString("failed"), 5000);
        QVERIFY(QFile::exists(coordinator.backupDirectory() + "/in-progress-elsewhere/0"));
    }
    void wifiBandReadsTheSelectedField() {
        Sandbox s;
        writeFile(s.env.sys("/sys/class/net/wlan0/wireless/x"), "");
        s.omarchy("printf 'band\\t5\\navailable\\t5\\nselected\\tauto\\n'");
        AppCore core(s.env);
        const ReadResult result = readSetting(core.engine, "network.band");
        QCOMPARE(result.status, QString("ok"));
        QCOMPARE(result.value, QString("auto"));
    }
    void toggleRollbackLooksBeforeItFlips() {
        // The command fails before toggling. Rollback must not then toggle it, which
        // would apply the very change that was just reported as failed.
        Sandbox s;
        s.omarchy("exit 1");
        AppCore core(s.env);
        const Transaction tx = applySetting(core.engine, "power.screensaver", "false");
        QVERIFY(tx.phase != QLatin1String("committed"));
        QCOMPARE(s.calls().filter("omarchy toggle screensaver").size(), 1);
        QVERIFY(!QFile::exists(s.env.stateHome + "/omarchy/toggles/screensaver-off"));
    }
    void rollbackThatDoesNotWorkIsNotCalledRestored() {
        // Setter claims success both ways and changes nothing back.
        Sandbox s;
        s.omarchy("f=\"$HOME/val\"; case \"$*\" in 'hyprland corners') cat \"$f\" 2>/dev/null || echo theme;; 'hyprland corners sharp') echo rounded > \"$f\";; esac");
        AppCore core(s.env);
        const Transaction tx = applySetting(core.engine, "appearance.corners", "sharp");
        QCOMPARE(tx.phase, QString("partial"));
        QVERIFY(!tx.message.contains("was restored"));
    }
    void uninstalledAppsAreRefusedEverywhere() {
        // Includes the CLI and undo, which never see the filtered list.
        Sandbox s;
        s.omarchy("exit 0");
        AppCore core(s.env);
        const ChangePlan plan = core.engine.plan("apps.default.browser", "firefox");
        QVERIFY(!plan.ok);
        QVERIFY(plan.error.contains("not installed"));
        QVERIFY(s.calls().isEmpty());
    }
    void undoOfAResetRespectsLaterEdits() {
        Sandbox s;
        s.omarchy("exit 0");
        const QString config = s.env.configHome + "/omarchy/shell.json";
        writeFile(config, "{\"idle\":{\"lock\":600}}");
        writeFile(s.env.omarchyPath + "/config/omarchy/shell.json", "{\"idle\":{\"lock\":300}}");
        AppCore core(s.env);
        Transaction reset;
        bool done = false;
        core.engine.reset("power.idle.lock", [&](const Transaction &t) { reset = t; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QCOMPARE(reset.phase, QString("committed"));
        QCOMPARE(reset.after, QString("300"));
        writeFile(config, "{\"idle\":{\"lock\":900}}");
        const Transaction undone = undoTx(core.engine, reset.id);
        QCOMPARE(undone.phase, QString("failed"));
        QVERIFY(readAll(config).contains("900"));
    }
    void aResetThatCannotBeConfirmedIsNotSuccess() {
        // The override comes out, but Hyprland then cannot say what is in effect.
        Sandbox s;
        s.minimalHyprland();
        HyprModule module(s.env);
        module.setOption({"input", "repeat_rate"}, 31);
        QVERIFY(module.save());
        QVERIFY(module.installImport());
        const QByteArray before = readAll(module.path());
        s.command("hyprctl", kReloadsButCannotAnswer);
        AppCore core(s.env);
        Transaction reset;
        bool done = false;
        core.engine.reset("input.repeat_rate", [&](const Transaction &t) { reset = t; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 8000);
        QCOMPARE(reset.phase, QString("rolled-back"));
        QCOMPARE(readAll(module.path()), before);
    }
    void weatherDoesNotPromiseAnUndoItCannotKeep() {
        Sandbox s;
        s.omarchy("exit 0");
        const QString file = s.env.stateHome + "/omarchy/settings/weather.json";
        writeFile(file, "{\"name\":\"Malibu\",\"latitude\":34,\"longitude\":-118}");
        s.omarchy("printf '{\"name\":\"%s\"}' \"$4\" > \"$HOME/.local/state/omarchy/settings/weather.json\"");
        AppCore core(s.env);
        const Transaction tx = applySetting(core.engine, "system.weather", "Oslo");
        QCOMPARE(tx.phase, QString("committed"));
        QVERIFY(!tx.undoable());
    }
    void anUnrestorableValueIsNotOfferedForUndo() {
        Sandbox s;
        s.omarchy("f=\"$HOME/dns\"; case \"$*\" in dns) cat \"$f\" 2>/dev/null || echo Custom;; 'dns '*) echo \"$2\" > \"$f\";; esac");
        AppCore core(s.env);
        const Transaction tx = applySetting(core.engine, "network.dns", "Cloudflare");
        QCOMPARE(tx.phase, QString("committed"));
        QCOMPARE(tx.before, QString("Custom"));
        QVERIFY(!tx.undoable());
    }
    void scaleRecordsWhatHyprlandSettledOn() {
        Sandbox s;
        s.command("hyprctl", "echo '{}'");
        s.omarchy("f=\"$HOME/scale\"; case \"$*\" in 'hyprland monitor scaling') cat \"$f\" 2>/dev/null || echo 1;; 'hyprland monitor scaling 1') echo 1 > \"$f\";; 'hyprland monitor scaling '*) echo 1.33333 > \"$f\";; esac");
        AppCore core(s.env);
        const Transaction tx = applySetting(core.engine, "displays.scale", "1.25");
        QCOMPARE(tx.phase, QString("committed"));
        QCOMPARE(tx.after, QString("1.33333"));
        QVERIFY(tx.undoable());
        QCOMPARE(undoTx(core.engine, tx.id).phase, QString("committed"));
    }
    void aScaleThatDoesNotChangeIsNotSuccess() {
        Sandbox s;
        s.command("hyprctl", "echo '{}'");
        s.omarchy("case \"$*\" in 'hyprland monitor scaling') echo 1;; esac");
        AppCore core(s.env);
        QVERIFY(applySetting(core.engine, "displays.scale", "2").phase != QLatin1String("committed"));
    }
    void aSavedLayoutBlocksTheScaleSetting() {
        Sandbox s;
        s.minimalHyprland();
        HyprModule module(s.env);
        module.setMonitors(QJsonArray{QJsonObject{{"output", "DP-1"}, {"mode", "1x1@60.00"}, {"position", "0x0"}, {"scale", 1}}});
        QVERIFY(module.save());
        AppCore core(s.env);
        const ChangePlan plan = core.engine.plan("displays.scale", "2");
        QVERIFY(!plan.ok);
        QVERIFY(plan.error.contains("Arrangement"));
    }
    void resettingAnUnsetOptionLeavesTheConfigAlone() {
        Sandbox s;
        s.minimalHyprland();
        const QByteArray stock = readAll(s.env.configHome + "/hypr/hyprland.lua");
        AppCore core(s.env);
        core.capabilities.scanServicesBlocking();
        Transaction reset;
        bool done = false;
        core.engine.reset("input.repeat_rate", [&](const Transaction &t) { reset = t; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QCOMPARE(reset.phase, QString("failed"));
        QCOMPARE(readAll(s.env.configHome + "/hypr/hyprland.lua"), stock);
        QVERIFY(!QFile::exists(s.env.configHome + "/hypr/omarchy-control.lua"));
    }
    void typedTextIsNotExpandedAsAPath() {
        Sandbox s;
        s.omarchy("echo \"$*\" >> \"$HOME/args\"");
        AppCore core(s.env);
        applySetting(core.engine, "system.weather", "~/{config}");
        QVERIFY(readAll(s.env.home + "/args").contains("~/{config}"));
    }
    void jsonSetKeepsACommentOnTheBraceLine() {
        QByteArray doc = "{ /* keep */ }";
        QVERIFY(FileOps::jsonSet(&doc, {"a"}, 1));
        QVERIFY(doc.contains("/* keep */"));
        QCOMPARE(FileOps::jsonGet(doc, {"a"}).toInt(), 1);
    }
    void trailingCommaRemovalLeavesStringsAlone() {
        const QByteArray doc = "{\"a\": \"x,}\", \"b\": [1, 2, ], }";
        QCOMPARE(FileOps::jsonGet(doc, {"a"}).toString(), QString("x,}"));
        QCOMPARE(FileOps::jsonGet(doc, {"b"}).toArray().size(), 2);
    }
    void barEntriesCanBeBareIds() {
        const auto layout = PluginsProvider::parseLayout(R"({"bar":{"layout":{"left":["omarchy.menu",{"id":"omarchy.clock"}]}}})");
        QCOMPARE(layout.value("left").toList().at(0).toString(), QString("omarchy.menu"));
        QCOMPARE(layout.value("left").toList().at(1).toString(), QString("omarchy.clock"));
    }
    void dockPinsFallBackToTheDefaults() {
        Sandbox s;
        s.omarchy("exit 1");
        writeFile(s.env.configHome + "/omarchy/shell.json", "{\"version\":1}");
        writeFile(s.env.omarchyPath + "/config/omarchy/shell.json", "{\"dock\":{\"pinned\":[\"firefox\"]}}");
        ChangeCoordinator coordinator(s.env);
        CommandRunner runner(s.env);
        PluginsProvider plugins(&runner, &coordinator);
        plugins.refresh();
        QCOMPARE(plugins.dockPins(), QStringList{"firefox"});
    }
    void aWidgetMoveToleratesASlowWrite() {
        Sandbox s;
        const QString config = s.env.configHome + "/omarchy/shell.json";
        writeFile(config, "{\"bar\":{\"layout\":{\"left\":[{\"id\":\"w\"}],\"right\":[]}}}");
        s.omarchy("case \"$1\" in bar) (sleep 0.4; printf '{\"bar\":{\"layout\":{\"left\":[],\"right\":[{\"id\":\"w\"}]}}}' > \"$HOME/.config/omarchy/shell.json\") & ;; plugin) exit 1;; esac");
        ChangeCoordinator coordinator(s.env);
        CommandRunner runner(s.env);
        PluginsProvider plugins(&runner, &coordinator);
        QSignalSpy messages(&plugins, &PluginsProvider::message);
        plugins.moveWidget("w", "right", 0, "left", 0);
        QTRY_VERIFY_WITH_TIMEOUT(messages.count() >= 1, 8000);
        QVERIFY2(!messages.first().at(1).toBool(), qPrintable(messages.first().at(0).toString()));
    }
    void autostartCanComeBackOn() {
        const QByteArray entry = ServicesProvider::withHidden("[Desktop Entry]\nName=A\nX-GNOME-Autostart-enabled=false\nHidden=true\n", false);
        QVERIFY(entry.contains("Hidden=false"));
        QVERIFY(entry.contains("X-GNOME-Autostart-enabled=true"));
        QVERIFY(ServicesProvider::withHidden("[Desktop Entry]\nName=A\n", true).contains("Hidden=true"));
    }
    void aOneShotServiceThatRanIsAStart() {
        Sandbox s;
        s.command("systemctl", "case \"$*\" in *show*) printf 'ActiveState=inactive\\nResult=success\\n';; *list-*) echo '[]';; esac");
        ChangeCoordinator coordinator(s.env);
        CommandRunner runner(s.env);
        ServicesProvider services(&runner, &coordinator);
        QSignalSpy messages(&services, &ServicesProvider::message);
        services.serviceAction("foo.service", "start", false);
        QTRY_VERIFY_WITH_TIMEOUT(messages.count() >= 1, 5000);
        QVERIFY(!messages.first().at(1).toBool());
    }
    void aFailedStatusQueryIsNotProofOfStopped() {
        Sandbox s;
        s.command("systemctl", "case \"$*\" in *show*) exit 1;; *list-*) echo '[]';; esac");
        ChangeCoordinator coordinator(s.env);
        CommandRunner runner(s.env);
        ServicesProvider services(&runner, &coordinator);
        QSignalSpy messages(&services, &ServicesProvider::message);
        services.serviceAction("foo.service", "stop", false);
        QTRY_VERIFY_WITH_TIMEOUT(messages.count() >= 1, 5000);
        QVERIFY(messages.first().at(1).toBool());
    }
    void networkRefusesAnOverlappingSwitch() {
        Sandbox s;
        s.command("busctl", "case \"$*\" in *CheckpointCreate*) echo 'o \"/org/freedesktop/NetworkManager/Checkpoint/7\"';; esac");
        s.command("nmcli", "exit 0");
        ChangeCoordinator coordinator(s.env);
        CommandRunner runner(s.env);
        NetworkProvider network(&runner, &coordinator);
        network.activate("11111111-1111-1111-1111-111111111111", "Cafe");
        QTRY_VERIFY_WITH_TIMEOUT(network.awaitingKeep(), 5000);
        QSignalSpy messages(&network, &NetworkProvider::message);
        network.activate("22222222-2222-2222-2222-222222222222", "Other");
        QCOMPARE(messages.count(), 1);
        QVERIFY(messages.first().at(1).toBool());
        QCOMPARE(s.calls().filter("CheckpointCreate").size(), 1);
    }
    void keepCountdownMatchesTheRealRollbackTimer() {
        // The timer starts when the checkpoint does, not when the connection comes up.
        Sandbox s;
        s.command("busctl", "case \"$*\" in *CheckpointCreate*) echo 'o \"/org/freedesktop/NetworkManager/Checkpoint/7\"';; esac");
        s.command("nmcli", "case \"$*\" in *'connection up'*) sleep 2;; esac");
        ChangeCoordinator coordinator(s.env);
        CommandRunner runner(s.env);
        NetworkProvider network(&runner, &coordinator);
        network.activate("11111111-1111-1111-1111-111111111111", "Cafe");
        QTRY_VERIFY_WITH_TIMEOUT(network.awaitingKeep(), 8000);
        QVERIFY2(network.keepSeconds() <= 58, qPrintable(QString::number(network.keepSeconds())));
    }
    void aFailedKeepIsNotReportedAsKept() {
        Sandbox s;
        s.command("busctl", "case \"$*\" in *CheckpointCreate*) echo 'o \"/org/freedesktop/NetworkManager/Checkpoint/7\"';; *CheckpointDestroy*) exit 1;; esac");
        s.command("nmcli", "exit 0");
        ChangeCoordinator coordinator(s.env);
        CommandRunner runner(s.env);
        NetworkProvider network(&runner, &coordinator);
        network.activate("11111111-1111-1111-1111-111111111111", "Cafe");
        QTRY_VERIFY_WITH_TIMEOUT(network.awaitingKeep(), 5000);
        QSignalSpy messages(&network, &NetworkProvider::message);
        network.keepConnection();
        QTRY_VERIFY_WITH_TIMEOUT(messages.count() >= 1, 5000);
        QVERIFY(messages.first().at(1).toBool());
    }
    void mirroredDisplaysComeBackMirrored() {
        const auto rows = DisplayProvider::parseMonitors(R"([{"name":"DP-1","width":1,"height":1,"refreshRate":60,"mirrorOf":"eDP-1","availableModes":[]}])");
        QCOMPARE(rows.first().toMap().value("mirrorOf").toString(), QString("eDP-1"));
        QVERIFY(DisplayProvider::monitorLua(QJsonObject{{"output", "DP-1"}, {"mode", "m"}, {"position", "0x0"}, {"scale", 1}, {"mirror", "eDP-1"}}).contains("mirror = \"eDP-1\""));
    }
    void recoveryCheckEndToEnd_data() {
        QTest::addColumn<bool>("compositorObeys");
        QTest::addColumn<bool>("expectPass");
        QTest::newRow("working compositor") << true << true;
        QTest::newRow("compositor ignores changes") << false << false;
    }
    void recoveryCheckEndToEnd() {
        // The whole flow with the real detached watchdog process and a stateful fake
        // compositor. The check passes only if a change really took and really came back.
        QFETCH(bool, compositorObeys);
        QFETCH(bool, expectPass);
        Sandbox s;
        s.minimalHyprland();
        writeFile(s.dir.path() + "/obeys", compositorObeys ? "1" : "0");
        writeFile(s.dir.path() + "/x", "0");
        s.command("hyprctl", kStatefulCompositor);
        Env env = s.env;
        env.selfBinary = QCoreApplication::applicationFilePath();
        env.process.insert("OC_TEST_ROOT", s.dir.path());
        CommandRunner runner(env);
        ChangeCoordinator coordinator(env);
        DisplayProvider displays(&runner, &coordinator);
        QSignalSpy finished(&displays, &DisplayProvider::checkFinished);
        displays.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(displays.monitors().size() == 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!displays.status().isEmpty(), 1000);
        displays.runRecoveryCheck();
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 20000);
        QCOMPARE(finished.first().at(0).toBool(), expectPass);
        QCOMPARE(displays.checkState(), QString(expectPass ? "passed" : "failed"));
        // Whatever the verdict, the display ended up where it started.
        QCOMPARE(readAll(s.dir.path() + "/x").trimmed(), QByteArray("0"));
        QTRY_COMPARE_WITH_TIMEOUT(displays.editingUnlocked(), expectPass, 3000);
    }
    void recoveryCheckFailsWhenTheWatchdogCannotStart() {
        Sandbox s;
        s.minimalHyprland();
        writeFile(s.dir.path() + "/hypr-monitors", twoMonitors());
        Env env = s.env;
        env.selfBinary = "/nonexistent/omarchy-control";
        CommandRunner runner(env);
        ChangeCoordinator coordinator(env);
        DisplayProvider displays(&runner, &coordinator);
        QSignalSpy finished(&displays, &DisplayProvider::checkFinished);
        displays.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(displays.monitors().size() == 3, 5000);
        displays.runRecoveryCheck();
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 8000);
        QVERIFY(!finished.first().at(0).toBool());
        QCOMPARE(displays.checkState(), QString("failed"));
        QVERIFY(!QFile::exists(s.dir.path() + "/hypr-eval"));
    }
    void unsafeOutputNamesNeverReachLua() {
        Sandbox s;
        s.minimalHyprland();
        writeFile(s.dir.path() + "/hypr-monitors", kHostileMonitor);
        CommandRunner runner(s.env);
        ChangeCoordinator coordinator(s.env);
        DisplayProvider displays(&runner, &coordinator);
        QSignalSpy finished(&displays, &DisplayProvider::checkFinished);
        displays.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(displays.monitors().size() == 1, 5000);
        displays.runRecoveryCheck();
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 5000);
        QVERIFY(!finished.first().at(0).toBool());
        QVERIFY(!QFile::exists(s.dir.path() + "/hypr-eval"));
    }
    void watchdogLastResortRefusesUnsafeNames() {
        Sandbox s;
        s.minimalHyprland();
        writeFile(s.dir.path() + "/hypr-monitors", kHostileMonitorNoModes);
        const QString plan = watchdogPlan(s, 0);
        QCOMPARE(DisplayProvider::runWatchdog(s.env, plan), 0);
        QVERIFY(!readAll(s.dir.path() + "/hypr-eval").contains("os.execute"));
        QCOMPARE(readAll(s.dir.path() + "/result").trimmed(), QByteArray("restore-failed"));
    }

    // ---- Compatibility, package lock, rules, and accessibility ----------------
    void aChangedCommandPausesOnlyItsSettings() {
        Sandbox s;
        s.env.enforceCompat = true;
        s.command("omarchy-hyprland-corners", "echo new version");
        s.minimalHyprland();
        s.omarchy("exit 0");
        AppCore core(s.env);
        // The fake's hash cannot match the reviewed one, which is exactly a changed script.
        const ChangePlan paused = core.engine.plan("appearance.corners", "sharp");
        QVERIFY(!paused.ok);
        QVERIFY(paused.error.contains("omarchy-hyprland-corners"));
        // A setting whose command is not on this machine is not reported as changed.
        QVERIFY(core.engine.plan("power.stayawake", "true").ok);
        QVERIFY(core.compat.changedCommands({}).contains("omarchy-hyprland-corners"));
        // Reading never pauses.
        QVERIFY(core.compat.changedFor(*core.catalog.find("system.version"), {}).isEmpty());
        core.compat.acceptAll({});
        QVERIFY(core.engine.plan("appearance.corners", "sharp").ok);
        QVERIFY(core.compat.changedCommands({}).isEmpty());
        // Accepting one version does not accept the next.
        s.command("omarchy-hyprland-corners", "echo another version");
        AppCore later(s.env);
        QVERIFY(!later.engine.plan("appearance.corners", "sharp").ok);
    }
    void aNewHyprlandPausesOverridesUntilAccepted() {
        Sandbox s;
        s.env.enforceCompat = true;
        s.minimalHyprland();
        Compat compat(s.env);
        Catalog catalog;
        const auto *entry = catalog.find("input.repeat_rate");
        QVERIFY(compat.changedFor(*entry, "0.56.2").isEmpty());
        QVERIFY(!compat.changedFor(*entry, "0.57.0").isEmpty());
        compat.acceptAll("0.57.0");
        QVERIFY(compat.changedFor(*entry, "0.57.0").isEmpty());
        QVERIFY(!compat.changedFor(*entry, "0.58.0").isEmpty());
    }
    void everyPinnedCommandMatchesTheInventoriedOnes() {
        QFile file(":/data/compat.json");
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto root = QJsonDocument::fromJson(file.readAll()).object();
        Catalog catalog;
        QVERIFY(root.value("settings").toObject().size() > 25);
        for (const auto &key : root.value("settings").toObject().keys())
            QVERIFY2(catalog.find(key), qPrintable(key));
    }
    void packageWorkWaitsForPacman() {
        Sandbox s;
        s.command("omarchy-launch-floating-terminal-with-presentation", "touch \"$HOME/launched\"");
        writeFile(s.env.sys("/var/lib/pacman/db.lck"), "");
        AppCore core(s.env);
        HelperLauncher launcher(&core.runner, &core.capabilities);
        HelperLauncher::Outcome outcome;
        bool done = false;
        launcher.launch(*core.catalog.find("update.omarchy"), [&](const HelperLauncher::Outcome &o) { outcome = o; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(!outcome.opened);
        QVERIFY(outcome.message.contains("package operation"));
        // Anything that does not touch packages is unaffected.
        done = false;
        launcher.launch(*core.catalog.find("snapshot.create"), [&](const HelperLauncher::Outcome &o) { outcome = o; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(!outcome.message.contains("package operation"));
    }
    void aMissingKernelModuleTreeMeansRestart() {
        Sandbox s;
        QDir().mkpath(s.env.sys("/usr/lib/modules/some-other-kernel"));
        AppCore core(s.env);
        QVERIFY(core.capabilities.has("reboot.needed"));
        QDir().mkpath(s.env.sys("/usr/lib/modules/") + QSysInfo::kernelVersion());
        writeFile(s.env.sys("/usr/lib/modules/") + QSysInfo::kernelVersion() + "/modules.builtin", "");
        core.capabilities.scanFiles();
        QVERIFY(!core.capabilities.has("reboot.needed"));
    }
    void aRestoreKeepsWhatItReplaced() {
        Sandbox s;
        const QString file = s.env.configHome + "/c.json";
        writeFile(file, "original");
        ChangeCoordinator coordinator(s.env);
        ChangeCoordinator::Request request;
        request.files << file;
        request.apply = [file](ChangeCoordinator::Done done) {
            FileOps::atomicWrite(file, "possibly someone's edit");
            done(false, "broke");
        };
        Transaction result;
        coordinator.submit(request, [&](const Transaction &tx) { result = tx; });
        QTRY_COMPARE(result.phase, QString("rolled-back"));
        QCOMPARE(readAll(file), QByteArray("original"));
        QCOMPARE(readAll(coordinator.backupDirectory() + "/" + result.id + "/replaced-c.json"), QByteArray("possibly someone's edit"));
    }
    void vrrSurvivesARule() {
        QVERIFY(DisplayProvider::monitorLua(QJsonObject{{"output", "DP-1"}, {"mode", "m"}, {"position", "0x0"}, {"scale", 1}, {"vrr", true}}).contains("vrr = 1"));
        Sandbox s;
        HyprModule module(s.env);
        module.setMonitors(QJsonArray{QJsonObject{{"output", "DP-1"}, {"mode", "m"}, {"position", "0x0"}, {"scale", 1}, {"vrr", true}}});
        QVERIFY(module.render().contains("vrr = 1"));
    }
    void nestedJsonFieldsParse() {
        CommandResult result;
        result.exitCode = 0;
        result.out = R"({"config":{"language":"en","autoPasteEnabled":false}})";
        bool ok = false;
        QCOMPARE(SettingsEngine::parseOutput(result, QJsonObject{{"parse", "json:config.language"}}, &ok), QString("en"));
        QVERIFY(ok);
        QCOMPARE(SettingsEngine::parseOutput(result, QJsonObject{{"parse", "json:config.autoPasteEnabled"}}, &ok), QString("false"));
        SettingsEngine::parseOutput(result, QJsonObject{{"parse", "json:config.nope"}}, &ok);
        QVERIFY(!ok);
    }
    void superwhisperSettingsSendJson() {
        Sandbox s;
        s.command("superwhisper", "f=\"$HOME/tone\"; case \"$*\" in 'general show') printf '{\"config\":{\"tone\":%s}}' \"$(cat \"$f\" 2>/dev/null || echo '\"casual\"')\";; 'general set tone '*) printf '%s' \"$4\" > \"$f\";; esac");
        AppCore core(s.env);
        QCOMPARE(readSetting(core.engine, "input.dictation.tone").value, QString("casual"));
        QCOMPARE(applySetting(core.engine, "input.dictation.tone", "formal").phase, QString("committed"));
        QVERIFY(s.calls().join('\n').contains("general set tone \"formal\""));
        QVERIFY(!core.engine.plan("input.dictation.language", "x\"y").ok);
    }
    void windowRulesRenderSafely() {
        Sandbox s;
        HyprModule module(s.env);
        module.setRules(QJsonArray{
            QJsonObject{{"match", "firefox"}, {"action", "float"}},
            QJsonObject{{"match", "x\")\nos.execute(\"y"}, {"action", "workspace"}, {"value", "3"}},
            QJsonObject{{"match", "term"}, {"action", "opacity"}, {"value", "0.9 0.85"}},
        });
        const QString lua = QString::fromUtf8(module.render());
        QVERIFY(lua.contains("o.window(\"firefox\", { float = true })"));
        QVERIFY(lua.contains("workspace = \"3\""));
        QVERIFY(lua.contains("opacity = \"0.9 0.85\""));
        QVERIFY(!lua.contains("\nos.execute"));
        QVERIFY(module.save());
        QCOMPARE(module.state(), HyprModule::Ours);
    }
    void windowRuleValidation_data() {
        QTest::addColumn<QString>("match");
        QTest::addColumn<QString>("action");
        QTest::addColumn<QString>("value");
        QTest::addColumn<bool>("ok");
        QTest::newRow("float") << "firefox" << "float" << "" << true;
        QTest::newRow("pattern") << "^(foo|bar)$" << "tile" << "" << true;
        QTest::newRow("bad pattern") << "(unclosed" << "tile" << "" << false;
        QTest::newRow("empty") << "  " << "float" << "" << false;
        QTest::newRow("workspace") << "x" << "workspace" << "3" << true;
        QTest::newRow("workspace junk") << "x" << "workspace" << "3\"); boom" << false;
        QTest::newRow("opacity pair") << "x" << "opacity" << "0.9 0.85" << true;
        QTest::newRow("opacity high") << "x" << "opacity" << "7" << false;
        QTest::newRow("unknown action") << "x" << "exec" << "rm" << false;
    }
    void windowRuleValidation() {
        QFETCH(QString, match);
        QFETCH(QString, action);
        QFETCH(QString, value);
        QFETCH(bool, ok);
        Sandbox s;
        CommandRunner runner(s.env);
        ChangeCoordinator coordinator(s.env);
        WindowRulesProvider provider(&runner, &coordinator);
        QCOMPARE(provider.validate(match, action, value).isEmpty(), ok);
    }
    void windowRuleAddsAndRemoves() {
        Sandbox s;
        s.minimalHyprland();
        CommandRunner runner(s.env);
        ChangeCoordinator coordinator(s.env);
        WindowRulesProvider provider(&runner, &coordinator);
        QSignalSpy messages(&provider, &WindowRulesProvider::message);
        provider.addRule("firefox", "float", "");
        QTRY_VERIFY_WITH_TIMEOUT(messages.count() == 1, 5000);
        QVERIFY2(!messages.first().at(1).toBool(), qPrintable(messages.first().at(0).toString()));
        QCOMPARE(provider.rules().size(), 1);
        provider.removeRule(0);
        QTRY_VERIFY_WITH_TIMEOUT(messages.count() == 2, 5000);
        QCOMPARE(provider.rules().size(), 0);
    }
    void redoReappliesAnUndo() {
        Sandbox s;
        s.omarchy("exit 0");
        const QString config = s.env.configHome + "/omarchy/shell.json";
        writeFile(config, "{\"idle\":{\"lock\":300}}");
        AppCore core(s.env);
        const Transaction change = applySetting(core.engine, "power.idle.lock", "600");
        const Transaction undone = undoTx(core.engine, change.id);
        QCOMPARE(undone.kind, QString("undo"));
        QVERIFY(undone.undoable());
        QVERIFY(readAll(config).contains("300"));
        QCOMPARE(undoTx(core.engine, undone.id).phase, QString("committed"));
        QVERIFY(readAll(config).contains("600"));
    }
    void everyFocusableControlIsNamedAndReachable() {
        // Screen-reader evidence that needs no screen reader: Tab through each page and
        // ask Qt's accessibility layer what it would announce.
        QAccessible::setActive(true);
        Sandbox s;
        fakeDesktop(s);
        QQuickStyle::setStyle("Material");
        Translator translator(s.env.configHome + "/omarchy/language", ":/i18n", s.env.omarchyPath + "/default/i18n/locales.json");
        Theme theme(s.env);
        Backend backend(s.env);
        QQmlApplicationEngine engine;
        engine.addImageProvider("icons", new IconProvider);
        engine.rootContext()->setContextProperty("translator", &translator);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("initialPage", QString());
        engine.rootContext()->setContextProperty("initialSetting", QString());
        engine.load(QUrl("qrc:/Main.qml"));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window, 3000));
        QStringList unnamed;
        for (const auto &value : backend.pages()) {
            const QString page = value.toMap().value("id").toString();
            QMetaObject::invokeMethod(window, "go", Q_ARG(QVariant, page), Q_ARG(QVariant, QString()));
            QTest::qWait(200);
            QSet<QQuickItem *> visited;
            for (int i = 0; i < 80; ++i) {
                QTest::keyClick(window, Qt::Key_Tab);
                QQuickItem *item = window->activeFocusItem();
                if (!item || visited.contains(item))
                    break;
                visited << item;
                QAccessibleInterface *accessible = QAccessible::queryAccessibleInterface(item);
                const QString name = accessible ? accessible->text(QAccessible::Name) : QString();
                const QString description = accessible ? accessible->text(QAccessible::Description) : QString();
                if (name.trimmed().isEmpty() && description.trimmed().isEmpty())
                    unnamed << page + ": " + QString::fromLatin1(item->metaObject()->className());
            }
            QVERIFY2(visited.size() >= 2, qPrintable(page));
        }
        QVERIFY2(unnamed.isEmpty(), qPrintable(unnamed.join('\n')));
    }

    // ---- Command line ---------------------------------------------------------
    void cliRefusesConsequentialChangesWithoutYes() {
        Sandbox s;
        s.omarchy("case \"$*\" in dns) echo DHCP;; 'dns '*) touch \"$HOME/changed\";; esac");
        QCOMPARE(Cli::run(s.env, {"apply", "network.dns", "Cloudflare"}), 1);
        QVERIFY(!QFile::exists(s.env.home + "/changed"));
        QVERIFY(s.calls().filter("dns Cloudflare").isEmpty());
    }
    void cliExitCodesAreMeaningful() {
        Sandbox s;
        s.minimalHyprland();
        s.omarchy("case \"$*\" in version) echo 4.5.0-3;; 'hyprland corners') echo theme;; esac");
        QCOMPARE(Cli::run(s.env, {"get", "appearance.corners"}), 0);
        QCOMPARE(Cli::run(s.env, {"get", "no.such.setting"}), 1);
        QCOMPARE(Cli::run(s.env, {"plan", "appearance.corners", "sharp"}), 0);
        QCOMPARE(Cli::run(s.env, {"plan", "appearance.corners", "wavy"}), 1);
        QCOMPARE(Cli::run(s.env, {"not-a-command"}), 1);
        QCOMPARE(Cli::run(s.env, {"--help"}), 0);
        QCOMPARE(Cli::run(s.env, {"catalog", "--json"}), 0);
        QCOMPARE(Cli::run(s.env, {"search", "theme"}), 0);
        QCOMPARE(Cli::run(s.env, {"history"}), 0);
    }
    void cliApplyThenUndoRoundTrips() {
        Sandbox s;
        s.omarchy("exit 0");
        const QString config = s.env.configHome + "/omarchy/shell.json";
        writeFile(config, "{\"idle\":{\"lock\":300}}");
        QCOMPARE(Cli::run(s.env, {"apply", "power.idle.lock", "600"}), 0);
        QVERIFY(readAll(config).contains("600"));
        ChangeCoordinator coordinator(s.env);
        QCOMPARE(Cli::run(s.env, {"undo", coordinator.history().first().id}), 0);
        QVERIFY(readAll(config).contains("300"));
    }
    void cliSettlesAnInterruptedChangeBeforeANewOne() {
        Sandbox s;
        s.omarchy("exit 0");
        writeFile(s.env.configHome + "/omarchy/shell.json", "{\"idle\":{\"lock\":300}}");
        ChangeCoordinator coordinator(s.env);
        Transaction tx;
        tx.id = "old";
        tx.label = "X";
        tx.phase = "applying";
        writeFile(coordinator.journalPath(), QJsonDocument(tx.toJson()).toJson(QJsonDocument::Compact) + "\n");
        QCOMPARE(Cli::run(s.env, {"apply", "power.idle.lock", "600"}), 0);
        QCOMPARE(coordinator.transaction("old").phase, QString("interrupted"));
    }
    void cliLaunchAsksFirstForConsequentialRoutes() {
        Sandbox s;
        s.omarchy("exit 0");
        s.command("omarchy-launch-floating-terminal-with-presentation", "touch \"$HOME/launched\"");
        QCOMPARE(Cli::run(s.env, {"launch", "setup.reset"}), 1);
        QTest::qWait(100);
        QVERIFY(!QFile::exists(s.env.home + "/launched"));
        QCOMPARE(Cli::run(s.env, {"launch", "not.a.route"}), 1);
        QCOMPARE(Cli::run(s.env, {"launch", "appearance.theme"}), 1);
    }
    void menuIntegrationKeepsExistingEntries() {
        Sandbox s;
        s.omarchy("exit 0");
        const QString menu = s.env.configHome + "/omarchy/extensions/omarchy-menu.jsonc";
        writeFile(menu, "{\n  // my comment\n  \"personal\": {\"label\": \"Personal\"},\n}\n");
        QCOMPARE(Cli::run(s.env, {"integrate", "menu"}), 0);
        QByteArray text = readAll(menu);
        QVERIFY(text.contains("// my comment"));
        QVERIFY(text.contains("\"personal\""));
        QCOMPARE(FileOps::jsonGet(text, {"omarchy-control", "action"}).toString(), QString("omarchy-control open"));
        // Running it twice does not stack up a second entry.
        QCOMPARE(Cli::run(s.env, {"integrate", "menu"}), 0);
        QCOMPARE(readAll(menu).count("\"omarchy-control\""), qsizetype(1));
        QCOMPARE(Cli::run(s.env, {"integrate", "menu", "--remove"}), 0);
        text = readAll(menu);
        QVERIFY(!text.contains("omarchy-control"));
        QVERIFY(text.contains("// my comment"));
        QVERIFY(text.contains("\"personal\""));
    }
    void menuIntegrationStartsFromTheStockTemplate() {
        Sandbox s;
        s.omarchy("exit 0");
        writeFile(s.env.omarchyPath + "/config/omarchy/extensions/omarchy-menu.jsonc", "{\n  // Extend the menu.\n}\n");
        QCOMPARE(Cli::run(s.env, {"integrate", "menu"}), 0);
        const QByteArray text = readAll(s.env.configHome + "/omarchy/extensions/omarchy-menu.jsonc");
        QVERIFY(text.contains("// Extend the menu."));
        QVERIFY(text.contains("Control Center"));
    }
    void skillInstallWritesTheBundledSkill() {
        Sandbox s;
        QCOMPARE(Cli::run(s.env, {"skill", "install"}), 0);
        const QByteArray skill = readAll(s.env.home + "/.claude/skills/omarchy-control/SKILL.md");
        QVERIFY(skill.startsWith("---\nname: omarchy-control"));
    }
    void cliRulesAndShortcutsRespectValidation() {
        Sandbox s;
        s.minimalHyprland();
        QCOMPARE(Cli::run(s.env, {"rule", "add", "firefox", "exec", "rm"}), 1);
        QVERIFY(!QFile::exists(s.env.configHome + "/hypr/omarchy-control.lua"));
        QCOMPARE(Cli::run(s.env, {"shortcut", "add", "F", "x", "y"}), 1);
        QVERIFY(!QFile::exists(s.env.configHome + "/hypr/omarchy-control.lua"));
    }

    // ---- Interface behavior ---------------------------------------------------
    void editsMadeOutsideTheAppAppearOnTheirOwn() {
        Sandbox s;
        fakeDesktop(s);
        const QString config = s.env.configHome + "/omarchy/shell.json";
        writeFile(config, "{\"idle\":{\"lock\":300}}");
        Backend backend(s.env);
        backend.pageShown("power");
        const auto value = [&] { return backend.states()->value(Backend::stateKey("power.idle.lock")).toMap().value("value").toString(); };
        QTRY_COMPARE_WITH_TIMEOUT(value(), QString("300"), 5000);
        // Replaced atomically, as an editor or Omarchy's own commands do.
        QVERIFY(FileOps::atomicWrite(config, "{\"idle\":{\"lock\":900}}"));
        QTRY_COMPARE_WITH_TIMEOUT(value(), QString("900"), 5000);
    }
    void theOwnershipPromptGuardsTheFirstWrite() {
        Sandbox s;
        fakeDesktop(s);
        QQuickStyle::setStyle("Material");
        Translator translator(s.env.configHome + "/omarchy/language", ":/i18n", s.env.omarchyPath + "/default/i18n/locales.json");
        Theme theme(s.env);
        Backend backend(s.env);
        QQmlApplicationEngine engine;
        engine.addImageProvider("icons", new IconProvider);
        engine.rootContext()->setContextProperty("translator", &translator);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("initialPage", QString());
        engine.rootContext()->setContextProperty("initialSetting", QString());
        engine.load(QUrl("qrc:/Main.qml"));
        QObject *window = engine.rootObjects().first();
        auto *dialog = window->findChild<QObject *>("confirmDialog");
        QVERIFY(dialog);
        const QString lua = s.env.configHome + "/hypr/hyprland.lua";
        const QByteArray stock = readAll(lua);
        QMetaObject::invokeMethod(window, "requestChange", Q_ARG(QVariant, "input.repeat_rate"), Q_ARG(QVariant, "31"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(dialog->property("body").toString().contains("hyprland.lua"));
        QTest::qWait(150);
        // Nothing moves while the question is open, and declining leaves it all alone.
        QCOMPARE(readAll(lua), stock);
        QVERIFY(!QFile::exists(s.env.configHome + "/hypr/omarchy-control.lua"));
        QMetaObject::invokeMethod(dialog, "close");
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QTest::qWait(150);
        QCOMPARE(readAll(lua), stock);
        QVERIFY(!QFile::exists(s.env.configHome + "/hypr/omarchy-control.lua"));
    }
    void aFailedChangeShowsOnItsRow() {
        Sandbox s;
        fakeDesktop(s);
        s.omarchy(R"(case "$*" in version) echo 4.5.0-3;; 'hyprland corners') echo theme;; 'hyprland corners '*) echo denied >&2; exit 4;; esac)");
        Backend backend(s.env);
        QSignalSpy toasts(&backend, &Backend::toast);
        backend.commit("appearance.corners", "sharp");
        QTRY_VERIFY_WITH_TIMEOUT(!backend.states()->value(Backend::stateKey("appearance.corners")).toMap().value("error").toString().isEmpty(), 8000);
        const auto state = backend.states()->value(Backend::stateKey("appearance.corners")).toMap();
        QVERIFY(!state.value("busy").toBool());
        QVERIFY(state.value("error").toString().contains("denied"));
        QTRY_COMPARE_WITH_TIMEOUT(backend.states()->value(Backend::stateKey("appearance.corners")).toMap().value("value").toString(), QString("theme"), 5000);
    }
    void restartNoticesAreCollected() {
        Sandbox s;
        fakeDesktop(s);
        s.omarchy(R"(f="$HOME/size"; case "$*" in version) echo 4.5.0-3;; 'display text size '*) printf '[font]\nbase-size = %s\n' "$4" > "$HOME/.config/omarchy/shell.toml";; esac)");
        Backend backend(s.env);
        backend.commit("appearance.textsize", "16");
        QTRY_COMPARE_WITH_TIMEOUT(backend.pendingRestarts().size(), 1, 8000);
        backend.clearRestartNotice();
        QVERIFY(backend.pendingRestarts().isEmpty());
    }

    // ---- Interface ------------------------------------------------------------
    void everyPageLoadsWithoutQmlWarnings() {
        // Loads the real QML against a fake machine and visits every page, so a
        // broken binding or a missing component fails here rather than on screen.
        Sandbox s;
        fakeDesktop(s);
        QQuickStyle::setStyle("Material");
        Translator translator(s.env.configHome + "/omarchy/language", ":/i18n", s.env.omarchyPath + "/default/i18n/locales.json");
        Theme theme(s.env);
        Backend backend(s.env);
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError> &list) {
            for (const auto &warning : list)
                warnings << warning.toString();
        });
        engine.addImageProvider("icons", new IconProvider);
        engine.rootContext()->setContextProperty("translator", &translator);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("initialPage", QString());
        engine.rootContext()->setContextProperty("initialSetting", QString());
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(warnings.join("\n")));
        QObject *window = engine.rootObjects().first();
        for (const auto &value : backend.pages()) {
            const QString page = value.toMap().value("id").toString();
            QMetaObject::invokeMethod(window, "go", Q_ARG(QVariant, page), Q_ARG(QVariant, QString()));
            QTest::qWait(250);
            QCOMPARE(window->property("currentPage").toString(), page);
        }
        // Search shows results; arriving by search scrolls to and marks the row.
        QMetaObject::invokeMethod(window, "go", Q_ARG(QVariant, "power"), Q_ARG(QVariant, "power.stayawake"));
        QTest::qWait(200);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join("\n")));
    }
    void keyboardDrivesSearchAndRespectsTyping() {
        Sandbox s;
        fakeDesktop(s);
        QQuickStyle::setStyle("Material");
        Translator translator(s.env.configHome + "/omarchy/language", ":/i18n", s.env.omarchyPath + "/default/i18n/locales.json");
        Theme theme(s.env);
        Backend backend(s.env);
        QQmlApplicationEngine engine;
        engine.addImageProvider("icons", new IconProvider);
        engine.rootContext()->setContextProperty("translator", &translator);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("initialPage", QString());
        engine.rootContext()->setContextProperty("initialSetting", QString());
        engine.load(QUrl("qrc:/Main.qml"));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window, 3000));
        QSignalSpy quit(&engine, &QQmlEngine::quit);

        // A letter that means Quit elsewhere is only a letter while typing.
        QTest::keyClick(window, Qt::Key_K, Qt::ControlModifier);
        QTRY_VERIFY(window->property("typing").toBool());
        for (const char c : QByteArray("quiet"))
            QTest::keyClick(window, c);
        QTest::qWait(30);
        QCOMPARE(quit.count(), 0);
        QVERIFY(window->activeFocusItem()->property("text").toString().contains("quiet"));
        QTest::keyClick(window, Qt::Key_Escape);
        QVERIFY(window->activeFocusItem()->property("text").toString().isEmpty());

        // Search then Enter lands on the destination without running anything.
        for (const char c : QByteArray("stay awake"))
            QTest::keyClick(window, c);
        QTest::qWait(30);
        const int commandsBefore = s.calls().size();
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(window->property("currentPage").toString(), QString("power"));
        QVERIFY(!s.calls().mid(commandsBefore).join('\n').contains("toggle idle stay-awake"));
        QVERIFY(!window->property("typing").toBool());

        // Sent without Shift: the offscreen platform has no keymap to resolve Shift+?
        // the way a real keyboard layout does. That path is on the live checklist.
        QTest::keyClick(window, Qt::Key_Question);
        QTRY_VERIFY(window->property("helpVisible").toBool());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!window->property("helpVisible").toBool());

        // Alt+Left returns to where search started from.
        QTest::keyClick(window, Qt::Key_Left, Qt::AltModifier);
        QTRY_COMPARE(window->property("currentPage").toString(), QString("overview"));

        QTest::keyClick(window, Qt::Key_Q);
        QTRY_COMPARE(quit.count(), 1);
    }
    void stateMapReflectsReads() {
        Sandbox s;
        fakeDesktop(s);
        Backend backend(s.env);
        backend.refresh("appearance.corners");
        QTRY_COMPARE_WITH_TIMEOUT(backend.states()->value(Backend::stateKey("appearance.corners")).toMap().value("status").toString(), QString("ok"), 5000);
        const auto state = backend.states()->value(Backend::stateKey("appearance.corners")).toMap();
        QCOMPARE(state.value("value").toString(), QString("theme"));
        QCOMPARE(state.value("display").toString(), QString("Follow theme"));
    }
    void pinsPersist() {
        Sandbox s;
        fakeDesktop(s);
        {
            Backend backend(s.env);
            backend.togglePin("appearance.theme");
            QCOMPARE(backend.pins(), QStringList{"appearance.theme"});
        }
        Backend again(s.env);
        QCOMPARE(again.pins(), QStringList{"appearance.theme"});
        again.togglePin("appearance.theme");
        QVERIFY(again.pins().isEmpty());
        again.togglePin("not.a.real.id");
        QVERIFY(again.pins().isEmpty());
    }
    void problemsAreSurfaced() {
        Sandbox s;
        s.omarchy(kScript15);
        s.command("hyprctl", "exit 1");
        Backend backend(s.env);
        QTRY_VERIFY_WITH_TIMEOUT(backend.problems().size() >= 2, 5000);
        const QString text = QVariant(backend.problems()).toStringList().join(' ');
        QStringList messages;
        for (const auto &row : backend.problems())
            messages << row.toMap().value("text").toString();
        QVERIFY(messages.join('\n').contains("shell"));
        QVERIFY(!backend.compatible());
    }

private:
    static QByteArray twoMonitors() {
        return R"([{"name":"eDP-1","width":1920,"height":1080,"refreshRate":60,"x":0,"y":0,"scale":1,"transform":0,"disabled":false,"focused":true,"availableModes":["1920x1080@60.00Hz"]},{"name":"DP-1","width":2560,"height":1440,"refreshRate":60,"x":1920,"y":0,"scale":1,"transform":0,"disabled":false,"availableModes":["2560x1440@60.00Hz"]},{"name":"DP-2","width":1920,"height":1080,"refreshRate":60,"x":0,"y":0,"scale":1,"transform":0,"disabled":true,"availableModes":["1920x1080@60.00Hz"]}])";
    }
    static void monitorFixture(const Sandbox &s, const QByteArray &monitors) {
        s.minimalHyprland();
        writeFile(s.dir.path() + "/hypr-monitors", monitors);
    }
    static QString watchdogPlan(const Sandbox &s, int seconds) {
        const QJsonArray previous{
            QJsonObject{{"output", "DP-2"}, {"disabled", true}},
            QJsonObject{{"output", "eDP-1"}, {"mode", "1920x1080@60.00"}, {"position", "0x0"}, {"scale", 1}, {"transform", 0}},
            QJsonObject{{"output", "DP-1"}, {"mode", "2560x1440@60.00"}, {"position", "1920x0"}, {"scale", 1}, {"transform", 0}},
        };
        const QJsonObject plan{{"previous", previous}, {"seconds", seconds}, {"keepFile", s.dir.path() + "/keep"}, {"revertFile", s.dir.path() + "/revert"}, {"resultFile", s.dir.path() + "/result"}};
        const QString path = s.dir.path() + "/plan.json";
        writeFile(path, QJsonDocument(plan).toJson());
        return path;
    }
    static void fakeDesktop(const Sandbox &s) {
        s.minimalHyprland();
        s.omarchy(kScript16);
        s.command("pactl", kScript17);
        s.command("nmcli", "exit 0");
        s.command("bluetoothctl", "exit 0");
        s.command("systemctl", "echo '[]'");
        s.command("xdg-mime", "exit 0");
        s.command("df", "exit 0");
        writeFile(s.env.configHome + "/omarchy/shell.json", "{}");
    }
};

int main(int argc, char *argv[]) {
    // Re-entered by watchdogSurvivesTheWindowDying as the detached process.
    // The real entry point, as DisplayProvider starts it, inside a test's sandbox.
    if (argc == 3 && QString::fromLatin1(argv[1]) == QLatin1String("display-watchdog")) {
        QCoreApplication app(argc, argv);
        const QString root = qEnvironmentVariable("OC_TEST_ROOT");
        return DisplayProvider::runWatchdog(Env::sandbox(root, root + "/bin"), QString::fromLocal8Bit(argv[2]));
    }
    if (argc == 4 && QString::fromLatin1(argv[1]) == QLatin1String("--watchdog")) {
        QCoreApplication app(argc, argv);
        const QString root = QString::fromLocal8Bit(argv[2]);
        return DisplayProvider::runWatchdog(Env::sandbox(root, root + "/bin"), QString::fromLocal8Bit(argv[3]));
    }
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    OmarchyControlTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "omarchy-control_tests.moc"
