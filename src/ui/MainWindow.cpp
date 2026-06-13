#include "MainWindow.h"
#include "ConfigWizard.h"
#include "ConnectionRing.h"
#include "TrafficGraph.h"
#include "core/ConfigImport.h"

#include <QApplication>
#include <QAction>
#include <QActionGroup>
#include <QClipboard>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QInputDialog>
#include <QPalette>
#include <QPainter>
#include <QPixmap>
#include <QScrollArea>
#include <QSvgRenderer>
#include <QProgressDialog>
#include <QPushButton>
#include <QPointer>
#include <QSet>
#include <QToolButton>
#include <QStatusBar>
#include <QStyle>
#include <QStyleHints>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWidget>
#include <QToolButton>
#include <QtGlobal>

#include <thread>

#include "common/logger.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
#endif

#include <QDesktopServices>
#include <QProcess>
#include <QTemporaryFile>

#include "AppSettings.h"
#include "AppUiUtils.h"
#include "ConfigInspector.h"
#include "ConfigStore.h"
#include "SettingsDialog.h"
#include "UpdateChecker.h"
#include "qt_trusttunnel_client.h"

static ag::LogLevel parseLogLevel(const QString &level) {
    const QString l = level.toLower();
    if (l == "error") return ag::LOG_LEVEL_ERROR;
    if (l == "warn" || l == "warning") return ag::LOG_LEVEL_WARN;
    if (l == "debug") return ag::LOG_LEVEL_DEBUG;
    if (l == "trace") return ag::LOG_LEVEL_TRACE;
    return ag::LOG_LEVEL_INFO;
}

class MainWindow : public QMainWindow {
public:
    MainWindow() {
#ifndef _WIN32
        m_isRoot = (::geteuid() == 0);
#endif
        m_appSettings = loadAppSettings();

        enforceFirstRunElevation();

        setupUi();
        setupLogic();
        applyTheme();
        applyLanguage(m_appSettings.language == "ru" ? "ru" : "en");
        refreshStoredList();
        processStartupArguments();
        if (m_configPath->text().trimmed().isEmpty()) {
            if (!m_appSettings.last_config_path.isEmpty() && QFile::exists(m_appSettings.last_config_path)) {
                m_configPath->setText(m_appSettings.last_config_path);
            } else if (m_configsList->count() > 0) {
                m_configsList->setCurrentRow(0);
                if (m_configsList->currentItem()) {
                    m_configPath->setText(m_configsList->currentItem()->text());
                }
            }
        }
        if (m_appSettings.auto_connect_on_start) {
            QTimer::singleShot(400, this, [this]() {
                if (!m_configPath->text().trimmed().isEmpty()) {
                    m_connectButton->click();
                } else {
                    log(tr("Auto-connect skipped: no config selected"));
                }
            });
        }

        // Restore logs panel visibility from settings
        if (m_toggleLogsAction) {
            m_toggleLogsAction->setChecked(m_appSettings.show_logs_panel);
            m_logBox->setVisible(m_appSettings.show_logs_panel);
        }

        // Restore notification toggles in tray availability
        if (!m_appSettings.notify_on_state && m_tray) {
            m_tray->hide();
            m_tray->show(); // ensure icon exists but notifications obey setting
        }
    }

protected:
    void closeEvent(QCloseEvent *event) override {
        if (!m_forceExit && m_tray && m_tray->isVisible()) {
            hide();
            statusBar()->showMessage(tr("Running in tray"), 3000);
            m_tray->showMessage(windowTitle(), tr("Application minimized to tray"), QSystemTrayIcon::Information, 1500);
            event->ignore();
            return;
        }
        QMainWindow::closeEvent(event);
    }

    bool eventFilter(QObject *obj, QEvent *event) override {
#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
        if (obj == qApp && event->type() == QEvent::ApplicationPaletteChange && m_appSettings.theme_mode == "system") {
            applyTheme();
        }
#endif
        return QMainWindow::eventFilter(obj, event);
    }

private:
    bool selectConfigPath(const QString &path) {
        const QString normalized = QFileInfo(path).absoluteFilePath();
        if (!QFileInfo::exists(normalized)) {
            return false;
        }
        m_configPath->setText(normalized);
        addCurrentToStorage();
        refreshStoredList();
        return true;
    }

    bool importConfigFromDeeplink(const QString &deeplink) {
        const QString trimmedLink = deeplink.trimmed();

        // Only the official TrustTunnel deep link is supported: tt://?<base64url>.
        // NOTE: the link contains cleartext credentials, so it is never logged.
        if (!trimmedLink.startsWith(QLatin1String("tt://"))) {
            log(tr("Unsupported deep link (expected a tt:// link)"));
            return false;
        }
        QString err;
        auto prepared = freetunnel::prepareDeepLinkImport(trimmedLink, &err);
        if (!prepared) {
            log(tr("Deep link error: %1").arg(err));
            return false;
        }
        const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QDir().mkpath(base);
        const QString target = QDir(base).filePath(prepared->fileName);
        QFile f(target);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            log(tr("Failed to write imported config: %1").arg(target));
            return false;
        }
        f.write(prepared->tomlContent.toUtf8());
        f.close();
        selectConfigPath(target);
        statusBar()->showMessage(tr("Config imported from tt:// deep link"), 2500);
        return true;
    }

    // Download routing list synchronously but process UI events during the
    // wait so the window remains responsive and the progress dialog animates.
    // The old implementation used QEventLoop::exec() which re-enters the event
    // loop — a well-known source of subtle reentrancy bugs. This version
    // uses a simple poll loop with processEvents() instead.
    bool downloadRoutingList(const QString &url, const QString &targetPath) {
        QNetworkAccessManager mgr;
        QNetworkRequest req(url);
        QNetworkReply *reply = mgr.get(req);

        QElapsedTimer elapsed;
        elapsed.start();
        constexpr int TIMEOUT_MS = 8000;

        while (!reply->isFinished()) {
            if (elapsed.elapsed() >= TIMEOUT_MS) {
                reply->abort();
                log(tr("Routing download timed out"));
                reply->deleteLater();
                return false;
            }
            // Process GUI events briefly — keeps the progress dialog alive
            // without re-entering a nested event loop.
            qApp->processEvents(QEventLoop::AllEvents, 50);
        }

        if (reply->error() != QNetworkReply::NoError) {
            log(tr("Routing download failed: %1").arg(reply->errorString()));
            reply->deleteLater();
            return false;
        }
        QByteArray data = reply->readAll();
        reply->deleteLater();
        QFileInfo info(targetPath);
        QDir().mkpath(info.absolutePath());
        QFile f(targetPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            log(tr("Cannot write routing cache: %1").arg(targetPath));
            return false;
        }
        f.write(data);
        return true;
    }

    bool prepareRoutingRules(std::vector<std::string> &includeOut, std::vector<std::string> &excludeOut) {
        if (!m_appSettings.routing_enabled) {
            return true;
        }
        const QString cache = m_appSettings.routing_cache_path;
        const QString url = m_appSettings.routing_source_url;
        QFileInfo fi(cache);
        std::unique_ptr<QProgressDialog> routingProgress;
        if (!fi.exists() || fi.size() == 0) {
            log(tr("Routing cache missing, downloading..."));
            routingProgress = std::make_unique<QProgressDialog>(
                    tr("Downloading routing list..."), QString(), 0, 0, this,
                    Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
            routingProgress->setWindowModality(Qt::ApplicationModal);
            routingProgress->setAutoClose(true);
            routingProgress->setMinimumDuration(0);
            routingProgress->setRange(0, 0);
            routingProgress->show();
            qApp->processEvents(QEventLoop::AllEvents, 50);
            if (!downloadRoutingList(url, cache)) {
                QMessageBox::warning(this, tr("Routing"),
                        tr("Failed to download routing list.\nCheck connection and try again."));
                if (routingProgress) routingProgress->close();
                return false;
            }
            log(tr("Routing list cached to %1").arg(cache));
            if (routingProgress) routingProgress->close();
        }
        QFile f(cache);
        if (!f.open(QIODevice::ReadOnly)) {
            log(tr("Failed to open routing cache: %1").arg(cache));
            return false;
        }
        includeOut.clear();
        excludeOut.clear();
        while (!f.atEnd()) {
            const QByteArray line = f.readLine();
            QString s = QString::fromUtf8(line).trimmed();
            if (s.isEmpty() || s.startsWith('#')) continue;
            if (m_appSettings.routing_mode == "bypass_ru") {
                excludeOut.emplace_back(s.toStdString());
            } else {
                includeOut.emplace_back(s.toStdString());
            }
        }
        const int count = static_cast<int>(includeOut.size() + excludeOut.size());
        log(tr("Routing rules loaded: %1 entries").arg(count));
        return true;
    }

    bool relaunchElevated() {
#ifndef _WIN32
        QString errorText;
        const QString exe = QCoreApplication::applicationFilePath();
        const QString cmd = QString("exec %1 >/tmp/freetunnel.relaunch.log 2>&1 &").arg(shellEscape(exe));
        if (!runElevatedShell(cmd, &errorText)) {
            QMessageBox::critical(this, tr("Elevation Failed"),
                    tr("Failed to restart with administrator privileges.\n\n%1").arg(errorText));
            return false;
        }
        m_forceExit = true;
        qApp->quit();
        return true;
#else
        const QString exe = QCoreApplication::applicationFilePath();
        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.hwnd = reinterpret_cast<HWND>(this->winId());
        sei.lpVerb = L"runas";
        std::wstring wexe = exe.toStdWString();
        sei.lpFile = wexe.c_str();
        sei.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&sei)) {
            QMessageBox::critical(this, tr("Elevation Failed"),
                    tr("Failed to restart with administrator privileges.\n\nError code: %1")
                            .arg(QString::number(GetLastError())));
            return false;
        }
        m_forceExit = true;
        qApp->quit();
        return true;
#endif
    }

#ifdef _WIN32
    bool isProcessElevated() const {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
            return false;
        }
        TOKEN_ELEVATION elevation{};
        DWORD size = 0;
        const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
        CloseHandle(token);
        return ok && elevation.TokenIsElevated;
    }
