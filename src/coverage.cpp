#include "coverage.h"

#include <QRegularExpression>
#include <QSet>

namespace Coverage {

bool wildcardMatch(const QString &pattern, const QString &text) {
    if (!pattern.contains('*'))
        return pattern == text;
    const QRegularExpression regex(QRegularExpression::wildcardToRegularExpression(pattern, QRegularExpression::UnanchoredWildcardConversion));
    const auto match = regex.match(text);
    return match.hasMatch() && match.capturedLength() == text.size();
}

static bool matchesAny(const QStringList &patterns, const QString &text) {
    for (const auto &pattern : patterns)
        if (wildcardMatch(pattern, text))
            return true;
    return false;
}

static void collectClaims(const Catalog &catalog, const QHash<QString, QString> &routeByBinary, QStringList *claims) {
    static const QRegularExpression token("omarchy-[a-z0-9-]+");
    for (const auto &entry : catalog.entries()) {
        *claims << entry.sources;
        const QJsonObject action = !entry.launch.isEmpty() ? entry.launch : entry.write;
        QStringList argv;
        for (const auto &part : action.value("argv").toArray())
            argv << part.toString();
        if (argv.value(0) == QLatin1String("omarchy")) {
            QStringList words;
            for (const auto &word : argv)
                if (!word.contains('{'))
                    words << word;
            *claims << words.join(' ');
        } else if (routeByBinary.contains(argv.value(0))) {
            *claims << routeByBinary.value(argv.value(0));
        }
        auto matches = token.globalMatch(action.value("command").toString());
        while (matches.hasNext()) {
            const QString binary = matches.next().captured();
            if (routeByBinary.contains(binary))
                *claims << routeByBinary.value(binary);
        }
    }
}

Result reconcileLive(const Catalog &catalog, const QJsonArray &commands) {
    Result result;
    QHash<QString, QString> routes;
    for (const auto &value : commands) {
        const auto command = value.toObject();
        routes.insert(command.value("binary").toString(), command.value("route").toString());
    }
    QStringList claims;
    collectClaims(catalog, routes, &claims);
    QStringList excluded;
    for (const auto &value : catalog.exclusions().value("commands").toArray())
        excluded << value.toObject().value("pattern").toString();
    for (const auto &value : commands) {
        const auto command = value.toObject();
        if (command.value("hidden").toBool())
            continue;
        const QString route = command.value("route").toString();
        if (matchesAny(claims, route))
            ++result.claimedCommands;
        else if (matchesAny(excluded, route))
            ++result.excludedCommands;
        else
            result.unclassifiedCommands << route;
    }
    return result;
}

Result reconcile(const Catalog &catalog, const QJsonObject &inventory) {
    Result result = reconcileLive(catalog, inventory.value("commands").toArray());
    QStringList claims;
    for (const auto &entry : catalog.entries())
        claims << entry.sources;
    QStringList excluded;
    for (const auto &value : catalog.exclusions().value("menu").toArray())
        excluded << value.toObject().value("pattern").toString();
    for (const auto &value : inventory.value("menu_entries").toArray()) {
        const auto node = value.toObject();
        // A row with neither an action nor a provider is just a submenu heading.
        if (!node.contains("action") && !node.contains("provider"))
            continue;
        const QString id = node.value("id").toString();
        if (matchesAny(claims, id))
            ++result.claimedMenu;
        else if (matchesAny(excluded, id))
            ++result.excludedMenu;
        else
            result.unclassifiedMenu << id;
    }
    return result;
}

QJsonArray ledger(const Catalog &catalog, const QJsonObject &evidence) {
    QJsonArray rows;
    for (const auto &entry : catalog.entries()) {
        QJsonObject row;
        row["id"] = entry.id;
        row["domain"] = entry.domain;
        row["source_references"] = QJsonArray::fromStringList(entry.sources);
        QStringList labels{entry.label};
        labels << entry.keywords;
        row["searchable_labels"] = QJsonArray::fromStringList(labels);
        QStringList capability = entry.requiresCaps;
        capability << entry.requiresHardware << entry.requiresCommands;
        row["applicable_capability"] = QJsonArray::fromStringList(capability);
        row["read_method"] = entry.read.value("kind").toString("none");
        row["integration_mode"] = entry.integration;
        const QJsonObject action = !entry.launch.isEmpty() ? entry.launch : entry.write;
        row["write_or_launch_method"] = action.value("kind").toString("none");
        QStringList semantics;
        if (!entry.reloads.isEmpty())
            semantics << entry.reloads;
        if (!entry.restart.isEmpty())
            semantics << entry.restart;
        row["apply_restart_semantics"] = semantics.join(' ');
        row["recovery_class"] = entry.recovery;
        row["supported_versions"] = QStringLiteral("omarchy 4.5.x");
        const QJsonObject proof = evidence.value(entry.id).toObject();
        // Nothing is called verified without a recorded live check. Fixture tests
        // and a resolvable route are evidence too, but they are named as such.
        row["implementation_status"] = proof.value("status").toString("implemented");
        row["validation_evidence"] = proof.value("evidence").toString(entry.isSetting() ? "fixture-tests" : "route-reviewed");
        row["known_limitation"] = entry.note;
        rows.append(row);
    }
    return rows;
}

}  // namespace Coverage
