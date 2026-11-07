// Every test runs against a temporary root with fake commands first on PATH. No
// test can reach the real desktop: the sandbox Env is the only way in.
#include <functional>

#include <QCryptographicHash>
#include <QDateTime>
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
#include "iconthemeimporter.h"
#include "iconthemeprovider.h"
#include "iconthemes.h"
#include "networkprovider.h"
#include "pluginsprovider.h"
#include "servicesprovider.h"
#include "settingsengine.h"
#include "shortcutsprovider.h"
#include "systemprovider.h"
#include "theme.h"
#include "translator.h"
#include "updater.h"

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

const char kSvg[] = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 48 48\"><rect width=\"48\" height=\"48\" fill=\"#ff0000\"/></svg>";

// A pack on disk, shaped as the spec describes. `icons` are placed in its scalable folder.
void addPack(const QString &root, const QString &id, const QStringList &icons = {"folder", "text-x-generic", "utilities-terminal", "web-browser"},
             const QString &inherits = {}, const QByteArray &extra = {}) {
    QByteArray index = "[Icon Theme]\nName=" + id.toUtf8() + "\n" + extra;
    if (!inherits.isEmpty())
        index += "Inherits=" + inherits.toUtf8() + "\n";
    index += "Directories=48x48/apps,scalable/apps\n\n[48x48/apps]\nSize=48\nType=Fixed\n\n[scalable/apps]\nSize=48\nMinSize=8\nMaxSize=512\nType=Scalable\n";
    writeFile(root + "/" + id + "/index.theme", index);
    QDir().mkpath(root + "/" + id + "/48x48/apps");
    for (const QString &icon : icons)
        writeFile(root + "/" + id + "/scalable/apps/" + icon + ".svg", kSvg);
}

// Icon packs and a fake session. Yaru-blue is what Omarchy's fallback names.
struct IconDesk {
    Sandbox &s;
    QString user, system;
    explicit IconDesk(Sandbox &sandbox) : s(sandbox), user(sandbox.env.dataHome + "/icons"), system(sandbox.env.dataDirs.last() + "/icons") {
        s.command("gsettings", kFakeGsettings);
        s.command("dconf", kFakeDconf);
        addPack(system, "Yaru-blue");
        addPack(system, "Papirus");
        addPack(system, "Numix");
    }
    QString effective() const { return QString::fromUtf8(readAll(s.env.home + "/.fake-icon-theme")); }
    void setEffective(const QString &value) const { writeFile(s.env.home + "/.fake-icon-theme", value.toUtf8()); }
    bool isUnset() const { return !QFile::exists(s.env.home + "/.fake-icon-theme"); }
    void themeAsks(const QString &id) const { writeFile(s.env.stateHome + "/omarchy/current/theme/icons.theme", id.toUtf8() + "\n"); }
    QString policy() const { return s.env.appConfigDir + "/icons.json"; }
    QString hook() const { return s.env.configHome + "/omarchy/hooks/theme-set.d/90-omarchy-control-icons"; }
    int writes() const {
        int count = 0;
        for (const QString &call : s.calls())
            if (call.startsWith("gsettings set") || call.startsWith("gsettings reset"))
                ++count;
        return count;
    }
};