#endif

    void processStartupArguments() {
        const QStringList args = QCoreApplication::arguments();
        for (int i = 1; i < args.size(); ++i) {
            const QString arg = args.at(i).trimmed();
            if (arg.isEmpty()) {
                continue;
            }
            if (arg.startsWith("tt://")) { // official deep link (case-sensitive)
                importConfigFromDeeplink(arg);
                continue;
            }
            if (arg.endsWith(".toml", Qt::CaseInsensitive)) {
                selectConfigPath(arg);
            }
        }
    }

    void createConfigFromTemplate() {
        auto *wizard = new ConfigWizard(m_currentLang, this);
        wizard->setAttribute(Qt::WA_DeleteOnClose);
        connect(wizard, &ConfigWizard::configCreated, this, [this](const QString &path) {
            selectConfigPath(path);
            statusBar()->showMessage(tr("Config created"), 2500);
        });
        wizard->show();
    }

    void setupUi() {
        setWindowTitle("FreeTunnel");
        resize(460, 720);
        setWindowIcon(makeAppIcon());

        // ── Menu bar (minimal) ──
        m_appMenu = menuBar()->addMenu("App");
        auto *appMenu = m_appMenu;
        m_settingsAction = appMenu->addAction("Settings");
        m_importDeeplinkAction = appMenu->addAction("Import Deeplink");
        m_createConfigAction = appMenu->addAction("Create Config");
        appMenu->addSeparator();
        m_checkUpdateAction = appMenu->addAction("Check for Updates...");
        appMenu->addSeparator();
        m_quitAction = appMenu->addAction("Quit");

        m_viewMenu = menuBar()->addMenu("View");
        auto *viewMenu = m_viewMenu;
        m_toggleLogsAction = viewMenu->addAction("Hide Logs");
        m_toggleLogsAction->setCheckable(true);
        m_toggleLogsAction->setChecked(true);

        m_languageMenu = menuBar()->addMenu("Language");
        auto *languageMenu = m_languageMenu;
        auto *langGroup = new QActionGroup(this);
        langGroup->setExclusive(true);
        m_langEnAction = languageMenu->addAction("English");
        m_langEnAction->setCheckable(true);
        m_langRuAction = languageMenu->addAction("Русский");
        m_langRuAction->setCheckable(true);
        langGroup->addAction(m_langEnAction);
        langGroup->addAction(m_langRuAction);
        m_langEnAction->setChecked(true);

        // ── Central root ──
        auto *root = new QWidget(this);
        auto *rootLayout = new QVBoxLayout(root);
        rootLayout->setContentsMargins(0, 0, 0, 0);
        rootLayout->setSpacing(0);

        // ── Stacked widget (3 pages) ──
        m_stack = new QStackedWidget(root);

        // ════════════════════════════════════════════
        // PAGE 0: HOME — ring + server info + graph
        // ════════════════════════════════════════════
        auto *homePage = new QWidget();
        auto *homeLayout = new QVBoxLayout(homePage);
        homeLayout->setContentsMargins(20, 8, 20, 0);
        homeLayout->setSpacing(0);

        homeLayout->addStretch(2);

        // Connection ring
        m_ring = new ConnectionRing(homePage);
        homeLayout->addWidget(m_ring, 0, Qt::AlignHCenter);

        homeLayout->addStretch(1);

        // Server info card
        auto *serverCard = new QWidget(homePage);
        serverCard->setObjectName("serverCard");
        auto *cardLayout = new QVBoxLayout(serverCard);
        cardLayout->setContentsMargins(20, 14, 20, 14);
        cardLayout->setSpacing(4);

        m_serverNameLabel = new QLabel("—", serverCard);
        m_serverNameLabel->setObjectName("serverNameLabel");
        m_serverNameLabel->setAlignment(Qt::AlignCenter);

        m_serverDetailLabel = new QLabel("", serverCard);
        m_serverDetailLabel->setObjectName("serverDetailLabel");
        m_serverDetailLabel->setAlignment(Qt::AlignCenter);

        // Connect / Disconnect buttons (compact, inside card)
        auto *btnRow = new QHBoxLayout();
        btnRow->setSpacing(10);
        m_connectButton = new QPushButton(homePage);
        m_disconnectButton = new QPushButton(homePage);
        m_connectButton->setObjectName("connectButton");
        m_disconnectButton->setObjectName("disconnectButton");
        m_connectButton->setMinimumHeight(42);
        m_disconnectButton->setMinimumHeight(42);
        m_disconnectButton->setEnabled(false);
        btnRow->addWidget(m_connectButton);
        btnRow->addWidget(m_disconnectButton);

        cardLayout->addWidget(m_serverNameLabel);
        cardLayout->addWidget(m_serverDetailLabel);
        cardLayout->addSpacing(8);
        cardLayout->addLayout(btnRow);

        homeLayout->addWidget(serverCard);
        homeLayout->addSpacing(6);

        // Hidden state label (kept for logic compatibility)
        m_stateLabel = new QLabel(homePage);
        m_stateLabel->setObjectName("stateLabel");
        m_stateLabel->setVisible(false);
        m_stateLabel->setText(tr("VPN: Disconnected"));
        homeLayout->addWidget(m_stateLabel);

        // Traffic graph
        m_trafficGraph = new TrafficGraph(homePage);
        m_trafficGraph->setFixedHeight(70);
        m_trafficGraph->setVisible(m_appSettings.show_traffic_graph);
        homeLayout->addWidget(m_trafficGraph);

        // Traffic stats label
        m_trafficStatsLabel = new QLabel(homePage);
        m_trafficStatsLabel->setAlignment(Qt::AlignCenter);
        m_trafficStatsLabel->setStyleSheet("color: #B0B0B0; font-size: 11px;");
        m_trafficStatsLabel->setText(tr("RX: 0 B | TX: 0 B"));
        homeLayout->addWidget(m_trafficStatsLabel);

        m_stack->addWidget(homePage);

        // ════════════════════════════════════════════
        // PAGE 1: CONFIGS
        // ════════════════════════════════════════════
        auto *configsPage = new QWidget();
        auto *configsPageLayout = new QVBoxLayout(configsPage);
        configsPageLayout->setContentsMargins(16, 12, 16, 12);
        configsPageLayout->setSpacing(10);

        auto *configsTitleLabel = new QLabel(configsPage);
        configsTitleLabel->setObjectName("pageTitle");
        configsTitleLabel->setText("Configs");
        m_configsPageTitle = configsTitleLabel;
        configsPageLayout->addWidget(configsTitleLabel);

        m_configsList = new QListWidget(configsPage);
        configsPageLayout->addWidget(m_configsList, 1);

        auto *configInfoBox = new QWidget(configsPage);
        configInfoBox->setObjectName("serverCard");
        auto *configInfoLayout = new QVBoxLayout(configInfoBox);
        configInfoLayout->setContentsMargins(12, 12, 12, 12);
        configInfoLayout->setSpacing(4);

        m_configNameLabel = new QLabel("—", configInfoBox);
        m_configNameLabel->setObjectName("serverNameLabel");
        m_configNameLabel->setAlignment(Qt::AlignCenter);

        m_configDetailLabel = new QLabel("", configInfoBox);
        m_configDetailLabel->setObjectName("serverDetailLabel");
        m_configDetailLabel->setAlignment(Qt::AlignCenter);

        configInfoLayout->addWidget(m_configNameLabel);
        configInfoLayout->addWidget(m_configDetailLabel);
        configsPageLayout->addWidget(configInfoBox);

        auto *pathRow = new QHBoxLayout();
        m_configPath = new QLineEdit(configsPage);
        m_configPath->setPlaceholderText("Path to TrustTunnel TOML config");
        m_configPath->setVisible(false);
        m_browseButton = new QPushButton(configsPage);
        m_viewConfigButton = new QPushButton(configsPage);
        pathRow->addWidget(m_configPath, 1);
        pathRow->addWidget(m_browseButton);
        pathRow->addWidget(m_viewConfigButton);
        configsPageLayout->addLayout(pathRow);

        auto *configsBtnRow = new QHBoxLayout();
        configsBtnRow->setSpacing(8);
        m_removeConfigButton = new QPushButton(configsPage);
        m_pingConfigButton = new QPushButton(configsPage);
        m_qrConfigButton = new QPushButton(configsPage);
        configsBtnRow->addWidget(m_browseButton);
        configsBtnRow->addWidget(m_removeConfigButton);
        configsBtnRow->addWidget(m_viewConfigButton);
        configsBtnRow->addWidget(m_pingConfigButton);
        configsBtnRow->addWidget(m_qrConfigButton);
        configsPageLayout->addLayout(configsBtnRow);

        // Hidden group boxes (for compatibility with applyLanguage)
        m_configsBox = new QGroupBox(configsPage);
        m_configsBox->setVisible(false);
        m_controlBox = new QGroupBox(configsPage);
        m_controlBox->setVisible(false);

        m_stack->addWidget(configsPage);

        // ════════════════════════════════════════════
        // PAGE 2: LOGS
        // ════════════════════════════════════════════
        auto *logsPage = new QWidget();
        auto *logsLayout = new QVBoxLayout(logsPage);
        logsLayout->setContentsMargins(16, 12, 16, 12);
        logsLayout->setSpacing(10);

        auto *logsTitleLabel = new QLabel("Logs", logsPage);
        logsTitleLabel->setObjectName("pageTitle");
        m_logsPageTitle = logsTitleLabel;
        logsLayout->addWidget(logsTitleLabel);

        m_logView = new QTextEdit(logsPage);
        m_logView->setReadOnly(true);
        m_logView->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        m_logView->setContextMenuPolicy(Qt::NoContextMenu);
        logsLayout->addWidget(m_logView, 1);

        m_logBox = new QGroupBox(logsPage);
        m_logBox->setVisible(false);   // compatibility stub

        m_stack->addWidget(logsPage);

        rootLayout->addWidget(m_stack, 1);

        // ════════════════════════════════════════════
        // BOTTOM NAVIGATION BAR
        // ════════════════════════════════════════════
        auto *navBar = new QWidget(root);
        navBar->setObjectName("navBar");
        navBar->setFixedHeight(56);
        auto *navLayout = new QHBoxLayout(navBar);
        navLayout->setContentsMargins(0, 0, 0, 0);
        navLayout->setSpacing(0);

        auto makeNavBtn = [&](const QString &iconPath, const QString &label) -> QPushButton* {
            auto *btn = new QPushButton(navBar);
            btn->setIcon(QIcon(iconPath));
            btn->setIconSize(QSize(22, 22));
            btn->setText(label);
            btn->setObjectName("navButton");
            btn->setFlat(true);
            btn->setCheckable(true);
            btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            navLayout->addWidget(btn);
            return btn;
        };

        m_navHome    = makeNavBtn(":/icons/home.svg",     "Home");
        m_navConfigs = makeNavBtn(":/icons/configs.svg",  "Configs");
        m_navLogs    = makeNavBtn(":/icons/log.svg",      "Logs");
        m_navSettings= makeNavBtn(":/icons/settings.svg", "Settings");
        m_navHome->setChecked(true);

        auto *navGroup = new QActionGroup(this);  // for exclusive toggle
        Q_UNUSED(navGroup);

        auto switchPage = [this](int idx) {
            m_stack->setCurrentIndex(idx);
            m_navHome->setChecked(idx == 0);
            m_navConfigs->setChecked(idx == 1);
            m_navLogs->setChecked(idx == 2);
            m_navSettings->setChecked(false);
        };

        connect(m_navHome,    &QPushButton::clicked, this, [switchPage]() { switchPage(0); });
        connect(m_navConfigs, &QPushButton::clicked, this, [switchPage]() { switchPage(1); });
        connect(m_navLogs,    &QPushButton::clicked, this, [switchPage]() { switchPage(2); });
        connect(m_navSettings, &QPushButton::clicked, this, [this, switchPage]() {
            switchPage(m_stack->currentIndex()); // keep current page
            openSettingsDialog();
        });

        rootLayout->addWidget(navBar);

        setCentralWidget(root);
        statusBar()->showMessage("Ready");

        // ── VPN Client ──
        m_vpnClient = new QtTrustTunnelClient(this);
        m_vpnClient->setLogLevel(m_appSettings.log_level);

        const ag::LogLevel uiLogLevel = parseLogLevel(m_appSettings.log_level);
        ag::Logger::set_callback([this, uiLogLevel](ag::LogLevel level, std::string_view msg) {
            if (level > uiLogLevel) return;
            const QString line = QString::fromUtf8(msg.data(), static_cast<int>(msg.size()));
            QMetaObject::invokeMethod(this, [this, line]() {
                log(QStringLiteral("[core] ") + line);
            }, Qt::QueuedConnection);
        });

        // Icon assignments (will be properly colored in recolorIcons via applyTheme)
        // Remove wrong icon assignments — config buttons use text only

        // ── System tray ──
        if (QSystemTrayIcon::isSystemTrayAvailable()) {
            auto *menu = new QMenu(this);
            auto *openAction = menu->addAction("Open");
            auto *exitAction = menu->addAction("Exit");

            m_tray = new QSystemTrayIcon(this);
            m_tray->setIcon(makeAppIcon());
            m_tray->setContextMenu(menu);
            m_tray->show();

            connect(openAction, &QAction::triggered, this, [this]() {
                showNormal(); raise(); activateWindow();
            });
            connect(exitAction, &QAction::triggered, this, [this]() {
                m_forceExit = true;
                if (m_vpnClient) m_vpnClient->disconnectVpn();
                qApp->quit();
            });
            connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                    showNormal(); raise(); activateWindow();
                }
            });
            qApp->setQuitOnLastWindowClosed(false);
        }

        m_hideLogsBtn = nullptr; // not used in new layout
    }

    void setupLogic() {
        auto syncLogsVisibility = [this](bool on) {
            m_appSettings.show_logs_panel = on;
            saveAppSettings(m_appSettings);
            applyLanguage(m_currentLang);
        };

        connect(m_toggleLogsAction, &QAction::toggled, this, syncLogsVisibility);

        // Ring click toggles VPN
        connect(m_ring, &ConnectionRing::clicked, this, [this]() {
            const auto s = m_vpnClient->state();
            if (s == QtTrustTunnelClient::State::Disconnected || s == QtTrustTunnelClient::State::Error) {
                m_connectButton->click();
            } else {
                m_disconnectButton->click();
            }
        });

        connect(m_langEnAction, &QAction::triggered, this, [this]() {
            applyLanguage("en");
            m_appSettings.language = "en";
            saveAppSettings(m_appSettings);
        });
        connect(m_langRuAction, &QAction::triggered, this, [this]() {
            applyLanguage("ru");
            m_appSettings.language = "ru";
            saveAppSettings(m_appSettings);
        });
        connect(m_settingsAction, &QAction::triggered, this, [this]() {
            openSettingsDialog();
        });
        if (m_settingsMenuAction) {
            connect(m_settingsMenuAction, &QAction::triggered, this, [this]() {
                openSettingsDialog();
            });
        }
        connect(m_quitAction, &QAction::triggered, this, [this]() {
            m_forceExit = true;
            close();
        });
        connect(m_importDeeplinkAction, &QAction::triggered, this, [this]() {
            bool ok = false;
            const QString link = QInputDialog::getText(
                    this, tr("Import Deeplink"), tr("Paste deeplink URL:"), QLineEdit::Normal, QString(), &ok);
            if (ok && !link.trimmed().isEmpty()) {
                importConfigFromDeeplink(link);
            }
        });
        connect(m_createConfigAction, &QAction::triggered, this, [this]() {
            createConfigFromTemplate();
        });

        // --- Auto-updater ---
        m_updateChecker = new UpdateChecker(
            QStringLiteral("enrvate/freetunnel"),
            QStringLiteral(FREETUNNEL_VERSION),
            this);

        connect(m_checkUpdateAction, &QAction::triggered, this, [this]() {
            statusBar()->showMessage(tr("Checking for updates..."), 3000);
            m_updateChecker->checkNow();
        });

        connect(m_updateChecker, &UpdateChecker::updateAvailable, this,
                [this](const UpdateChecker::ReleaseInfo &info) {
            showUpdateDialog(info);
        });
        connect(m_updateChecker, &UpdateChecker::noUpdateAvailable, this,
                [this](const QString &msg) {
            log(tr("Update check: %1").arg(msg));
        });

        // Check for updates 3 seconds after startup (non-blocking)
        QTimer::singleShot(3000, this, [this]() {
            m_updateChecker->checkNow();
        });

        connect(m_removeConfigButton, &QPushButton::clicked, this, [this]() {
            QListWidgetItem *item = m_configsList->currentItem();
            if (!item) {
                return;
            }
            const QString selected = item->text();
            QStringList configs = loadStoredConfigs();
            configs.removeAll(selected);
            saveStoredConfigs(configs);
            if (m_configPath->text() == selected) {
                m_configPath->clear();
            }
            refreshStoredList();
        });

        connect(m_pingConfigButton, &QPushButton::clicked, this, [this]() {
            QString path = m_configPath->text();
            if (path.isEmpty() && m_configsList->currentItem()) {
                path = m_configsList->currentItem()->text();
            }
            if (path.isEmpty()) {
                log(tr("Ping: choose config first"));
                return;
            }
            log(tr("Ping %1 ...").arg(path));
            m_pingConfigButton->setEnabled(false);
            // Run ping in a worker thread to avoid blocking the UI for 1.8s.
            auto *watcher = new QObject(this);
            std::thread([this, path, watcher]() {
                const QString result = pingConfigFile(path);
                QMetaObject::invokeMethod(watcher, [this, result, watcher]() {
                    log(tr("Ping result: %1").arg(result));
                    m_pingConfigButton->setEnabled(true);
                    watcher->deleteLater();
                }, Qt::QueuedConnection);
            }).detach();
        });

        connect(m_qrConfigButton, &QPushButton::clicked, this, [this]() {
            QString path = m_configPath->text();
            if (path.isEmpty() && m_configsList->currentItem()) {
                path = m_configsList->currentItem()->text();
            }
            if (path.isEmpty()) {
                QMessageBox::information(this, tr("QR Code"), tr("Select config first"));
                return;
            }
            showConfigQRCode(path);
        });

        connect(m_configsList, &QListWidget::itemSelectionChanged, this, [this]() {
            QListWidgetItem *item = m_configsList->currentItem();
            if (item) {
                m_configPath->setText(item->text());
            }
        });

        connect(m_browseButton, &QPushButton::clicked, this, [this]() {
            const QString path = QFileDialog::getOpenFileName(
                    this, tr("Select TrustTunnel config"), QString(), tr("TOML files (*.toml);;All files (*)"));
            if (!path.isEmpty()) {
                const QString normalized = QFileInfo(path).absoluteFilePath();
                m_configPath->setText(normalized);
                addCurrentToStorage();
                refreshStoredList();
            }
        });

        connect(m_viewConfigButton, &QPushButton::clicked, this, [this]() {
            const QString path = m_configPath->text().trimmed();
            if (path.isEmpty()) {
                QMessageBox::information(this, tr("Config"), tr("Select config file first."));
                return;
            }
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) {
                QMessageBox::warning(this, tr("Config"), tr("Failed to open config file."));
                return;
            }
            auto *dlg = new QDialog(this);
            dlg->setWindowTitle(tr("Config Preview"));
            dlg->resize(860, 560);
            auto *v = new QVBoxLayout(dlg);
            auto *tabs = new QTabWidget(dlg);
            auto *summary = new QTextBrowser(dlg);
            summary->setHtml(buildConfigSummaryHtml(path));
            auto *validation = new QTextBrowser(dlg);
            validation->setHtml(buildConfigValidationHtml(path));
            auto *raw = new QTextEdit(dlg);
            raw->setReadOnly(true);
            raw->setPlainText(QString::fromUtf8(f.readAll()));
            tabs->addTab(summary, tr("Overview"));
            tabs->addTab(validation, tr("Validation"));
            tabs->addTab(raw, tr("Raw"));
            v->addWidget(tabs);
            dlg->exec();
            dlg->deleteLater();
        });

        // Update connect button state when config path changes
        connect(m_configPath, &QLineEdit::textChanged, this, [this]() {
            const auto s = m_vpnClient->state();
            bool isDisconnected = (s == QtTrustTunnelClient::State::Disconnected ||
                                   s == QtTrustTunnelClient::State::Error);
            bool hasConfig = !m_configPath->text().trimmed().isEmpty();
            m_connectButton->setEnabled(isDisconnected && hasConfig);
            m_qrConfigButton->setEnabled(hasConfig);
            updateConfigInfo();
            if (hasConfig) {
                m_appSettings.last_config_path = m_configPath->text();
                saveAppSettings(m_appSettings);
            }
        });

        connect(m_connectButton, &QPushButton::clicked, this, [this]() {
            const auto s = m_vpnClient->state();
            if (s != QtTrustTunnelClient::State::Disconnected && s != QtTrustTunnelClient::State::Error) {
                log(tr("Connect ignored: state=%1").arg(static_cast<int>(s)));
                return;
            }
            if (m_configPath->text().isEmpty()) {
                QMessageBox::warning(this, tr("Config Required"), tr("Select config file first."));
                return;
            }
            // reset counters on a fresh session
            m_bytesRx = 0;
            m_bytesTx = 0;
            m_lastGraphRx = 0;
            m_lastGraphTx = 0;
            m_trafficGraph->reset();
            m_loggedConnectionInfos.clear();
            statusBar()->showMessage(tr("Preparing routing rules..."), 1500);
#ifndef _WIN32
            if (!m_isRoot) {
                log(tr("Restarting app with sudo for VPN privileges"));
                relaunchElevated();
                return;
            }
#endif
            std::vector<std::string> includeRoutes;
            std::vector<std::string> excludeRoutes;
            if (!prepareRoutingRules(includeRoutes, excludeRoutes)) {
                statusBar()->showMessage(tr("Routing update failed"), 3000);
                return;
            }
            statusBar()->showMessage(tr("Routing rules ready"), 1500);
            if (!m_vpnClient->loadConfigFromFile(m_configPath->text())) {
                statusBar()->showMessage(tr("Config load failed"), 3000);
                return;
            }
            m_vpnClient->setRoutingRules(includeRoutes, excludeRoutes);

            // Apply custom DNS if enabled
            if (m_appSettings.custom_dns_enabled && !m_appSettings.custom_dns_servers.isEmpty()) {
                std::vector<std::string> dnsServers;
                for (const auto &s : m_appSettings.custom_dns_servers) {
                    if (!s.trimmed().isEmpty()) {
                        dnsServers.push_back(s.trimmed().toStdString());
                    }
                }
                if (!dnsServers.empty()) {
                    m_vpnClient->setCustomDns(dnsServers);
                    log(tr("Custom DNS applied: %1 server(s)").arg(dnsServers.size()));
                }
            }

            // Apply domain bypass exclusions and traffic bypass rules
            std::vector<std::string> exclusions;
            int appliedCount = 0;

            // Add domain bypass rules if enabled
            if (m_appSettings.domain_bypass_enabled && !m_appSettings.domain_bypass_rules.isEmpty()) {
                for (const auto &rule : m_appSettings.domain_bypass_rules) {
                    if (!rule.trimmed().isEmpty()) {
                        exclusions.push_back(rule.trimmed().toStdString());
                        appliedCount++;
                    }
                }
            }

            // Add SSH bypass (port 22) if enabled
            if (m_appSettings.ssh_bypass_enabled) {
                exclusions.push_back("||:22");  // exclude destination port 22 (SSH)
                appliedCount++;
            }

            // Add P2P bypass if enabled
            if (m_appSettings.p2p_bypass_enabled) {
                // Common P2P port ranges: 6881-6889 (BitTorrent), 6969 (tracker)
                // Using pattern for common P2P applications
                exclusions.push_back("||:6881-6889");  // BitTorrent default range
                exclusions.push_back("||:6969");        // BitTorrent tracker
                appliedCount += 2;
            }

            if (!exclusions.empty()) {
                m_vpnClient->setExtraExclusions(exclusions);
                log(tr("Bypass rules applied: %1 rule(s)").arg(appliedCount));
            } else {
                // Explicitly clear any previously set exclusions so they don't
                // persist across reconnects or after the user disables bypass.
                m_vpnClient->setExtraExclusions({});
            }

            // Scan for adapter conflicts if enabled
            if (m_appSettings.scan_adapter_conflicts) {
                handleScanConflictsBeforeConnect();
            }

            log(tr("Connecting VPN..."));
            statusBar()->showMessage(tr("Connecting..."), 1500);
            m_vpnClient->connectVpn();
            addCurrentToStorage();
        });

        connect(m_disconnectButton, &QPushButton::clicked, this, [this]() {
            const auto s = m_vpnClient->state();
            if (s == QtTrustTunnelClient::State::Disconnected || s == QtTrustTunnelClient::State::Error) {
                log(tr("Disconnect ignored: state=%1").arg(static_cast<int>(s)));
                return;
            }
            m_vpnClient->disconnectVpn();
            log(tr("Disconnect requested"));
        });

        auto updateStateUi = [this](QtTrustTunnelClient::State s) {
            switch (s) {
            case QtTrustTunnelClient::State::Connecting:
                m_stateLabel->setText(tr("VPN: Connecting"));
                m_ring->setStatus(ConnectionRing::Connecting);
                updateRingText();
                m_connectButton->setEnabled(false);
                m_disconnectButton->setEnabled(true);
                m_statsTimer.start(1500);
                break;
            case QtTrustTunnelClient::State::Connected:
                m_stateLabel->setText(tr("VPN: Connected"));
                m_ring->setStatus(ConnectionRing::Connected);
                updateRingText();
                m_connectButton->setEnabled(false);
                m_disconnectButton->setEnabled(true);
                m_statsTimer.start(1500);
                statusBar()->showMessage(tr("VPN connected"), 2000);
                updateServerInfo();
                if (m_appSettings.enable_notifications) {
                    showNotification(tr("VPN Connected"), tr("Successfully connected to VPN"));
                }
                break;
            case QtTrustTunnelClient::State::Reconnecting:
                m_stateLabel->setText(tr("VPN: Reconnecting"));
                m_ring->setStatus(ConnectionRing::Reconnecting);
                updateRingText();
                m_connectButton->setEnabled(false);
                m_disconnectButton->setEnabled(true);
                m_statsTimer.start(1500);
                break;
            case QtTrustTunnelClient::State::WaitingForNetwork:
                m_stateLabel->setText(tr("VPN: No Network"));
                m_ring->setStatus(ConnectionRing::Connecting);
                m_ring->setStatusText(m_currentLang == "ru" ? "Нет сети" : "No Network");
                m_connectButton->setEnabled(false);
                m_disconnectButton->setEnabled(true);
                m_statsTimer.stop();
                statusBar()->showMessage(tr("Network connection lost, waiting..."), 4000);
                break;
            case QtTrustTunnelClient::State::Disconnecting:
                m_stateLabel->setText(tr("VPN: Disconnecting"));
                m_ring->setStatus(ConnectionRing::Disconnecting);
                updateRingText();
                m_connectButton->setEnabled(false);
                m_disconnectButton->setEnabled(false);
                m_statsTimer.stop();
                break;
            case QtTrustTunnelClient::State::Error:
                m_stateLabel->setText(tr("VPN: Error"));
                m_ring->setStatus(ConnectionRing::Error);
                updateRingText();
                m_connectButton->setEnabled(!m_configPath->text().trimmed().isEmpty());
                m_disconnectButton->setEnabled(true);
                m_statsTimer.stop();
                if (m_appSettings.enable_notifications) {
                    showNotification(tr("VPN Error"), tr("Connection error occurred"));
                }
                break;
            case QtTrustTunnelClient::State::Disconnected:
            default:
                m_stateLabel->setText(tr("VPN: Disconnected"));
                m_ring->setStatus(ConnectionRing::Disconnected);
                updateRingText();
                m_connectButton->setEnabled(!m_configPath->text().trimmed().isEmpty());
                m_disconnectButton->setEnabled(false);
                if (m_appSettings.enable_notifications && s == QtTrustTunnelClient::State::Disconnected) {
                    showNotification(tr("VPN Disconnected"), tr("Successfully disconnected from VPN"));
                }
                m_statsTimer.stop();
                m_trafficGraph->reset();
                break;
            }
        };

        connect(m_vpnClient, &QtTrustTunnelClient::stateChanged, this, updateStateUi);
        // Sync UI with the actual current state at startup
        updateStateUi(m_vpnClient->state());

        connect(m_vpnClient, &QtTrustTunnelClient::vpnConnected, this, [this]() {
            log(tr("VPN connected"));
            if (m_appSettings.notify_on_state && !m_appSettings.notify_only_errors && m_tray) {
                m_tray->showMessage(windowTitle(), tr("VPN connected"), QSystemTrayIcon::Information, 2000);
            }
        });
        connect(m_vpnClient, &QtTrustTunnelClient::vpnDisconnected, this, [this]() {
            log(tr("VPN disconnected"));
            if (m_appSettings.notify_on_state && !m_appSettings.notify_only_errors && m_tray) {
                m_tray->showMessage(windowTitle(), tr("VPN disconnected"), QSystemTrayIcon::Information, 2000);
            }
        });
        connect(m_vpnClient, &QtTrustTunnelClient::vpnError, this, [this](const QString &msg) {
            log(tr("VPN error: %1").arg(msg));
            statusBar()->showMessage(msg, 4000);
            if (m_appSettings.notify_on_state && m_tray) {
                m_tray->showMessage(windowTitle(), msg, QSystemTrayIcon::Critical, 4000);
            }
        });
        connect(m_vpnClient, &QtTrustTunnelClient::connectionInfo, this, [this](const QString &msg) {
            // Deduplicate: only log each unique "action domain" message once per session.
            // The core fires this callback for every TCP connection, which can produce
            // hundreds of identical lines (e.g. "bypass yandex.ru") for a single page load.
            if (!m_loggedConnectionInfos.contains(msg)) {
                m_loggedConnectionInfos.insert(msg);
                log(tr("Connection: %1").arg(msg));
            }
        });
        connect(m_vpnClient, &QtTrustTunnelClient::connectProgress, this, [this](const QString &step) {
            m_stateLabel->setText(tr("VPN: %1").arg(step));
            statusBar()->showMessage(step, 3000);
        });
        // Accumulate traffic from per-packet output callback (non-TUN-fd platforms)
        connect(m_vpnClient, &QtTrustTunnelClient::clientOutput, this, [this](const QString &bytes) {
            bool ok = false;
            const quint64 b = bytes.toULongLong(&ok);
            if (ok) {
                m_bytesRx += b;
            }
        });

        // Accumulate traffic from per-connection tunnel stats (works on all platforms incl. macOS TUN)
        connect(m_vpnClient, &QtTrustTunnelClient::tunnelStats, this, [this](quint64 upload, quint64 download) {
            m_bytesRx += download;
            m_bytesTx += upload;
        });

        m_statsTimer.setSingleShot(false);
        m_statsTimer.setInterval(1500);
        connect(&m_statsTimer, &QTimer::timeout, this, [this]() {
            // Feed traffic graph with delta since last sample
            const quint64 rxNow = m_bytesRx;
            const quint64 txNow = m_bytesTx;
            const quint64 rxDelta = (rxNow >= m_lastGraphRx) ? (rxNow - m_lastGraphRx) : rxNow;
            const quint64 txDelta = (txNow >= m_lastGraphTx) ? (txNow - m_lastGraphTx) : txNow;
            m_lastGraphRx = rxNow;
            m_lastGraphTx = txNow;
            m_trafficGraph->addSample(rxDelta, txDelta);

            // Update ring sub-text with live speed
            if (m_ring && m_ring->status() == ConnectionRing::Connected) {
                auto formatBytes = [](quint64 bytes) -> QString {
                    if (bytes < 1024) return QString("%1 B/s").arg(bytes);
                    if (bytes < 1024 * 1024) return QString("%1 KB/s").arg(bytes / 1024);
                    return QString("%1 MB/s").arg(bytes / (1024 * 1024));
                };
                // deltas are bytes per 1.5s, convert to per-second rate
                const quint64 rxPerSec = rxDelta * 2 / 3;
                const quint64 txPerSec = txDelta * 2 / 3;
                m_ring->setSubText(QString::fromUtf8("\u2193 ") + formatBytes(rxPerSec)
                        + "  " + QString::fromUtf8("\u2191 ") + formatBytes(txPerSec));
            }

            if (!m_appSettings.show_traffic_in_status) return;
            auto fmtTotal = [](quint64 bytes) -> QString {
                if (bytes < 1024) return QString("%1 B").arg(bytes);
                if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024);
                if (bytes < 1024ULL * 1024 * 1024) return QString("%1.%2 MB").arg(bytes / (1024 * 1024)).arg((bytes / (1024 * 100)) % 10);
                return QString("%1.%2 GB").arg(bytes / (1024ULL * 1024 * 1024)).arg((bytes / (1024ULL * 1024 * 100)) % 10);
            };
            statusBar()->showMessage(
                    QString::fromUtf8("\u2193 ") + fmtTotal(m_bytesRx)
                    + "  " + QString::fromUtf8("\u2191 ") + fmtTotal(m_bytesTx), 1400);

            // Update traffic stats label
            m_totalSessionRx = m_bytesRx;
            m_totalSessionTx = m_bytesTx;
            if (m_trafficStatsLabel) {
                m_trafficStatsLabel->setText(
                    QString(tr("RX: %1 | TX: %2"))
                        .arg(fmtTotal(m_totalSessionRx))
                        .arg(fmtTotal(m_totalSessionTx)));
            }
        });

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme) {
            if (m_appSettings.theme_mode == "system") {
                applyTheme();
            }
        });
