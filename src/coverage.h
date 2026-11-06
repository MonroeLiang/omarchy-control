#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "catalog.h"

// Reconciles the catalog against a discovery inventory. A new first-party command
// or menu row with no destination and no recorded exclusion is unclassified, and
// unclassified entries fail the coverage test rather than vanish quietly.
namespace Coverage {

struct Result {
    QStringList unclassifiedCommands;
    QStringList unclassifiedMenu;
    int claimedCommands = 0, excludedCommands = 0;
    int claimedMenu = 0, excludedMenu = 0;
};

Result reconcile(const Catalog &catalog, const QJsonObject &inventory);
Result reconcileLive(const Catalog &catalog, const QJsonArray &commands);

// One row per setting or workflow, with the fields docs/COVERAGE.md specifies.
QJsonArray ledger(const Catalog &catalog, const QJsonObject &evidence = {});

bool wildcardMatch(const QString &pattern, const QString &text);

}  // namespace Coverage
