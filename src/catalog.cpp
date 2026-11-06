#include "catalog.h"

#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

static QStringList toStrings(const QJsonValue &value) {
    QStringList out;
    for (const auto &item : value.toArray())
        out << item.toString();
    return out;
}

QVariantMap CatalogEntry::toVariant() const {
    QVariantMap m;
    m["id"] = id;
    m["page"] = page;
    m["section"] = section;
    m["label"] = label;
    m["description"] = description;
    m["type"] = type;
    m["domain"] = domain;
    m["integration"] = integration;
    m["unit"] = unit;
    m["min"] = min;
    m["max"] = max;
    m["step"] = step;
    m["scope"] = scope;
    m["recovery"] = recovery;
    m["restart"] = restart;
    m["reloads"] = reloads;
    m["confirm"] = confirm;
    m["confirmText"] = confirmText;
    m["effects"] = effects;
    m["placeholder"] = placeholder;
    m["installHint"] = installHint;
    m["keywords"] = keywords;
    m["allowCustomValue"] = allowCustomValue;
    m["owned"] = owned;
    m["hasReset"] = !reset.isEmpty() || owned;
    m["editable"] = isEditable();
    m["crossPages"] = crossPages;
    m["launchKind"] = launch.value("kind").toString();
    m["linkPage"] = launch.value("page").toString();
    m["linkFocus"] = launch.value("focus").toString();
    // Technical details stay out of the row until someone expands them.
    QStringList details;
    if (launch.contains("command"))
        details << launch.value("command").toString();
    if (launch.contains("argv"))
        details << toStrings(launch.value("argv")).join(' ');
    if (launch.contains("file"))
        details << launch.value("file").toString();
    if (launch.contains("target"))
        details << launch.value("target").toString();
    if (write.contains("argv"))
        details << toStrings(write.value("argv")).join(' ');
    if (read.contains("file"))
        details << read.value("file").toString();
    if (write.contains("file") && write.value("file") != read.value("file"))
        details << write.value("file").toString();
    if (read.contains("option"))
        details << read.value("option").toString();
    m["details"] = details;
    return m;
}

Catalog::Catalog(const QString &directory) {
    QFile pages(directory + "/pages.json");
    if (pages.open(QIODevice::ReadOnly)) {
        for (const auto &value : QJsonDocument::fromJson(pages.readAll()).array()) {
            const auto o = value.toObject();
            PageInfo page;
            page.id = o.value("id").toString();
            page.label = o.value("label").toString();
            page.icon = o.value("icon").toString();
            page.description = o.value("description").toString();
            page.sections = toStrings(o.value("sections"));
            page.blocks = toStrings(o.value("blocks"));
            m_pages << page;
        }
    }
    load(directory + "/settings.json");
    load(directory + "/workflows.json");
    QFile exclusions(directory + "/coverage-exclusions.json");
    if (exclusions.open(QIODevice::ReadOnly))
        m_exclusions = QJsonDocument::fromJson(exclusions.readAll()).object();
}

void Catalog::load(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const auto array = QJsonDocument::fromJson(file.readAll()).array();
    m_entries.reserve(m_entries.size() + array.size());
    for (const auto &value : array) {
        const auto o = value.toObject();
        CatalogEntry e;
        e.id = o.value("id").toString();
        e.page = o.value("page").toString();
        e.section = o.value("section").toString();
        e.label = o.value("label").toString();
        e.description = o.value("description").toString();
        e.type = o.value("type").toString();
        e.domain = o.value("domain").toString();
        e.integration = o.value("integration").toString();
        e.keywords = toStrings(o.value("keywords"));
        e.sources = toStrings(o.value("sources"));
        e.crossPages = toStrings(o.value("crossPages"));
        e.effects = toStrings(o.value("effects"));
        e.requiresCommands = toStrings(o.value("requiresCommands"));
        e.requiresCaps = toStrings(o.value("requiresCaps"));
        e.requiresHardware = toStrings(o.value("requiresHardware"));
        e.files = toStrings(o.value("files"));
        e.read = o.value("read").toObject();
        e.write = o.value("write").toObject();
        e.launch = o.value("launch").toObject();
        e.choices = o.value("choices").toArray();
        e.choicesFrom = o.value("choicesFrom").toObject();
        e.confirmWhen = o.value("confirmWhen").toObject();
        e.valueMap = o.value("map").toObject();
        e.reset = o.value("reset").toArray();
        e.hasRange = o.contains("min");
        e.min = o.value("min").toDouble();
        e.max = o.value("max").toDouble();
        e.step = o.value("step").toDouble(1);
        e.allowCustomValue = o.value("allowCustomValue").toBool();
        e.owned = o.value("owned").toBool();
        e.softRequires = o.value("softRequires").toBool();
        e.quick = o.value("quick").toBool();
        e.unit = o.value("unit").toString();
        e.pattern = o.value("pattern").toString();
        e.scope = o.value("scope").toString();
        e.recovery = o.value("recovery").toString(e.isAction() ? QStringLiteral("no-undo") : QStringLiteral("auto-undo"));
        e.restart = o.value("restart").toString();
        e.reloads = o.value("reloads").toString();
        e.confirm = o.value("confirm").toString();
        e.confirmText = o.value("confirmText").toString();
        e.confirmAlways = o.value("confirmAlways").toString();
        e.note = o.value("note").toString();
        e.placeholder = o.value("placeholder").toString();
        e.installHint = o.value("installHint").toString();
        m_index.insert(e.id, m_entries.size());
        m_entries << e;
    }
}