#else
        qApp->installEventFilter(this);
#endif
    }

    void applyLanguage(const QString &lang) {
        m_currentLang = lang;
        const bool ru = (lang == "ru");
        if (m_langEnAction) m_langEnAction->setChecked(!ru);
        if (m_langRuAction) m_langRuAction->setChecked(ru);
        if (m_appMenu) m_appMenu->setTitle(ru ? "Приложение" : "App");
        if (m_settingsMenu) m_settingsMenu->setTitle(ru ? "Настройки" : "Settings");
        if (m_viewMenu) m_viewMenu->setTitle(ru ? "Вид" : "View");
        if (m_languageMenu) m_languageMenu->setTitle(ru ? "Язык" : "Language");
        m_configsBox->setTitle(ru ? "Сохраненные конфиги" : "Saved Configs");
        m_controlBox->setTitle(ru ? "Подключение" : "Connection");
        m_logBox->setTitle(ru ? "Логи" : "Logs");

        m_browseButton->setText(ru ? "Добавить" : "Add");
        m_removeConfigButton->setText(ru ? "Удалить" : "Remove");
        m_viewConfigButton->setText(ru ? "Просмотр" : "View");
        m_pingConfigButton->setText(ru ? "Пинг" : "Ping");
        m_qrConfigButton->setText(ru ? "QR" : "QR");
        m_connectButton->setText(ru ? "Подключить" : "Connect");
        m_disconnectButton->setText(ru ? "Отключить" : "Disconnect");

        // Nav bar
        m_navHome->setText(ru ? "Главная" : "Home");
        m_navConfigs->setText(ru ? "Конфиги" : "Configs");
        m_navLogs->setText(ru ? "Логи" : "Logs");
        m_navSettings->setText(ru ? "Настройки" : "Settings");

        // Page titles
        if (m_configsPageTitle) m_configsPageTitle->setText(ru ? "Конфигурации" : "Configs");
        if (m_logsPageTitle)    m_logsPageTitle->setText(ru ? "Журнал" : "Logs");

        // Update ring text for current language
        updateRingText();
        updateServerInfo();
        updateConfigInfo();
        if (m_settingsAction) m_settingsAction->setText(ru ? "Настройки" : "Settings");
        if (m_importDeeplinkAction) m_importDeeplinkAction->setText(ru ? "Импорт Deeplink" : "Import Deeplink");
        if (m_createConfigAction) m_createConfigAction->setText(ru ? "Создать конфиг" : "Create Config");
        if (m_settingsMenuAction) m_settingsMenuAction->setText(ru ? "Открыть настройки" : "Open Settings");
        if (m_checkUpdateAction) m_checkUpdateAction->setText(ru ? "Проверить обновления..." : "Check for Updates...");
        if (m_quitAction) m_quitAction->setText(ru ? "Выход" : "Quit");
        if (m_toggleLogsAction) {
            m_toggleLogsAction->setText(m_toggleLogsAction->isChecked() ? (ru ? "Скрыть логи" : "Hide Logs")
                                                                         : (ru ? "Показать логи" : "Show Logs"));
        }

        const QString text = m_stateLabel->text();
        // Order matters: check "Disconnecting" and "Disconnected" BEFORE "Connected",
        // because "Disconnected" contains the substring "Connected".
        if (text.contains("Disconnecting", Qt::CaseInsensitive)
                || text.contains("отключени", Qt::CaseInsensitive)) {
            m_stateLabel->setText(ru ? "VPN: отключение" : "VPN: Disconnecting");
        } else if (text.contains("Disconnected", Qt::CaseInsensitive)
                || text.contains("отключено", Qt::CaseInsensitive)) {
            m_stateLabel->setText(ru ? "VPN: отключено" : "VPN: Disconnected");
        } else if (text.contains("Reconnecting", Qt::CaseInsensitive)
                || text.contains("переподк", Qt::CaseInsensitive)) {
            m_stateLabel->setText(ru ? "VPN: переподключение" : "VPN: Reconnecting");
        } else if (text.contains("Connecting", Qt::CaseInsensitive)
                || text.contains("соедин", Qt::CaseInsensitive)
                || text.contains("подключение", Qt::CaseInsensitive)) {
            m_stateLabel->setText(ru ? "VPN: подключение" : "VPN: Connecting");
        } else if (text.contains("No Network", Qt::CaseInsensitive)
                || text.contains("нет сети", Qt::CaseInsensitive)) {
            m_stateLabel->setText(ru ? "VPN: нет сети" : "VPN: No Network");
        } else if (text.contains("Connected", Qt::CaseInsensitive)
                || text.contains("подключен", Qt::CaseInsensitive)) {
            m_stateLabel->setText(ru ? "VPN: подключено" : "VPN: Connected");
        } else if (text.contains("Error", Qt::CaseInsensitive)
                || text.contains("ошиб", Qt::CaseInsensitive)) {
            m_stateLabel->setText(ru ? "VPN: ошибка" : "VPN: Error");
        } else {
            m_stateLabel->setText(ru ? "VPN: отключено" : "VPN: Disconnected");
        }

        const QString baseTitle = ru ? "FreeTunnel" : "FreeTunnel";
        const QString full = ru ? " (Полный режим)" : " (Full mode)";
        const bool elevated = m_isRoot
#ifdef _WIN32
                || isProcessElevated()
#endif
                ;
        setWindowTitle(elevated ? baseTitle + full : baseTitle);
    }

    void enforceFirstRunElevation() {
#ifdef _WIN32
        if (m_appSettings.first_run_checked) {
            return;
        }
        if (!isProcessElevated()) {
            const auto res = QMessageBox::warning(this,
                    tr("Administrator privileges required"),
                    tr("For proper VPN setup we need to run as Administrator on first launch. Restart now as admin?"),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (res == QMessageBox::Yes) {
                m_appSettings.first_run_checked = true;
                saveAppSettings(m_appSettings);
                relaunchElevated();
            }
        } else {
            m_appSettings.first_run_checked = true;
            saveAppSettings(m_appSettings);
        }
#endif
    }

    void showNotification(const QString &title, const QString &message) {
        statusBar()->showMessage(title + ": " + message, 3000);
    }

    void appendLogChunk(const QByteArray &chunk) {
        if (chunk.isEmpty()) {
            return;
        }
        if (m_appSettings.save_logs && !m_appSettings.log_path.isEmpty()) {
            // Keep the log file open to avoid exhausting file descriptors
            // by repeatedly opening/closing on every log line.
            if (!m_logFile.isOpen() || m_logFile.fileName() != m_appSettings.log_path) {
                if (m_logFile.isOpen()) m_logFile.close();
                m_logFile.setFileName(m_appSettings.log_path);
                QFileInfo info(m_logFile);
                QDir().mkpath(info.absolutePath());
                if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Append)) {
                    // Failed to open — skip writing this chunk
                }
            }
            if (m_logFile.isOpen()) {
                m_logFile.write(chunk);
                m_logFile.flush();
            }
        }
        // Cap the log view to prevent unbounded memory growth.
        // QTextEdit stores a rich-text document internally; thousands of
        // appended lines will consume hundreds of megabytes of RAM.
        static constexpr int MAX_LOG_BLOCKS = 5000;
        QTextDocument *doc = m_logView->document();
        if (doc->blockCount() > MAX_LOG_BLOCKS) {
            QTextCursor trimCursor(doc);
            trimCursor.movePosition(QTextCursor::Start);
            // Remove the oldest 20% of lines in one operation
            const int removeCount = MAX_LOG_BLOCKS / 5;
            trimCursor.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor, removeCount);
            trimCursor.removeSelectedText();
        }
        m_logView->moveCursor(QTextCursor::End);
        m_logView->insertPlainText(QString::fromLocal8Bit(chunk));
        m_logView->ensureCursorVisible();
    }

    void log(const QString &line) {
        appendLogChunk(line.toUtf8() + '\n');
    }

    void applyTheme() {
        auto makeLightPalette = []() {
            QPalette p;
            p.setColor(QPalette::Window, QColor(0xF7, 0xF8, 0xFC));
            p.setColor(QPalette::WindowText, QColor(0x1E, 0x20, 0x30));
            p.setColor(QPalette::Base, QColor(0xFF, 0xFF, 0xFF));
            p.setColor(QPalette::AlternateBase, QColor(0xF0, 0xF2, 0xF8));
            p.setColor(QPalette::Text, QColor(0x1E, 0x20, 0x30));
            p.setColor(QPalette::Button, QColor(0xEB, 0xED, 0xF5));
            p.setColor(QPalette::ButtonText, QColor(0x1E, 0x20, 0x30));
            p.setColor(QPalette::Highlight, QColor(0x5B, 0x6E, 0xF5));
            p.setColor(QPalette::HighlightedText, QColor(0xFF, 0xFF, 0xFF));
            return p;
        };
        auto makeDarkPalette = []() {
            QPalette p;
            p.setColor(QPalette::Window, QColor(0x10, 0x11, 0x18));
            p.setColor(QPalette::WindowText, QColor(0xE0, 0xE2, 0xEA));
            p.setColor(QPalette::Base, QColor(0x18, 0x19, 0x22));
            p.setColor(QPalette::AlternateBase, QColor(0x20, 0x21, 0x2C));
            p.setColor(QPalette::Text, QColor(0xE0, 0xE2, 0xEA));
            p.setColor(QPalette::Button, QColor(0x20, 0x21, 0x2C));
            p.setColor(QPalette::ButtonText, QColor(0xE0, 0xE2, 0xEA));
            p.setColor(QPalette::Highlight, QColor(0x5B, 0x6E, 0xF5));
            p.setColor(QPalette::HighlightedText, QColor(0xFF, 0xFF, 0xFF));
            return p;
        };

        const QString lightQss =
            "QMainWindow { background: #F7F8FC; }"
            "QListWidget, QTextEdit, QLineEdit { background: #FFFFFF; border: 1px solid #DDE0EA; border-radius: 10px; padding: 8px; color: #1E2030; }"
            "QListWidget::item { padding: 6px 4px; border-radius: 6px; }"
            "QListWidget::item:selected { background: #EEF0FF; color: #4A5ADB; }"
            "QPushButton { background: #EBEDF5; border: 1px solid #D5D8E5; border-radius: 10px; padding: 8px 14px; color: #1E2030; font-weight: 500; }"
            "QPushButton:hover { background: #E0E3F0; }"
            "QPushButton#connectButton { background: #5B6EF5; color: #FFFFFF; border: none; font-weight: 700; }"
            "QPushButton#connectButton:hover { background: #4A5ADB; }"
            "QPushButton#disconnectButton { background: #F0F1F5; color: #444; border: 1px solid #CCC; font-weight: 600; }"
            "QPushButton#disconnectButton:hover { background: #E8E9EE; }"
            "QPushButton#navButton { background: transparent; border: none; color: #8890A0; font-size: 11px; padding: 6px 0; }"
            "QPushButton#navButton:checked { color: #5B6EF5; }"
            "QWidget#navBar { background: #F0F1F6; border-top: 1px solid #DDE0EA; }"
            "QWidget#serverCard { background: #FFFFFF; border-radius: 16px; border: 1px solid #E5E8F0; }"
            "QLabel#serverNameLabel { font-size: 20px; font-weight: 700; color: #1E2030; }"
            "QLabel#serverDetailLabel { font-size: 12px; color: #8890A0; }"
            "QLabel#pageTitle { font-size: 18px; font-weight: 700; color: #1E2030; }"
            "QMenuBar { background: #F7F8FC; color: #333; }"
            "QMenuBar::item:selected { background: #E5E8F0; }"
            "QMenu { background: #FFFFFF; color: #1E2030; border: 1px solid #DDE0EA; }"
            "QMenu::item:selected { background: #EEF0FF; }"
            "QStatusBar { background: #F7F8FC; color: #8890A0; }";

        const QString darkQss =
            "QMainWindow { background: #101118; }"
            "QListWidget, QTextEdit, QLineEdit { background: #181922; border: 1px solid #282A38; border-radius: 10px; padding: 8px; color: #E0E2EA; }"
            "QListWidget::item { padding: 6px 4px; border-radius: 6px; }"
            "QListWidget::item:selected { background: #252840; color: #8B9CF5; }"
            "QPushButton { background: #20212C; border: 1px solid #30323E; border-radius: 10px; padding: 8px 14px; color: #D0D2DA; font-weight: 500; }"
            "QPushButton:hover { background: #2A2B3A; }"
            "QPushButton#connectButton { background: #5B6EF5; color: #FFFFFF; border: none; font-weight: 700; }"
            "QPushButton#connectButton:hover { background: #4A5ADB; }"
            "QPushButton#disconnectButton { background: #1C1D28; color: #D0D2DA; border: 1px solid #383A48; font-weight: 600; }"
            "QPushButton#disconnectButton:hover { background: #252638; }"
            "QPushButton#navButton { background: transparent; border: none; color: #555870; font-size: 11px; padding: 6px 0; }"
            "QPushButton#navButton:checked { color: #7B8CF5; }"
            "QWidget#navBar { background: #141520; border-top: 1px solid #282A38; }"
            "QWidget#serverCard { background: #181924; border-radius: 16px; border: 1px solid #282A38; }"
            "QLabel#serverNameLabel { font-size: 20px; font-weight: 700; color: #EEEEEE; }"
            "QLabel#serverDetailLabel { font-size: 12px; color: #7A7E90; }"
            "QLabel#pageTitle { font-size: 18px; font-weight: 700; color: #EEEEEE; }"
            "QMenuBar { background: #101118; color: #CCCCCC; }"
            "QMenuBar::item:selected { background: #282A38; }"
            "QMenu { background: #181924; color: #D0D2DA; border: 1px solid #333; }"
            "QMenu::item:selected { background: #252840; }"
            "QStatusBar { background: #101118; color: #555870; }";

        bool dark = false;
        bool useClaudeStyle = (m_appSettings.theme_mode == "claude");

        if (m_appSettings.theme_mode == "dark") {
            dark = true;
        } else if (m_appSettings.theme_mode == "light") {
            dark = false;
        } else if (!useClaudeStyle) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
            dark = (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);
#else
            const QColor base = qApp->palette().color(QPalette::Window);
            dark = (base.lightness() < 128);
#endif
        }

        qApp->setStyle("Fusion");

        if (useClaudeStyle) {
            // Load Claude style from file
            QFile claudeStyleFile(":/styles/claude.qss");
            if (claudeStyleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QString claudeQss = QString::fromUtf8(claudeStyleFile.readAll());
                setStyleSheet(claudeQss);
                // Set Claude color palette
                QPalette claudePalette;
                claudePalette.setColor(QPalette::Window, QColor(0x0A, 0x0A, 0x0A));
                claudePalette.setColor(QPalette::Base, QColor(0x1A, 0x1A, 0x1A));
                claudePalette.setColor(QPalette::Text, QColor(0xFF, 0xFF, 0xFF));
                claudePalette.setColor(QPalette::Button, QColor(0x33, 0x33, 0x33));
                claudePalette.setColor(QPalette::ButtonText, QColor(0xFF, 0xFF, 0xFF));
                claudePalette.setColor(QPalette::Highlight, QColor(0xC9, 0x74, 0x56));
                claudePalette.setColor(QPalette::HighlightedText, QColor(0xFF, 0xFF, 0xFF));
                qApp->setPalette(claudePalette);
                // Recolor icons for Anthropic Claude style
                if (m_ring) {
                    m_ring->setTextColor(QColor(0xFF, 0xFF, 0xFF));
                    m_ring->setSubTextColor(QColor(0xD4, 0xB8, 0x96));
                }
                recolorIconsForClaudeStyle();
            } else {
                // Fallback to dark theme if file not found
                qApp->setPalette(makeDarkPalette());
                setStyleSheet(darkQss);
                if (m_ring) {
                    m_ring->setTextColor(QColor(0xEE, 0xEE, 0xEE));
                    m_ring->setSubTextColor(QColor(0x94, 0xA3, 0xB8));
                }
                recolorIcons(true);
            }
        } else {
            qApp->setPalette(dark ? makeDarkPalette() : makeLightPalette());
            setStyleSheet(dark ? darkQss : lightQss);
            if (m_ring) {
                m_ring->setTextColor(dark ? QColor(0xEE, 0xEE, 0xEE) : QColor(0x1E, 0x20, 0x30));
                m_ring->setSubTextColor(dark ? QColor(0x94, 0xA3, 0xB8) : QColor(0x66, 0x6A, 0x80));
            }
            recolorIcons(dark);
        }
    }

    void recolorIcons(bool dark) {
        // Nav bar icons: inactive=muted, we just set the base color;
        // Qt stylesheet :checked color handles the accent.
        const QColor navColor = dark ? QColor(0x99, 0x9B, 0xAA) : QColor(0x66, 0x6A, 0x80);
        const QColor navActiveColor = dark ? QColor(0x7B, 0x8C, 0xF5) : QColor(0x5B, 0x6E, 0xF5);

        // Set both normal and checked icons via QIcon modes
        auto makeNavIcon = [&](const QString &path) -> QIcon {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) return {};
            QByteArray svg = f.readAll();

            auto renderSvg = [](const QByteArray &svgData, const QColor &col, int sz) -> QPixmap {
                QByteArray colored = svgData;
                colored.replace("currentColor", col.name(QColor::HexRgb).toUtf8());
                QPixmap pix(sz, sz);
                pix.fill(Qt::transparent);
                QPainter painter(&pix);
                painter.setRenderHint(QPainter::Antialiasing, true);
                QSvgRenderer renderer(colored);
                renderer.render(&painter);
                return pix;
            };

            QIcon icon;
            icon.addPixmap(renderSvg(svg, navColor, 22), QIcon::Normal, QIcon::Off);
            icon.addPixmap(renderSvg(svg, navActiveColor, 22), QIcon::Normal, QIcon::On);
            return icon;
        };

        if (m_navHome)     m_navHome->setIcon(makeNavIcon(":/icons/home.svg"));
        if (m_navConfigs)  m_navConfigs->setIcon(makeNavIcon(":/icons/configs.svg"));
        if (m_navLogs)     m_navLogs->setIcon(makeNavIcon(":/icons/log.svg"));
        if (m_navSettings) m_navSettings->setIcon(makeNavIcon(":/icons/settings.svg"));
    }

    void updateRingText() {
        if (!m_ring) return;
        const bool ru = (m_currentLang == "ru");
        switch (m_ring->status()) {
        case ConnectionRing::Disconnected:
            m_ring->setStatusText(ru ? "Отключено" : "Disconnected");
            break;
        case ConnectionRing::Connecting:
            m_ring->setStatusText(ru ? "Подключение..." : "Connecting...");
            break;
        case ConnectionRing::Connected:
            m_ring->setStatusText(ru ? "Подключено" : "Connected");
            break;
        case ConnectionRing::Reconnecting:
            m_ring->setStatusText(ru ? "Переподключение..." : "Reconnecting...");
            break;
        case ConnectionRing::Disconnecting:
            m_ring->setStatusText(ru ? "Отключение..." : "Disconnecting...");
            break;
        case ConnectionRing::Error:
            m_ring->setStatusText(ru ? "Ошибка" : "Error");
            break;
        }
    }

    void updateServerInfo() {
        const bool ru = (m_currentLang == "ru");
        const QString path = m_configPath ? m_configPath->text() : QString();
        if (path.isEmpty()) {
            m_serverNameLabel->setText(ru ? "Нет конфигурации" : "No config");
            m_serverDetailLabel->setText(ru ? "Задайте конфигурацию на вкладке Configs" : "Set a config in the Configs tab");
            return;
        }
        // Try to extract server info from config TOML
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QFileInfo fi(path);
            m_serverNameLabel->setText(fi.baseName());
            m_serverDetailLabel->setText(path);
            return;
        }
        const QString content = QString::fromUtf8(f.readAll());
        f.close();

        // Simple TOML parsing for display
        QString host, port, proto;
        for (const QString &line : content.split('\n')) {
            const QString trimmed = line.trimmed();
            if (trimmed.startsWith("host") && trimmed.contains('=')) {
                host = trimmed.section('=', 1).trimmed().remove('"');
            } else if (trimmed.startsWith("port") && trimmed.contains('=')) {
                port = trimmed.section('=', 1).trimmed().remove('"');
            } else if (trimmed.startsWith("proto") && trimmed.contains('=')) {
                proto = trimmed.section('=', 1).trimmed().remove('"').toUpper();
            }
        }

        if (host.isEmpty()) {
            QFileInfo fi(path);
            m_serverNameLabel->setText(fi.baseName());
            m_serverDetailLabel->setText(path);
        } else {
            m_serverNameLabel->setText(host);
            QString detail = proto.isEmpty() ? "" : proto;
            if (!port.isEmpty()) detail += (detail.isEmpty() ? "" : ":") + port;
            m_serverDetailLabel->setText(detail.isEmpty() ? path : detail);
        }
    }

    void updateConfigInfo() {
        const QString path = m_configPath ? m_configPath->text() : QString();
        if (path.isEmpty()) {
            m_configNameLabel->setText("—");
            m_configDetailLabel->setText("");
            return;
        }
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QFileInfo fi(path);
            m_configNameLabel->setText(fi.baseName());
            m_configDetailLabel->setText("");
            return;
        }
        const QString content = QString::fromUtf8(f.readAll());
        f.close();

        QString host, port, proto;
        for (const QString &line : content.split('\n')) {
            const QString trimmed = line.trimmed();
            if (trimmed.startsWith("host") && trimmed.contains('=')) {
                host = trimmed.section('=', 1).trimmed().remove('"');
            } else if (trimmed.startsWith("port") && trimmed.contains('=')) {
                port = trimmed.section('=', 1).trimmed().remove('"');
            } else if (trimmed.startsWith("proto") && trimmed.contains('=')) {
                proto = trimmed.section('=', 1).trimmed().remove('"').toUpper();
            }
        }

        QFileInfo fi(path);
        if (host.isEmpty()) {
            m_configNameLabel->setText(fi.baseName());
            m_configDetailLabel->setText("");
        } else {
            m_configNameLabel->setText(host);
            QString detail = proto.isEmpty() ? "" : proto;
            if (!port.isEmpty()) detail += (detail.isEmpty() ? "" : ":") + port;
            m_configDetailLabel->setText(detail);
        }
    }

    void refreshStoredList() {
        const QString current = m_configPath->text();
        m_configsList->clear();
        for (const QString &p : loadStoredConfigs()) {
            m_configsList->addItem(p);
        }
        for (int i = 0; i < m_configsList->count(); ++i) {
            if (m_configsList->item(i)->text() == current) {
                m_configsList->setCurrentRow(i);
                break;
            }
        }
    }

    void addCurrentToStorage() {
        if (m_configPath->text().isEmpty()) {
            return;
        }
        QStringList configs = loadStoredConfigs();
        if (!configs.contains(m_configPath->text())) {
            configs.append(m_configPath->text());
            saveStoredConfigs(configs);
            refreshStoredList();
        }
    }

    void showConfigQRCode(const QString &path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, tr("QR Code"), tr("Failed to open config file."));
            return;
        }
        const QByteArray fileData = f.readAll();
        f.close();

        const QString base64Data = QString::fromLatin1(fileData.toBase64());

        auto *dlg = new QDialog(this);
        dlg->setWindowTitle(tr("Config QR Code"));
        dlg->resize(500, 580);
        auto *layout = new QVBoxLayout(dlg);
        layout->setContentsMargins(16, 16, 16, 16);
        layout->setSpacing(12);

        auto *qrDisplay = new QLabel(dlg);
        qrDisplay->setAlignment(Qt::AlignCenter);
        qrDisplay->setMinimumSize(400, 400);
        qrDisplay->setStyleSheet("background-color: white; border-radius: 8px;");
        layout->addWidget(qrDisplay, 1);

        auto *closeBtn = new QPushButton(tr("Close"), dlg);
        connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::accept);
        layout->addWidget(closeBtn);

        dlg->show();

        auto *nam = new QNetworkAccessManager(dlg);
        const QString encodedData = QString::fromUtf8(QUrl::toPercentEncoding(base64Data));
        const QString qrUrl = QString("https://api.qrserver.com/v1/create-qr-code/?size=400x400&data=%1").arg(encodedData);

        auto *reply = nam->get(QNetworkRequest(QUrl(qrUrl)));
        connect(reply, &QNetworkReply::finished, [reply, qrDisplay]() {
            if (reply->error() == QNetworkReply::NoError) {
                QPixmap qrPixmap;
                qrPixmap.loadFromData(reply->readAll());
                if (!qrPixmap.isNull()) {
                    qrDisplay->setPixmap(qrPixmap.scaledToWidth(400, Qt::SmoothTransformation));
                } else {
                    qrDisplay->setText(tr("Failed to generate QR code"));
                }
            } else {
                qrDisplay->setText(tr("Network error: %1").arg(reply->errorString()));
            }
            reply->deleteLater();
        });
    }

    void showUpdateDialog(const UpdateChecker::ReleaseInfo &info) {
        const bool ru = (m_currentLang == "ru");
        const QString title = ru ? "Доступно обновление" : "Update Available";
        const QString text = ru
            ? QString("Доступна новая версия <b>%1</b> (текущая: <b>%2</b>).<br><br>"
                      "<b>Что нового:</b><br>%3")
                  .arg(info.version, QStringLiteral(FREETUNNEL_VERSION),
                       info.body.toHtmlEscaped().replace("\n", "<br>"))
            : QString("A new version <b>%1</b> is available (current: <b>%2</b>).<br><br>"
                      "<b>Release notes:</b><br>%3")
                  .arg(info.version, QStringLiteral(FREETUNNEL_VERSION),
                       info.body.toHtmlEscaped().replace("\n", "<br>"));

        QMessageBox box(this);
        box.setWindowTitle(title);
        box.setTextFormat(Qt::RichText);
        box.setText(text);
        box.setIcon(QMessageBox::Information);

#ifdef _WIN32
        QPushButton *downloadBtn = nullptr;
        if (!info.installerUrl.isEmpty()) {
            downloadBtn = box.addButton(ru ? "Скачать и установить" : "Download && Install", QMessageBox::AcceptRole);
        }
#endif
        auto *openPageBtn = box.addButton(ru ? "Открыть страницу" : "Open Release Page", QMessageBox::ActionRole);
        box.addButton(ru ? "Позже" : "Later", QMessageBox::RejectRole);
        box.exec();

#ifdef _WIN32
        if (downloadBtn && box.clickedButton() == downloadBtn) {
            downloadAndInstallUpdate(info);
            return;
        }
#endif
        if (box.clickedButton() == openPageBtn) {
            QDesktopServices::openUrl(QUrl(info.htmlUrl));
        }
    }