QString importPack(IconThemeImporter &importer, const QString &folder) {
    QSignalSpy finished(&importer, &IconThemeImporter::finished);
    importer.choosePath(folder);
    if (importer.busy())
        finished.wait(30000);
    return importer.state();
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
        QCOMPARE(catalog.pages().size(), 13);
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
            for (int i = 0; i < 400; ++i) {
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
    void hoveringASettingRowMovesNothing() {
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
        QMetaObject::invokeMethod(window, "go", Q_ARG(QVariant, "appearance"), Q_ARG(QVariant, QString()));
        QTest::qWait(400);

        // Every full row in view, found by walking the real scene.
        QList<QQuickItem *> rows;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
            if (!item->isVisible())
                return;
            if (!item->property("entryId").toString().isEmpty() && !item->property("compact").toBool()) {
                const QRectF box = item->mapRectToScene(item->boundingRect());
                if (box.top() >= 60 && box.bottom() <= window->height())
                    rows << item;
                return;
            }
            for (QQuickItem *child : item->childItems())
                walk(child);
        };
        walk(window->contentItem());
        QVERIFY2(rows.size() >= 3, qPrintable(QString::number(rows.size())));

        const auto named = [](QQuickItem *row, const QString &name) {
            QQuickItem *found = nullptr;
            std::function<void(QQuickItem *)> search = [&](QQuickItem *item) {
                if (item->objectName() == name)
                    found = item;
                for (QQuickItem *child : item->childItems())
                    if (!found)
                        search(child);
            };
            search(row);
            return found;
        };
        const auto layoutOf = [&](QQuickItem *row) {
            QList<QRectF> boxes{row->mapRectToScene(row->boundingRect())};
            for (const QString &name : {QStringLiteral("settingControl"), QStringLiteral("pinButton"), QStringLiteral("detailsButton")})
                boxes << named(row, name)->mapRectToScene(named(row, name)->boundingRect());
            return boxes;
        };

        // Every control ends on the same edge, down the whole page.
        QSet<int> rightEdges;
        for (QQuickItem *row : rows) {
            for (const QString &name : {QStringLiteral("settingControl"), QStringLiteral("actionButton")}) {
                QQuickItem *control = named(row, name);
                if (control && control->isVisible())
                    rightEdges << qRound(control->mapRectToScene(control->boundingRect()).right());
            }
        }
        QVERIFY(!rightEdges.isEmpty());
        QCOMPARE(rightEdges.size(), 1);

        QTest::mouseMove(window, QPoint(1, 1));
        QTest::qWait(250);
        QQuickItem *firstPin = named(rows.first(), "pinButton");
        QCOMPARE(firstPin->opacity(), 0.0);
        const QString firstId = rows.first()->property("entryId").toString();

        // At rest a tap on the hidden button changes nothing.
        QMetaObject::invokeMethod(firstPin, "clicked");
        QVERIFY(!backend.pins().contains(firstId));

        QList<QList<QRectF>> before;
        for (QQuickItem *row : rows)
            before << layoutOf(row);
        for (int i = 0; i < rows.size(); ++i) {
            const QPointF center = rows[i]->mapToScene(QPointF(40, rows[i]->height() / 2));
            QTest::mouseMove(window, center.toPoint());
            // The button showing is what proves the hover reached the row.
            QTRY_VERIFY2(named(rows[i], "pinButton")->opacity() > 0.4, qPrintable(rows[i]->property("entryId").toString()));
            QTest::qWait(250);
            for (int j = 0; j < rows.size(); ++j)
                QCOMPARE(layoutOf(rows[j]), before[j]);
        }
    }
    void aTabAlwaysOpensAtTheTopAndSearchStillLandsOnItsRow() {
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
        // Short enough that every page scrolls and the first tabs are still on screen.
        window->resize(1000, 400);
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window, 3000));
        QTest::qWait(200);

        const auto find = [&](const QString &name) {
            QQuickItem *found = nullptr;
            std::function<void(QQuickItem *)> search = [&](QQuickItem *item) {
                if (item->objectName() == name)
                    found = item;
                for (QQuickItem *child : item->childItems())
                    if (!found)
                        search(child);
            };
            search(window->contentItem());
            return found;
        };
        const auto view = [&] {
            return find("pageLoader")->property("item").value<QObject *>()->property("contentItem").value<QObject *>();
        };
        const auto offset = [&] { return view()->property("contentY").toReal(); };
        const auto scrollDown = [&] {
            view()->setProperty("contentY", 120);
            QCOMPARE(offset(), 120.0);
        };
        const auto clickTab = [&](const QString &page) {
            QQuickItem *tab = find("nav_" + page);
            QVERIFY2(tab, qPrintable(page));
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, tab->mapToScene(QPointF(tab->width() / 2, tab->height() / 2)).toPoint());
            QTRY_COMPARE(window->property("currentPage").toString(), page);
            QTest::qWait(100);
        };

        clickTab("appearance");
        QCOMPARE(offset(), 0.0);
        scrollDown();
        // A different tab, sharing the same scrolling view.
        clickTab("desktop");
        QCOMPARE(offset(), 0.0);
        scrollDown();
        // The tab already open.
        clickTab("desktop");
        QCOMPARE(offset(), 0.0);
        scrollDown();
        QMetaObject::invokeMethod(window, "back");
        QTRY_COMPARE(window->property("currentPage").toString(), QString("appearance"));
        QCOMPARE(offset(), 0.0);
        // Overview has its own view.
        clickTab("overview");
        scrollDown();
        clickTab("overview");
        QCOMPARE(offset(), 0.0);

        // A search result lands on its row, and keeps doing so after the highlight clears.
        const QString far = "appearance.plymouth";
        QMetaObject::invokeMethod(window, "go", Q_ARG(QVariant, "appearance"), Q_ARG(QVariant, far));
        QTRY_VERIFY(offset() > 0);
        QTRY_VERIFY_WITH_TIMEOUT(window->property("focusRequest").toString().isEmpty(), 4000);
        QTest::qWait(200);
        QVERIFY(offset() > 0);
        clickTab("appearance");
        QCOMPARE(offset(), 0.0);
        QMetaObject::invokeMethod(window, "go", Q_ARG(QVariant, "appearance"), Q_ARG(QVariant, far));
        QTRY_VERIFY(offset() > 0);
    }

    // ---- Look ------------------------------------------------------------------
    void textColorsStayReadableOnAnyTheme() {
        // Catppuccin Latte's own secondary text, on its own background.
        const QStringList latte{"#eff1f5", "#dce0e8"};
        QVERIFY(Theme::contrast("#9ca0b0", "#eff1f5") < 3);
        const QString fixed = Theme::readable("#9ca0b0", "#4c4f69", latte);
        QVERIFY2(Theme::contrast(fixed, "#eff1f5") >= 4.5, qPrintable(fixed));
        QVERIFY2(Theme::contrast(fixed, "#dce0e8") >= 4.5, qPrintable(fixed));
        // A color that already reads is left exactly as the theme wrote it.
        QCOMPARE(Theme::readable("#ff9f0a", "#f5f5f7", {"#1c1c1e", "#2c2c2e"}), QString("#ff9f0a"));
        // A warning that needs help stays a warning rather than turning gray.
        const QColor amber(Theme::readable("#df8e1d", "#4c4f69", latte));
        QVERIFY2(Theme::contrast(amber.name(), "#eff1f5") >= 4.5, qPrintable(amber.name()));
        QVERIFY2(amber.hslSaturationF() > 0.4, qPrintable(amber.name()));
        QVERIFY2(qAbs(amber.hslHueF() - QColor("#df8e1d").hslHueF()) < 0.02, qPrintable(amber.name()));
        // Nothing can be done past the foreground itself, and it does not hang trying.
        QCOMPARE(Theme::readable("#888888", "#888888", {"#888888"}), QString("#888888"));
    }
    void themePreviewsComeFromTheThemesThemselves() {
        Sandbox s;
        fakeDesktop(s);
        writeFile(s.env.omarchyPath + "/themes/tokyo-night/colors.toml", "accent = \"#7aa2f7\"\nbackground = \"#1a1b26\"\n");
        writeFile(s.env.omarchyPath + "/themes/tokyo-night/preview.png", "x");
        writeFile(s.env.omarchyPath + "/themes/mine/colors.toml", "accent = \"#000001\"\n");
        writeFile(s.env.configHome + "/omarchy/themes/mine/colors.toml", "accent = \"#000002\"\n");
        Theme theme(s.env);
        const QVariantMap tokyo = theme.preview("Tokyo Night");
        QCOMPARE(tokyo.value("accent").toString(), QString("#7aa2f7"));
        QVERIFY(tokyo.value("image").toString().endsWith("/themes/tokyo-night/preview.png"));
        // The user's own copy of a theme is the one that will be used.
        QCOMPARE(theme.preview("Mine").value("accent").toString(), QString("#000002"));
        QVERIFY(!theme.preview("Mine").contains("image"));
        // No preview, but wallpapers: the first still stands in. A video cannot.
        writeFile(s.env.configHome + "/omarchy/themes/mine/backgrounds/1-motion.mp4", "x");
        writeFile(s.env.configHome + "/omarchy/themes/mine/backgrounds/2-still.webp", "x");
        QVERIFY(theme.preview("Mine").value("image").toString().endsWith("/backgrounds/2-still.webp"));
        QVERIFY(theme.preview("Not Installed").isEmpty());
        // The name comes from a command's output, so it cannot walk out of the folder.
        QVERIFY(theme.preview("../../etc").isEmpty());
        QVERIFY(theme.preview(".hidden").isEmpty());
    }
    void picturesAreCutToTheDesktopsCorners() {
        Sandbox s;
        fakeDesktop(s);
        const QString path = s.dir.path() + "/a picture %.png";
        QImage source(800, 200, QImage::Format_RGB32);
        source.fill(Qt::red);
        QVERIFY(source.save(path));
        Theme theme(s.env);
        IconProvider provider;
        const auto request = [&](int radius) {
            const QString id = theme.thumbnail(path, radius).mid(QString("image://icons/").size());
            QSize size;
            const QImage image = provider.requestImage(id, &size, QSize(160, 100));
            return std::make_pair(image, size);
        };
        const auto [rounded, roundedSize] = request(12);
        QCOMPARE(roundedSize, QSize(160, 100));
        QCOMPARE(rounded.size(), QSize(160, 100));
        QCOMPARE(qAlpha(rounded.pixel(0, 0)), 0);
        QCOMPARE(qAlpha(rounded.pixel(80, 50)), 255);
        // A square desktop gets square pictures, with no corner cut at all.
        const auto [square, squareSize] = request(0);
        Q_UNUSED(squareSize);
        QCOMPARE(qAlpha(square.pixel(0, 0)), 255);
        // A file URL means the same picture, and a missing one is simply empty.
        QCOMPARE(theme.thumbnail(QUrl::fromLocalFile(path).toString(), 0), theme.thumbnail(path, 0));
        QVERIFY(theme.thumbnail("", 4).isEmpty());
        QSize size;
        const QImage missing = provider.requestImage("thumb/4/0/" + QString::fromUtf8(QUrl::toPercentEncoding("/no/such.png")), &size, QSize(32, 32));
        QCOMPARE(qAlpha(missing.pixel(16, 16)), 0);
    }
    void aThemesBackgroundsFollowTheTheme() {
        Sandbox s;
        fakeDesktop(s);
        const QString current = s.env.stateHome + "/omarchy/current";
        const QString state = s.dir.path() + "/picked";
        const auto swapTo = [&](const QString &name, const QStringList &pictures) {
            // What Omarchy does: the theme's folder is replaced outright.
            QDir(current + "/theme").removeRecursively();
            for (const auto &picture : pictures)
                writeFile(current + "/theme/backgrounds/" + picture, "x");
            QFile::remove(current + "/background");
            QFile::link(current + "/theme/backgrounds/" + pictures.first(), current + "/background");
            writeFile(current + "/theme.name", name.toUtf8());
            writeFile(state, name.toUtf8());
        };
        swapTo("One", {"one-a.png", "one-b.png"});
        s.omarchy("state='" + state.toUtf8() + "'\ncurrent='" + current.toUtf8() + "'\ncase \"$*\" in\n"
                  "  'theme list') printf 'One\\nTwo\\n';;\n"
                  "  'theme current') cat \"$state\";;\n"
                  "  'theme set Two') rm -rf \"$current/theme\"; mkdir -p \"$current/theme/backgrounds\"; : > \"$current/theme/backgrounds/two-a.png\"; : > \"$current/theme/backgrounds/two-b.png\"; : > \"$current/theme/backgrounds/two-c.png\"; ln -sf \"$current/theme/backgrounds/two-b.png\" \"$current/background\"; printf Two > \"$current/theme.name\"; printf Two > \"$state\";;\n"
                  "  version) echo 4.5.0-3;;\nesac\n");
        Backend backend(s.env);
        backend.pageShown("appearance");
        const auto background = [&] { return backend.states()->value(Backend::stateKey("appearance.background")).toMap(); };
        const auto labels = [&] {
            QStringList names;
            for (const auto &choice : background().value("choices").toList())
                names << choice.toMap().value("label").toString();
            return names;
        };
        QTRY_COMPARE(labels(), QStringList({"one-a", "one-b"}));
        QVERIFY(background().value("value").toString().endsWith("/one-a.png"));

        // Changed in this window.
        backend.commit("appearance.theme", "Two");
        QTRY_COMPARE_WITH_TIMEOUT(labels(), QStringList({"two-a", "two-b", "two-c"}), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(background().value("value").toString().endsWith("/two-b.png"), 5000);

        // Changed from outside it, such as from the Omarchy menu.
        swapTo("One", {"one-a.png", "one-b.png"});
        QTRY_COMPARE_WITH_TIMEOUT(labels(), QStringList({"one-a", "one-b"}), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(background().value("value").toString().endsWith("/one-a.png"), 5000);
    }
    void aSameNamedPictureFromAnotherThemeIsNotTheCachedOne() {
        Sandbox s;
        fakeDesktop(s);
        const QString current = s.env.stateHome + "/omarchy/current";
        const QString picture = current + "/theme/backgrounds/1-main.png";
        writeFile(picture, "first");
        QFile::remove(current + "/background");
        QFile::link(picture, current + "/background");
        QFile first(picture);
        QVERIFY(first.open(QIODevice::ReadWrite));
        QVERIFY(first.setFileTime(QDateTime::fromSecsSinceEpoch(1700000000), QFileDevice::FileModificationTime));
        first.close();
        Theme theme(s.env);
        const QString before = theme.thumbnail(picture, 8);
        QTRY_VERIFY(!theme.wallpaper().isEmpty());
        const QString wallpaperBefore = theme.wallpaper();
        // Same path, new theme: Omarchy replaces the folder outright.
        QVERIFY(QDir(current + "/theme").removeRecursively());
        writeFile(picture, "second");
        QVERIFY(theme.thumbnail(picture, 8) != before);
        QTRY_VERIFY_WITH_TIMEOUT(theme.wallpaper() != wallpaperBefore, 3000);
        // Still the same file underneath.
        QCOMPARE(QUrl(theme.wallpaper()).toLocalFile(), QFileInfo(picture).canonicalFilePath());
    }
    void cornersFollowTheDesktop() {
        Sandbox s;
        fakeDesktop(s);
        Theme theme(s.env);
        QVERIFY(theme.radius() >= 0);
        s.command("hyprctl", "echo '{\"int\":0}'");
        theme.refreshRounding();
        QTRY_COMPARE(theme.radius(), 0);
        QCOMPARE(theme.panelRadius(), 0);
        QCOMPARE(theme.controlRadius(), 0);
        QVERIFY(!theme.rounded());
        s.command("hyprctl", "echo '{\"int\":10}'");
        theme.refreshRounding();
        QTRY_COMPARE(theme.radius(), 10);
        QCOMPARE(theme.panelRadius(), 10);
        QVERIFY(theme.controlRadius() > 0 && theme.controlRadius() <= 10);
        QVERIFY(theme.rounded());
    }
    void overviewReadsEverySettingItShows() {
        // A row Overview draws but never reads sits on its loading state forever.
        Catalog catalog;
        const QString page = QString::fromUtf8(readAll(PROJECT_ROOT "/src/OverviewPage.qml"));
        static const QRegularExpression row("SettingRow \\{[^}]*backend\\.entry\\(\"([^\"]+)\"\\)");
        auto matches = row.globalMatch(page);
        int rows = 0;
        while (matches.hasNext()) {
            const QString id = matches.next().captured(1);
            ++rows;
            QVERIFY2(catalog.find(id) && catalog.find(id)->quick, qPrintable(id));
        }
        QVERIFY(rows >= 6);
    }
    void buttonsSayWhatTheyDo() {
        Catalog catalog;
        const auto verb = [&](const char *id) { return catalog.find(id)->verb(); };
        QCOMPARE(verb("system.reboot"), QString("Reboot"));
        QCOMPARE(verb("system.shutdown"), QString("Shut down"));
        QCOMPARE(verb("system.logout"), QString("Log out"));
        QCOMPARE(verb("system.lock"), QString("Lock"));
        QCOMPARE(verb("hibernation.remove"), QString("Remove"));
        QCOMPARE(verb("hibernation.setup"), QString("Set up"));
        QCOMPARE(verb("update.check"), QString("Check"));
        QCOMPARE(verb("panel.power"), QString("Open"));
        // Every verb a row can show is translated wherever the app claims to be.
        for (const char *language : {"da", "de", "ar"}) {
            const auto strings = QJsonDocument::fromJson(readAll(QString(PROJECT_ROOT "/src/i18n/") + language + ".json")).object();
            for (const auto &entry : catalog.entries())
                if (entry.isAction())
                    QVERIFY2(strings.contains(entry.verb()), qPrintable(QString(language) + ": " + entry.verb()));
        }
    }
    void aDestructiveDialogStartsOnCancel() {
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
        auto *dialog = window->findChild<QObject *>("confirmDialog");
        QVERIFY(dialog);
        const auto open = [&](bool danger) {
            QMetaObject::invokeMethod(dialog, "open_", Q_ARG(QVariant, "Remove"), Q_ARG(QVariant, "Gone for good."), Q_ARG(QVariant, QVariantList()),
                                      Q_ARG(QVariant, "Remove"), Q_ARG(QVariant, danger), Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant()));
        };
        open(true);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QTRY_COMPARE(window->activeFocusItem()->property("text").toString(), QString("Cancel"));
        QMetaObject::invokeMethod(dialog, "close");
        QTRY_VERIFY(!dialog->property("visible").toBool());
        open(false);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QTRY_COMPARE(window->activeFocusItem()->property("text").toString(), QString("Remove"));
        QMetaObject::invokeMethod(dialog, "close");
    }
    void tabNeverLeavesTheKeyboardOffScreen() {
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
        window->resize(1000, 420);
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window, 3000));
        QMetaObject::invokeMethod(window, "go", Q_ARG(QVariant, "input"), Q_ARG(QVariant, QString()));
        QTest::qWait(300);
        int inPage = 0;
        for (int i = 0; i < 90; ++i) {
            QTest::keyClick(window, Qt::Key_Tab);
            QTest::qWait(5);
            QQuickItem *item = window->activeFocusItem();
            QVERIFY(item);
            const QRectF box = item->mapRectToScene(item->boundingRect());
            if (box.left() < 250)
                continue;   // The sidebar and toolbar do not scroll.
            ++inPage;
            QVERIFY2(box.top() >= 0 && box.bottom() <= window->height(), qPrintable(QString("Tab %1 at %2..%3").arg(i).arg(box.top()).arg(box.bottom())));
        }
        QVERIFY(inPage > 20);
    }
    void pickingAThemeTileChangesTheTheme() {
        Sandbox s;
        fakeDesktop(s);
        writeFile(s.env.stateHome + "/picked", "One");
        s.omarchy("state='" + s.env.stateHome.toUtf8() + "/picked'\ncase \"$*\" in\n  'theme list') printf 'One\\nTwo\\n';;\n  'theme current') cat \"$state\";;\n  'theme set '*) shift 2; printf '%s' \"$*\" > \"$state\";;\n  version) echo 4.5.0-3;;\nesac\n");
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
        QMetaObject::invokeMethod(window, "go", Q_ARG(QVariant, "appearance"), Q_ARG(QVariant, QString()));
        const auto tile = [&](const QString &name) {
            QQuickItem *found = nullptr;
            std::function<void(QQuickItem *)> search = [&](QQuickItem *item) {
                if (item->property("look").isValid() && item->property("text").toString().isEmpty()) {
                    QAccessibleInterface *accessible = QAccessible::queryAccessibleInterface(item);
                    if (accessible && accessible->text(QAccessible::Name) == name)
                        found = item;
                }
                for (QQuickItem *child : item->childItems())
                    if (!found)
                        search(child);
            };
            search(window->contentItem());
            return found;
        };
        QAccessible::setActive(true);
        QTRY_VERIFY(tile("Two"));
        QQuickItem *two = tile("Two");
        QTRY_VERIFY(tile("One") && tile("One")->property("selected").toBool());
        QVERIFY(!two->property("selected").toBool());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, two->mapToScene(QPointF(two->width() / 2, 30)).toPoint());
        QTRY_VERIFY_WITH_TIMEOUT(!s.calls().filter("omarchy theme set Two").isEmpty(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(tile("Two") && tile("Two")->property("selected").toBool(), 5000);
    }

    // ---- Updates --------------------------------------------------------------
    static QByteArray releaseJson(const QString &version) {
        const QByteArray v = version.toUtf8();
        const QByteArray base = "https://github.com/last-refuge/omarchy-control/releases/download/v" + v + "/";
        return "{\"tag_name\":\"v" + v + "\",\"body\":\"Better things\",\"html_url\":\"https://github.com/last-refuge/omarchy-control/releases/tag/v" + v + "\",\"assets\":["
               "{\"name\":\"SHA256SUMS\",\"browser_download_url\":\"" + base + "SHA256SUMS\"},"
               "{\"name\":\"omarchy-control-" + v + "-1-x86_64.pkg.tar.zst\",\"browser_download_url\":\"" + base + "omarchy-control-" + v + "-1-x86_64.pkg.tar.zst\"}]}";
    }
    // A fake curl that answers the API, then serves the two release files.
    static void fakeGitHub(const Sandbox &s, const QString &version, const QByteArray &packageBytes, const QByteArray &listedBytes) {
        const QByteArray name = "omarchy-control-" + version.toUtf8() + "-1-x86_64.pkg.tar.zst";
        const QByteArray sum = QCryptographicHash::hash(listedBytes, QCryptographicHash::Sha256).toHex();
        writeFile(s.dir.path() + "/api.json", releaseJson(version));
        writeFile(s.dir.path() + "/package", packageBytes);
        s.command("curl", "for last; do :; done\n"
                          "while [ $# -gt 0 ]; do [ \"$1\" = -o ] && dest=$2; shift; done\n"
                          "case \"$last\" in\n"
                          "  *releases/latest) cat '" + s.dir.path().toUtf8() + "/api.json'; printf '\\n200';;\n"
                          "  *SHA256SUMS) printf '%s  %s\\n' " + sum + " " + name + ";;\n"
                          "  *.pkg.tar.zst) cp '" + s.dir.path().toUtf8() + "/package' \"$dest\";;\n"
                          "esac\n");
    }
    void versionsCompareAsNumbers() {
        QVERIFY(Updater::isNewer("0.2.0", "0.1.0"));
        QVERIFY(Updater::isNewer("0.10.0", "0.9.0"));
        QVERIFY(Updater::isNewer("1.0.0", "0.99.99"));
        QVERIFY(!Updater::isNewer("0.1.0", "0.1.0"));
        QVERIFY(!Updater::isNewer("0.1.0", "0.2.0"));
        // A tag this cannot read never counts as an update.
        QVERIFY(!Updater::isNewer("1.0.0-beta", "0.1.0"));
        QVERIFY(!Updater::isNewer("latest", "0.1.0"));
        QVERIFY(!Updater::isNewer("", "0.1.0"));
        QCOMPARE(Updater::versionFromPacman("omarchy-control 0.2.0-1\n"), QString("0.2.0"));
        QCOMPARE(Updater::versionFromPacman("error: package 'omarchy-control' was not found\n"), QString());
    }
    void releaseNotesReadAsPlainText() {
        QCOMPARE(Updater::plainNotes("## What's Changed\n* Fix a jump by @someone in https://github.com/x/y/pull/1\n- Add About\n\n\n**Full Changelog**: https://github.com/x/y/compare/a...b\n"),
                 QString("What's Changed\n• Fix a jump\n• Add About"));
        QCOMPARE(Updater::plainNotes(""), QString());
    }
    void checksumsMatchTheExactFile() {
        const QByteArray a(64, 'a'), b(64, 'b');
        const QByteArray sums = a + "  one.pkg.tar.zst\n" + b + " *two.pkg.tar.zst\nnot a checksum line\n";
        QCOMPARE(Updater::checksumFor(sums, "one.pkg.tar.zst"), QString::fromLatin1(a));
        QCOMPARE(Updater::checksumFor(sums, "two.pkg.tar.zst"), QString::fromLatin1(b));
        QCOMPARE(Updater::checksumFor(sums, "pkg.tar.zst"), QString());
    }
    void releaseResponsesAreReadCarefully() {
        const auto respond = [](const QByteArray &body, int status) {
            CommandResult result;
            result.exitCode = 0;
            result.out = body + "\n" + QByteArray::number(status);
            return result;
        };
        Updater::Check found = Updater::interpret(respond(releaseJson("0.2.0"), 200), "x86_64");
        QCOMPARE(found.outcome, Updater::Check::Found);
        QCOMPARE(found.release.version, QString("0.2.0"));
        QCOMPARE(found.release.packageName, QString("omarchy-control-0.2.0-1-x86_64.pkg.tar.zst"));
        QCOMPARE(found.release.notes, QString("Better things"));
        QVERIFY(found.release.installable());

        // No package for this architecture: known about, but not installable.
        QVERIFY(!Updater::interpret(respond(releaseJson("0.2.0"), 200), "aarch64").release.installable());

        // A response cannot point the download anywhere else.
        QByteArray elsewhere = releaseJson("0.2.0");
        elsewhere.replace("https://github.com/last-refuge/omarchy-control/releases/download/", "https://evil.example/");
        QVERIFY(!Updater::interpret(respond(elsewhere, 200), "x86_64").release.installable());
        // Nor name a different file, even from the right place.
        QByteArray renamed = releaseJson("0.2.0");
        renamed.replace("\"name\":\"omarchy-control-0.2.0-1-x86_64.pkg.tar.zst\"", "\"name\":\"../../evil.pkg.tar.zst\"");
        QVERIFY(!Updater::interpret(respond(renamed, 200), "x86_64").release.installable());

        QCOMPARE(Updater::interpret(respond("{\"message\":\"Not Found\"}", 404), "x86_64").outcome, Updater::Check::NoReleases);
        QVERIFY(Updater::interpret(respond("{}", 403), "x86_64").error.contains("limiting"));
        QCOMPARE(Updater::interpret(respond("{}", 500), "x86_64").outcome, Updater::Check::Failed);
        QCOMPARE(Updater::interpret(respond("not json", 200), "x86_64").outcome, Updater::Check::Failed);
        CommandResult offline;
        offline.exitCode = 6;
        QVERIFY(Updater::interpret(offline, "x86_64").error.contains("connection"));
    }
    void updateCheckNeverInstalls() {
        QCoreApplication::setApplicationVersion("0.1.0");
        Sandbox s;
        fakeGitHub(s, "0.2.0", "bytes", "bytes");
        s.command("sudo", "exit 0");
        QCOMPARE(Cli::run(s.env, {"update"}), 0);
        QCOMPARE(Cli::run(s.env, {"update", "check", "--json"}), 0);
        QVERIFY(s.calls().filter("sudo").isEmpty());
        QCOMPARE(s.calls().filter("curl").size(), 2);
        QCOMPARE(Cli::run(s.env, {"update", "nonsense"}), 1);
    }
    void updateInstallHandsAVerifiedPackageToPacman() {
        QCoreApplication::setApplicationVersion("0.1.0");
        Sandbox s;
        fakeGitHub(s, "0.2.0", "real package", "real package");
        // Copies what it was given, because the download is cleaned up afterwards.
        s.command("sudo", "for last; do :; done\ncp \"$last\" '" + s.dir.path().toUtf8() + "/installed'\n");
        QCOMPARE(Cli::run(s.env, {"update", "install"}), 0);
        const QStringList sudo = s.calls().filter(QRegularExpression("^sudo "));
        QCOMPARE(sudo.size(), 1);
        QVERIFY2(sudo.first().startsWith("sudo pacman -U " + s.env.appCacheDir + "/updates/omarchy-control-0.2.0-1-x86_64.pkg.tar.zst"), qPrintable(sudo.first()));
        QVERIFY(!sudo.first().contains("--noconfirm"));
        QCOMPARE(readAll(s.dir.path() + "/installed"), QByteArray("real package"));
        QVERIFY(!QFile::exists(s.env.appCacheDir + "/updates"));
    }
    void updateInstallRefusesATamperedPackage() {
        QCoreApplication::setApplicationVersion("0.1.0");
        Sandbox s;
        fakeGitHub(s, "0.2.0", "tampered", "real package");
        s.command("sudo", "exit 0");
        QCOMPARE(Cli::run(s.env, {"update", "install"}), 1);
        QVERIFY(s.calls().filter(QRegularExpression("^sudo ")).isEmpty());
        QVERIFY(!QFile::exists(s.env.appCacheDir + "/updates"));
    }
    void updateInstallLeavesTheCurrentVersionAlone() {
        QCoreApplication::setApplicationVersion("0.2.0");
        Sandbox s;
        fakeGitHub(s, "0.2.0", "bytes", "bytes");
        s.command("sudo", "exit 0");
        QCOMPARE(Cli::run(s.env, {"update", "install"}), 0);
        QVERIFY(s.calls().filter(QRegularExpression("^sudo ")).isEmpty());
        QVERIFY(s.calls().filter(".pkg.tar.zst").isEmpty());
        QCoreApplication::setApplicationVersion("0.1.0");
    }
    void updateInstallWaitsForPacman() {
        QCoreApplication::setApplicationVersion("0.1.0");
        Sandbox s;
        fakeGitHub(s, "0.2.0", "bytes", "bytes");
        s.command("sudo", "exit 0");
        writeFile(s.env.sys("/var/lib/pacman/db.lck"), "");
        QCOMPARE(Cli::run(s.env, {"update", "install"}), 1);
        QVERIFY(s.calls().filter(QRegularExpression("^sudo ")).isEmpty());
        QVERIFY(s.calls().filter(".pkg.tar.zst").isEmpty());
    }
    void aFailedPacmanIsReportedAsAFailedUpdate() {
        QCoreApplication::setApplicationVersion("0.1.0");
        Sandbox s;
        fakeGitHub(s, "0.2.0", "bytes", "bytes");
        s.command("sudo", "exit 1");
        QCOMPARE(Cli::run(s.env, {"update", "install"}), 1);
        QCOMPARE(s.calls().filter(QRegularExpression("^sudo ")).size(), 1);
    }
    void theAboutPageChecksInstallsAndRestarts() {
        QCoreApplication::setApplicationVersion("0.1.0");
        Sandbox s;
        fakeDesktop(s);
        fakeGitHub(s, "0.2.0", "bytes", "bytes");
        s.command("omarchy-control", "exit 0");
        s.command("omarchy-launch-floating-terminal-with-presentation", "exit 0");
        s.command("bash", "exit 0");
        s.command("pacman", "echo 'omarchy-control 0.1.0-1'");
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

        // About is the last tab, at the bottom of the list.
        QCOMPARE(backend.pages().last().toMap().value("id").toString(), QString("about"));

        // Every wider gap in the list has a line in it, so grouping never reads as a mistake.
        int separators = 0;
        std::function<void(QQuickItem *)> count = [&](QQuickItem *item) {
            if (item->objectName() == "navSeparator" && item->isVisible())
                ++separators;
            for (QQuickItem *child : item->childItems())
                count(child);
        };
        count(window->contentItem());
        QCOMPARE(separators, 3);

        QMetaObject::invokeMethod(window, "go", Q_ARG(QVariant, "about"), Q_ARG(QVariant, QString()));
        QTest::qWait(300);
        const auto find = [&](const QString &name) {
            QQuickItem *found = nullptr;
            std::function<void(QQuickItem *)> search = [&](QQuickItem *item) {
                if (item->objectName() == name)
                    found = item;
                for (QQuickItem *child : item->childItems())
                    if (!found)
                        search(child);
            };
            search(window->contentItem());
            return found;
        };
        const auto click = [&](const QString &name) {
            QQuickItem *button = find(name);
            QVERIFY2(button && button->isVisible(), qPrintable(name));
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
        };
        QVERIFY(find("aboutVersion")->property("text").toString().contains("0.1.0"));
        // Opening the page asks nothing of the network.
        QVERIFY(s.calls().filter("curl").isEmpty());
        QVERIFY(!find("installUpdate")->isVisible());

        click("checkUpdates");
        QTRY_COMPARE(backend.updates()->state(), QString("available"));
        QCOMPARE(backend.updates()->latestVersion(), QString("0.2.0"));
        QTRY_VERIFY(find("updateStatus")->property("text").toString().contains("0.2.0"));
        QTest::qWait(100);

        // Installing opens the terminal route and goes no further on its own.
        click("installUpdate");
        QTRY_VERIFY(backend.updates()->installStarted());
        QTRY_VERIFY(!s.calls().filter("omarchy-launch-floating-terminal-with-presentation omarchy-control update install").isEmpty());
        QVERIFY(s.calls().filter(QRegularExpression("^sudo ")).isEmpty());
        QCOMPARE(backend.updates()->state(), QString("available"));

        // Coming back with the same version still installed changes nothing.
        backend.windowActivated();
        QTest::qWait(300);
        QCOMPARE(backend.updates()->state(), QString("available"));

        // The window notices the finished update when it gets focus back.
        s.command("pacman", "echo 'omarchy-control 0.2.0-1'");
        backend.windowActivated();
        QTRY_COMPARE(backend.updates()->state(), QString("restart"));
        QTRY_VERIFY(find("restartApp")->isVisible());
        QVERIFY(!find("checkUpdates")->isVisible());
        QTest::qWait(100);
        QSignalSpy quit(&engine, &QQmlEngine::quit);
        click("restartApp");
        QTRY_COMPARE(quit.size(), 1);
        QTRY_VERIFY(!s.calls().filter("omarchy-control open --page about").isEmpty());
    }
    void aCheckReportsWhenThereIsNothingToFind() {
        QCoreApplication::setApplicationVersion("0.1.0");
        Sandbox s;
        s.command("curl", "printf '{}\\n404'");
        CommandRunner runner(s.env);
        UpdateProvider updates(&runner);
        updates.check();
        QTRY_COMPARE(updates.state(), QString("none"));
        s.command("curl", "exit 6");
        updates.check();
        QTRY_COMPARE(updates.state(), QString("error"));
        QVERIFY(updates.error().contains("connection"));
        s.command("curl", "printf '{\"tag_name\":\"v0.1.0\"}\\n200'");
        updates.check();
        QTRY_COMPARE(updates.state(), QString("current"));
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

private slots:
    // --- The Desktop page ---

    void aDockPinShowsWhatItsDesktopEntrySays() {
        Sandbox s;
        IconDesk desk(s);
        const QString shared = s.env.dataDirs.last() + "/applications/";
        addPack(desk.system, "Yaru-blue", {"folder", "named-app"});
        addPack(desk.system, "Other", {"named-app"});
        addPack(desk.system, "hicolor", {"only-in-hicolor"});
        writeFile(shared + "named.desktop", "[Desktop Entry]\nName=Named App\nIcon=named-app\n[Desktop Action x]\nName=Not this\nIcon=not-this\n");
        writeFile(shared + "hicolor.desktop", "[Desktop Entry]\nName=H\nIcon=only-in-hicolor\n");
        writeFile(shared + "absolute.desktop", "[Desktop Entry]\nName=A\nIcon=" + s.dir.path().toUtf8() + "/pic.png\n");
        writeFile(s.dir.path() + "/pic.png", "x");
        writeFile(shared + "gone.desktop", "[Desktop Entry]\nName=G\nIcon=/nowhere/pic.png\n");
        writeFile(shared + "noicon.desktop", "[Desktop Entry]\nName=Plain\n");
        writeFile(shared + "pixmap.desktop", "[Desktop Entry]\nName=P\nIcon=oldapp.png\n");
        writeFile(s.env.dataDirs.last() + "/pixmaps/oldapp.png", "x");
        writeFile(shared + "mine.desktop", "[Desktop Entry]\nName=System copy\n");
        writeFile(s.env.dataHome + "/applications/mine.desktop", "[Desktop Entry]\nName=My copy\n");

        const auto app = [&](const QString &id) { return PluginsProvider::resolveDockApp(s.env, id, "Yaru-blue"); };
        QCOMPARE(app("named").value("name").toString(), QString("Named App"));
        QVERIFY(app("named").value("icon").toString().contains("/Yaru-blue/"));
        // The pack in use decides, as it does for every other app.
        QVERIFY(PluginsProvider::resolveDockApp(s.env, "named", "Other").value("icon").toString().contains("/Other/"));
        QVERIFY(app("hicolor").value("icon").toString().contains("/hicolor/"));
        QVERIFY(app("absolute").value("icon").toString().endsWith("/pic.png"));
        QVERIFY(app("pixmap").value("icon").toString().endsWith("/pixmaps/oldapp.png"));
        QCOMPARE(app("mine").value("name").toString(), QString("My copy"));
        // Each of these falls through to the same drawn fallback, with its entry's word on it.
        QCOMPARE(app("gone").value("icon").toString(), QString());
        QCOMPARE(app("noicon").value("icon").toString(), QString());
        QCOMPARE(app("noicon").value("name").toString(), QString("Plain"));
        QVERIFY(!app("not-installed").value("installed").toBool());
        QCOMPARE(app("../../etc/passwd").value("name").toString(), QString());
    }

    void dockAppsKeepTheirOrderAndUseThePackInUse() {
        Sandbox s;
        IconDesk desk(s);
        const QString shared = s.env.dataDirs.last() + "/applications/";
        writeFile(shared + "b.desktop", "[Desktop Entry]\nName=Bee\nIcon=folder\n");
        writeFile(shared + "a.desktop", "[Desktop Entry]\nName=Ay\nIcon=folder\n");
        writeFile(s.env.configHome + "/omarchy/shell.json", "{\"dock\":{\"pinned\":[\"b\",\"missing\",\"a\"]}}");
        desk.setEffective("Numix");
        ChangeCoordinator coordinator(s.env);
        CommandRunner runner(s.env);
        PluginsProvider plugins(&runner, &coordinator);
        plugins.refresh();
        QTRY_VERIFY(plugins.dockApps().value(0).toMap().value("icon").toString().contains("/Numix/"));
        QStringList names;
        for (const auto &row : plugins.dockApps())
            names << row.toMap().value("name").toString();
        QCOMPARE(names, QStringList({"Bee", "", "Ay"}));
        QCOMPARE(plugins.dockPins(), QStringList({"b", "missing", "a"}));
    }

    void aReorderThatDidNotHappenIsNotReportedAsDone() {
        Sandbox s;
        const QByteArray before = "{\"bar\":{\"layout\":{\"left\":[\"a\",\"b\",\"c\"]}}}\n";
        const QString config = s.env.configHome + "/omarchy/shell.json";
        writeFile(config, before);
        // Exits cleanly and changes nothing. The widget is still in its section, which is
        // all the old check looked at.
        s.omarchy("exit 0");
        ChangeCoordinator coordinator(s.env);
        CommandRunner runner(s.env);
        PluginsProvider plugins(&runner, &coordinator);
        QSignalSpy messages(&plugins, &PluginsProvider::message);
        plugins.moveWidget("a", "left", 2, "left", 0);
        QVERIFY(plugins.changing());
        QTRY_VERIFY_WITH_TIMEOUT(messages.count() >= 1, 8000);
        QVERIFY(messages.first().at(1).toBool());
        QVERIFY(messages.first().at(0).toString().contains("did not move"));
        QVERIFY(!plugins.changing());
        QCOMPARE(readAll(config), before);
    }

private:
    struct DesktopPage {
        Sandbox &s;
        Translator translator;
        Theme theme;
        Backend backend;
        QQmlApplicationEngine engine;
        QQuickWindow *window = nullptr;
        explicit DesktopPage(Sandbox &sandbox)
            : s(sandbox), translator(sandbox.env.configHome + "/omarchy/language", ":/i18n", sandbox.env.omarchyPath + "/default/i18n/locales.json"), theme(sandbox.env), backend(sandbox.env) {
            QQuickStyle::setStyle("Material");
            engine.addImageProvider("icons", new IconProvider);
            engine.addImageProvider("iconpacks", new IconPackImageProvider(sandbox.env));
            engine.rootContext()->setContextProperty("translator", &translator);
            engine.rootContext()->setContextProperty("theme", &theme);
            engine.rootContext()->setContextProperty("backend", &backend);
            engine.rootContext()->setContextProperty("initialPage", QString());
            engine.rootContext()->setContextProperty("initialSetting", QString());
            engine.load(QUrl("qrc:/Main.qml"));
            window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            window->resize(1427, 1102);
            window->requestActivate();
        }
        void show(const QString &focus = {}) {
            QMetaObject::invokeMethod(window, "go", Q_ARG(QVariant, "desktop"), Q_ARG(QVariant, focus));
        }
        QList<QQuickItem *> named(const QString &objectName) const {
            QList<QQuickItem *> found;
            std::function<void(QQuickItem *)> search = [&](QQuickItem *item) {
                if (item->objectName() == objectName && item->isVisible())
                    found << item;
                for (QQuickItem *child : item->childItems())
                    search(child);
            };
            search(window->contentItem());
            return found;
        }
        QQuickItem *chip(const QString &id) const {
            for (QQuickItem *item : named("barWidgetChip"))
                if (item->property("widgetId").toString() == id)
                    return item;
            return nullptr;
        }
        QQuickItem *handle(const QString &id) const {
            QQuickItem *c = chip(id);
            if (!c)
                return nullptr;
            for (QQuickItem *child : c->childItems())
                if (child->objectName() == "barWidgetHandle")
                    return child;
            return nullptr;
        }
        QStringList section(const QString &id) {
            QStringList ids;
            for (const auto &value : backend.plugins()->barLayout().value(id).toList())
                ids << value.toString();
            return ids;
        }
        QStringList moves() const {
            QStringList out;
            for (const QString &call : s.calls())
                if (call.startsWith("omarchy bar move"))
                    out << call.mid(17);
            return out;
        }
    };

    static void desktopFixture(Sandbox &s) {
        fakeDesktop(s);
        s.omarchy(kFakeBar);
        const QString state = s.env.home + "/bar-state/";
        writeFile(state + "left", "omarchy.menu omarchy.workspaces\n");
        writeFile(state + "centerBegin", "\n");
        writeFile(state + "center", "omarchy.clock\n");
        writeFile(state + "centerEnd", "omarchy.weather omarchy.system-update\n");
        writeFile(state + "right", "omarchy.tray omarchy.network omarchy.audio omarchy.power mystery.thing\n");
        writeFile(state + "tray", "\n");
        writeFile(state + "pins", "firefox gone org.gnome.Nautilus\n");
        writeFile(s.env.configHome + "/omarchy/shell.json", "{\"bar\":{\"id\":\"local.test-bar\",\"layout\":{\"left\":[\"omarchy.menu\",\"omarchy.workspaces\"],\"centerBegin\":[],\"center\":[{\"id\":\"omarchy.clock\"}],\"centerEnd\":[\"omarchy.weather\",\"omarchy.system-update\"],\"right\":[\"omarchy.tray\",\"omarchy.network\",\"omarchy.audio\",\"omarchy.power\",\"mystery.thing\"],\"tray\":[]}},\"dock\":{\"pinned\":[\"firefox\",\"gone\",\"org.gnome.Nautilus\"]}}\n");
        const QString shared = s.env.dataDirs.last();
        addPack(shared + "/icons", "Yaru-blue", {"web-browser", "folder"});
        writeFile(shared + "/applications/firefox.desktop", "[Desktop Entry]\nName=Firefox\nIcon=web-browser\n");
        writeFile(shared + "/applications/org.gnome.Nautilus.desktop", "[Desktop Entry]\nName=Files\nIcon=folder\n");
    }

private slots:
    void theBarEditorShowsTheModelItEdits() {
        Sandbox s;
        desktopFixture(s);
        DesktopPage page(s);
        QVERIFY(QTest::qWaitForWindowActive(page.window, 3000));
        QAccessible::setActive(true);
        page.show();
        QTRY_VERIFY(page.chip("omarchy.clock"));

        // Five regions in the preview, the same five the editor has, holding the same widgets.
        QTRY_COMPARE(page.named("barPreviewRegion").size(), 5);
        QStringList regions;
        for (QQuickItem *region : page.named("barPreviewRegion"))
            regions << region->property("regionId").toString();
        QCOMPARE(regions, QStringList({"left", "centerBegin", "center", "centerEnd", "right"}));
        QCOMPARE(page.named("barPreviewRegion").at(4)->property("regionItems").toList().size(), 5);
        QVERIFY(page.named("barPreview").size() == 1);
        // The tray drawer is off the bar, so only the editor has it, with its empty state.
        QVERIFY(!page.named("barSection_tray").isEmpty());

        // The name from the running plugin list, its glyph, and the plain one for the unknown.
        QTRY_COMPARE(page.chip("omarchy.menu")->property("widgetName").toString(), QString("Omarchy menu"));
        QCOMPARE(page.chip("omarchy.menu")->property("widgetIcon").toString(), QString("logo"));
        QCOMPARE(page.chip("mystery.thing")->property("widgetName").toString(), QString("Thing"));
        QCOMPARE(page.chip("mystery.thing")->property("widgetIcon").toString(), QString("plugin"));

        QTRY_VERIFY(!page.named("activeBar").isEmpty());
        QVERIFY(QAccessible::queryAccessibleInterface(page.named("activeBar").first())->text(QAccessible::Name).contains("local.test-bar"));

        // Every placement stays reachable from the dropdown, with the reorders added on.
        QCOMPARE(page.chip("omarchy.network")->property("actions").toList().size(), 8);
        QCOMPARE(page.chip("omarchy.tray")->property("actions").toList().size(), 7);
        QAccessible::setActive(false);
    }

    void theArrowKeysReorderAndKeepTheKeyboardThere() {
        Sandbox s;
        desktopFixture(s);
        DesktopPage page(s);
        QVERIFY(QTest::qWaitForWindowActive(page.window, 3000));
        page.show();
        QTRY_VERIFY(page.handle("omarchy.audio"));

        page.handle("omarchy.audio")->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(page.window, Qt::Key_Left);
        QTRY_COMPARE(page.section("right"), QStringList({"omarchy.tray", "omarchy.audio", "omarchy.network", "omarchy.power", "mystery.thing"}));
        QCOMPARE(page.moves().last(), QString("omarchy.audio --section right --index 1 --from-section right --from-index 2"));
        QTRY_VERIFY(page.window->activeFocusItem() == page.handle("omarchy.audio"));
        QTRY_VERIFY(!page.backend.plugins()->changing());

        // Omarchy takes it out before putting it in, so one along is one along.
        QTest::keyClick(page.window, Qt::Key_Right);
        QTRY_COMPARE(page.section("right").at(2), QString("omarchy.audio"));
        QCOMPARE(page.moves().last(), QString("omarchy.audio --section right --index 2 --from-section right --from-index 1"));
        QTRY_VERIFY(!page.backend.plugins()->changing());
        QTRY_VERIFY(page.window->activeFocusItem() == page.handle("omarchy.audio"));

        // Up goes to the end of the region before it.
        QTest::keyClick(page.window, Qt::Key_Up);
        QTRY_COMPARE(page.section("centerEnd"), QStringList({"omarchy.weather", "omarchy.system-update", "omarchy.audio"}));
        QVERIFY(!page.section("right").contains("omarchy.audio"));
        QTRY_VERIFY(!page.backend.plugins()->changing());
        QTRY_VERIFY(page.window->activeFocusItem() == page.handle("omarchy.audio"));

        // Nowhere further to go is not a change at all.
        const int count = page.moves().size();
        page.handle("omarchy.menu")->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(page.window, Qt::Key_Left);
        QTest::keyClick(page.window, Qt::Key_Up);
        QTest::qWait(100);
        QCOMPARE(page.moves().size(), count);

        // A press while one is landing would use positions about to change.
        page.handle("omarchy.tray")->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(page.window, Qt::Key_Right);
        QTest::keyClick(page.window, Qt::Key_Right);
        QTRY_VERIFY(!page.backend.plugins()->changing());
        QCOMPARE(page.moves().size(), count + 1);
    }

    void draggingAWidgetPlacesItWhereItIsDropped() {
        Sandbox s;
        desktopFixture(s);
        DesktopPage page(s);
        QVERIFY(QTest::qWaitForWindowActive(page.window, 3000));
        page.show();
        QTRY_VERIFY(page.handle("omarchy.tray"));
        QTest::qWait(150);
        const auto centre = [&](QQuickItem *item, qreal fx = 0.5) { return item->mapToScene(QPointF(item->width() * fx, item->height() / 2)).toPoint(); };
        const auto drag = [&](const QString &id, const QPoint &to) {
            const QPoint from = centre(page.handle(id));
            QTest::mousePress(page.window, Qt::LeftButton, Qt::NoModifier, from);
            for (int i = 1; i <= 6; ++i)
                QTest::mouseMove(page.window, from + (to - from) * i / 6);
            QVERIFY(!page.named("barDragGhost").isEmpty());
            QTest::mouseRelease(page.window, Qt::LeftButton, Qt::NoModifier, to);
        };

        // Further along its own region: dropped before power, which is index 2 once it has left.
        drag("omarchy.tray", centre(page.chip("omarchy.power"), 0.2));
        QTRY_COMPARE(page.section("right"), QStringList({"omarchy.network", "omarchy.audio", "omarchy.tray", "omarchy.power", "mystery.thing"}));
        QCOMPARE(page.moves().last(), QString("omarchy.tray --section right --index 2 --from-section right --from-index 0"));
        QTRY_VERIFY(!page.backend.plugins()->changing());
        QTRY_VERIFY(page.handle("omarchy.clock"));
        QTest::qWait(150);

        // Into another region, ahead of what is there.
        drag("omarchy.audio", centre(page.chip("omarchy.clock"), 0.2));
        QTRY_COMPARE(page.section("center"), QStringList({"omarchy.audio", "omarchy.clock"}));
        QTRY_VERIFY(!page.backend.plugins()->changing());
        QTest::qWait(150);

        // Into an empty one, which only has its label to aim at.
        QQuickItem *empty = page.named("barSection_tray").first();
        drag("omarchy.weather", empty->mapToScene(QPointF(empty->width() * 0.6, empty->height() / 2)).toPoint());
        QTRY_COMPARE(page.section("tray"), QStringList({"omarchy.weather"}));
        QTRY_VERIFY(!page.backend.plugins()->changing());
        QTest::qWait(150);

        // Back where it started, or out over nothing, or called off: none of them is a change.
        const int count = page.moves().size();
        const QPoint start = centre(page.handle("omarchy.network"));
        QTest::mousePress(page.window, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(page.window, start + QPoint(20, 0));
        QTest::mouseMove(page.window, centre(page.chip("omarchy.network"), 0.3));
        QTest::mouseRelease(page.window, Qt::LeftButton, Qt::NoModifier, centre(page.chip("omarchy.network"), 0.3));
        drag("omarchy.network", QPoint(page.window->width() - 4, 4));
        QTest::mousePress(page.window, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(page.window, start + QPoint(40, 0));
        QTest::keyClick(page.window, Qt::Key_Escape);
        QVERIFY(page.named("barDragGhost").isEmpty());
        QTest::mouseRelease(page.window, Qt::LeftButton, Qt::NoModifier, start + QPoint(40, 0));
        QTest::qWait(100);
        QCOMPARE(page.moves().size(), count);
    }

    void pinnedAppsShowIconsAndCanBeRemoved() {
        Sandbox s;
        desktopFixture(s);
        DesktopPage page(s);
        QVERIFY(QTest::qWaitForWindowActive(page.window, 3000));
        QAccessible::setActive(true);
        page.show();
        QTRY_COMPARE(page.named("dockApp").size(), 3);
        // In their order, the real icon where there is one and the same fallback where not.
        QCOMPARE(page.named("dockApp").at(0)->property("appName").toString(), QString("Firefox"));
        QCOMPARE(page.named("dockApp").at(1)->property("appName").toString(), QString("Gone"));
        QCOMPARE(page.named("dockApp").at(2)->property("appName").toString(), QString("Files"));
        QTRY_COMPARE(page.named("dockAppIcon").size(), 2);
        QCOMPARE(page.named("dockAppFallback").size(), 1);

        QQuickItem *remove = page.named("dockRemove").at(1);
        QCOMPARE(QAccessible::queryAccessibleInterface(remove)->text(QAccessible::Name), QString("Unpin Gone"));
        remove->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(page.window, Qt::Key_Space);
        QTRY_COMPARE(page.named("dockApp").size(), 2);
        QVERIFY(s.calls().contains("omarchy dock unpin gone"));
        // The keyboard moves to whichever took its place.
        QTRY_VERIFY(page.window->activeFocusItem() == page.named("dockRemove").at(1));
        QAccessible::setActive(false);
    }

    void theFoldedToolsCountAndOpenInPlace() {
        Sandbox s;
        desktopFixture(s);
        DesktopPage page(s);
        QVERIFY(QTest::qWaitForWindowActive(page.window, 3000));
        QAccessible::setActive(true);
        page.show();
        QTRY_VERIFY(!page.named("disclosure_plugins").isEmpty());
        QVERIFY(!page.named("disclosure_shortcuts").isEmpty());
        QVERIFY(!page.named("disclosure_rules").isEmpty());
        QTRY_VERIFY(QAccessible::queryAccessibleInterface(page.named("disclosure_plugins").first())->text(QAccessible::Name).contains("2"));

        // Nothing is built until it is opened, and it opens where it is.
        QQuickItem *header = page.named("disclosure_plugins").first();
        QCOMPARE(page.named("iconPackSearch").size(), 0);
        const qreal below = page.named("disclosure_shortcuts").first()->mapToScene(QPointF(0, 0)).y();
        header->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(page.window, Qt::Key_Space);
        QTRY_VERIFY(page.named("disclosure_shortcuts").first()->mapToScene(QPointF(0, 0)).y() > below + 40);
        QCOMPARE(page.window->activeFocusItem(), header);
        QTest::keyClick(page.window, Qt::Key_Space);
        QTRY_COMPARE(page.named("disclosure_shortcuts").first()->mapToScene(QPointF(0, 0)).y(), below);

        // Search reaches each one by name, opens it, and leaves the keyboard on it.
        page.show("block.rules");
        QTRY_VERIFY(page.window->activeFocusItem() == page.named("disclosure_rules").first());
        QAccessible::setActive(false);
    }

    // --- Icon packs ---

    void iconPacksComeFromEveryRootWithHiddenAndCursorOnesLeftOut() {
        Sandbox s;
        IconDesk desk(s);
        addPack(s.env.home + "/.icons", "Legacy");
        addPack(desk.user, "Mine");
        addPack(desk.system, "Secret", {"folder"}, {}, "Hidden=true\n");
        addPack(desk.system, "follow-theme");
        writeFile(desk.system + "/CursorOnly/index.theme", "[Icon Theme]\nName=Pointers\nInherits=Adwaita\n");
        writeFile(desk.system + "/LooseIcons/some.png", "x");
        writeFile(desk.system + "/Broken/index.theme", "no group here\n");
        QStringList ids;
        for (const IconPack &pack : IconThemes::discover(s.env))
            ids << pack.id;
        QCOMPARE(ids, QStringList({"follow-theme", "Legacy", "Mine", "Numix", "Papirus", "Yaru-blue"}));

        // What is in use stays visible, hidden or not, and a pack's name is not a mode.
        QVERIFY(std::any_of(IconThemes::discover(s.env, "Secret").cbegin(), IconThemes::discover(s.env, "Secret").cend(), [](const IconPack &p) { return p.id == "Secret"; }));
        AppCore core(s.env);
        QCOMPARE(core.engine.plan("appearance.icons", "follow-theme", {{QVariantMap{{"value", "pack:follow-theme"}}}, {QVariantMap{{"value", "follow-theme"}}}}).normalized, QString("follow-theme"));
        QCOMPARE(IconThemeProvider::normalizeChoice("follow-theme"), QString("follow-theme"));
        QCOMPARE(IconThemeProvider::normalizeChoice("pack:follow-theme"), QString("pack:follow-theme"));
        QCOMPARE(IconThemeProvider::normalizeChoice("Papirus"), QString("pack:Papirus"));
    }

    void aThemeSpreadAcrossRootsIsOneTheme() {
        Sandbox s;
        IconDesk desk(s);
        // The user's copy of a descriptor wins, and the system copy's folders still count.
        addPack(desk.user, "Papirus", {"folder"}, {}, "Comment=Mine\n");
        const IconPack pack = IconThemes::load(s.env, "Papirus");
        QCOMPARE(pack.comment, QString("Mine"));
        QCOMPARE(pack.locations.size(), 2);
        QVERIFY(pack.indexPath.startsWith(desk.user));
        QString from;
        QVERIFY(IconThemes::findIcon(s.env, "Papirus", {"utilities-terminal"}, 48, &from).startsWith(desk.system));
        QVERIFY(IconThemes::findIcon(s.env, "Papirus", {"folder"}, 48, &from).startsWith(desk.user));
        int seen = 0;
        for (const IconPack &each : IconThemes::discover(s.env))
            seen += each.id == "Papirus" ? 1 : 0;
        QCOMPARE(seen, 1);
    }

    void aPackNameIsTranslatedButItsIdIsNot() {
        const IconPack pack = IconThemes::parseIndex("[Icon Theme]\nName=Blue\nName[de]=Blau\nName[pt_BR]=Azul\nDirectories=a\n[a]\nSize=48\n", "the-id", "de_DE");
        QCOMPARE(pack.name, QString("Blau"));
        QCOMPARE(pack.id, QString("the-id"));
        QCOMPARE(IconThemes::parseIndex("[Icon Theme]\nName=Blue\nName[pt_BR]=Azul\n", "x", "pt_BR").name, QString("Azul"));
        QCOMPARE(IconThemes::parseIndex("[Icon Theme]\nName=Blue\n", "x", "fr_FR").name, QString("Blue"));
        QVERIFY(!IconThemes::parseIndex("Name=Blue\n", "x").found);
        // A pack does not get to point lookups outside itself.
        QVERIFY(IconThemes::parseIndex("[Icon Theme]\nDirectories=../../etc,/etc\n[../../etc]\nSize=48\n[/etc]\nSize=48\n", "x").directories.isEmpty());
    }

    void dataRootsFollowTheEnvironmentAndIgnoreRelativePaths() {
        const QByteArray home = qgetenv("XDG_DATA_HOME"), dirs = qgetenv("XDG_DATA_DIRS");
        qputenv("XDG_DATA_HOME", "/custom/data");
        qputenv("XDG_DATA_DIRS", "/a:relative/b:/c:/a");
        Env env = Env::system();
        QCOMPARE(env.dataHome, QString("/custom/data"));
        QCOMPARE(env.dataDirs, QStringList({"/a", "/c"}));
        QCOMPARE(IconThemes::searchRoots(env).first(), QString("/custom/data/icons"));
        qputenv("XDG_DATA_HOME", "not/absolute");
        qunsetenv("XDG_DATA_DIRS");
        env = Env::system();
        QCOMPARE(env.dataHome, env.home + "/.local/share");
        QCOMPARE(env.dataDirs, QStringList({"/usr/local/share", "/usr/share"}));
        home.isNull() ? qunsetenv("XDG_DATA_HOME") : qputenv("XDG_DATA_HOME", home);
        dirs.isNull() ? qunsetenv("XDG_DATA_DIRS") : qputenv("XDG_DATA_DIRS", dirs);
    }

    void aPreviewOnlyEverShowsThatPacksOwnChain() {
        Sandbox s;
        IconDesk desk(s);
        addPack(desk.system, "Parent", {"folder", "web-browser"});
        addPack(desk.system, "Child", {"folder"}, "Parent");
        addPack(desk.system, "Unrelated", {"secret-icon"});
        addPack(desk.system, "hicolor", {"last-resort"});
        QString from;
        QVERIFY(IconThemes::findIcon(s.env, "Child", {"folder"}, 48, &from).contains("/Child/"));
        QCOMPARE(from, QString("Child"));
        QVERIFY(IconThemes::findIcon(s.env, "Child", {"web-browser"}, 48, &from).contains("/Parent/"));
        QCOMPARE(from, QString("Parent"));
        QVERIFY(IconThemes::findIcon(s.env, "Child", {"secret-icon"}, 48, &from).isEmpty());
        QVERIFY(IconThemes::findIcon(s.env, "Child", {"last-resort"}, 48, &from).contains("/hicolor/"));

        // A family that inherits in a circle, or from something missing, still ends.
        addPack(desk.system, "LoopA", {"a"}, "LoopB,NotInstalled");
        addPack(desk.system, "LoopB", {"b"}, "LoopA");
        QVERIFY(IconThemes::findIcon(s.env, "LoopA", {"nothing"}, 48).isEmpty());
        QVERIFY(IconThemes::findIcon(s.env, "LoopA", {"b"}, 48).contains("/LoopB/"));
        QVERIFY(IconThemes::findIcon(s.env, "../Unrelated", {"secret-icon"}, 48).isEmpty());

        // The tile says so when nothing shown is really the pack's own.
        addPack(desk.system, "Sparse", {"unrelated"}, "Parent");
        AppCore core(s.env);
        QCOMPARE(core.icons.samples("Sparse").value("borrowedFrom").toString(), QString("Parent"));
        QCOMPARE(core.icons.samples("Child").value("borrowedFrom").toString(), QString());
        const auto samples = core.icons.samples("Child").value("samples").toList();
        QCOMPARE(samples.size(), 4);
        QVERIFY(samples.first().toMap().value("url").toString().startsWith("image://iconpacks/Child/folder/"));
    }

    void previewsDrawBoundedAndNeverTouchTheThemeInUse() {
        Sandbox s;
        IconDesk desk(s);
        addPack(desk.system, "A Pack With Spaces");
        IconPackImageProvider provider(s.env);
        QVERIFY(provider.flags() & QQmlImageProviderBase::ForceAsynchronousImageLoading);
        QSize size;
        QImage image = provider.requestImage("A%20Pack%20With%20Spaces/folder/1", &size, QSize(64, 64));
        QCOMPARE(size, QSize(64, 64));
        QCOMPARE(image.pixelColor(32, 32), QColor("#ff0000"));
        // An absurd request is limited, and a missing sample is an empty square, not a failure.
        QCOMPARE(provider.requestImage("Papirus/folder/1", &size, QSize(9000, 9000)).size(), QSize(192, 192));
        QCOMPARE(provider.requestImage("Papirus/nonsense/1", &size, QSize(32, 32)).pixelColor(16, 16).alpha(), 0);
        QCOMPARE(provider.requestImage("NotInstalled/folder/1", &size, QSize(32, 32)).pixelColor(16, 16).alpha(), 0);
        QByteArray huge = kSvg;
        huge.append(QByteArray(3 * 1024 * 1024, ' '));
        writeFile(desk.system + "/Papirus/scalable/apps/web-browser.svg", huge);
        QCOMPARE(provider.requestImage("Papirus/browser/1", &size, QSize(32, 32)).pixelColor(16, 16).alpha(), 0);
        QCOMPARE(desk.writes(), 0);
    }

    void gsettingsOutputIsReadAsAGVariantString() {
        bool ok = false;
        QCOMPARE(IconThemes::parseGVariantString("'Yaru-blue'\n", &ok), QString("Yaru-blue"));
        QVERIFY(ok);
        QCOMPARE(IconThemes::parseGVariantString("\"a b'c\"\n", &ok), QString("a b'c"));
        QVERIFY(ok);
        QCOMPARE(IconThemes::parseGVariantString("'it\\'s'", &ok), QString("it's"));
        QVERIFY(ok);
        IconThemes::parseGVariantString("@ms nothing", &ok);
        QVERIFY(!ok);
    }

    void iniEditsTouchOnlyTheValue() {
        QByteArray doc = "# keep me\r\n[Other]\ngtk-icon-theme-name = untouched\n\n[Settings]\ngtk-theme-name=X\n  gtk-icon-theme-name  =  Old Name  \r\n; gtk-icon-theme-name=comment\n";
        const auto read = FileOps::iniGet(doc, "Settings", "gtk-icon-theme-name");
        QVERIFY(read.found && !read.ambiguous);
        QCOMPARE(read.value, QString("Old Name"));
        QVERIFY(FileOps::iniSet(&doc, "Settings", "gtk-icon-theme-name", "New"));
        QCOMPARE(doc, QByteArray("# keep me\r\n[Other]\ngtk-icon-theme-name = untouched\n\n[Settings]\ngtk-theme-name=X\n  gtk-icon-theme-name  =  New  \r\n; gtk-icon-theme-name=comment\n"));
        QString error;
        QVERIFY(!FileOps::iniSet(&doc, "Settings", "absent", "x", &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!FileOps::iniSet(&doc, "NoGroup", "gtk-theme-name", "x"));
        QByteArray twice = "[A]\nk=1\nk=2\n";
        QVERIFY(FileOps::iniGet(twice, "A", "k").ambiguous);
        QVERIFY(!FileOps::iniSet(&twice, "A", "k", "3"));
        QCOMPARE(twice, QByteArray("[A]\nk=1\nk=2\n"));
    }

    void openingAppearanceChangesNothing() {
        Sandbox s;
        IconDesk desk(s);
        desk.setEffective("Papirus");
        AppCore core(s.env);
        const ReadResult read = readSetting(core.engine, "appearance.icons");
        QCOMPARE(read.status, QString("ok"));
        QCOMPARE(read.value, QString("pack:Papirus"));
        QCOMPARE(read.extra.value("mode").toString(), QString("unmanaged"));
        QVERIFY(read.reason.isEmpty());
        QCOMPARE(desk.writes(), 0);
        QVERIFY(!QFile::exists(desk.policy()));
        QVERIFY(!QFile::exists(desk.hook()));

        // With no choice saved, Omarchy's own value is simply following the theme.
        desk.setEffective("Yaru-blue");
        QCOMPARE(readSetting(core.engine, "appearance.icons").value, QString("follow-theme"));
        desk.themeAsks("Numix");
        QCOMPARE(readSetting(core.engine, "appearance.icons").value, QString("pack:Yaru-blue"));

        // A pack that is not installed is shown as it is, and not adopted or replaced.
        desk.setEffective("Yaru-gray");
        const ReadResult missing = readSetting(core.engine, "appearance.icons");
        QCOMPARE(missing.value, QString("pack:Yaru-gray"));
        QVERIFY(missing.reason.contains("not installed"));
        QCOMPARE(desk.writes(), 0);
    }

    void choosingAPackSavesAHookAndGoesBackExactly() {
        Sandbox s;
        IconDesk desk(s);
        AppCore core(s.env);
        QVERIFY(desk.isUnset());
        const ChangePlan plan = core.engine.plan("appearance.icons", "Papirus", {QVariantMap{{"value", "pack:Papirus"}}});
        QVERIFY(plan.ok);
        QCOMPARE(plan.normalized, QString("pack:Papirus"));
        QVERIFY(plan.files.contains(desk.hook()));
        QVERIFY(plan.effects.join(' ').contains("hook"));
        QVERIFY(!QFile::exists(desk.hook()));

        const Transaction tx = applySetting(core.engine, "appearance.icons", "pack:Papirus");
        QCOMPARE(tx.phase, QString("committed"));
        QCOMPARE(desk.effective(), QString("Papirus"));
        QCOMPARE(QJsonDocument::fromJson(readAll(desk.policy())).object().value("theme").toString(), QString("Papirus"));
        QCOMPARE(readAll(desk.hook()), IconThemeProvider::hookScript());
        QVERIFY(readAll(desk.hook()).contains("omarchy-control icons reapply"));
        QVERIFY(tx.undoable());
        const ReadResult after = readSetting(core.engine, "appearance.icons");
        QCOMPARE(after.value, QString("pack:Papirus"));
        QCOMPARE(after.extra.value("mode").toString(), QString("fixed"));

        // A second choice leaves the hook alone, and undoing it returns to the first.
        const Transaction second = applySetting(core.engine, "appearance.icons", "Numix");
        QCOMPARE(second.phase, QString("committed"));
        QCOMPARE(undoTx(core.engine, second.id).phase, QString("committed"));
        QCOMPARE(desk.effective(), QString("Papirus"));
        QVERIFY(QFile::exists(desk.hook()));

        // Undoing the first goes back to a key no one ever set, and removes what it added.
        const Transaction back = undoTx(core.engine, tx.id);
        QCOMPARE(back.phase, QString("committed"));
        QVERIFY(desk.isUnset());
        QVERIFY(!QFile::exists(desk.policy()));
        QVERIFY(!QFile::exists(desk.hook()));
        QVERIFY(s.calls().contains("gsettings reset org.gnome.desktop.interface icon-theme"));
        QCOMPARE(readSetting(core.engine, "appearance.icons").extra.value("mode").toString(), QString("unmanaged"));
    }

    void undoLeavesLaterChangesAlone() {
        Sandbox s;
        IconDesk desk(s);
        desk.setEffective("Numix");
        AppCore core(s.env);
        const Transaction tx = applySetting(core.engine, "appearance.icons", "Papirus");
        QCOMPARE(tx.phase, QString("committed"));

        desk.setEffective("Yaru-blue");
        const Transaction refused = undoTx(core.engine, tx.id);
        QCOMPARE(refused.phase, QString("failed"));
        QCOMPARE(desk.effective(), QString("Yaru-blue"));
        QVERIFY(QFile::exists(desk.hook()));

        // The saved choice was edited by someone: not this undo's to overwrite.
        desk.setEffective("Papirus");
        const QByteArray saved = readAll(desk.policy());
        writeFile(desk.policy(), QJsonDocument(QJsonObject{{"version", 1}, {"mode", "fixed"}, {"theme", "Numix"}}).toJson());
        QCOMPARE(undoTx(core.engine, tx.id).phase, QString("failed"));
        QCOMPARE(desk.effective(), QString("Papirus"));
        writeFile(desk.policy(), saved);

        QCOMPARE(undoTx(core.engine, tx.id).phase, QString("committed"));
        QCOMPARE(desk.effective(), QString("Numix"));
        QVERIFY(!QFile::exists(desk.hook()));
    }

    void anEditedHookDoesNotStopAnUndoAndIsKept() {
        Sandbox s;
        IconDesk desk(s);
        desk.setEffective("Numix");
        AppCore core(s.env);
        const Transaction tx = applySetting(core.engine, "appearance.icons", "Papirus");
        writeFile(desk.hook(), "#!/bin/bash\n# edited by hand\n");
        QCOMPARE(undoTx(core.engine, tx.id).phase, QString("committed"));
        QCOMPARE(desk.effective(), QString("Numix"));
        QCOMPARE(readAll(desk.hook()), QByteArray("#!/bin/bash\n# edited by hand\n"));
        QVERIFY(!QFile::exists(desk.policy()));
    }

    void undoingBackToFollowingUsesTheThemeInUseNow() {
        Sandbox s;
        IconDesk desk(s);
        desk.themeAsks("Numix");
        desk.setEffective("Numix");
        AppCore core(s.env);
        const Transaction tx = applySetting(core.engine, "appearance.icons", "Papirus");
        QCOMPARE(tx.before, QString("follow-theme"));
        // A theme change later, the pack from before it would be the wrong one to go back to.
        desk.themeAsks("Yaru-blue");
        QString message;
        QCOMPARE(core.icons.reapply(&message), 0);
        QCOMPARE(desk.effective(), QString("Papirus"));
        QCOMPARE(undoTx(core.engine, tx.id).phase, QString("committed"));
        QCOMPARE(desk.effective(), QString("Yaru-blue"));
        QCOMPARE(readSetting(core.engine, "appearance.icons").value, QString("follow-theme"));
        QVERIFY(readSetting(core.engine, "appearance.icons").reason.isEmpty());
    }

    void aFailedRollbackStillRestoresEverythingElse() {
        Sandbox s;
        IconDesk desk(s);
        desk.setEffective("Numix");
        // Sorted first, so stopping at this file would leave the choice and the hook behind.
        const QString gtk3 = s.env.configHome + "/gtk-3.0/settings.ini";
        writeFile(gtk3, "[Settings]\ngtk-icon-theme-name=Old\n");
        // GSettings ignores the write, so this fails after the file was already patched, and
        // by then something else has rewritten the file.
        writeFile(s.env.home + "/.fake-icon-theme-deaf", "");
        s.command("gsettings", QByteArray(kFakeGsettings) + "\n[ \"$1\" = get ] && [ -f '" + gtk3.toUtf8() + "' ] && grep -q Papirus '" + gtk3.toUtf8() + "' && printf '[Settings]\\ngtk-icon-theme-name=TheirEdit\\n' > '" + gtk3.toUtf8() + "'\nexit 0\n");
        AppCore core(s.env);
        const Transaction tx = applySetting(core.engine, "appearance.icons", "Papirus");
        QCOMPARE(tx.phase, QString("partial"));
        QVERIFY(tx.message.contains("left as it is"));
        QVERIFY(readAll(gtk3).contains("TheirEdit"));
        QVERIFY(!QFile::exists(desk.policy()));
        QVERIFY(!QFile::exists(desk.hook()));
    }

    void aChangeBetweenPlanningAndTheLockIsNotRecordedAsTheWayBack() {
        Sandbox s;
        IconDesk desk(s);
        desk.setEffective("Numix");
        AppCore core(s.env);
        // Two picks in a row, the second planned before the first has run.
        Transaction first, second;
        bool firstDone = false, secondDone = false;
        core.icons.submit("pack:Papirus", "set", "Icon pack", [&](const Transaction &tx) { first = tx; firstDone = true; });
        core.icons.submit("pack:Yaru-blue", "set", "Icon pack", [&](const Transaction &tx) { second = tx; secondDone = true; });
        QTRY_VERIFY(firstDone && secondDone);
        const QStringList phases{first.phase, second.phase};
        QVERIFY2(phases.count("committed") >= 1, qPrintable(phases.join(' ')));
        // Whichever ran second, its way back is what was really there when it ran.
        const Transaction &last = second.phase == QLatin1String("committed") ? second : first;
        const QString wayBack = last.extra.value("gsettings").toObject().value("before").toString();
        QVERIFY(first.phase != QLatin1String("committed") || second.phase != QLatin1String("committed") || wayBack == QLatin1String("Papirus"));
        QCOMPARE(undoTx(core.engine, last.id).phase, QString("committed"));
        QCOMPARE(desk.effective(), wayBack);
    }

    void aFailedPickKeepsAStandoffInPlace() {
        Sandbox s;
        IconDesk desk(s);
        AppCore core(s.env);
        QCOMPARE(applySetting(core.engine, "appearance.icons", "Papirus").phase, QString("committed"));
        QString message;
        for (int i = 0; i < 3; ++i) {
            desk.setEffective("Their-Pack");
            core.icons.reapply(&message);
        }
        QVERIFY(readSetting(core.engine, "appearance.icons").extra.value("suspended").toBool());
        writeFile(s.env.home + "/.fake-icon-theme-deaf", "");
        QCOMPARE(applySetting(core.engine, "appearance.icons", "Numix").phase, QString("rolled-back"));
        QVERIFY(readSetting(core.engine, "appearance.icons").extra.value("suspended").toBool());
    }

    void anOlderSwitchsHookLeavesItToTheNewerOne() {
        Sandbox s;
        IconDesk desk(s);
        AppCore core(s.env);
        QCOMPARE(applySetting(core.engine, "appearance.icons", "Papirus").phase, QString("committed"));
        writeFile(s.env.stateHome + "/omarchy/current/theme.name", "newer\n");
        desk.setEffective("Numix");
        const int writes = desk.writes();
        QString message;
        // However often this happens it is not another icon manager, and it writes nothing.
        for (int i = 0; i < 5; ++i)
            QCOMPARE(core.icons.reapply(&message, "older"), 0);
        QCOMPARE(desk.writes(), writes);
        QVERIFY(!readSetting(core.engine, "appearance.icons").extra.value("suspended").toBool());
        QVERIFY(readAll(IconThemeProvider(&core.runner, &core.coordinator).hookPath()).contains("reapply \"$1\""));
        QCOMPARE(core.icons.reapply(&message, "newer"), 0);
        QCOMPARE(desk.effective(), QString("Papirus"));
    }

    void aThemeChangeLeavesAHandEditedConfigAlone() {
        Sandbox s;
        IconDesk desk(s);
        const QString gtk3 = s.env.configHome + "/gtk-3.0/settings.ini";
        writeFile(gtk3, "[Settings]\ngtk-icon-theme-name=Old\n");
        AppCore core(s.env);
        QCOMPARE(applySetting(core.engine, "appearance.icons", "Papirus").phase, QString("committed"));
        QCOMPARE(readAll(gtk3), QByteArray("[Settings]\ngtk-icon-theme-name=Papirus\n"));
        writeFile(gtk3, "[Settings]\ngtk-icon-theme-name=Mine\n");
        desk.setEffective("Numix");
        QString message;
        QCOMPARE(core.icons.reapply(&message), 0);
        QCOMPARE(desk.effective(), QString("Papirus"));
        QCOMPARE(readAll(gtk3), QByteArray("[Settings]\ngtk-icon-theme-name=Mine\n"));
        QVERIFY(readSetting(core.engine, "appearance.icons").extra.value("mixed").toBool());
        // A deliberate choice is what brings it back into line.
        QCOMPARE(applySetting(core.engine, "appearance.icons", "Papirus").phase, QString("committed"));
        QCOMPARE(readAll(gtk3), QByteArray("[Settings]\ngtk-icon-theme-name=Papirus\n"));
    }

    void anImportDoesNotHoldUpOtherChanges() {
        Sandbox s;
        IconDesk desk(s);
        const QString source = s.dir.path() + "/in/Long";
        addPack(s.dir.path() + "/in", "Long");
        const QByteArray bulk(2 * 1024 * 1024, 'x');
        for (int i = 0; i < 60; ++i)
            writeFile(source + "/scalable/apps/bulk" + QString::number(i) + ".svg", bulk);
        AppCore core(s.env);
        IconThemeImporter importer(s.env, &core.coordinator);
        QSignalSpy finished(&importer, &IconThemeImporter::finished);
        importer.choosePath(source);
        QVERIFY(importer.busy());
        // The theme hook's lock is free the whole time, and a second import is told to wait.
        QLockFile change(s.env.appStateDir + "/change.lock");
        QVERIFY(change.tryLock(0));
        change.unlock();
        IconThemeImporter second(s.env, &core.coordinator);
        addPack(s.dir.path() + "/in", "Second");
        QCOMPARE(importPack(second, s.dir.path() + "/in/Second"), QString("failed"));
        QVERIFY(second.message().contains("Another icon pack"));
        // And a new window does not take a live staging folder for a leftover.
        QVERIFY(second.recoverStaging().isEmpty());
        QVERIFY(finished.count() || finished.wait(30000));
        QCOMPARE(importer.state(), QString("imported"));
        QVERIFY(QFile::exists(source + "/scalable/apps/bulk59.svg"));
        QCOMPARE(QFileInfo(desk.user + "/Long/scalable/apps/bulk59.svg").size(), qint64(bulk.size()));
    }

    void aHiddenPackImportsButIsNotOfferedAsAChoice() {
        Sandbox s;
        IconDesk desk(s);
        addPack(s.dir.path() + "/in", "Base", {"folder"}, {}, "Hidden=true\n");
        AppCore core(s.env);
        IconThemeImporter importer(s.env, &core.coordinator);
        QCOMPARE(importPack(importer, s.dir.path() + "/in/Base"), QString("imported"));
        QVERIFY(!importer.usable());
        QVERIFY(importer.message().contains("hidden"));
        addPack(s.dir.path() + "/in", "Shown");
        QCOMPARE(importPack(importer, s.dir.path() + "/in/Shown"), QString("imported"));
        QVERIFY(importer.usable());
    }

    void anEditedHookIsKeptAndExplained() {
        Sandbox s;
        IconDesk desk(s);
        writeFile(desk.hook(), "#!/bin/bash\n# mine\n");
        AppCore core(s.env);
        QCOMPARE(applySetting(core.engine, "appearance.icons", "Papirus").phase, QString("committed"));
        QCOMPARE(readAll(desk.hook()), QByteArray("#!/bin/bash\n# mine\n"));
        const ReadResult read = readSetting(core.engine, "appearance.icons");
        QCOMPARE(read.extra.value("hook").toString(), QString("edited"));
        QVERIFY(read.reason.contains("edited"));
    }

    void aFailedChangeLeavesEverythingAsItWas() {
        Sandbox s;
        IconDesk desk(s);
        desk.setEffective("Numix");
        AppCore core(s.env);

        writeFile(s.env.home + "/.fake-icon-theme-broken", "");
        Transaction tx = applySetting(core.engine, "appearance.icons", "Papirus");
        QCOMPARE(tx.phase, QString("rolled-back"));
        QCOMPARE(desk.effective(), QString("Numix"));
        QVERIFY(!QFile::exists(desk.policy()));
        QVERIFY(!QFile::exists(desk.hook()));
        QFile::remove(s.env.home + "/.fake-icon-theme-broken");

        // A write that reports success without changing anything is caught by reading back.
        writeFile(s.env.home + "/.fake-icon-theme-deaf", "");
        tx = applySetting(core.engine, "appearance.icons", "Papirus");
        QCOMPARE(tx.phase, QString("rolled-back"));
        QVERIFY(tx.message.contains("Numix"));
        QVERIFY(!QFile::exists(desk.policy()));
        QVERIFY(!QFile::exists(desk.hook()));
        QFile::remove(s.env.home + "/.fake-icon-theme-deaf");

        // A pack that is not on offer never reaches a write.
        const int before = desk.writes();
        QCOMPARE(applySetting(core.engine, "appearance.icons", "pack:NotInstalled").phase, QString("failed"));
        QCOMPARE(desk.writes(), before);
    }

    void aMissingCommandKeepsPreviewsButStopsChanges() {
        Sandbox s;
        IconDesk desk(s);
        QFile::remove(s.bin("gsettings"));
        AppCore core(s.env);
        QCOMPARE(readSetting(core.engine, "appearance.icons").status, QString("unavailable"));
        QVERIFY(!core.engine.plan("appearance.icons", "follow-theme").ok);
        QCOMPARE(core.icons.samples("Papirus").value("samples").toList().size(), 4);
    }

    void configFilesAreOnlyTouchedWhenTheyAlreadyNameAnIconTheme() {
        Sandbox s;
        IconDesk desk(s);
        const QString gtk3 = s.env.configHome + "/gtk-3.0/settings.ini";
        const QString gtk4 = s.env.configHome + "/gtk-4.0/settings.ini";
        const QString kde = s.env.configHome + "/kdeglobals";
        const QString qt6 = s.env.configHome + "/qt6ct/qt6ct.conf";
        const QByteArray gtk3Text = "# mine\n[Settings]\ngtk-icon-theme-name=Old\ngtk-theme-name=Adwaita\n";
        const QByteArray gtk4Text = "[Settings]\ngtk-theme-name=Adwaita\n";
        writeFile(gtk3, gtk3Text);
        writeFile(gtk4, gtk4Text);
        writeFile(kde, "[Icons]\nTheme=Old\n[General]\nName=x\n");
        // Qt is on the gtk3 platform theme, as Omarchy sets it, so qt6ct has no say.
        writeFile(qt6, "[Appearance]\nicon_theme=Old\n");
        AppCore core(s.env);
        desk.setEffective("Numix");
        const ReadResult mixed = readSetting(core.engine, "appearance.icons");
        QVERIFY(mixed.extra.value("mixed").toBool());
        QVERIFY(mixed.extra.value("reconcile").toBool());
        QVERIFY(mixed.reason.contains("GTK 3"));

        const Transaction tx = applySetting(core.engine, "appearance.icons", "Papirus");
        QCOMPARE(tx.phase, QString("committed"));
        QCOMPARE(readAll(gtk3), QByteArray("# mine\n[Settings]\ngtk-icon-theme-name=Papirus\ngtk-theme-name=Adwaita\n"));
        QCOMPARE(readAll(gtk4), gtk4Text);
        QCOMPARE(readAll(kde), QByteArray("[Icons]\nTheme=Papirus\n[General]\nName=x\n"));
        QCOMPARE(readAll(qt6), QByteArray("[Appearance]\nicon_theme=Old\n"));
        QVERIFY(!QFile::exists(s.env.configHome + "/qt5ct/qt5ct.conf"));
        QVERIFY(readSetting(core.engine, "appearance.icons").reason.isEmpty());

        QCOMPARE(undoTx(core.engine, tx.id).phase, QString("committed"));
        QCOMPARE(readAll(gtk3), gtk3Text);
        QCOMPARE(desk.effective(), QString("Numix"));

        // When Qt is told to use it, it counts.
        Env qtEnv = s.env;
        qtEnv.process.insert("QT_QPA_PLATFORMTHEME", "qt6ct");
        AppCore qtCore(qtEnv);
        QCOMPARE(applySetting(qtCore.engine, "appearance.icons", "Papirus").phase, QString("committed"));
        QCOMPARE(readAll(qt6), QByteArray("[Appearance]\nicon_theme=Papirus\n"));
    }

    void anAmbiguousConfigIsReportedAndLeftAlone() {
        Sandbox s;
        IconDesk desk(s);
        const QString gtk3 = s.env.configHome + "/gtk-3.0/settings.ini";
        const QByteArray text = "[Settings]\ngtk-icon-theme-name=A\ngtk-icon-theme-name=B\n";
        writeFile(gtk3, text);
        AppCore core(s.env);
        QVERIFY(core.engine.plan("appearance.icons", "follow-theme", {QVariantMap{{"value", "follow-theme"}}}).effects.join(' ').contains("more than once"));
        QCOMPARE(applySetting(core.engine, "appearance.icons", "Papirus").phase, QString("committed"));
        QCOMPARE(readAll(gtk3), text);
    }

    void aSymlinkedConfigStaysALink() {
        Sandbox s;
        IconDesk desk(s);
        const QString target = s.env.home + "/dotfiles/settings.ini";
        const QString link = s.env.configHome + "/gtk-3.0/settings.ini";
        writeFile(target, "[Settings]\ngtk-icon-theme-name=Old\n");
        QDir().mkpath(QFileInfo(link).absolutePath());
        QFile::link(target, link);
        AppCore core(s.env);
        QCOMPARE(applySetting(core.engine, "appearance.icons", "Papirus").phase, QString("committed"));
        QVERIFY(QFileInfo(link).isSymLink());
        QCOMPARE(readAll(target), QByteArray("[Settings]\ngtk-icon-theme-name=Papirus\n"));
    }

    void changesElsewhereAreShownAndNeverForcedBack() {
        Sandbox s;
        IconDesk desk(s);
        AppCore core(s.env);
        QCOMPARE(applySetting(core.engine, "appearance.icons", "Papirus").phase, QString("committed"));
        const int writes = desk.writes();
        desk.setEffective("Numix");
        const ReadResult read = readSetting(core.engine, "appearance.icons");
        QCOMPARE(read.value, QString("pack:Papirus"));
        QVERIFY(read.extra.value("conflict").toBool());
        QVERIFY(read.reason.contains("Numix"));
        QCOMPARE(desk.effective(), QString("Numix"));
        QCOMPARE(desk.writes(), writes);

        // A chosen pack that goes away is remembered, and explained, not replaced.
        QDir(desk.system + "/Papirus").removeRecursively();
        const ReadResult gone = readSetting(core.engine, "appearance.icons");
        QCOMPARE(gone.value, QString("pack:Papirus"));
        QVERIFY(gone.reason.contains("not installed any more"));
        QVERIFY(readAll(desk.policy()).contains("Papirus"));
    }

    void followingTheThemeClearsTheFixedChoice() {
        Sandbox s;
        IconDesk desk(s);
        desk.themeAsks("Numix");
        desk.setEffective("Numix");
        AppCore core(s.env);
        QCOMPARE(applySetting(core.engine, "appearance.icons", "Papirus").phase, QString("committed"));
        desk.themeAsks("Yaru-blue");
        // Not the pack captured earlier, but what the theme in use asks for now.
        QCOMPARE(applySetting(core.engine, "appearance.icons", "follow-theme").phase, QString("committed"));
        QCOMPARE(desk.effective(), QString("Yaru-blue"));
        const QJsonObject policy = QJsonDocument::fromJson(readAll(desk.policy())).object();
        QCOMPARE(policy.value("mode").toString(), QString("follow-theme"));
        QVERIFY(!policy.contains("theme"));
        QVERIFY(QFile::exists(desk.hook()));
        QCOMPARE(readSetting(core.engine, "appearance.icons").value, QString("follow-theme"));

        // Use default means the same thing here.
        QCOMPARE(applySetting(core.engine, "appearance.icons", "Papirus").phase, QString("committed"));
        Transaction reset;
        bool done = false;
        core.engine.reset("appearance.icons", [&](const Transaction &tx) { reset = tx; done = true; });
        QTRY_VERIFY(done);
        QCOMPARE(reset.phase, QString("committed"));
        QCOMPARE(reset.kind, QString("reset"));
        QCOMPARE(desk.effective(), QString("Yaru-blue"));
    }

    void aThemeChangeKeepsAFixedChoiceWithNoWindowOpen() {
        Sandbox s;
        IconDesk desk(s);
        AppCore core(s.env);
        // Nothing chosen here: what Omarchy set stands, and nothing is even read.
        QString message;
        desk.setEffective("Numix");
        QCOMPARE(core.icons.reapply(&message), 0);
        QCOMPARE(desk.effective(), QString("Numix"));
        QCOMPARE(desk.writes(), 0);

        QCOMPARE(applySetting(core.engine, "appearance.icons", "Papirus").phase, QString("committed"));
        // Omarchy's theme change writes the theme's pack, then runs the hook.
        desk.themeAsks("Numix");
        desk.setEffective("Numix");
        QCOMPARE(Cli::run(s.env, {"icons", "reapply"}), 0);
        QCOMPARE(desk.effective(), QString("Papirus"));

        // Already right: no write at all.
        const int writes = desk.writes();
        QCOMPARE(core.icons.reapply(&message), 0);
        QCOMPARE(desk.writes(), writes);

        // A pack that was removed is left out of it, with the choice kept.
        QDir(desk.system + "/Papirus").removeRecursively();
        desk.setEffective("Numix");
        QCOMPARE(core.icons.reapply(&message), 0);
        QVERIFY(message.contains("not installed"));
        QCOMPARE(desk.effective(), QString("Numix"));
        QVERIFY(readAll(desk.policy()).contains("Papirus"));
    }

    void followingKeepsUpWithTheThemeWithNoWindowOpen() {
        Sandbox s;
        IconDesk desk(s);
        const QString gtk3 = s.env.configHome + "/gtk-3.0/settings.ini";
        writeFile(gtk3, "[Settings]\ngtk-icon-theme-name=Old\n");
        AppCore core(s.env);
        QCOMPARE(applySetting(core.engine, "appearance.icons", "follow-theme").phase, QString("committed"));
        // Stock Omarchy does not update this file, which is what the hook is still there for.
        desk.themeAsks("Numix");
        desk.setEffective("Numix");
        QString message;
        QCOMPARE(core.icons.reapply(&message), 0);
        QCOMPARE(readAll(gtk3), QByteArray("[Settings]\ngtk-icon-theme-name=Numix\n"));
        QCOMPARE(desk.effective(), QString("Numix"));
    }

    void anotherIconManagerEndsAStandoffNotAFight() {
        Sandbox s;
        IconDesk desk(s);
        AppCore core(s.env);
        QCOMPARE(applySetting(core.engine, "appearance.icons", "Papirus").phase, QString("committed"));
        QString message;
        // The first two look like coincidence and are corrected.
        for (int i = 0; i < 2; ++i) {
            desk.setEffective("Their-Pack");
            QCOMPARE(core.icons.reapply(&message), 0);
            QCOMPARE(desk.effective(), QString("Papirus"));
        }
        const int writes = desk.writes();
        desk.setEffective("Their-Pack");
        QCOMPARE(core.icons.reapply(&message), 0);
        QVERIFY(message.contains("another icon manager"));
        QCOMPARE(desk.effective(), QString("Their-Pack"));
        QCOMPARE(desk.writes(), writes);
        const ReadResult read = readSetting(core.engine, "appearance.icons");
        QVERIFY(read.extra.value("suspended").toBool());
        QVERIFY(read.reason.contains("no longer reapplied"));
        // It stays out of it until someone chooses, and that settles it.
        message.clear();
        QCOMPARE(core.icons.reapply(&message), 0);
        QCOMPARE(desk.effective(), QString("Their-Pack"));
        QCOMPARE(applySetting(core.engine, "appearance.icons", "Papirus").phase, QString("committed"));
        QVERIFY(!readSetting(core.engine, "appearance.icons").extra.value("suspended").toBool());
        desk.setEffective("Numix");
        QCOMPARE(core.icons.reapply(&message), 0);
        QCOMPARE(desk.effective(), QString("Papirus"));
    }

    void reapplyDoesNotRaceAChangeInProgress() {
        Sandbox s;
        IconDesk desk(s);
        AppCore core(s.env);
        QCOMPARE(applySetting(core.engine, "appearance.icons", "Papirus").phase, QString("committed"));
        desk.setEffective("Numix");
        // Another process is mid-change. Its lock has a long wait, so a stale reader is
        // what this guards against: the reapply reads the choice only once it has the lock.
        writeFile(desk.policy(), QJsonDocument(QJsonObject{{"version", 1}, {"mode", "fixed"}, {"theme", "Yaru-blue"}}).toJson());
        QString message;
        QCOMPARE(core.icons.reapply(&message), 0);
        QCOMPARE(desk.effective(), QString("Yaru-blue"));
    }

    void removingTheIntegrationLeavesTheIconsAsTheyAre() {
        Sandbox s;
        IconDesk desk(s);
        AppCore core(s.env);
        QCOMPARE(applySetting(core.engine, "appearance.icons", "Papirus").phase, QString("committed"));
        QCOMPARE(Cli::run(s.env, {"icons", "remove-integration"}), 0);
        QVERIFY(!QFile::exists(desk.hook()));
        QVERIFY(!QFile::exists(desk.policy()));
        QCOMPARE(desk.effective(), QString("Papirus"));

        writeFile(desk.hook(), "#!/bin/bash\n# mine\n");
        QString message;
        QCOMPARE(core.icons.removeIntegration(&message), 0);
        QVERIFY(QFile::exists(desk.hook()));
        QVERIFY(message.contains("edited"));
    }

    void aDamagedChoiceIsReportedAndIgnored() {
        Sandbox s;
        IconDesk desk(s);
        for (const QByteArray &bad : {QByteArray("not json"), QByteArray("{\"version\":99,\"mode\":\"fixed\",\"theme\":\"X\"}"), QByteArray("{\"version\":1,\"mode\":\"fixed\",\"theme\":\"../x\"}")}) {
            writeFile(desk.policy(), bad);
            AppCore core(s.env);
            desk.setEffective("Numix");
            const ReadResult read = readSetting(core.engine, "appearance.icons");
            QCOMPARE(read.extra.value("mode").toString(), QString("unmanaged"));
            QVERIFY(read.reason.contains("could not be read"));
            QString message;
            QCOMPARE(core.icons.reapply(&message), 0);
            QCOMPARE(desk.effective(), QString("Numix"));
            QCOMPARE(readAll(desk.policy()), bad);
        }
    }

    void aPausedOmarchyPausesTheHookToo() {
        Sandbox s;
        IconDesk desk(s);
        {
            AppCore core(s.env);
            QCOMPARE(applySetting(core.engine, "appearance.icons", "Papirus").phase, QString("committed"));
        }
        writeFile(s.bin("omarchy-theme-set-gnome"), "#!/bin/bash\n# not what was reviewed\n");
        QFile::setPermissions(s.bin("omarchy-theme-set-gnome"), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        desk.setEffective("Numix");
        Env strict = s.env;
        strict.enforceCompat = true;
        AppCore core(strict);
        const CatalogEntry *entry = core.catalog.find("appearance.icons");
        QVERIFY(entry);
        QCOMPARE(core.compat.changedFor(*entry, {}), QString("omarchy-theme-set-gnome"));
        QVERIFY(!core.engine.plan("appearance.icons", "Papirus", {QVariantMap{{"value", "pack:Papirus"}}}).ok);
        QCOMPARE(Cli::run(strict, {"icons", "reapply"}), 0);
        QCOMPARE(desk.effective(), QString("Numix"));
    }

    void aStoppedChangeIsReportedAndNotBlindlyRestored() {
        Sandbox s;
        IconDesk desk(s);
        QString firstId;
        {
            AppCore core(s.env);
            const Transaction tx = applySetting(core.engine, "appearance.icons", "Papirus");
            QCOMPARE(tx.phase, QString("committed"));
            firstId = tx.id;
            QVERIFY(!tx.extra.isEmpty());
        }
        // The journal keeps what files cannot: whether the key had ever been set.
        AppCore core(s.env);
        QCOMPARE(core.coordinator.transaction(firstId).extra.value("gsettings").toObject().value("beforeUnset").toBool(), true);
        Transaction stuck = core.coordinator.transaction(firstId);
        stuck.id = "stoppedhere0";
        stuck.phase = "applying";
        QFile journal(core.coordinator.journalPath());
        QVERIFY(journal.open(QIODevice::Append));
        journal.write(QJsonDocument(stuck.toJson()).toJson(QJsonDocument::Compact) + "\n");
        journal.close();
        const int writes = desk.writes();
        QCOMPARE(core.coordinator.recoverInterrupted().size(), 1);
        QCOMPARE(core.coordinator.transaction("stoppedhere0").phase, QString("interrupted"));
        QCOMPARE(desk.writes(), writes);
        QCOMPARE(desk.effective(), QString("Papirus"));

        // A record with nothing to go back with says so instead of guessing.
        Transaction old = core.coordinator.transaction(firstId);
        old.id = "oldrecord000";
        old.phase = "committed";
        old.extra = {};
        QVERIFY(journal.open(QIODevice::Append));
        journal.write(QJsonDocument(old.toJson()).toJson(QJsonDocument::Compact) + "\n");
        journal.close();
        QCOMPARE(undoTx(core.engine, "oldrecord000").phase, QString("failed"));
    }

    void theCliGivesIconStateItsStructure() {
        Sandbox s;
        IconDesk desk(s);
        addPack(desk.system, "Orphan", {"folder"}, "NotInstalled");
        AppCore core(s.env);
        QStringList problems;
        const QVariantList packs = core.icons.listJson(&problems);
        QVERIFY(std::any_of(packs.cbegin(), packs.cend(), [](const QVariant &p) { return p.toMap().value("id") == "Papirus"; }));
        QVERIFY(problems.join('\n').contains("Orphan: inherits NotInstalled"));
        QCOMPARE(Cli::run(s.env, {"icons", "list", "--json"}), 0);
        QCOMPARE(Cli::run(s.env, {"get", "appearance.icons", "--json"}), 0);
        QCOMPARE(Cli::run(s.env, {"plan", "appearance.icons", "Papirus"}), 0);
        QCOMPARE(Cli::run(s.env, {"apply", "appearance.icons", "Papirus"}), 0);
        QCOMPARE(desk.effective(), QString("Papirus"));
        QCOMPARE(Cli::run(s.env, {"apply", "appearance.icons", "NotInstalled"}), 1);
        QCOMPARE(Cli::run(s.env, {"icons", "nonsense"}), 1);
    }

    // --- Importing a pack ---

    void importingCopiesAPackAndLeavesTheSourceAlone() {
        Sandbox s;
        IconDesk desk(s);
        const QString source = s.dir.path() + "/downloads/Fresh";
        addPack(s.dir.path() + "/downloads", "Fresh");
        writeFile(source + "/LICENSE", "free");
        writeFile(source + "/install.sh", "#!/bin/sh\ntouch " + s.dir.path().toUtf8() + "/ran\n");
        QFile::setPermissions(source + "/install.sh", QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        writeFile(source + "/.git/config", "x");
        QVERIFY(QFile::link("folder.svg", source + "/scalable/apps/inode-directory.svg"));
        QDir().mkpath(source + "/scalable/places");
        QVERIFY(QFile::link("../apps/folder.svg", source + "/scalable/places/folder.svg"));
        const QStringList before = QDir(source).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);

        AppCore core(s.env);
        IconThemeImporter importer(s.env, &core.coordinator);
        QCOMPARE(importPack(importer, source), QString("imported"));
        QCOMPARE(importer.themeId(), QString("Fresh"));
        const QString copy = desk.user + "/Fresh";
        QVERIFY(QFile::exists(copy + "/index.theme"));
        QCOMPARE(readAll(copy + "/LICENSE"), QByteArray("free"));
        QCOMPARE(readAll(copy + "/scalable/apps/folder.svg"), QByteArray(kSvg));
        QVERIFY(QFileInfo(copy + "/scalable/places/folder.svg").isSymLink());
        QCOMPARE(QFile::symLinkTarget(copy + "/scalable/places/folder.svg"), QFileInfo(copy + "/scalable/apps/folder.svg").absoluteFilePath());
        QVERIFY(!QFile::exists(copy + "/.git"));
        QVERIFY(!QFile::exists(copy + "/.omarchy-control-import.json"));
        // Nothing from a pack can be run: not by the import, and not by accident later.
        QVERIFY(!QFile::exists(s.dir.path() + "/ran"));
        QVERIFY(!(QFile::permissions(copy + "/install.sh") & (QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther)));
        QCOMPARE(QDir(source).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot), before);
        QVERIFY(QFile::exists(source + "/.git/config"));
        QCOMPARE(QDir(desk.user).entryList(QDir::AllDirs | QDir::Hidden | QDir::NoDotAndDotDot), QStringList({"Fresh"}));

        // Importing never selects, and it is on record with no pretense of undo.
        QCOMPARE(desk.writes(), 0);
        QVERIFY(desk.isUnset());
        const Transaction tx = core.coordinator.history(1).first();
        QCOMPARE(tx.settingId, QString("appearance.icons.import"));
        QCOMPARE(tx.phase, QString("committed"));
        QVERIFY(!tx.undoable());
        QVERIFY(std::any_of(IconThemes::discover(s.env).cbegin(), IconThemes::discover(s.env).cend(), [](const IconPack &p) { return p.id == "Fresh"; }));
        QCOMPARE(applySetting(core.engine, "appearance.icons", "Fresh").phase, QString("committed"));
    }

    void badFoldersAreExplainedAndNothingIsCopied() {
        Sandbox s;
        IconDesk desk(s);
        const QString in = s.dir.path() + "/in";
        AppCore core(s.env);
        IconThemeImporter importer(s.env, &core.coordinator);
        const auto refused = [&](const QString &path, const QString &words) {
            const QString state = importPack(importer, path);
            if (state != QLatin1String("failed") || !importer.message().contains(words)) {
                qWarning() << path << state << importer.message();
                return false;
            }
            return QDir(desk.user).entryList(QDir::AllDirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty();
        };

        QVERIFY(refused(in + "/nothing-here", "could not be found"));
        writeFile(in + "/pack.tar.gz", "x");
        QVERIFY(refused(in + "/pack.tar.gz", "Extract it first"));
        writeFile(in + "/file.txt", "x");
        QVERIFY(refused(in + "/file.txt", "This is a file"));
        QDir().mkpath(in + "/empty");
        QVERIFY(refused(in + "/empty", "no index.theme"));
        addPack(in + "/wrapper", "Inner");
        QVERIFY(refused(in + "/wrapper", "Choose that folder instead"));
        addPack(in + "/two", "One");
        addPack(in + "/two", "Other");
        QVERIFY(refused(in + "/two", "holds 2 packs"));
        writeFile(in + "/Cursors/index.theme", "[Icon Theme]\nName=Cursors\nInherits=x\n");
        QVERIFY(refused(in + "/Cursors", "cursor theme"));
        writeFile(in + "/Bad/index.theme", "garbage\n");
        QVERIFY(refused(in + "/Bad", "not valid"));
        writeFile(in + "/Ghost/index.theme", "[Icon Theme]\nName=Ghost\nDirectories=a\n[a]\nSize=48\n");
        QVERIFY(refused(in + "/Ghost", "would show no icons"));

        addPack(in, "Escape");
        QVERIFY(QFile::link("../../../../../../etc/passwd", in + "/Escape/scalable/apps/passwd.svg"));
        QVERIFY(refused(in + "/Escape", "points outside"));
        addPack(in, "Absolute");
        QVERIFY(QFile::link("/etc/passwd", in + "/Absolute/scalable/apps/passwd.svg"));
        QVERIFY(refused(in + "/Absolute", "points outside"));
        addPack(in, "Loop");
        QVERIFY(QFile::link("b.svg", in + "/Loop/scalable/apps/a.svg"));
        QVERIFY(QFile::link("a.svg", in + "/Loop/scalable/apps/b.svg"));
        QVERIFY(refused(in + "/Loop", "in a circle"));
        addPack(in, "Ancestor");
        QVERIFY(QFile::link("..", in + "/Ancestor/scalable/apps/up"));
        QVERIFY(refused(in + "/Ancestor", "in a circle"));
        // Only following the links shows this one leaves: its text alone stays inside.
        addPack(in, "Sneaky");
        QDir().mkpath(in + "/Sneaky/deep/er");
        QDir().mkpath(in + "/Sneaky/other");
        QVERIFY(QFile::link("../../other", in + "/Sneaky/deep/er/hop"));
        QVERIFY(QFile::link("hop/../..", in + "/Sneaky/deep/er/out"));
        QVERIFY(refused(in + "/Sneaky", "points outside"));
        addPack(in, "Device");
        QCOMPARE(QProcess::execute("mkfifo", {in + "/Device/pipe"}), 0);
        QVERIFY(refused(in + "/Device", "device or socket"));

        // The source and the active choice are untouched by any of that.
        QVERIFY(QFile::exists(in + "/Escape/index.theme"));
        QCOMPARE(desk.writes(), 0);
    }

    void anOccupiedDestinationIsNeverReplaced() {
        Sandbox s;
        IconDesk desk(s);
        addPack(desk.user, "Taken", {"mine"});
        addPack(s.dir.path() + "/in", "Taken", {"theirs"});
        AppCore core(s.env);
        IconThemeImporter importer(s.env, &core.coordinator);
        QCOMPARE(importPack(importer, s.dir.path() + "/in/Taken"), QString("failed"));
        QVERIFY(importer.message().contains("already in your icons folder"));
        QVERIFY(QFile::exists(desk.user + "/Taken/scalable/apps/mine.svg"));
        QVERIFY(!QFile::exists(desk.user + "/Taken/scalable/apps/theirs.svg"));
    }

    void hidingAnotherCopyOfAPackAsksFirst() {
        Sandbox s;
        IconDesk desk(s);
        addPack(s.dir.path() + "/in", "Papirus", {"mine"}, "NotInstalled");
        AppCore core(s.env);
        IconThemeImporter importer(s.env, &core.coordinator);
        QCOMPARE(importPack(importer, s.dir.path() + "/in/Papirus"), QString("confirm"));
        QVERIFY(importer.message().contains(desk.system + "/Papirus"));
        QVERIFY(!QFile::exists(desk.user + "/Papirus"));
        importer.cancel();
        QCOMPARE(importer.state(), QString("idle"));

        QCOMPARE(importPack(importer, s.dir.path() + "/in/Papirus"), QString("confirm"));
        QSignalSpy finished(&importer, &IconThemeImporter::finished);
        importer.confirm();
        QVERIFY(finished.wait(20000));
        QCOMPARE(importer.state(), QString("imported"));
        QVERIFY(QFile::exists(desk.user + "/Papirus/scalable/apps/mine.svg"));
        // What it needs is named, and not fetched from anywhere.
        QVERIFY(importer.message().contains("NotInstalled"));
        QVERIFY(QDir(desk.system + "/Papirus").exists());

        addPack(s.dir.path() + "/in", "Numix");
        QCOMPARE(Cli::run(s.env, {"icons", "import", s.dir.path() + "/in/Numix"}), 1);
        QVERIFY(!QFile::exists(desk.user + "/Numix"));
        QCOMPARE(Cli::run(s.env, {"icons", "import", s.dir.path() + "/in/Numix", "--yes"}), 0);
        QVERIFY(QFile::exists(desk.user + "/Numix/index.theme"));
    }

    void aCancelledImportLeavesNoTrace() {
        Sandbox s;
        IconDesk desk(s);
        const QString source = s.dir.path() + "/in/Big";
        addPack(s.dir.path() + "/in", "Big");
        const QByteArray bulk(2 * 1024 * 1024, 'x');
        for (int i = 0; i < 40; ++i)
            writeFile(source + "/scalable/apps/bulk" + QString::number(i) + ".svg", bulk);
        AppCore core(s.env);
        IconThemeImporter importer(s.env, &core.coordinator);
        QSignalSpy finished(&importer, &IconThemeImporter::finished);
        importer.choosePath(source);
        QVERIFY(importer.busy());
        // A second folder while copying is not a second import.
        importer.choosePath(s.dir.path() + "/elsewhere");
        QVERIFY(importer.busy());
        importer.cancel();
        QVERIFY(finished.count() || finished.wait(20000));
        QCOMPARE(finished.first().first().toBool(), false);
        QCOMPARE(importer.state(), QString("idle"));
        QVERIFY(importer.message().contains("cancelled"));
        QVERIFY(QDir(desk.user).entryList(QDir::AllDirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
        QVERIFY(QFile::exists(source + "/scalable/apps/bulk0.svg"));
        // Nothing changed, so there is nothing for History to say.
        QVERIFY(core.coordinator.history(1).isEmpty());
    }

    void aSourceThatChangesMidCopyIsNotImported() {
        Sandbox s;
        IconDesk desk(s);
        addPack(s.dir.path() + "/in", "Moving");
        AppCore core(s.env);
        IconThemeImporter importer(s.env, &core.coordinator);
        QSignalSpy finished(&importer, &IconThemeImporter::finished);
        const QString source = s.dir.path() + "/in/Moving";
        const QByteArray bulk(2 * 1024 * 1024, 'x');
        for (int i = 0; i < 40; ++i)
            writeFile(source + "/scalable/apps/bulk" + QString::number(i) + ".svg", bulk);
        importer.choosePath(source);
        QVERIFY(importer.busy());
        writeFile(source + "/added-during-copy.svg", "x");
        QVERIFY(finished.count() || finished.wait(30000));
        if (importer.state() == QLatin1String("failed")) {
            QVERIFY(importer.message().contains("changed while"));
            QVERIFY(QDir(desk.user).entryList(QDir::AllDirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
        } else {
            // The copy finished before the file landed, in which case it is an exact one.
            QCOMPARE(importer.state(), QString("imported"));
            QVERIFY(QFile::exists(desk.user + "/Moving/index.theme"));
        }
    }

    void leftoversFromAStoppedImportAreSortedOutSafely() {
        Sandbox s;
        IconDesk desk(s);
        AppCore core(s.env);
        const auto leftover = [&](const QString &name, const QJsonObject &paths) {
            const QString folder = desk.user + "/.omarchy-control-import-" + name;
            writeFile(folder + "/.omarchy-control-import.json", QJsonDocument(QJsonObject{{"paths", paths}}).toJson());
            return folder;
        };
        const QString clean = leftover("clean", {{"index.theme", 4}, {"sub", 0}, {"sub/a.svg", 10}});
        writeFile(clean + "/index.theme", "abcd");
        writeFile(clean + "/sub/a.svg", "partial");
        const QString touched = leftover("touched", {{"index.theme", 4}});
        writeFile(touched + "/index.theme", "abcd");
        writeFile(touched + "/somebody-elses.txt", "keep me");
        const QString unowned = desk.user + "/.omarchy-control-import-unowned";
        writeFile(unowned + "/precious.txt", "keep me");

        IconThemeImporter importer(s.env, &core.coordinator);
        // Never while something else may be doing an import.
        {
            QLockFile lock(s.env.appStateDir + "/icon-import.lock");
            QVERIFY(lock.tryLock(0));
            QVERIFY(importer.recoverStaging().isEmpty());
            QVERIFY(QDir(clean).exists());
        }
        const QStringList messages = importer.recoverStaging();
        QCOMPARE(messages.size(), 3);
        QVERIFY(!QDir(clean).exists());
        QVERIFY(QFile::exists(touched + "/somebody-elses.txt"));
        QVERIFY(QFile::exists(unowned + "/precious.txt"));
        QVERIFY(messages.join('\n').contains(touched));
        QVERIFY(IconThemes::discover(s.env).size() == 3);
    }

    void appearanceShowsPacksAndChoosingOneWorks() {
        Sandbox s;
        fakeDesktop(s);
        IconDesk desk(s);
        addPack(desk.system, "A Very Long Icon Pack Name That Has To Be Cut Off Somewhere Sensible");
        for (int i = 0; i < 12; ++i)
            addPack(desk.system, "Extra" + QString::number(i));
        desk.setEffective("Yaru-blue");
        QQuickStyle::setStyle("Material");
        Translator translator(s.env.configHome + "/omarchy/language", ":/i18n", s.env.omarchyPath + "/default/i18n/locales.json");
        Theme theme(s.env);
        Backend backend(s.env);
        QQmlApplicationEngine engine;
        engine.addImageProvider("icons", new IconProvider);
        engine.addImageProvider("iconpacks", new IconPackImageProvider(s.env));
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
        QAccessible::setActive(true);
        QMetaObject::invokeMethod(window, "go", Q_ARG(QVariant, "appearance"), Q_ARG(QVariant, "appearance.icons"));
        const auto named = [&](const QString &objectName) {
            QList<QQuickItem *> found;
            std::function<void(QQuickItem *)> search = [&](QQuickItem *item) {
                if (item->objectName() == objectName && item->isVisible())
                    found << item;
                for (QQuickItem *child : item->childItems())
                    search(child);
            };
            search(window->contentItem());
            return found;
        };
        const auto tile = [&](const QString &name) -> QQuickItem * {
            for (QQuickItem *item : named("iconPackTile")) {
                QAccessibleInterface *accessible = QAccessible::queryAccessibleInterface(item);
                if (accessible && accessible->text(QAccessible::Name).startsWith(name))
                    return item;
            }
            return nullptr;
        };
        QTRY_VERIFY(tile("Follow desktop theme"));
        // Omarchy's own value counts as following it, and the tile names the pack it means.
        QTRY_VERIFY(tile("Follow desktop theme")->property("selected").toBool());
        QVERIFY(QAccessible::queryAccessibleInterface(tile("Follow desktop theme"))->text(QAccessible::Name).contains("Yaru-blue"));
        // Search lands on the pack in use, ready to change it.
        QTRY_VERIFY(window->activeFocusItem() == tile("Follow desktop theme"));
        QVERIFY(named("iconPackSample").size() >= 4);
        QVERIFY(!named("iconImportButton").isEmpty());
        QVERIFY(!named("iconPackSearch").isEmpty());
        QCOMPARE(desk.writes(), 0);

        // Searching narrows the list and keeps what is in use on screen.
        named("iconPackSearch").first()->setProperty("text", "numix");
        QTRY_VERIFY(tile("Numix"));
        QVERIFY(!tile("Papirus"));
        QVERIFY(tile("Follow desktop theme"));
        QTest::qWait(50);  // The keyboard goes back to its pack on the next tick.

        // Choosing confirms through a read back, and focus stays where it was.
        QQuickItem *numix = tile("Numix");
        numix->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_COMPARE(desk.effective(), QString("Numix"));
        QTRY_VERIFY(tile("Numix") && tile("Numix")->property("selected").toBool());
        QTRY_VERIFY(!backend.states()->value(Backend::stateKey("appearance.icons")).toMap().value("busy").toBool());
        QTRY_VERIFY(window->activeFocusItem() == tile("Numix"));
        named("iconPackSearch").first()->setProperty("text", "");

        // A dropped folder goes through the importer, and using it is a separate step.
        addPack(s.dir.path() + "/downloads", "Dropped");
        backend.iconImporter()->choose(QUrl::fromLocalFile(s.dir.path() + "/downloads/Dropped"));
        QTRY_COMPARE(backend.iconImporter()->state(), QString("imported"));
        QCOMPARE(desk.effective(), QString("Numix"));
        QTRY_VERIFY(!named("iconUseImported").isEmpty());
        QMetaObject::invokeMethod(named("iconUseImported").first(), "clicked");
        QTRY_COMPARE(desk.effective(), QString("Dropped"));
        QCOMPARE(backend.iconImporter()->state(), QString("idle"));
        // The button that was pressed has gone, and the keyboard did not go with it.
        QTRY_VERIFY(window->activeFocusItem() == named("iconImportButton").first());

        // The chooser loads and opens, whichever kind this platform provides.
        QMetaObject::invokeMethod(named("iconImportButton").first(), "clicked");
        QTest::qWait(100);
        QAccessible::setActive(false);
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
