#include "SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QDesktopServices>
#include <QFileInfo>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "ConfigInspector.h"
#include "ConfigStore.h"
#include "NetworkAdapterManager.h"
#include "vpn/trusttunnel/version.h"

#include <QTimer>

#ifndef FREETUNNEL_VERSION
#define FREETUNNEL_VERSION "dev"
#endif
#ifndef FREETUNNEL_GIT_VERSION
#define FREETUNNEL_GIT_VERSION "unknown"
#endif

SettingsDialog::SettingsDialog(const QString &lang, const AppSettings &settings, QWidget *parent) : QDialog(parent) {
    const bool ru = (lang == "ru");
    setWindowTitle("Settings");
    resize(680, 460);

    auto *layout = new QVBoxLayout(this);

    // Navigation: left list with icons, right stacked pages
    auto *bodyLayout = new QHBoxLayout();
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    m_navList = new QListWidget(this);
    m_navList->setViewMode(QListView::ListMode);
    m_navList->setMovement(QListView::Static);
    m_navList->setFixedWidth(180);
    m_navList->setSpacing(4);
    m_stack = new QStackedWidget(this);

    auto addNavItem = [this](const QString &title, QWidget *page, const QIcon &icon) {
        auto *item = new QListWidgetItem(icon, title, m_navList);
        m_stack->addWidget(page);
        m_navList->addItem(item);
    };

    // ════════════════════════════════════════════════════════════════
    // 1. Основные / General  —  Theme, Auto-connect, Notifications
    // ════════════════════════════════════════════════════════════════
    auto *generalPage = new QWidget(this);
    auto *generalLayout = new QVBoxLayout(generalPage);

    auto *appearGroup = new QGroupBox(ru ? "Внешний вид" : "Appearance", generalPage);
    auto *appearGroupLayout = new QFormLayout(appearGroup);
    m_themeModeCombo = new QComboBox(appearGroup);
    m_themeModeCombo->addItem("System", "system");
    m_themeModeCombo->addItem("Light", "light");
    m_themeModeCombo->addItem("Dark", "dark");
    m_themeModeCombo->addItem("Claude", "claude");
    int themeIdx = m_themeModeCombo->findData(settings.theme_mode);
    m_themeModeCombo->setCurrentIndex(themeIdx >= 0 ? themeIdx : 0);
    appearGroupLayout->addRow(ru ? "Тема:" : "Theme:", m_themeModeCombo);
    generalLayout->addWidget(appearGroup);

    auto *startupGroup = new QGroupBox(ru ? "Запуск" : "Startup", generalPage);
    auto *startupGroupLayout = new QVBoxLayout(startupGroup);
    m_autoConnectCheck = new QCheckBox(ru ? "Автоподключение при запуске" : "Auto-connect on app start", startupGroup);
    m_autoConnectCheck->setChecked(settings.auto_connect_on_start);
    startupGroupLayout->addWidget(m_autoConnectCheck);
    generalLayout->addWidget(startupGroup);

    auto *notifyGroup = new QGroupBox(ru ? "Уведомления" : "Notifications", generalPage);
    auto *notifyGroupLayout = new QVBoxLayout(notifyGroup);
    m_notifyOnStateCheck = new QCheckBox(ru ? "Уведомлять о смене состояния" : "Notify on state changes", notifyGroup);
    m_notifyOnStateCheck->setChecked(settings.notify_on_state);
    m_notifyErrorsOnlyCheck = new QCheckBox(ru ? "Только при ошибках" : "Only on errors", notifyGroup);
    m_notifyErrorsOnlyCheck->setChecked(settings.notify_only_errors);
    notifyGroupLayout->addWidget(m_notifyOnStateCheck);
    notifyGroupLayout->addWidget(m_notifyErrorsOnlyCheck);
    generalLayout->addWidget(notifyGroup);

    generalLayout->addStretch();
    addNavItem(ru ? "Основные" : "General", generalPage, QIcon(":/icons/general.svg"));

    // ════════════════════════════════════════════════════════════════
    // 2. Подключение / Connection  —  Security + Routing
    // ════════════════════════════════════════════════════════════════
    auto *connectionPage = new QWidget(this);
    auto *connectionLayout = new QVBoxLayout(connectionPage);

    auto *securityGroup = new QGroupBox(ru ? "Безопасность" : "Security", connectionPage);
    auto *securityGroupLayout = new QVBoxLayout(securityGroup);
    m_killswitchCheck = new QCheckBox(ru ? "Kill switch (блокировать трафик вне VPN)" : "Kill switch (block untunneled traffic)", securityGroup);
    m_killswitchCheck->setChecked(settings.killswitch_enabled);
    m_strictCertCheck = new QCheckBox(ru ? "Строгая проверка сертификата" : "Strict certificate verification", securityGroup);
    m_strictCertCheck->setChecked(settings.strict_certificate_check);
    securityGroupLayout->addWidget(m_killswitchCheck);
    securityGroupLayout->addWidget(m_strictCertCheck);
    connectionLayout->addWidget(securityGroup);

    auto *perAppGroup = new QGroupBox(ru ? "Управление приложениями" : "Per-App Control", connectionPage);
    auto *perAppLayout = new QVBoxLayout(perAppGroup);
    m_perAppRulesCheck = new QCheckBox(ru ? "Включить правила для приложений" : "Enable per-app rules", perAppGroup);
    m_perAppRulesCheck->setChecked(settings.per_app_rules_enabled);
    auto *perAppBtnLayout = new QHBoxLayout();
    auto *perAppManageBtn = new QPushButton(ru ? "Управлять приложениями" : "Manage Apps");
    perAppBtnLayout->addWidget(m_perAppRulesCheck);
    perAppBtnLayout->addStretch();
    perAppBtnLayout->addWidget(perAppManageBtn);
    perAppLayout->addLayout(perAppBtnLayout);
    connectionLayout->addWidget(perAppGroup);

    connect(perAppManageBtn, &QPushButton::clicked, this, [this, lang, settings]() {
        showPerAppRulesDialog(lang, settings);
    });

    auto *routingGroup = new QGroupBox(ru ? "Маршрутизация" : "Routing", connectionPage);
    auto *routingGroupLayout = new QFormLayout(routingGroup);
    m_routingEnableCheck = new QCheckBox(ru ? "Включить маршрутизацию" : "Enable routing", routingGroup);
    m_routingEnableCheck->setChecked(settings.routing_enabled);
    m_routingTunnelRadio = new QRadioButton(ru ? "Туннелировать РФ" : "Tunnel RU", routingGroup);
    m_routingBypassRadio = new QRadioButton(ru ? "Обходить РФ" : "Bypass RU", routingGroup);
    if (settings.routing_mode == "bypass_ru") {
        m_routingBypassRadio->setChecked(true);
    } else {
        m_routingTunnelRadio->setChecked(true);
    }
    auto *modeRow = new QHBoxLayout();
    modeRow->addWidget(m_routingTunnelRadio);
    modeRow->addWidget(m_routingBypassRadio);
    m_routingUrlEdit = new QLineEdit(settings.routing_source_url, routingGroup);
    m_routingCacheEdit = new QLineEdit(settings.routing_cache_path, routingGroup);
    auto *browseCacheBtn = new QPushButton(ru ? "Обзор..." : "Browse...", routingGroup);
    auto *cacheRow = new QHBoxLayout();
    cacheRow->addWidget(m_routingCacheEdit);
    cacheRow->addWidget(browseCacheBtn);
    routingGroupLayout->addRow(m_routingEnableCheck);
    routingGroupLayout->addRow(ru ? "Режим:" : "Mode:", modeRow);
    routingGroupLayout->addRow(ru ? "URL списка подсетей:" : "Subnets URL:", m_routingUrlEdit);
    routingGroupLayout->addRow(ru ? "Файл кеша:" : "Cache file:", cacheRow);
    connectionLayout->addWidget(routingGroup);

    connectionLayout->addStretch();
    addNavItem(ru ? "Подключение" : "Connection", connectionPage, QIcon(":/icons/connection.svg"));

    // ════════════════════════════════════════════════════════════════
    // 3. Сеть / Network  —  Custom DNS, Domain Bypass, SSH/P2P Bypass
    // ════════════════════════════════════════════════════════════════
    auto *networkPage = new QWidget(this);
    auto *networkLayout = new QVBoxLayout(networkPage);

    // --- Custom DNS ---
    auto *dnsCustomGroup = new QGroupBox(ru ? "Пользовательские DNS" : "Custom DNS Servers", networkPage);
    auto *dnsCustomLayout = new QVBoxLayout(dnsCustomGroup);
    m_customDnsCheck = new QCheckBox(ru ? "Использовать свои DNS-серверы" : "Use custom DNS servers", dnsCustomGroup);
    m_customDnsCheck->setChecked(settings.custom_dns_enabled);
    m_customDnsEdit = new QPlainTextEdit(dnsCustomGroup);
    m_customDnsEdit->setPlaceholderText(ru
            ? "Один DNS-сервер на строку:\n1.1.1.1\n8.8.8.8\ntls://1.1.1.1\nhttps://dns.adguard.com/dns-query"
            : "One DNS server per line:\n1.1.1.1\n8.8.8.8\ntls://1.1.1.1\nhttps://dns.adguard.com/dns-query");
    m_customDnsEdit->setMaximumHeight(100);
    m_customDnsEdit->setPlainText(settings.custom_dns_servers.join('\n'));
    m_customDnsEdit->setEnabled(settings.custom_dns_enabled);
    auto *dnsHint = new QLabel(ru
            ? "<i>Поддерживаемые форматы: 8.8.8.8:53, tcp://8.8.8.8:53, tls://1.1.1.1, "
              "https://dns.adguard.com/dns-query, quic://dns.adguard.com:8853, sdns://...</i>"
            : "<i>Supported: 8.8.8.8:53, tcp://8.8.8.8:53, tls://1.1.1.1, "
              "https://dns.adguard.com/dns-query, quic://dns.adguard.com:8853, sdns://...</i>",
            dnsCustomGroup);
    dnsHint->setWordWrap(true);
    dnsCustomLayout->addWidget(m_customDnsCheck);
    dnsCustomLayout->addWidget(m_customDnsEdit);
    dnsCustomLayout->addWidget(dnsHint);
    connect(m_customDnsCheck, &QCheckBox::toggled, m_customDnsEdit, &QPlainTextEdit::setEnabled);
    networkLayout->addWidget(dnsCustomGroup);

    // --- Domain bypass rules ---
    auto *bypassGroup = new QGroupBox(ru ? "Обход доменов" : "Domain Bypass Rules", networkPage);
    auto *bypassLayout = new QVBoxLayout(bypassGroup);
    m_domainBypassCheck = new QCheckBox(ru
            ? "Включить правила обхода доменов"
            : "Enable domain bypass rules", bypassGroup);
    m_domainBypassCheck->setChecked(settings.domain_bypass_enabled);
    m_domainBypassEdit = new QPlainTextEdit(bypassGroup);
    m_domainBypassEdit->setPlaceholderText(ru
            ? "Один домен/маска на строку:\nexample.com\n*.google.com\n192.168.1.0/24\n10.0.0.0/8"
            : "One domain/mask per line:\nexample.com\n*.google.com\n192.168.1.0/24\n10.0.0.0/8");
    m_domainBypassEdit->setMaximumHeight(120);
    m_domainBypassEdit->setPlainText(settings.domain_bypass_rules.join('\n'));
    m_domainBypassEdit->setEnabled(settings.domain_bypass_enabled);
    auto *bypassHint = new QLabel(ru
            ? "<i>Домены и адреса, перечисленные здесь, будут добавлены в исключения (exclusions) конфига. "
              "Форматы: domain.com, *.domain.com, IP, IP:port, CIDR (IP/mask).</i>"
            : "<i>Domains and addresses listed here are added to config exclusions. "
              "Formats: domain.com, *.domain.com, IP, IP:port, CIDR (IP/mask).</i>",
            bypassGroup);
    bypassHint->setWordWrap(true);
    bypassLayout->addWidget(m_domainBypassCheck);
    bypassLayout->addWidget(m_domainBypassEdit);
    bypassLayout->addWidget(bypassHint);
    connect(m_domainBypassCheck, &QCheckBox::toggled, m_domainBypassEdit, &QPlainTextEdit::setEnabled);
    networkLayout->addWidget(bypassGroup);

    // --- SSH / P2P Bypass ---
    auto *bypassTrafficGroup = new QGroupBox(ru ? "Обход трафика" : "Traffic Bypass", networkPage);
    auto *bypassTrafficLayout = new QVBoxLayout(bypassTrafficGroup);
    auto *bypassTrafficHint = new QLabel(
            ru ? "<i>Выберите типы трафика, которые должны идти мимо VPN-туннеля (напрямую).</i>"
               : "<i>Select traffic types that should bypass the VPN tunnel (go direct).</i>",
            bypassTrafficGroup);
    bypassTrafficHint->setWordWrap(true);
    bypassTrafficLayout->addWidget(bypassTrafficHint);
    m_sshBypassCheck = new QCheckBox(ru ? "Обходить SSH-трафик (порт 22)" : "Bypass SSH traffic (port 22)", bypassTrafficGroup);
    m_sshBypassCheck->setChecked(settings.ssh_bypass_enabled);
    m_sshBypassCheck->setToolTip(ru
            ? "SSH-соединения (порт 22) будут идти напрямую, минуя VPN-туннель."
            : "SSH connections (port 22) will go direct, bypassing the VPN tunnel.");
    bypassTrafficLayout->addWidget(m_sshBypassCheck);
    m_p2pBypassCheck = new QCheckBox(ru ? "Обходить P2P-трафик (BitTorrent и др.)" : "Bypass P2P traffic (BitTorrent, etc.)", bypassTrafficGroup);
    m_p2pBypassCheck->setChecked(settings.p2p_bypass_enabled);
    m_p2pBypassCheck->setToolTip(ru
            ? "P2P-трафик (BitTorrent, DHT и др.) будет идти напрямую, минуя VPN-туннель."
            : "P2P traffic (BitTorrent, DHT, etc.) will go direct, bypassing the VPN tunnel.");
    bypassTrafficLayout->addWidget(m_p2pBypassCheck);
    networkLayout->addWidget(bypassTrafficGroup);

    networkLayout->addStretch();
    addNavItem(ru ? "Сеть" : "Network", networkPage, QIcon(":/icons/network.svg"));

    // ════════════════════════════════════════════════════════════════
    // 4. Адаптеры / Adapters  —  Conflicts, Detected adapters, Reinstall
    // ════════════════════════════════════════════════════════════════
    auto *adaptersPage = new QWidget(this);
    auto *adaptersLayout = new QVBoxLayout(adaptersPage);

    // --- Adapter conflict scanner ---
    auto *conflictGroup = new QGroupBox(ru ? "Конфликты адаптеров" : "Adapter Conflicts", adaptersPage);
    auto *conflictLayout = new QVBoxLayout(conflictGroup);
    m_scanConflictsCheck = new QCheckBox(ru
            ? "Сканировать конфликты при подключении"
            : "Scan for adapter conflicts on connect", conflictGroup);
    m_scanConflictsCheck->setChecked(settings.scan_adapter_conflicts);
    auto *conflictHint = new QLabel(ru
            ? "<i>Перед подключением приложение проверит наличие конфликтующих VPN-адаптеров "
              "(Radmin VPN, Hamachi, OpenVPN TAP и др.) и предупредит, если они могут мешать.</i>"
            : "<i>Before connecting, the app checks for conflicting VPN adapters "
              "(Radmin VPN, Hamachi, OpenVPN TAP, etc.) and warns if they may interfere.</i>",
            conflictGroup);
    conflictHint->setWordWrap(true);
    auto *scanNowBtn = new QPushButton(ru ? "Сканировать сейчас" : "Scan Now", conflictGroup);
    conflictLayout->addWidget(m_scanConflictsCheck);
    conflictLayout->addWidget(conflictHint);
    conflictLayout->addWidget(scanNowBtn);
    adaptersLayout->addWidget(conflictGroup);

    connect(scanNowBtn, &QPushButton::clicked, this, [this]() {
        emit advancedAction("scan_conflicts");
    });

    // --- Detected VPN adapters ---
    auto *adapterGroup = new QGroupBox(ru ? "Обнаруженные VPN-адаптеры" : "Detected VPN Adapters", adaptersPage);
    auto *adapterGroupLayout = new QVBoxLayout(adapterGroup);
    auto *adapterHint = new QLabel(
            ru ? "<i>Сторонние VPN-адаптеры (Radmin VPN, Hamachi, WireGuard и др.) могут конфликтовать "
                 "с адаптером от FreeTunnel. Рекомендуется деактивировать их перед подключением.</i>"
               : "<i>Third-party VPN adapters (Radmin VPN, Hamachi, WireGuard, etc.) may conflict "
                 "with the FreeTunnel adapter. It is recommended to deactivate them before connecting.</i>",
            adapterGroup);
    adapterHint->setWordWrap(true);
    adapterGroupLayout->addWidget(adapterHint);

    m_adapterTree = new QTreeWidget(adapterGroup);
    m_adapterTree->setHeaderLabels({
        ru ? "Адаптер" : "Adapter",
        ru ? "Описание" : "Description",
        ru ? "Статус" : "Status",
        ""  // action column
    });
    m_adapterTree->setRootIsDecorated(false);
    m_adapterTree->setAlternatingRowColors(true);
    m_adapterTree->setMinimumHeight(140);
    m_adapterTree->header()->setStretchLastSection(false);
    m_adapterTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_adapterTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_adapterTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_adapterTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    adapterGroupLayout->addWidget(m_adapterTree);

    auto *rescanAdaptersBtn = new QPushButton(ru ? "Обновить список" : "Refresh List", adapterGroup);
    adapterGroupLayout->addWidget(rescanAdaptersBtn);
    adaptersLayout->addWidget(adapterGroup);

    // Lambda to populate the adapter tree
    auto populateAdapters = [this, ru]() {
        m_adapterTree->clear();
        const auto adapters = m_adapterManager.scanAdapters();
        for (const auto &info : adapters) {
            auto *item = new QTreeWidgetItem(m_adapterTree);
            item->setText(0, info.name);
            item->setText(1, info.description);
            item->setText(2, info.enabled ? (ru ? "Активен" : "Enabled")
                                          : (ru ? "Отключён" : "Disabled"));
            if (info.isOurs) {
                item->setForeground(0, QColor("#22aa22"));
                item->setText(3, "FreeTunnel");
            } else {
                auto *btn = new QPushButton(
                        info.enabled ? (ru ? "Деактивировать" : "Deactivate")
                                     : (ru ? "Активировать" : "Activate"),
                        m_adapterTree);
                if (info.enabled) {
                    btn->setStyleSheet("QPushButton { color: #cc3333; }");
                } else {
                    btn->setStyleSheet("QPushButton { color: #22aa22; }");
                }
                const QString adapterName = info.name;
                const bool wasEnabled = info.enabled;
                connect(btn, &QPushButton::clicked, this, [this, adapterName, wasEnabled, ru]() {
                    bool ok = false;
                    if (wasEnabled) {
                        auto res = QMessageBox::question(this,
                            ru ? "Деактивация адаптера" : "Deactivate Adapter",
                            QString(ru ? "Деактивировать адаптер '%1'?\n\n"
                                  "Данный адаптер может конфликтовать с адаптером от FreeTunnel, "
                                  "по этому его рекомендуется деактивировать."
                                : "Deactivate adapter '%1'?\n\n"
                                  "This adapter may conflict with the FreeTunnel adapter "
                                  "and it is recommended to deactivate it.")
                                .arg(adapterName),
                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                        if (res == QMessageBox::Yes) {
                            ok = m_adapterManager.disableAdapter(adapterName);
                        }
                    } else {
                        ok = m_adapterManager.enableAdapter(adapterName);
                    }
                    if (ok) {
                        emit advancedAction("refresh_adapters");
                        QTimer::singleShot(500, this, [this]() {
                            emit advancedAction("rescan_adapters_ui");
                        });
                    }
                });
                m_adapterTree->setItemWidget(item, 3, btn);
                item->setForeground(0, QColor("#cc3333"));
            }
        }
        if (adapters.isEmpty()) {
            auto *emptyItem = new QTreeWidgetItem(m_adapterTree);
            emptyItem->setText(0, ru ? "Адаптеры не обнаружены" : "No adapters found");
            emptyItem->setFlags(emptyItem->flags() & ~Qt::ItemIsSelectable);
        }
    };

    populateAdapters();

    connect(rescanAdaptersBtn, &QPushButton::clicked, this, populateAdapters);
    connect(this, &SettingsDialog::advancedAction, this, [populateAdapters](const QString &action) {
        if (action == "rescan_adapters_ui") {
            populateAdapters();
        }
    });

    // --- Reinstall tunnel adapters ---
    auto *tunnelGroup = new QGroupBox(ru ? "Туннельные адаптеры" : "Tunnel Adapters", adaptersPage);
    auto *tunnelGroupLayout = new QVBoxLayout(tunnelGroup);
    auto *reinstallBtn = new QPushButton(ru ? "Переустановить туннели" : "Reinstall Tunnel Adapters", tunnelGroup);
    reinstallBtn->setToolTip(ru
            ? "Удаляет все виртуальные сетевые адаптеры (WinTUN) и пересоздаёт их при следующем подключении."
            : "Removes all virtual network adapters (WinTUN) and recreates them on next connection.");
    auto *reinstallNote = new QLabel(
            ru ? "<i>Если VPN не подключается — попробуйте переустановить адаптеры.</i>"
               : "<i>If VPN fails to connect, try reinstalling the adapters.</i>",
            tunnelGroup);
    reinstallNote->setWordWrap(true);
    tunnelGroupLayout->addWidget(reinstallBtn);
    tunnelGroupLayout->addWidget(reinstallNote);
    adaptersLayout->addWidget(tunnelGroup);

    adaptersLayout->addStretch();
    addNavItem(ru ? "Адаптеры" : "Adapters", adaptersPage, QIcon(":/icons/adapters.svg"));

    // ════════════════════════════════════════════════════════════════
    // 5. Логирование / Logging
    // ════════════════════════════════════════════════════════════════
    auto *logsPage = new QWidget(this);
    auto *logsLayout = new QFormLayout(logsPage);
    m_saveLogsCheck = new QCheckBox(ru ? "Сохранять логи в файл" : "Save logs to file", logsPage);
    m_saveLogsCheck->setChecked(settings.save_logs);
    m_logLevelCombo = new QComboBox(logsPage);
    m_logLevelCombo->addItems({"error", "warn", "info", "debug", "trace"});
    const int idx = m_logLevelCombo->findText(settings.log_level);
    m_logLevelCombo->setCurrentIndex(idx >= 0 ? idx : 2);
    m_logPathEdit = new QLineEdit(settings.log_path, logsPage);
    auto *pickPathBtn = new QPushButton(ru ? "Выбрать..." : "Browse...", logsPage);
    auto *pathRow = new QHBoxLayout();
    pathRow->addWidget(m_logPathEdit);
    pathRow->addWidget(pickPathBtn);
    m_showLogsPanelCheck = new QCheckBox(ru ? "Показывать панель логов" : "Show logs panel", logsPage);
    m_showLogsPanelCheck->setChecked(settings.show_logs_panel);
    m_showTrafficCheck = new QCheckBox(ru ? "Показывать трафик в статусе" : "Show traffic in status bar", logsPage);
    m_showTrafficCheck->setChecked(settings.show_traffic_in_status);
    m_showTrafficGraphCheck = new QCheckBox(ru ? "Показывать график трафика" : "Show traffic graph", logsPage);
    m_showTrafficGraphCheck->setChecked(settings.show_traffic_graph);
    auto *openLogBtn = new QPushButton(ru ? "Открыть файл логов" : "Open log file", logsPage);
    connect(openLogBtn, &QPushButton::clicked, this, [this, ru]() {
        const QString path = m_logPathEdit->text().trimmed();
        if (path.isEmpty()) {
            QMessageBox::information(this,
                ru ? "Логи" : "Logs",
                ru ? "Путь к файлу логов не задан." : "Log file path is not set.");
            return;
        }
        QFileInfo fi(path);
        if (!fi.exists()) {
            QMessageBox::information(this,
                ru ? "Логи" : "Logs",
                ru ? "Файл логов ещё не создан.\nОн появится после начала записи."
                   : "Log file does not exist yet.\nIt will be created once logging starts.");
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    logsLayout->addRow(m_saveLogsCheck);
    logsLayout->addRow(ru ? "Уровень логов:" : "Log level:", m_logLevelCombo);
    logsLayout->addRow(ru ? "Файл логов:" : "Log file:", pathRow);
    logsLayout->addRow(openLogBtn);
    logsLayout->addRow(m_showLogsPanelCheck);
    logsLayout->addRow(m_showTrafficCheck);
    logsLayout->addRow(m_showTrafficGraphCheck);
    addNavItem(ru ? "Логирование" : "Logging", logsPage, QIcon(":/icons/logging.svg"));

    // ════════════════════════════════════════════════════════════════
    // 6. Обслуживание / Maintenance  —  Flush DNS, Clear SSL, Reset
    // ════════════════════════════════════════════════════════════════
    auto *maintPage = new QWidget(this);
    auto *maintLayout = new QVBoxLayout(maintPage);

    auto *dnsGroup = new QGroupBox("DNS", maintPage);
    auto *dnsGroupLayout = new QVBoxLayout(dnsGroup);
    auto *flushDnsBtn = new QPushButton(ru ? "Сбросить DNS-кеш" : "Flush DNS Cache", dnsGroup);
    flushDnsBtn->setToolTip(ru
            ? "Запускает очистку системного DNS-кеша (ipconfig /flushdns на Windows, dscacheutil -flushcache на macOS)."
            : "Runs system DNS cache flush (ipconfig /flushdns on Windows, dscacheutil -flushcache on macOS).");
    dnsGroupLayout->addWidget(flushDnsBtn);
    maintLayout->addWidget(dnsGroup);

    auto *cacheGroup = new QGroupBox(ru ? "Кеш и данные" : "Cache & Data", maintPage);
    auto *cacheGroupLayout = new QVBoxLayout(cacheGroup);
    auto *clearSslBtn = new QPushButton(ru ? "Очистить кеш SSL-сессий" : "Clear SSL Session Cache", cacheGroup);
    clearSslBtn->setToolTip(ru
            ? "Удаляет сохранённые TLS-сессии. Может помочь при проблемах с подключением."
            : "Deletes saved TLS sessions. May help with connection issues.");
    cacheGroupLayout->addWidget(clearSslBtn);
    maintLayout->addWidget(cacheGroup);

    auto *resetGroup = new QGroupBox(ru ? "Сброс" : "Reset", maintPage);
    auto *resetGroupLayout = new QVBoxLayout(resetGroup);
    auto *resetSettingsBtn = new QPushButton(ru ? "Сбросить все настройки" : "Reset All Settings", resetGroup);
    resetSettingsBtn->setStyleSheet("QPushButton { color: #cc3333; }");
    resetSettingsBtn->setToolTip(ru
            ? "Возвращает все настройки приложения к значениям по умолчанию."
            : "Resets all application settings to their defaults.");
    auto *resetHint = new QLabel(
            ru ? "<i>Это действие нельзя отменить. Все настройки будут сброшены к исходным.</i>"
               : "<i>This cannot be undone. All settings will be restored to defaults.</i>",
            resetGroup);
    resetHint->setWordWrap(true);
    resetGroupLayout->addWidget(resetSettingsBtn);
    resetGroupLayout->addWidget(resetHint);
    maintLayout->addWidget(resetGroup);

    maintLayout->addStretch();
    addNavItem(ru ? "Обслуживание" : "Maintenance", maintPage, QIcon(":/icons/maintenance.svg"));

    // ════════════════════════════════════════════════════════════════
    // 7. О программе / About
    // ════════════════════════════════════════════════════════════════
    auto *aboutPage = new QWidget(this);
    auto *aboutLayout = new QFormLayout(aboutPage);
    const QString fireTunnelDetails = ru
            ? "Клиент: FreeTunnel<br/>Разработчик: pnsrc<br/>GitHub: <a href=\"https://github.com/pnsrc/TrustTunnelClient\">https://github.com/pnsrc/TrustTunnelClient</a>"
            : "Client: FreeTunnel<br/>Developer: pnsrc<br/>GitHub: <a href=\"https://github.com/pnsrc/TrustTunnelClient\">https://github.com/pnsrc/TrustTunnelClient</a>";
    const QString trustTunnelDetails = ru
            ? "Базовый VPN-движок: TrustTunnel<br/>"
              "Документация: <a href=\"https://trusttunnel.org/\">https://trusttunnel.org/</a><br/>"
              "Репозиторий: <a href=\"https://github.com/TrustTunnel/TrustTunnel\">https://github.com/TrustTunnel/TrustTunnel</a><br/>"
              "Кратко по docs:<ul>"
              "<li>Трафик маскируется под обычный HTTPS.</li>"
              "<li>Использует TLS и транспорт HTTP/2/HTTP/3.</li>"
              "<li>Поддерживает туннелирование TCP/UDP/ICMP.</li>"
              "<li>Есть split tunneling, system-wide tunnel и SOCKS5-режим.</li>"
              "</ul>"
            : "Core VPN engine: TrustTunnel<br/>"
              "Documentation: <a href=\"https://trusttunnel.org/\">https://trusttunnel.org/</a><br/>"
              "Repository: <a href=\"https://github.com/TrustTunnel/TrustTunnel\">https://github.com/TrustTunnel/TrustTunnel</a><br/>"
              "Docs summary:<ul>"
              "<li>Traffic is designed to look like regular HTTPS.</li>"
              "<li>Uses TLS with HTTP/2 and HTTP/3 transport.</li>"
              "<li>Supports TCP/UDP/ICMP tunneling.</li>"
              "<li>Includes split tunneling, system-wide tunnel, and SOCKS5 mode.</li>"
              "</ul>";
    const QString buildStamp = QString("%1 %2").arg(__DATE__).arg(__TIME__);
    auto *ftLabel = new QTextBrowser(aboutPage);
    ftLabel->setOpenExternalLinks(true);
    ftLabel->setMaximumHeight(92);
    ftLabel->setHtml(fireTunnelDetails);
    auto *ttLabel = new QTextBrowser(aboutPage);
    ttLabel->setOpenExternalLinks(true);
    ttLabel->setMaximumHeight(220);
    ttLabel->setHtml(trustTunnelDetails);
    aboutLayout->addRow(ru ? "Приложение:" : "Application:", new QLabel("FreeTunnel", aboutPage));
    aboutLayout->addRow(ru ? "Версия:" : "Version:", new QLabel(QString::fromLatin1(FREETUNNEL_VERSION), aboutPage));
    aboutLayout->addRow(ru ? "Git ревизия:" : "Git revision:", new QLabel(QString::fromLatin1(FREETUNNEL_GIT_VERSION), aboutPage));
    aboutLayout->addRow(ru ? "Версия ядра TrustTunnel:" : "TrustTunnel core version:", new QLabel(QString::fromLatin1(TRUSTTUNNEL_VERSION), aboutPage));
    aboutLayout->addRow(ru ? "Сборка:" : "Build:", new QLabel(buildStamp, aboutPage));
    aboutLayout->addRow("Qt:", new QLabel(QString::fromLatin1(qVersion()), aboutPage));
    aboutLayout->addRow(ru ? "Хранилище конфигов:" : "Config storage:", new QLabel(storagePath(), aboutPage));
    aboutLayout->addRow(ru ? "О FreeTunnel:" : "About FreeTunnel:", ftLabel);
    aboutLayout->addRow(ru ? "О TrustTunnel:" : "About TrustTunnel:", ttLabel);
    addNavItem(ru ? "О программе" : "About", aboutPage, QIcon(":/icons/about.svg"));

    // ════════════════════════════════════════════════════════════════
    // 8. Лицензии / Licenses
    // ════════════════════════════════════════════════════════════════
    auto *licensePage = new QWidget(this);
    auto *licenseLayout = new QVBoxLayout(licensePage);
    auto *licenseView = new QTextBrowser(licensePage);
    licenseView->setReadOnly(true);
    licenseView->setFontFamily("Menlo, Consolas, monospace");
    licenseView->setOpenExternalLinks(true);
    licenseView->setLineWrapMode(QTextEdit::NoWrap);
    licenseView->setPlainText(loadLicenseText());
    licenseLayout->addWidget(licenseView);
    addNavItem(ru ? "Лицензии" : "Licenses", licensePage, QIcon(":/icons/licenses.svg"));

    // ════════════════════════════════════════════════════════════════
    // Connections for buttons defined above
    // ════════════════════════════════════════════════════════════════
    connect(reinstallBtn, &QPushButton::clicked, this, [this, ru]() {
        auto res = QMessageBox::question(this,
                ru ? "Переустановка туннелей" : "Reinstall Tunnels",
                ru ? "Виртуальные адаптеры будут удалены и пересозданы при следующем подключении. Продолжить?"
                   : "Virtual adapters will be removed and recreated on next connection. Continue?",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (res == QMessageBox::Yes) {
            m_reinstallTunnels = true;
            emit advancedAction("reinstall_tunnels");
        }
    });

    connect(flushDnsBtn, &QPushButton::clicked, this, [this]() {
        m_flushDns = true;
        emit advancedAction("flush_dns");
    });

    connect(clearSslBtn, &QPushButton::clicked, this, [this, ru]() {
        m_clearSslCache = true;
        QMessageBox::information(this,
                ru ? "Кеш SSL" : "SSL Cache",
                ru ? "Кеш SSL-сессий будет очищен." : "SSL session cache will be cleared.");
        emit advancedAction("clear_ssl_cache");
    });

    connect(resetSettingsBtn, &QPushButton::clicked, this, [this, ru]() {
        auto res = QMessageBox::warning(this,
                ru ? "Сброс настроек" : "Reset Settings",
                ru ? "Все настройки приложения будут сброшены к значениям по умолчанию. Это действие нельзя отменить. Продолжить?"
                   : "All application settings will be reset to defaults. This cannot be undone. Continue?",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (res == QMessageBox::Yes) {
            m_resetSettings = true;
            accept();
        }
    });

    connect(browseCacheBtn, &QPushButton::clicked, this, [this, ru]() {
        const QString path = QFileDialog::getSaveFileName(
                this,
                ru ? "Путь кеша подсетей" : "Cache file path",
                m_routingCacheEdit->text(),
                ru ? "Text files (*.lst *.txt);;All files (*)" : "Text files (*.lst *.txt);;All files (*)");
        if (!path.isEmpty()) {
            m_routingCacheEdit->setText(path);
        }
    });

    connect(pickPathBtn, &QPushButton::clicked, this, [this, ru]() {
        const QString path = QFileDialog::getSaveFileName(
                this,
                ru ? "Путь файла логов" : "Log file path",
                m_logPathEdit->text().isEmpty() ? "/tmp/freetunnel.log" : m_logPathEdit->text(),
                ru ? "Log files (*.log);;All files (*)" : "Log files (*.log);;All files (*)");
        if (!path.isEmpty()) {
            m_logPathEdit->setText(path);
        }
    });

    bodyLayout->addWidget(m_navList);
    bodyLayout->addWidget(m_stack, 1);
    layout->addLayout(bodyLayout);

    connect(m_navList, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
    if (m_navList->count() > 0) {
        m_navList->setCurrentRow(0);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

bool SettingsDialog::saveLogs() const { return m_saveLogsCheck && m_saveLogsCheck->isChecked(); }
QString SettingsDialog::logLevel() const { return m_logLevelCombo ? m_logLevelCombo->currentText() : "info"; }
QString SettingsDialog::logPath() const { return m_logPathEdit ? m_logPathEdit->text().trimmed() : QString(); }
QString SettingsDialog::themeMode() const { return m_themeModeCombo ? m_themeModeCombo->currentData().toString() : "system"; }
bool SettingsDialog::autoConnectOnStart() const { return m_autoConnectCheck && m_autoConnectCheck->isChecked(); }
bool SettingsDialog::showLogsPanel() const { return m_showLogsPanelCheck && m_showLogsPanelCheck->isChecked(); }
bool SettingsDialog::showTrafficInStatus() const { return m_showTrafficCheck && m_showTrafficCheck->isChecked(); }
bool SettingsDialog::showTrafficGraph() const { return m_showTrafficGraphCheck && m_showTrafficGraphCheck->isChecked(); }
bool SettingsDialog::notifyOnState() const { return m_notifyOnStateCheck && m_notifyOnStateCheck->isChecked(); }
bool SettingsDialog::notifyOnlyErrors() const { return m_notifyErrorsOnlyCheck && m_notifyErrorsOnlyCheck->isChecked(); }
bool SettingsDialog::killswitchEnabled() const { return m_killswitchCheck && m_killswitchCheck->isChecked(); }
bool SettingsDialog::strictCertificateCheck() const { return m_strictCertCheck && m_strictCertCheck->isChecked(); }
bool SettingsDialog::routingEnabled() const { return m_routingEnableCheck && m_routingEnableCheck->isChecked(); }
QString SettingsDialog::routingMode() const {
    if (m_routingBypassRadio && m_routingBypassRadio->isChecked()) return "bypass_ru";
    return "tunnel_ru";
}
QString SettingsDialog::routingSourceUrl() const { return m_routingUrlEdit ? m_routingUrlEdit->text().trimmed() : QString(); }
QString SettingsDialog::routingCachePath() const { return m_routingCacheEdit ? m_routingCacheEdit->text().trimmed() : QString(); }
bool SettingsDialog::reinstallTunnelsRequested() const { return m_reinstallTunnels; }
bool SettingsDialog::flushDnsRequested() const { return m_flushDns; }
bool SettingsDialog::clearSslCacheRequested() const { return m_clearSslCache; }
bool SettingsDialog::resetSettingsRequested() const { return m_resetSettings; }
bool SettingsDialog::customDnsEnabled() const { return m_customDnsCheck && m_customDnsCheck->isChecked(); }
QStringList SettingsDialog::customDnsServers() const {
    if (!m_customDnsEdit) return {};
    const QString text = m_customDnsEdit->toPlainText().trimmed();
    if (text.isEmpty()) return {};
    QStringList lines;
    for (const QString &line : text.split('\n', Qt::SkipEmptyParts)) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty() && !trimmed.startsWith('#')) {
            lines.append(trimmed);
        }
    }
    return lines;
}
bool SettingsDialog::domainBypassEnabled() const { return m_domainBypassCheck && m_domainBypassCheck->isChecked(); }
QStringList SettingsDialog::domainBypassRules() const {
    if (!m_domainBypassEdit) return {};
    const QString text = m_domainBypassEdit->toPlainText().trimmed();
    if (text.isEmpty()) return {};
    QStringList lines;
    for (const QString &line : text.split('\n', Qt::SkipEmptyParts)) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty() && !trimmed.startsWith('#')) {
            lines.append(trimmed);
        }
    }
    return lines;
}
bool SettingsDialog::scanAdapterConflicts() const { return m_scanConflictsCheck && m_scanConflictsCheck->isChecked(); }
bool SettingsDialog::sshBypassEnabled() const { return m_sshBypassCheck && m_sshBypassCheck->isChecked(); }
bool SettingsDialog::p2pBypassEnabled() const { return m_p2pBypassCheck && m_p2pBypassCheck->isChecked(); }
bool SettingsDialog::perAppRulesEnabled() const { return m_perAppRulesCheck && m_perAppRulesCheck->isChecked(); }

#include "ProcessManager.h"

void SettingsDialog::showPerAppRulesDialog(const QString &lang, const AppSettings &settings) {
    const bool ru = (lang == "ru");
    auto *dlg = new QDialog(this);
    dlg->setWindowTitle(ru ? "Правила для приложений" : "Per-App Rules");
    dlg->resize(700, 500);

    auto *layout = new QVBoxLayout(dlg);

    // Current rules section
    auto *rulesLabel = new QLabel(ru ? "Текущие правила:" : "Current Rules:");
    layout->addWidget(rulesLabel);

    auto *listWidget = new QListWidget(dlg);
    QStringList ruleItems;
    for (const auto &rule : settings.app_rules) {
        const QString text = QString("%1: %2 KB/s").arg(rule.appName, QString::number(rule.throttleSpeed));
        ruleItems << text;
    }
    listWidget->addItems(ruleItems);
    layout->addWidget(listWidget, 1);

    // Process selection section
    auto *processLabel = new QLabel(ru ? "Запущенные приложения:" : "Running Applications:");
    layout->addWidget(processLabel);

    // Search box
    auto *searchEdit = new QLineEdit(dlg);
    searchEdit->setPlaceholderText(ru ? "Поиск приложения..." : "Search application...");
    layout->addWidget(searchEdit);

    auto *processListWidget = new QListWidget(dlg);
    processListWidget->setSelectionMode(QAbstractItemView::MultiSelection);
    processListWidget->setMaximumHeight(150);

    auto updateProcessList = [processListWidget](const QString &filter) {
        processListWidget->clear();
        auto processes = ProcessManager::getRunningProcesses();
        for (const auto &proc : processes) {
            if (proc.displayName.length() > 0) {
                // Фильтр поиска
                if (!filter.isEmpty() && !proc.displayName.toLower().contains(filter.toLower())) {
                    continue;
                }
                const QString text = QString("%1 (%2)").arg(proc.displayName, proc.path);
                auto *item = new QListWidgetItem(text, processListWidget);
                item->setData(Qt::UserRole, proc.path);
            }
        }
    };

    // Первоначальная загрузка
    updateProcessList("");

    // Подключаем поиск
    connect(searchEdit, &QLineEdit::textChanged, [updateProcessList](const QString &text) {
        updateProcessList(text);
    });

    layout->addWidget(processListWidget);

    // Rule selector
    auto *ruleLayout = new QHBoxLayout();
    auto *ruleLabel = new QLabel(ru ? "Правило:" : "Rule:");
    auto *ruleCombo = new QComboBox(dlg);
    ruleCombo->addItem(ru ? "Туннель (весь трафик через VPN)" : "Tunnel (all traffic through VPN)");
    ruleCombo->addItem(ru ? "Обход (без VPN)" : "Bypass (no VPN)");
    ruleCombo->addItem(ru ? "Дросселирование" : "Throttle");
    ruleLayout->addWidget(ruleLabel);
    ruleLayout->addWidget(ruleCombo);
    ruleLayout->addStretch();
    layout->addLayout(ruleLayout);

    // Buttons
    auto *btnLayout = new QHBoxLayout();
    auto *addBtn = new QPushButton(ru ? "Добавить выбранное" : "Add Selected");
    auto *removeBtn = new QPushButton(ru ? "Удалить" : "Remove");
    auto *closeBtn = new QPushButton(ru ? "Закрыть" : "Close");
    btnLayout->addWidget(addBtn);
    btnLayout->addWidget(removeBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    layout->addLayout(btnLayout);

    connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::accept);
    connect(removeBtn, &QPushButton::clicked, [listWidget, ru, dlg]() {
        auto *item = listWidget->currentItem();
        if (!item) {
            QMessageBox::information(dlg, ru ? "Ошибка" : "Error",
                ru ? "Выберите приложение для удаления" : "Select an app to remove");
            return;
        }
        delete item;
    });
    connect(addBtn, &QPushButton::clicked, [processListWidget, ruleCombo, listWidget, ru, dlg]() {
        auto selectedItems = processListWidget->selectedItems();
        if (selectedItems.isEmpty()) {
            QMessageBox::information(dlg, ru ? "Ошибка" : "Error",
                ru ? "Выберите одно или несколько приложений" : "Select one or more apps");
            return;
        }
        const QString rule = ruleCombo->currentText();
        for (auto *item : selectedItems) {
            const QString appName = item->text().split(" (")[0];
            const QString text = QString("%1: %2").arg(appName, rule);
            // Проверим, не добавлен ли уже
            bool exists = false;
            for (int i = 0; i < listWidget->count(); i++) {
                if (listWidget->item(i)->text().startsWith(appName + ":")) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                listWidget->addItem(text);
            }
        }
    });

    dlg->exec();
    dlg->deleteLater();
}