#ifdef _WIN32
    void downloadAndInstallUpdate(const UpdateChecker::ReleaseInfo &info) {
        const bool ru = (m_currentLang == "ru");
        log(tr("Downloading update %1...").arg(info.version));

        auto *progress = new QProgressDialog(
            ru ? "Загрузка обновления..." : "Downloading update...",
            ru ? "Отмена" : "Cancel", 0, 100, this);
        progress->setWindowModality(Qt::WindowModal);
        progress->setMinimumDuration(0);
        progress->setValue(0);

        auto *nam = new QNetworkAccessManager(this);
        QNetworkRequest req(info.installerUrl);
        req.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("FreeTunnel/%1").arg(QStringLiteral(FREETUNNEL_VERSION)));
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply *reply = nam->get(req);

        connect(reply, &QNetworkReply::downloadProgress, progress,
                [progress](qint64 received, qint64 total) {
            if (total > 0) {
                progress->setMaximum(static_cast<int>(total));
                progress->setValue(static_cast<int>(received));
            }
        });
        connect(progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, progress, nam, info, ru]() {
            progress->close();
            progress->deleteLater();
            reply->deleteLater();
            nam->deleteLater();

            if (reply->error() != QNetworkReply::NoError) {
                log(tr("Download failed: %1").arg(reply->errorString()));
                QMessageBox::warning(this,
                    ru ? "Ошибка загрузки" : "Download Error",
                    reply->errorString());
                return;
            }

            // Save to temp dir
            const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
            const QString installerPath = QDir(tempDir).filePath(info.assetName);
            QFile file(installerPath);
            if (!file.open(QIODevice::WriteOnly)) {
                log(tr("Cannot write installer to %1").arg(installerPath));
                return;
            }
            file.write(reply->readAll());
            file.close();

            log(tr("Update downloaded to %1, launching installer...").arg(installerPath));

            // Launch NSIS installer and quit the app
            // /S = silent mode is optional; without it the user sees the wizard
            if (QProcess::startDetached(installerPath, {})) {
                m_forceExit = true;
                qApp->quit();
            } else {
                QMessageBox::warning(this,
                    ru ? "Ошибка" : "Error",
                    ru ? "Не удалось запустить установщик" : "Failed to launch installer");
            }
        });
    }
