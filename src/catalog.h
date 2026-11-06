#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// One searchable destination: a typed setting, a helper launch, or a config file.
// Entries are compiled into the binary and reviewed. This is deliberately not a
// plugin system: nothing outside the binary can add a command to run.
struct CatalogEntry {
    QString id, page, section, label, description, type, domain, integration;
    QStringList keywords, sources, crossPages, effects, requiresCommands, requiresCaps, requiresHardware;
    QJsonObject read, write, launch, choicesFrom, confirmWhen, valueMap;
    QJsonArray choices, reset;
    double min = 0, max = 0, step = 1;
    bool hasRange = false, allowCustomValue = false, owned = false, softRequires = false, quick = false;
    QString unit, pattern, scope, recovery, restart, reloads, confirm, confirmText, confirmAlways, note, placeholder, installHint;
    QStringList files;

    bool isAction() const { return type == QLatin1String("action"); }
    bool isSetting() const { return !isAction(); }
    bool isEditable() const { return isSetting() && type != QLatin1String("status") && !write.isEmpty(); }
    QVariantMap toVariant() const;
};

struct PageInfo {
    QString id, label, icon, description;
    QStringList sections, blocks;
};

struct SearchHit {
    const CatalogEntry *entry = nullptr;
    int score = 0;
};

class Catalog {
public:
    // Loads the compiled-in data; `directory` lets tests supply their own.
    explicit Catalog(const QString &directory = QStringLiteral(":/data"));

    const QList<CatalogEntry> &entries() const { return m_entries; }
    const QList<PageInfo> &pages() const { return m_pages; }
    const CatalogEntry *find(const QString &id) const;
    const PageInfo *page(const QString &id) const;
    QList<const CatalogEntry *> entriesForPage(const QString &page) const;
    QJsonObject exclusions() const { return m_exclusions; }

    // Ranked, local, and side-effect free: searching never runs anything. An
    // optional translator callback lets a translated label match as well.
    QList<SearchHit> search(const QString &query, int limit = 40) const;

    static QString normalize(const QString &text);

private:
    void load(const QString &path);

    QList<CatalogEntry> m_entries;
    QList<PageInfo> m_pages;
    QHash<QString, int> m_index;
    QJsonObject m_exclusions;
};
