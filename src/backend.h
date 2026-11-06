#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QQmlPropertyMap>
#include <QTimer>
#include <QVariantList>
#include <memory>

#include "appcore.h"
#include "audioprovider.h"
#include "bluetoothprovider.h"
#include "displayprovider.h"
#include "helperlauncher.h"
#include "networkprovider.h"
#include "pluginsprovider.h"
#include "servicesprovider.h"
#include "shortcutsprovider.h"
#include "systemprovider.h"
#include "updater.h"
#include "windowrulesprovider.h"

// QQmlPropertyMap must be subclassed to construct it without the deprecated path.
class StateMap : public QQmlPropertyMap {
public:
    explicit StateMap(QObject *parent = nullptr) : QQmlPropertyMap(this, parent) {}
};

// What QML sees. Rows read the live value of a setting from `states`, keyed by
// stateKey(id); actions go through the same engine the CLI uses.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QQmlPropertyMap *states READ states CONSTANT)
    Q_PROPERTY(QVariantList pages READ pages CONSTANT)
    Q_PROPERTY(QVariantMap capabilities READ capabilityValues NOTIFY capabilitiesChanged)
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    Q_PROPERTY(QStringList pins READ pins NOTIFY pinsChanged)
    Q_PROPERTY(QVariantList problems READ problems NOTIFY problemsChanged)
    Q_PROPERTY(QStringList pendingRestarts READ pendingRestarts NOTIFY problemsChanged)
    Q_PROPERTY(QString omarchyVersion READ omarchyVersion NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool compatible READ compatible NOTIFY capabilitiesChanged)
    Q_PROPERTY(AudioProvider *audio READ audio CONSTANT)
    Q_PROPERTY(BluetoothProvider *bluetooth READ bluetooth CONSTANT)
    Q_PROPERTY(NetworkProvider *network READ network CONSTANT)
    Q_PROPERTY(DisplayProvider *displays READ displays CONSTANT)
    Q_PROPERTY(ServicesProvider *services READ services CONSTANT)
    Q_PROPERTY(ShortcutsProvider *shortcuts READ shortcuts CONSTANT)
    Q_PROPERTY(PluginsProvider *plugins READ plugins CONSTANT)
    Q_PROPERTY(SystemProvider *system READ system CONSTANT)
    Q_PROPERTY(WindowRulesProvider *windowRules READ windowRules CONSTANT)
    Q_PROPERTY(UpdateProvider *updates READ updates CONSTANT)
    Q_PROPERTY(QVariantList compatChanges READ compatChanges NOTIFY problemsChanged)

public:
    explicit Backend(const Env &env, QObject *parent = nullptr);

    QQmlPropertyMap *states() { return &m_states; }
    QVariantList pages() const;
    QVariantMap capabilityValues() const { return m_core->capabilities.values(); }
    QVariantList history() const;
    QStringList pins() const { return m_pins; }
    QVariantList problems() const;
    QStringList pendingRestarts() const { return m_pendingRestarts; }
    QString omarchyVersion() const;
    bool compatible() const;

    AudioProvider *audio() { return &m_audio; }
    BluetoothProvider *bluetooth() { return &m_bluetooth; }
    NetworkProvider *network() { return &m_network; }
    DisplayProvider *displays() { return &m_displays; }
    ServicesProvider *services() { return &m_services; }
    ShortcutsProvider *shortcuts() { return &m_shortcuts; }
    PluginsProvider *plugins() { return &m_plugins; }
    SystemProvider *system() { return &m_system; }
    WindowRulesProvider *windowRules() { return &m_windowRules; }
    UpdateProvider *updates() { return &m_updates; }
    QVariantList compatChanges() const;
    Q_INVOKABLE void acceptCompat();

    Q_INVOKABLE static QString stateKey(const QString &id);
    Q_INVOKABLE QVariantMap entry(const QString &id) const;
    Q_INVOKABLE QVariantList sections(const QString &page) const;
    Q_INVOKABLE QStringList blocks(const QString &page) const;
    Q_INVOKABLE QVariantList search(const QString &query) const;
    Q_INVOKABLE QVariantList pinnedEntries() const;
    Q_INVOKABLE QVariantMap availability(const QString &id) const;

    Q_INVOKABLE void refresh(const QString &id);
    Q_INVOKABLE void refreshPage(const QString &page);
    Q_INVOKABLE void pageShown(const QString &page);
    Q_INVOKABLE void windowActivated();

    Q_INVOKABLE QVariantMap planChange(const QString &id, const QString &value);
    Q_INVOKABLE void commit(const QString &id, const QString &value);
    Q_INVOKABLE void resetValue(const QString &id);
    Q_INVOKABLE void undo(const QString &transactionId);
    Q_INVOKABLE void launch(const QString &id);
    Q_INVOKABLE void togglePin(const QString &id);
    Q_INVOKABLE void clearRestartNotice();

signals:
    void capabilitiesChanged();
    void historyChanged();
    void pinsChanged();
    void problemsChanged();
    void toast(const QString &text, const QString &kind);
    void navigate(const QString &page, const QString &focus);
    void raiseRequested(const QString &page, const QString &focus);

public slots:
    void requestRaise(const QString &page, const QString &focus) { emit raiseRequested(page, focus); }

private:
    void setState(const QString &id, const QVariantMap &state);
    QVariantMap currentState(const QString &id) const;
    void watchFiles();
    void loadPreferences();
    void savePreferences();
    void handleResult(const QString &id, const CatalogEntry *entry, const Transaction &tx);
    QString noteFor(const CatalogEntry &entry) const;

    std::unique_ptr<AppCore> m_core;
    HelperLauncher m_launcher;
    AudioProvider m_audio;
    BluetoothProvider m_bluetooth;
    NetworkProvider m_network;
    DisplayProvider m_displays;
    ServicesProvider m_services;
    ShortcutsProvider m_shortcuts;
    PluginsProvider m_plugins;
    SystemProvider m_system;
    WindowRulesProvider m_windowRules;
    UpdateProvider m_updates;
    StateMap m_states;
    QHash<QString, QVariantList> m_choices;
    QStringList m_pins;
    QStringList m_pendingRestarts;
    QString m_currentPage = "overview";
    QFileSystemWatcher m_watcher;
    QTimer m_watchDebounce;
    QTimer m_capabilityDebounce;
};