#endif

private:
    bool m_forceExit = false;

    QGroupBox *m_configsBox = nullptr;
    QGroupBox *m_controlBox = nullptr;
    QGroupBox *m_logBox = nullptr;

    QListWidget *m_configsList = nullptr;
    QLineEdit *m_configPath = nullptr;
    QLabel *m_configNameLabel = nullptr;
    QLabel *m_configDetailLabel = nullptr;
    QPushButton *m_removeConfigButton = nullptr;
    QPushButton *m_pingConfigButton = nullptr;
    QPushButton *m_qrConfigButton = nullptr;
    QPushButton *m_browseButton = nullptr;
    QPushButton *m_viewConfigButton = nullptr;
    QPushButton *m_connectButton = nullptr;
    QPushButton *m_disconnectButton = nullptr;
    QLabel *m_stateLabel = nullptr;
    QTextEdit *m_logView = nullptr;

    // — new minimalist UI members —
    ConnectionRing *m_ring = nullptr;
    TrafficGraph *m_trafficGraph = nullptr;
    QStackedWidget *m_stack = nullptr;
    QPushButton *m_navHome = nullptr;
    QPushButton *m_navConfigs = nullptr;
    QPushButton *m_navLogs = nullptr;
    QPushButton *m_navSettings = nullptr;
    QLabel *m_serverNameLabel = nullptr;
    QLabel *m_serverDetailLabel = nullptr;
    QLabel *m_trafficStatsLabel = nullptr;
    quint64 m_lastGraphRx = 0;
    quint64 m_lastGraphTx = 0;
    quint64 m_totalSessionRx = 0;
    quint64 m_totalSessionTx = 0;
    QLabel *m_configsPageTitle = nullptr;
    QLabel *m_logsPageTitle = nullptr;

    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_appMenu = nullptr;
    QMenu *m_settingsMenu = nullptr;
    QMenu *m_viewMenu = nullptr;
    QMenu *m_languageMenu = nullptr;
    QAction *m_settingsAction = nullptr;
    QAction *m_importDeeplinkAction = nullptr;
    QAction *m_createConfigAction = nullptr;
    QAction *m_settingsMenuAction = nullptr;
    QAction *m_checkUpdateAction = nullptr;
    QAction *m_quitAction = nullptr;
    QAction *m_toggleLogsAction = nullptr;
    UpdateChecker *m_updateChecker = nullptr;
    QAction *m_langEnAction = nullptr;
    QAction *m_langRuAction = nullptr;
    QString m_currentLang = "en";
    QToolButton *m_hideLogsBtn = nullptr;

    bool m_isRoot = false;
    quint64 m_bytesRx = 0;
    quint64 m_bytesTx = 0;
    QSet<QString> m_loggedConnectionInfos;  // dedup connection info logs
    QFile m_logFile;  // persistent log file handle
    QTimer m_statsTimer;
    QtTrustTunnelClient *m_vpnClient = nullptr;
    AppSettings m_appSettings;

    void openSettingsDialog() {
        SettingsDialog dlg(m_currentLang, m_appSettings, this);

        // Handle advanced actions in real-time (before dialog is closed)
        connect(&dlg, &SettingsDialog::advancedAction, this, [this](const QString &action) {
            const bool ru = (m_currentLang == "ru");
            if (action == "reinstall_tunnels") {
                handleReinstallTunnels(ru);
            } else if (action == "flush_dns") {
                handleFlushDns(ru);
            } else if (action == "clear_ssl_cache") {
                handleClearSslCache(ru);
            } else if (action == "scan_conflicts") {
                handleScanConflicts(ru);
            }
        });

        if (dlg.exec() != QDialog::Accepted) {
            return;
        }

        // Handle settings reset
        if (dlg.resetSettingsRequested()) {
            m_appSettings = AppSettings{}; // defaults
            saveAppSettings(m_appSettings);
            applyTheme();
            m_vpnClient->setLogLevel(m_appSettings.log_level);
            if (m_toggleLogsAction) m_toggleLogsAction->setChecked(m_appSettings.show_logs_panel);
            statusBar()->showMessage(
                    m_currentLang == "ru" ? "Настройки сброшены" : "Settings reset to defaults", 3000);
            return;
        }

        m_appSettings.save_logs = dlg.saveLogs();
        m_appSettings.log_level = dlg.logLevel();
        const QString newPath = dlg.logPath();
        if (!newPath.isEmpty()) {
            m_appSettings.log_path = newPath;
        }
        m_appSettings.theme_mode = dlg.themeMode();
        m_appSettings.auto_connect_on_start = dlg.autoConnectOnStart();
        m_appSettings.notify_on_state = dlg.notifyOnState();
        m_appSettings.notify_only_errors = dlg.notifyOnlyErrors();
        m_appSettings.killswitch_enabled = dlg.killswitchEnabled();
        m_appSettings.strict_certificate_check = dlg.strictCertificateCheck();
        m_appSettings.show_logs_panel = dlg.showLogsPanel();
        m_appSettings.show_traffic_in_status = dlg.showTrafficInStatus();
        m_appSettings.show_traffic_graph = dlg.showTrafficGraph();
        m_appSettings.routing_enabled = dlg.routingEnabled();
        m_appSettings.routing_mode = dlg.routingMode();
        if (!dlg.routingSourceUrl().isEmpty()) m_appSettings.routing_source_url = dlg.routingSourceUrl();
        if (!dlg.routingCachePath().isEmpty()) m_appSettings.routing_cache_path = dlg.routingCachePath();
        m_appSettings.custom_dns_enabled = dlg.customDnsEnabled();
        m_appSettings.custom_dns_servers = dlg.customDnsServers();
        m_appSettings.domain_bypass_enabled = dlg.domainBypassEnabled();
        m_appSettings.domain_bypass_rules = dlg.domainBypassRules();
        m_appSettings.scan_adapter_conflicts = dlg.scanAdapterConflicts();
        m_appSettings.ssh_bypass_enabled = dlg.sshBypassEnabled();
        m_appSettings.p2p_bypass_enabled = dlg.p2pBypassEnabled();
        m_vpnClient->setLogLevel(m_appSettings.log_level);
        saveAppSettings(m_appSettings);
        applyTheme();
        if (m_toggleLogsAction) m_toggleLogsAction->setChecked(m_appSettings.show_logs_panel);
        if (m_trafficGraph) m_trafficGraph->setVisible(m_appSettings.show_traffic_graph);
        if (!m_appSettings.show_traffic_in_status) statusBar()->clearMessage();
        statusBar()->showMessage(tr("Settings saved"), 2000);
    }

    void handleReinstallTunnels(bool ru) {
#ifdef _WIN32
        // Remove all known WinTUN adapters via netsh and pnputil
        QProcess proc;
        proc.setProgram("cmd.exe");
        proc.setArguments({"/c",
                "netsh interface set interface name=\"FreeTunnel Adapter\" admin=disable 2>nul & "
                "netsh interface set interface name=\"test tunnel\" admin=disable 2>nul & "
                "pnputil /remove-device /deviceid \"wintun\" /subtree 2>nul"});
        proc.start();
        proc.waitForFinished(10000);
        log(ru ? "Туннельные адаптеры переустановлены. Переподключитесь для применения."
               : "Tunnel adapters reinstalled. Reconnect to apply.");
        statusBar()->showMessage(
                ru ? "Адаптеры переустановлены" : "Adapters reinstalled", 3000);
#elif defined(__APPLE__)
        log(ru ? "На macOS utun-адаптеры создаются автоматически. Переподключитесь."
               : "On macOS utun adapters are created automatically. Reconnect.");
        statusBar()->showMessage(
                ru ? "Переподключитесь" : "Reconnect to apply", 3000);
#else
        log(ru ? "На Linux tun-адаптеры управляются ядром. Переподключитесь."
               : "On Linux tun adapters are managed by the kernel. Reconnect.");
        statusBar()->showMessage(
                ru ? "Переподключитесь" : "Reconnect to apply", 3000);
#endif
    }

    void handleFlushDns(bool ru) {
        QProcess proc;
#ifdef _WIN32
        proc.start("cmd.exe", {"/c", "ipconfig /flushdns"});
#elif defined(__APPLE__)
        proc.start("dscacheutil", {"-flushcache"});
#else
        proc.start("resolvectl", {"flush-caches"});
#endif
        proc.waitForFinished(5000);
        log(ru ? "DNS-кеш очищен." : "DNS cache flushed.");
        statusBar()->showMessage(
                ru ? "DNS-кеш очищен" : "DNS cache flushed", 2000);
    }

    void handleClearSslCache(bool ru) {
        // Remove SSL session cache file if it exists
        const QString sslCachePath = QStandardPaths::writableLocation(
                QStandardPaths::AppConfigLocation) + "/ssl_sessions";
        if (QFile::exists(sslCachePath)) {
            QFile::remove(sslCachePath);
        }
        // Also try common locations
        const QStringList candidates = {
                QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/ssl_sessions",
                QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/ssl_session_cache",
        };
        for (const QString &path : candidates) {
            if (QFile::exists(path)) QFile::remove(path);
        }
        log(ru ? "Кеш SSL-сессий очищен." : "SSL session cache cleared.");
        statusBar()->showMessage(
                ru ? "SSL-кеш очищен" : "SSL cache cleared", 2000);
    }

    void handleScanConflicts(bool ru) {
        QStringList conflicts;
        const QStringList knownConflicts = {
            "Radmin VPN", "Hamachi", "TAP-Windows", "OpenVPN",
            "WireGuard", "ZeroTier", "Hyper-V Virtual", "VMware",
            "VirtualBox Host-Only"
        };
#ifdef _WIN32
        QProcess proc;
        proc.start("powershell", {"-NoProfile", "-Command",
            "Get-NetAdapter | Select-Object -ExpandProperty Name"});
        proc.waitForFinished(10000);
        const QString output = QString::fromUtf8(proc.readAllStandardOutput());
        const QStringList adapters = output.split('\n', Qt::SkipEmptyParts);
        for (const QString &adapter : adapters) {
            const QString trimmed = adapter.trimmed();
            for (const QString &known : knownConflicts) {
                if (trimmed.contains(known, Qt::CaseInsensitive)) {
                    conflicts.append(trimmed);
                    break;
                }
            }
        }
#elif defined(__APPLE__)
        QProcess proc;
        proc.start("ifconfig", {"-l"});
        proc.waitForFinished(5000);
        const QString output = QString::fromUtf8(proc.readAllStandardOutput());
        const QStringList ifaces = output.trimmed().split(' ', Qt::SkipEmptyParts);
        // On macOS, check for known VPN-related utun/tap interfaces
        for (const QString &iface : ifaces) {
            if (iface.startsWith("tap") || iface.startsWith("tun")
                || iface.startsWith("feth") || iface.startsWith("vmnet")) {
                conflicts.append(iface);
            }
        }
#else
        QProcess proc;
        proc.start("ip", {"link", "show"});
        proc.waitForFinished(5000);
        const QString output = QString::fromUtf8(proc.readAllStandardOutput());
        const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            for (const QString &known : knownConflicts) {
                if (line.contains(known, Qt::CaseInsensitive)) {
                    conflicts.append(line.trimmed());
                    break;
                }
            }
        }