const CatalogEntry *Catalog::find(const QString &id) const {
    const auto it = m_index.constFind(id);
    return it == m_index.constEnd() ? nullptr : &m_entries[it.value()];
}

const PageInfo *Catalog::page(const QString &id) const {
    for (const auto &page : m_pages)
        if (page.id == id)
            return &page;
    return nullptr;
}

QList<const CatalogEntry *> Catalog::entriesForPage(const QString &page) const {
    QList<const CatalogEntry *> out;
    for (const auto &entry : m_entries)
        if (entry.page == page)
            out << &entry;
    return out;
}

QString Catalog::normalize(const QString &text) {
    QString out;
    out.reserve(text.size());
    for (const QChar c : text.normalized(QString::NormalizationForm_KD).toCaseFolded()) {
        if (c.isLetterOrNumber())
            out += c;
        else if (!c.isMark() && !out.endsWith(' '))
            out += ' ';
    }
    return out.trimmed();
}

// Task words people actually type. A synonym points at vocabulary already in a
// row's keywords, so one phrasing does not have to be repeated on every row.
static const QHash<QString, QString> &synonyms() {
    static const QHash<QString, QString> table{
        {"wallpaper", "background"}, {"sleep", "suspend"}, {"mic", "microphone"}, {"wifi", "wi fi"},
        {"speakers", "output"}, {"headphones", "bluetooth"}, {"colour", "color"}, {"hotkey", "shortcut"},
        {"hotkeys", "shortcuts"}, {"keybinding", "shortcut"}, {"keybindings", "shortcuts"}, {"autostart", "startup"},
        {"login", "startup"}, {"resolution", "displays"}, {"monitor", "displays"}, {"monitors", "displays"},
        {"screen", "displays"}, {"backup", "snapshot"}, {"backups", "snapshots"}, {"upgrade", "update"},
        {"password", "sign in"}, {"volume", "output"}, {"uninstall", "remove"}, {"add", "install"},
    };
    return table;
}

QList<SearchHit> Catalog::search(const QString &query, int limit) const {
    QList<SearchHit> hits;
    const QString q = normalize(query);
    if (q.isEmpty())
        return hits;
    // "Change my microphone" means "microphone": filler words say what kind of
    // sentence this is, not which setting, and must not have to match anything.
    static const QSet<QString> filler{"a", "an", "the", "my", "to", "at", "of", "on", "in", "for", "how", "do", "i", "can", "change", "set", "make", "get", "turn", "fix", "use", "want", "please", "is", "it"};
    QStringList tokens;
    for (const auto &word : q.split(' ', Qt::SkipEmptyParts))
        if (!filler.contains(word))
            tokens << word;
    if (tokens.isEmpty())
        tokens = q.split(' ', Qt::SkipEmptyParts);
    const QString meaning = tokens.join(' ');

    for (const auto &entry : m_entries) {
        // Translated text participates, so a search works in the app's language.
        const QString translated = QCoreApplication::translate("catalog", entry.label.toUtf8().constData());
        const QString label = normalize(entry.label);
        const QString local = normalize(translated);
        const QString description = normalize(entry.description);
        const QString section = normalize(entry.section);
        QString keywords;
        for (const auto &keyword : entry.keywords)
            keywords += ' ' + normalize(keyword);

        int score = 0;
        if (label == meaning || local == meaning)
            score += 1000;
        else if (label.startsWith(meaning) || local.startsWith(meaning))
            score += 700;
        else if (label.contains(meaning) || local.contains(meaning))
            score += 500;
        if (keywords.contains(q) || keywords.contains(meaning))
            score += 650;
        if (description.contains(meaning))
            score += 200;

        int matched = 0;
        for (const auto &token : tokens) {
            int best = 0;
            auto test = [&](const QString &t) {
                if (label.contains(t) || local.contains(t))
                    best = qMax(best, 120);
                if (keywords.contains(t))
                    best = qMax(best, 100);
                if (section.contains(t))
                    best = qMax(best, 40);
                if (description.contains(t))
                    best = qMax(best, 30);
            };
            test(token);
            const auto synonym = synonyms().constFind(token);
            if (synonym != synonyms().constEnd()) {
                const int before = best;
                test(synonym.value());
                if (best > before)
                    best = qMax(before, best - 15);
            }
            if (best > 0)
                ++matched;
            score += best;
        }
        // Every word has to land somewhere, or short words match half the catalog.
        if (matched < tokens.size())
            score = label.contains(meaning) || keywords.contains(q) ? score : 0;
        if (score <= 0)
            continue;
        // A setting answers "change X" better than a helper that merely mentions it.
        if (entry.isSetting())
            score += 30;
        if (entry.integration == QLatin1String("advanced-config"))
            score -= 20;
        hits << SearchHit{&entry, score};
    }
    std::stable_sort(hits.begin(), hits.end(), [](const SearchHit &a, const SearchHit &b) { return a.score > b.score; });
    if (hits.size() > limit)
        hits.resize(limit);
    return hits;
}