#endif
        if (conflicts.isEmpty()) {
            const QString msg = ru
                ? "Конфликтующие адаптеры не обнаружены."
                : "No conflicting adapters found.";
            log(msg);
            QMessageBox::information(this, ru ? "Сканирование" : "Adapter Scan", msg);
        } else {
            const QString msg = (ru
                ? "Обнаружены потенциально конфликтующие адаптеры:\n\n"
                : "Potentially conflicting adapters found:\n\n")
                + conflicts.join("\n")
                + (ru ? "\n\nРекомендуется отключить их перед подключением VPN."
                      : "\n\nConsider disabling them before connecting.");
            log(msg);
            QMessageBox::warning(this, ru ? "Конфликты" : "Adapter Conflicts", msg);
        }
        statusBar()->showMessage(
            ru ? QString("Найдено конфликтов: %1").arg(conflicts.size())
               : QString("Conflicts found: %1").arg(conflicts.size()), 3000);
    }

    void recolorIconsForClaudeStyle() {
        // Anthropic Claude official icon colors
        const QColor navColor = QColor(0xD4, 0xB8, 0x96);          // Kraft
        const QColor navActiveColor = QColor(0xC9, 0x74, 0x56);    // Terracotta

        // Recolor nav bar icons
        auto makeNavIcon = [&](const QString &path) -> QIcon {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) return {};
            QByteArray svg = f.readAll();

            auto renderSvg = [](const QByteArray &svgData, const QColor &col, int sz) -> QPixmap {
                QSvgRenderer renderer(svgData);
                QPixmap pix(sz, sz);
                pix.fill(Qt::transparent);
                QPainter painter(&pix);
                renderer.render(&painter);
                painter.end();

                // Colorize
                if (col != QColor(0, 0, 0)) {
                    QPixmap colorized(sz, sz);
                    colorized.fill(Qt::transparent);
                    QPainter p(&colorized);
                    p.setCompositionMode(QPainter::CompositionMode_Screen);
                    p.fillRect(colorized.rect(), col);
                    p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                    p.drawPixmap(0, 0, pix);
                    return colorized;
                }
                return pix;
            };

            QIcon icon;
            icon.addPixmap(renderSvg(svg, navColor, 16), QIcon::Normal);
            icon.addPixmap(renderSvg(svg, navActiveColor, 16), QIcon::Active);
            return icon;
        };

        // Apply icons to nav buttons
        const QStringList navPaths = {
            ":/icons/home.svg",
            ":/icons/configs.svg",
            ":/icons/logging.svg",
            ":/icons/settings.svg"
        };

        // Update nav button icons if they exist
        QList<QPushButton*> navButtons = findChildren<QPushButton*>();
        for (int i = 0; i < navButtons.count() && i < navPaths.count(); ++i) {
            if (navButtons[i]->objectName() == "navButton") {
                navButtons[i]->setIcon(makeNavIcon(navPaths[i]));
            }
        }
    }

    void handleScanConflictsBeforeConnect() {
        // Silent scan before connect — only warn if conflicts found
        const bool ru = (m_currentLang == "ru");
#ifdef _WIN32
        QProcess proc;
        proc.start("powershell", {"-NoProfile", "-Command",
            "Get-NetAdapter -Physical | Where-Object { $_.Status -eq 'Up' } | Select-Object -ExpandProperty Name"});
        proc.waitForFinished(10000);
        const QString output = QString::fromUtf8(proc.readAllStandardOutput());
        const QStringList adapters = output.split('\n', Qt::SkipEmptyParts);
        const QStringList knownConflicts = {
            "Radmin VPN", "Hamachi", "TAP-Windows", "OpenVPN",
            "WireGuard", "ZeroTier"
        };
        QStringList conflicts;
        for (const QString &adapter : adapters) {
            const QString trimmed = adapter.trimmed();
            for (const QString &known : knownConflicts) {
                if (trimmed.contains(known, Qt::CaseInsensitive)) {
                    conflicts.append(trimmed);
                    break;
                }
            }
        }
        if (!conflicts.isEmpty()) {
            log(ru ? QString("⚠ Обнаружены конфликтующие адаптеры: %1").arg(conflicts.join(", "))
                   : QString("⚠ Conflicting adapters detected: %1").arg(conflicts.join(", ")));
        }
#else
        // On non-Windows, skip silent scan (less common conflict issues)
        Q_UNUSED(ru);
#endif
    }
};

QMainWindow *createMainWindow() {
    auto *w = new MainWindow();
    w->setWindowIcon(makeAppIcon());
    return w;
}
