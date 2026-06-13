#include "ConfigWizard.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>
#include <QWizardPage>

#include <QFile>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QElapsedTimer>

#include <thread>

// ═══════════════════════════════════════════════════════════
// Constructor
// ═══════════════════════════════════════════════════════════

ConfigWizard::ConfigWizard(const QString &lang, QWidget *parent)
    : QWizard(parent), m_ru(lang == "ru")
{
    setWindowTitle(m_ru ? "Мастер создания конфигурации" : "Configuration Wizard");
    setWizardStyle(QWizard::ModernStyle);
    setMinimumSize(720, 520);
    setOption(QWizard::NoBackButtonOnStartPage, true);

    addPage(createWelcomePage());
    addPage(createServerPage());
    addPage(createConnectionPage());
    addPage(createTunnelPage());
    addPage(createValidationPage());

    // When the validation page becomes visible, refresh summary
    connect(this, &QWizard::currentIdChanged, this, [this](int id) {
        if (id == 4) {
            refreshSummary();
        }
    });
}

QString ConfigWizard::generatedConfigPath() const {
    return m_savedPath;
}

// ═══════════════════════════════════════════════════════════
// Page 0: Welcome / Import method
// ═══════════════════════════════════════════════════════════

QWizardPage *ConfigWizard::createWelcomePage() {
    auto *page = new QWizardPage(this);
    page->setTitle(m_ru ? "Добро пожаловать" : "Welcome");
    page->setSubTitle(m_ru
        ? "Выберите способ создания конфигурации VPN-подключения."
        : "Choose how to create your VPN connection configuration.");

    auto *layout = new QVBoxLayout(page);

    auto *intro = new QLabel(m_ru
        ? "Этот мастер поможет настроить подключение к серверу TrustTunnel.\n"
          "Вы можете создать конфигурацию вручную или импортировать её."
        : "This wizard will help you set up a TrustTunnel server connection.\n"
          "You can create the configuration manually or import it.", page);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    layout->addSpacing(12);

    m_methodManual = new QRadioButton(m_ru ? "Создать вручную" : "Create manually", page);
    m_methodDeeplink = new QRadioButton(m_ru ? "Импортировать из Deep-link (tt://...)" : "Import from Deep-link (tt://...)", page);
    m_methodFile = new QRadioButton(m_ru ? "Импортировать из файла конфигурации endpoint" : "Import from endpoint config file", page);
    m_methodManual->setChecked(true);
    layout->addWidget(m_methodManual);

    layout->addWidget(m_methodDeeplink);
    auto *dlRow = new QHBoxLayout();
    m_deeplinkEdit = new QLineEdit(page);
    m_deeplinkEdit->setPlaceholderText("tt://...");
    m_deeplinkEdit->setEnabled(false);
    dlRow->addWidget(m_deeplinkEdit);
    layout->addLayout(dlRow);

    layout->addWidget(m_methodFile);
    auto *fileRow = new QHBoxLayout();
    m_filePathEdit = new QLineEdit(page);
    m_filePathEdit->setPlaceholderText(m_ru ? "Путь к .toml файлу" : "Path to .toml file");
    m_filePathEdit->setEnabled(false);
    m_fileBrowseBtn = new QPushButton(m_ru ? "Обзор..." : "Browse...", page);
    m_fileBrowseBtn->setEnabled(false);
    fileRow->addWidget(m_filePathEdit);
    fileRow->addWidget(m_fileBrowseBtn);
    layout->addLayout(fileRow);

    layout->addSpacing(8);
    m_importBtn = new QPushButton(m_ru ? "Импортировать" : "Import", page);
    m_importBtn->setEnabled(false);
    m_importStatus = new QLabel(page);
    m_importStatus->setWordWrap(true);
    auto *importRow = new QHBoxLayout();
    importRow->addWidget(m_importBtn);
    importRow->addWidget(m_importStatus, 1);
    layout->addLayout(importRow);

    layout->addStretch();

    // Enable/disable fields based on selected method
    auto updateMethodUi = [this]() {
        const bool isDl = m_methodDeeplink->isChecked();
        const bool isFile = m_methodFile->isChecked();
        m_deeplinkEdit->setEnabled(isDl);
        m_filePathEdit->setEnabled(isFile);
        m_fileBrowseBtn->setEnabled(isFile);
        m_importBtn->setEnabled(isDl || isFile);
    };
    connect(m_methodManual, &QRadioButton::toggled, this, updateMethodUi);
    connect(m_methodDeeplink, &QRadioButton::toggled, this, updateMethodUi);
    connect(m_methodFile, &QRadioButton::toggled, this, updateMethodUi);

    connect(m_fileBrowseBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this,
            m_ru ? "Выберите endpoint конфиг" : "Select endpoint config",
            QString(),
            m_ru ? "TOML файлы (*.toml);;Все файлы (*)" : "TOML files (*.toml);;All files (*)");
        if (!path.isEmpty()) m_filePathEdit->setText(path);
    });

    connect(m_importBtn, &QPushButton::clicked, this, [this]() {
        m_importStatus->setText(m_ru ? "Импорт..." : "Importing...");
        m_importStatus->setStyleSheet("");
        if (m_methodDeeplink->isChecked()) {
            importDeeplink(m_deeplinkEdit->text().trimmed());
        } else if (m_methodFile->isChecked()) {
            importEndpointFile(m_filePathEdit->text().trimmed());
        }
    });

    return page;
}

// ═══════════════════════════════════════════════════════════
// Page 1: Server settings
// ═══════════════════════════════════════════════════════════

QWizardPage *ConfigWizard::createServerPage() {
    auto *page = new QWizardPage(this);
    page->setTitle(m_ru ? "Сервер" : "Server");
    page->setSubTitle(m_ru
        ? "Введите параметры подключения к VPN-серверу."
        : "Enter your VPN server connection parameters.");

    auto *layout = new QFormLayout(page);

    m_hostEdit = new QLineEdit(page);
    m_hostEdit->setPlaceholderText("vpn.example.com");
    layout->addRow(m_ru ? "Hostname сервера:" : "Server hostname:", m_hostEdit);

    m_addrEdit = new QLineEdit(page);
    m_addrEdit->setPlaceholderText("vpn.example.com:443");
    auto *addrHint = new QLabel(m_ru
        ? "<i>Один или несколько адресов через запятую. Формат: host:port</i>"
        : "<i>One or more addresses comma-separated. Format: host:port</i>", page);
    addrHint->setWordWrap(true);
    layout->addRow(m_ru ? "Адрес(а) сервера:" : "Server address(es):", m_addrEdit);
    layout->addRow(QString(), addrHint);

    m_userEdit = new QLineEdit(page);
    layout->addRow(m_ru ? "Имя пользователя:" : "Username:", m_userEdit);

    m_passEdit = new QLineEdit(page);
    m_passEdit->setEchoMode(QLineEdit::Password);
    layout->addRow(m_ru ? "Пароль:" : "Password:", m_passEdit);

    layout->addRow(new QLabel(" "));

    m_certEdit = new QPlainTextEdit(page);
    m_certEdit->setPlaceholderText(m_ru
        ? "Вставьте PEM-сертификат сервера или оставьте пустым"
        : "Paste server PEM certificate or leave empty");
    m_certEdit->setMaximumHeight(100);
    m_certBrowse = new QPushButton(m_ru ? "Загрузить из файла..." : "Load from file...", page);
    layout->addRow(m_ru ? "Сертификат:" : "Certificate:", m_certEdit);
    layout->addRow(QString(), m_certBrowse);

    m_skipVerify = new QCheckBox(m_ru
        ? "Пропустить проверку сертификата (skip_verification)"
        : "Skip certificate verification", page);
    layout->addRow(QString(), m_skipVerify);

    connect(m_certBrowse, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this,
            m_ru ? "Сертификат сервера" : "Server certificate",
            QString(),
            m_ru ? "PEM файлы (*.pem *.crt *.cert);;Все файлы (*)" : "PEM files (*.pem *.crt *.cert);;All files (*)");
        if (path.isEmpty()) return;
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            m_certEdit->setPlainText(QString::fromUtf8(f.readAll()));
            m_skipVerify->setChecked(false);
        }
    });

    connect(m_certEdit, &QPlainTextEdit::textChanged, this, [this]() {
        if (!m_certEdit->toPlainText().trimmed().isEmpty()) {
            m_skipVerify->setChecked(false);
        }
    });

    return page;
}

// ═══════════════════════════════════════════════════════════
// Page 2: Connection options
// ═══════════════════════════════════════════════════════════

QWizardPage *ConfigWizard::createConnectionPage() {
    auto *page = new QWizardPage(this);
    page->setTitle(m_ru ? "Параметры соединения" : "Connection Options");
    page->setSubTitle(m_ru
        ? "Настройте протокол, Anti-DPI и дополнительные параметры."
        : "Configure protocol, Anti-DPI, and additional options.");

    auto *layout = new QFormLayout(page);

    m_protocolCombo = new QComboBox(page);
    m_protocolCombo->addItems({"http2", "http3", "http"});
    m_protocolCombo->setCurrentIndex(0);
    layout->addRow(m_ru ? "Протокол:" : "Protocol:", m_protocolCombo);

    m_fallbackCombo = new QComboBox(page);
    m_fallbackCombo->addItems({"http", "http2", "http3", "(none)"});
    m_fallbackCombo->setCurrentIndex(0);
    layout->addRow(m_ru ? "Фоллбэк протокол:" : "Fallback protocol:", m_fallbackCombo);

    m_antiDpiCheck = new QCheckBox(m_ru ? "Включить Anti-DPI" : "Enable Anti-DPI", page);
    layout->addRow(QString(), m_antiDpiCheck);

    m_customSniEdit = new QLineEdit(page);
    m_customSniEdit->setPlaceholderText(m_ru ? "Оставьте пустым если не нужен" : "Leave empty if not needed");
    layout->addRow(m_ru ? "Custom SNI:" : "Custom SNI:", m_customSniEdit);

    m_ipv6Check = new QCheckBox(m_ru ? "Сервер поддерживает IPv6" : "Server supports IPv6", page);
    layout->addRow(QString(), m_ipv6Check);

    m_clientRandomEdit = new QLineEdit(page);
    m_clientRandomEdit->setPlaceholderText(m_ru ? "Оставьте пустым если не нужен" : "Leave empty if not needed");
    layout->addRow("Client random:", m_clientRandomEdit);

    return page;
}

// ═══════════════════════════════════════════════════════════
// Page 3: Tunnel settings
// ═══════════════════════════════════════════════════════════

QWizardPage *ConfigWizard::createTunnelPage() {
    auto *page = new QWizardPage(this);
    page->setTitle(m_ru ? "Настройки туннеля" : "Tunnel Settings");
    page->setSubTitle(m_ru
        ? "Выберите тип listener, режим VPN и сетевые параметры."
        : "Choose listener type, VPN mode, and network settings.");

    auto *layout = new QVBoxLayout(page);

    // Listener type
    auto *listenerGroup = new QGroupBox(m_ru ? "Тип listener" : "Listener Type", page);
    auto *listenerLayout = new QHBoxLayout(listenerGroup);
    m_tunRadio = new QRadioButton("TUN", listenerGroup);
    m_socksRadio = new QRadioButton("SOCKS5", listenerGroup);
    m_tunRadio->setChecked(true);
    listenerLayout->addWidget(m_tunRadio);
    listenerLayout->addWidget(m_socksRadio);
    layout->addWidget(listenerGroup);

    // VPN mode + kill switch
    auto *vpnGroup = new QGroupBox(m_ru ? "Режим VPN" : "VPN Mode", page);
    auto *vpnLayout = new QFormLayout(vpnGroup);
    m_vpnModeCombo = new QComboBox(vpnGroup);
    m_vpnModeCombo->addItem(QStringLiteral("general \u2014 ") + (m_ru ? QString::fromUtf8("всё через VPN, кроме исключений") : QStringLiteral("all traffic through VPN except exclusions")), QStringLiteral("general"));
    m_vpnModeCombo->addItem(QStringLiteral("selective \u2014 ") + (m_ru ? QString::fromUtf8("только исключения через VPN") : QStringLiteral("only exclusions through VPN")), QStringLiteral("selective"));
    vpnLayout->addRow(m_ru ? "Режим:" : "Mode:", m_vpnModeCombo);
    m_killswitchCheck = new QCheckBox(m_ru ? "Kill switch (блокировать трафик без VPN)" : "Kill switch (block traffic without VPN)", vpnGroup);
    m_killswitchCheck->setChecked(true);
    vpnLayout->addRow(QString(), m_killswitchCheck);
    m_pqCheck = new QCheckBox(m_ru ? "Post-quantum ключевой обмен" : "Post-quantum key exchange", vpnGroup);
    m_pqCheck->setChecked(true);
    vpnLayout->addRow(QString(), m_pqCheck);
    layout->addWidget(vpnGroup);

    // Network
    auto *netGroup = new QGroupBox(m_ru ? "Сеть" : "Network", page);
    auto *netLayout = new QFormLayout(netGroup);
    m_dnsEdit = new QPlainTextEdit(netGroup);
    m_dnsEdit->setPlaceholderText(m_ru ? "Один DNS на строку:\n1.1.1.1\n8.8.8.8" : "One DNS per line:\n1.1.1.1\n8.8.8.8");
    m_dnsEdit->setMaximumHeight(60);
    m_dnsEdit->setPlainText("1.1.1.1\n8.8.8.8");
    netLayout->addRow(m_ru ? "DNS серверы:" : "DNS servers:", m_dnsEdit);
    m_mtuSpin = new QSpinBox(netGroup);
    m_mtuSpin->setRange(576, 9000);
    m_mtuSpin->setValue(1500);
    netLayout->addRow("MTU:", m_mtuSpin);
    m_changeDnsCheck = new QCheckBox(m_ru ? "Менять системные DNS" : "Change system DNS", netGroup);
    m_changeDnsCheck->setChecked(true);
    netLayout->addRow(QString(), m_changeDnsCheck);
    layout->addWidget(netGroup);

    // Exclusions
    m_exclusionsEdit = new QPlainTextEdit(page);
    m_exclusionsEdit->setPlaceholderText(m_ru
        ? "Домены исключений, по одному на строку (необязательно)"
        : "Exclusion domains, one per line (optional)");
    m_exclusionsEdit->setMaximumHeight(60);
    auto *exclLabel = new QLabel(m_ru ? "Исключения (exclusions):" : "Exclusions:", page);
    layout->addWidget(exclLabel);
    layout->addWidget(m_exclusionsEdit);

    return page;
}

// ═══════════════════════════════════════════════════════════
// Page 4: Validation & Ping
// ═══════════════════════════════════════════════════════════

QWizardPage *ConfigWizard::createValidationPage() {
    auto *page = new QWizardPage(this);
    page->setTitle(m_ru ? "Проверка и сохранение" : "Validation & Save");
    page->setSubTitle(m_ru
        ? "Проверьте конфигурацию, протестируйте соединение и сохраните файл."
        : "Review your configuration, test connectivity, and save the file.");

    auto *layout = new QVBoxLayout(page);

    m_summaryBrowser = new QTextBrowser(page);
    m_summaryBrowser->setOpenExternalLinks(false);
    m_summaryBrowser->setMinimumHeight(180);
    layout->addWidget(m_summaryBrowser);

    // Ping
    auto *pingRow = new QHBoxLayout();
    m_pingBtn = new QPushButton(m_ru ? "Проверить соединение" : "Test Connection", page);
    m_pingResult = new QLabel(page);
    m_pingProgress = new QProgressBar(page);
    m_pingProgress->setRange(0, 0); // indeterminate
    m_pingProgress->setMaximumWidth(120);
    m_pingProgress->setVisible(false);
    pingRow->addWidget(m_pingBtn);
    pingRow->addWidget(m_pingProgress);
    pingRow->addWidget(m_pingResult, 1);
    layout->addLayout(pingRow);

    layout->addSpacing(8);

    // Save
    auto *saveGroup = new QGroupBox(m_ru ? "Сохранение" : "Save", page);
    auto *saveLayout = new QVBoxLayout(saveGroup);
    auto *pathRow = new QHBoxLayout();
    m_savePathEdit = new QLineEdit(QDir::homePath() + "/trusttunnel-config.toml", saveGroup);
    m_saveBrowse = new QPushButton(m_ru ? "Обзор..." : "Browse...", saveGroup);
    pathRow->addWidget(m_savePathEdit);
    pathRow->addWidget(m_saveBrowse);
    saveLayout->addLayout(pathRow);
    auto *saveBtnRow = new QHBoxLayout();
    m_saveBtn = new QPushButton(m_ru ? "Сохранить конфигурацию" : "Save Configuration", saveGroup);
    m_saveBtn->setStyleSheet("QPushButton { font-weight: bold; }");
    m_saveStatus = new QLabel(saveGroup);
    saveBtnRow->addWidget(m_saveBtn);
    saveBtnRow->addWidget(m_saveStatus, 1);
    saveLayout->addLayout(saveBtnRow);
    layout->addWidget(saveGroup);

    layout->addStretch();

    // Connections
    connect(m_pingBtn, &QPushButton::clicked, this, &ConfigWizard::runPing);

    connect(m_saveBrowse, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this,
            m_ru ? "Сохранить конфиг" : "Save config",
            m_savePathEdit->text(),
            m_ru ? "TOML файлы (*.toml);;Все файлы (*)" : "TOML files (*.toml);;All files (*)");
        if (!path.isEmpty()) m_savePathEdit->setText(path);
    });

    connect(m_saveBtn, &QPushButton::clicked, this, [this]() {
        const QString path = m_savePathEdit->text().trimmed();
        if (path.isEmpty()) {
            m_saveStatus->setText(m_ru ? "Укажите путь!" : "Specify a path!");
            m_saveStatus->setStyleSheet("color: #cc3333;");
            return;
        }
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            m_saveStatus->setText(m_ru ? "Ошибка записи!" : "Write error!");
            m_saveStatus->setStyleSheet("color: #cc3333;");
            return;
        }
        f.write(buildToml().toUtf8());
        f.close();
        m_savedPath = path;
        m_saveStatus->setText(m_ru ? "Сохранено!" : "Saved!");
        m_saveStatus->setStyleSheet("color: #1f7a3f; font-weight: bold;");
        emit configCreated(path);
    });

    return page;
}

// ═══════════════════════════════════════════════════════════
// TOML parsing helpers (simple regex-based)
// ═══════════════════════════════════════════════════════════

static QString tomlField(const QString &text, const QString &key) {
    QRegularExpression re(key + QStringLiteral("\\s*=\\s*\"([^\"]*)\""));
    auto match = re.match(text);
    return match.hasMatch() ? match.captured(1) : QString();
}

static QStringList tomlArray(const QString &text, const QString &key) {
    QRegularExpression re(key + QStringLiteral("\\s*=\\s*\\[([^\\]]*)\\]"));
    auto match = re.match(text);
    if (!match.hasMatch()) return {};
    QString inside = match.captured(1);
    QStringList result;
    QRegularExpression itemRe(QStringLiteral("\"([^\"]*)\""));
    auto it = itemRe.globalMatch(inside);
    while (it.hasNext()) result << it.next().captured(1);
    return result;
}

static bool tomlBool(const QString &text, const QString &key, bool def) {
    QRegularExpression re(key + QStringLiteral("\\s*=\\s*(true|false)"));
    auto match = re.match(text);
    return match.hasMatch() ? (match.captured(1) == QStringLiteral("true")) : def;
}

// ═══════════════════════════════════════════════════════════
// Import helpers
// ═══════════════════════════════════════════════════════════

void ConfigWizard::importDeeplink(const QString &uri) {
    if (!uri.startsWith(QStringLiteral("tt://"))) {
        m_importStatus->setText(m_ru ? "Ошибка: URI должен начинаться с tt://" : "Error: URI must start with tt://");
        m_importStatus->setStyleSheet("color: #cc3333;");
        return;
    }
    const QString payload = uri.mid(5);
    const QByteArray decoded = QByteArray::fromBase64(payload.toUtf8());
    if (decoded.isEmpty()) {
        m_importStatus->setText(m_ru ? "Ошибка декодирования deep-link" : "Deep-link decode error");
        m_importStatus->setStyleSheet("color: #cc3333;");
        return;
    }
    const QString text = QString::fromUtf8(decoded);
    applyImportedToml(text);
}

void ConfigWizard::importEndpointFile(const QString &path) {
    if (path.isEmpty()) {
        m_importStatus->setText(m_ru ? "Укажите путь к файлу" : "Specify file path");
        m_importStatus->setStyleSheet("color: #cc3333;");
        return;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        m_importStatus->setText(m_ru ? "Не удалось открыть файл" : "Failed to open file");
        m_importStatus->setStyleSheet("color: #cc3333;");
        return;
    }
    const QString text = QString::fromUtf8(f.readAll());
    applyImportedToml(text);
}

void ConfigWizard::applyImportedToml(const QString &text) {
    const QString host = tomlField(text, QStringLiteral("hostname"));
    const QStringList addrs = tomlArray(text, QStringLiteral("addresses"));
    const QString user = tomlField(text, QStringLiteral("username"));
    const QString pass = tomlField(text, QStringLiteral("password"));
    const QString cert = tomlField(text, QStringLiteral("certificate"));
    const QString protocol = tomlField(text, QStringLiteral("upstream_protocol"));
    const QString sni = tomlField(text, QStringLiteral("custom_sni"));
    const QString crandom = tomlField(text, QStringLiteral("client_random"));
    const bool skipV = tomlBool(text, QStringLiteral("skip_verification"), false);
    const bool ipv6 = tomlBool(text, QStringLiteral("has_ipv6"), false);
    const bool adpi = tomlBool(text, QStringLiteral("anti_dpi"), false);

    if (host.isEmpty() && addrs.isEmpty()) {
        m_importStatus->setText(m_ru ? "Ошибка: не удалось извлечь данные" : "Error: could not extract data");
        m_importStatus->setStyleSheet("color: #cc3333;");
        return;
    }

    m_hostEdit->setText(host);
    m_addrEdit->setText(addrs.join(QStringLiteral(", ")));
    m_userEdit->setText(user);
    m_passEdit->setText(pass);
    if (!cert.isEmpty()) m_certEdit->setPlainText(cert);
    m_skipVerify->setChecked(skipV);
    if (!protocol.isEmpty()) {
        int idx = m_protocolCombo->findText(protocol);
        if (idx >= 0) m_protocolCombo->setCurrentIndex(idx);
    }
    m_customSniEdit->setText(sni);
    m_clientRandomEdit->setText(crandom);
    m_ipv6Check->setChecked(ipv6);
    m_antiDpiCheck->setChecked(adpi);

    m_importStatus->setText(m_ru
        ? QString("Импортировано! Хост: %1, адресов: %2").arg(host).arg(addrs.size())
        : QString("Imported! Host: %1, addresses: %2").arg(host).arg(addrs.size()));
    m_importStatus->setStyleSheet("color: #1f7a3f;");
}

// ═══════════════════════════════════════════════════════════
// Ping
// ═══════════════════════════════════════════════════════════

void ConfigWizard::runPing() {
    const QString addrsRaw = m_addrEdit->text().trimmed();
    if (addrsRaw.isEmpty()) {
        m_pingResult->setText(m_ru ? "Нет адресов для проверки" : "No addresses to test");
        m_pingResult->setStyleSheet("color: #cc3333;");
        return;
    }

    m_pingBtn->setEnabled(false);
    m_pingProgress->setVisible(true);
    m_pingResult->setText(m_ru ? "Проверка..." : "Testing...");
    m_pingResult->setStyleSheet("");

    const QStringList addrs = addrsRaw.split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts);

    // Run in background thread
    auto *watcher = new QObject(this);
    std::thread([this, addrs, watcher]() {
        QStringList results;
        bool anyOk = false;

        for (const QString &addr : addrs) {
            QString host;
            quint16 port = 443;
            // Parse host:port
            const int lastColon = addr.lastIndexOf(':');
            if (lastColon > 0) {
                host = addr.left(lastColon);
                bool ok = false;
                int p = addr.mid(lastColon + 1).toInt(&ok);
                if (ok && p > 0 && p <= 65535) port = static_cast<quint16>(p);
            } else {
                host = addr;
            }
            // Handle | prefix (anti-DPI)
            if (host.startsWith('|')) host = host.mid(1);

            QTcpSocket sock;
            QElapsedTimer timer;
            timer.start();
            sock.connectToHost(host, port);
            if (sock.waitForConnected(2500)) {
                sock.abort();
                const qint64 ms = timer.elapsed();
                results << QString("%1:%2 — OK (%3 ms)").arg(host).arg(port).arg(ms);
                anyOk = true;
            } else {
                results << QString("%1:%2 — FAIL").arg(host).arg(port);
            }
        }

        const QString summary = results.join("\n");
        const bool ok = anyOk;
        QMetaObject::invokeMethod(watcher, [this, summary, ok, watcher]() {
            m_pingBtn->setEnabled(true);
            m_pingProgress->setVisible(false);
            m_pingResult->setText(summary);
            m_pingResult->setStyleSheet(ok ? "color: #1f7a3f;" : "color: #cc3333;");
            emit pingFinished(summary);
            watcher->deleteLater();
        }, Qt::QueuedConnection);
    }).detach();
}

// ═══════════════════════════════════════════════════════════
// Build TOML
// ═══════════════════════════════════════════════════════════

QString ConfigWizard::buildToml() const {
    // DNS upstreams
    QStringList dnsList;
    for (const QString &line : m_dnsEdit->toPlainText().split('\n', Qt::SkipEmptyParts)) {
        const QString t = line.trimmed();
        if (!t.isEmpty() && !t.startsWith('#')) dnsList << QString("\"%1\"").arg(t);
    }

    // Exclusions
    QStringList exclList;
    for (const QString &line : m_exclusionsEdit->toPlainText().split('\n', Qt::SkipEmptyParts)) {
        const QString t = line.trimmed();
        if (!t.isEmpty() && !t.startsWith('#')) exclList << QString("\"%1\"").arg(t);
    }

    // Addresses
    const QStringList rawAddrs = m_addrEdit->text().split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts);
    QStringList addrList;
    for (const QString &a : rawAddrs) {
        addrList << QString("\"%1\"").arg(a.trimmed());
    }

    const QString cert = m_certEdit->toPlainText().trimmed();
    const QString fallback = m_fallbackCombo->currentText();

    QString toml;
    toml += QString("loglevel = \"info\"\n");
    toml += QString("vpn_mode = \"%1\"\n").arg(m_vpnModeCombo->currentData().toString());
    toml += QString("killswitch_enabled = %1\n").arg(m_killswitchCheck->isChecked() ? "true" : "false");
    toml += QString("post_quantum_group_enabled = %1\n").arg(m_pqCheck->isChecked() ? "true" : "false");
    toml += QString("exclusions = [%1]\n").arg(exclList.join(", "));
    toml += QString("dns_upstreams = [%1]\n").arg(dnsList.join(", "));
    toml += "\n";

    toml += "[endpoint]\n";
    toml += QString("hostname = \"%1\"\n").arg(m_hostEdit->text().trimmed());
    toml += QString("addresses = [%1]\n").arg(addrList.join(", "));
    toml += QString("username = \"%1\"\n").arg(m_userEdit->text().trimmed());
    toml += QString("password = \"%1\"\n").arg(m_passEdit->text());
    toml += QString("client_random = \"%1\"\n").arg(m_clientRandomEdit->text().trimmed());
    toml += QString("custom_sni = \"%1\"\n").arg(m_customSniEdit->text().trimmed());
    toml += QString("has_ipv6 = %1\n").arg(m_ipv6Check->isChecked() ? "true" : "false");
    toml += QString("skip_verification = %1\n").arg(m_skipVerify->isChecked() ? "true" : "false");
    toml += QString("upstream_protocol = \"%1\"\n").arg(m_protocolCombo->currentText());
    if (fallback != "(none)") {
        toml += QString("upstream_fallback_protocol = \"%1\"\n").arg(fallback);
    }
    toml += QString("anti_dpi = %1\n").arg(m_antiDpiCheck->isChecked() ? "true" : "false");
    if (!cert.isEmpty()) {
        // Multi-line TOML string
        toml += QString("certificate = \"\"\"%1\"\"\"\n").arg(cert);
    } else {
        toml += "certificate = \"\"\n";
    }
    toml += "\n";

    if (m_tunRadio->isChecked()) {
        toml += "[listener.tun]\n";
        toml += "bound_if = \"\"\n";
        toml += QString("mtu_size = %1\n").arg(m_mtuSpin->value());
        toml += QString("change_system_dns = %1\n").arg(m_changeDnsCheck->isChecked() ? "true" : "false");
        toml += "included_routes = [\"0.0.0.0/0\", \"2000::/3\"]\n";
        toml += "excluded_routes = [\"0.0.0.0/8\", \"10.0.0.0/8\", \"169.254.0.0/16\", \"172.16.0.0/12\", \"192.168.0.0/16\", \"224.0.0.0/3\"]\n";
    } else {
        toml += "[listener.socks]\n";
        toml += "address = \"127.0.0.1:1080\"\n";
    }

    return toml;
}

// ═══════════════════════════════════════════════════════════
// Refresh summary
// ═══════════════════════════════════════════════════════════

void ConfigWizard::refreshSummary() {
    const QString host = m_hostEdit->text().trimmed();
    const QString addrs = m_addrEdit->text().trimmed();
    const QString user = m_userEdit->text().trimmed();
    const QString proto = m_protocolCombo->currentText();
    const QString fallback = m_fallbackCombo->currentText();
    const QString vpnMode = m_vpnModeCombo->currentData().toString();
    const bool ks = m_killswitchCheck->isChecked();
    const bool adpi = m_antiDpiCheck->isChecked();
    const bool ipv6 = m_ipv6Check->isChecked();
    const bool skipV = m_skipVerify->isChecked();
    const bool pq = m_pqCheck->isChecked();
    const QString sni = m_customSniEdit->text().trimmed();
    const QString listener = m_tunRadio->isChecked() ? "TUN" : "SOCKS5";
    const int mtu = m_mtuSpin->value();
    const bool hasCert = !m_certEdit->toPlainText().trimmed().isEmpty();

    // Validation
    QStringList errors;
    QStringList warnings;
    if (host.isEmpty()) errors << (m_ru ? "Hostname не указан" : "Hostname is empty");
    if (addrs.isEmpty()) errors << (m_ru ? "Адрес сервера не указан" : "Server address is empty");
    if (user.isEmpty()) warnings << (m_ru ? "Имя пользователя пустое" : "Username is empty");
    if (m_passEdit->text().isEmpty()) warnings << (m_ru ? "Пароль пустой" : "Password is empty");
    if (!hasCert && !skipV) warnings << (m_ru ? "Нет сертификата и skip_verification=false — возможна ошибка подключения" : "No certificate and skip_verification=false — connection may fail");
    if (skipV) warnings << (m_ru ? "Проверка сертификата отключена — менее безопасно" : "Certificate verification disabled — less secure");

    // Check address format
    const QStringList addrList = addrs.split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts);
    for (const QString &a : addrList) {
        if (!a.contains(':')) {
            errors << (m_ru ? QString("Адрес '%1' не содержит порт (нужен формат host:port)").arg(a)
                            : QString("Address '%1' missing port (expected host:port)").arg(a));
        }
    }

    QString html;
    html += QString("<h3>%1</h3>").arg(m_ru ? "Сводка конфигурации" : "Configuration Summary");
    html += "<table cellspacing='4' cellpadding='2'>";
    auto row = [&](const QString &label, const QString &val) {
        html += QString("<tr><td><b>%1</b></td><td>%2</td></tr>").arg(label.toHtmlEscaped(), val.toHtmlEscaped());
    };
    row(m_ru ? "Хост" : "Host", host);
    row(m_ru ? "Адреса" : "Addresses", addrs);
    row(m_ru ? "Пользователь" : "User", user);
    row(m_ru ? "Протокол" : "Protocol", proto);
    row(m_ru ? "Фоллбэк" : "Fallback", fallback);
    row(m_ru ? "Режим VPN" : "VPN Mode", vpnMode);
    row("Kill switch", ks ? "ON" : "OFF");
    row("Post-quantum", pq ? "ON" : "OFF");
    row("Anti-DPI", adpi ? "ON" : "OFF");
    row("IPv6", ipv6 ? "ON" : "OFF");
    row("Custom SNI", sni.isEmpty() ? "-" : sni);
    row(m_ru ? "Сертификат" : "Certificate", hasCert ? (m_ru ? "Есть" : "Present") : (m_ru ? "Нет" : "None"));
    row("Skip verification", skipV ? "ON" : "OFF");
    row("Listener", listener);
    row("MTU", QString::number(mtu));
    html += "</table>";

    // Validation section
    if (!errors.isEmpty() || !warnings.isEmpty()) {
        html += QString("<h3>%1</h3>").arg(m_ru ? "Проверка" : "Validation");
        if (!errors.isEmpty()) {
            html += "<p style='color:#cc3333;'><b>" + QString(m_ru ? "Ошибки:" : "Errors:") + "</b></p><ul>";
            for (const QString &e : errors) html += "<li style='color:#cc3333;'>" + e.toHtmlEscaped() + "</li>";
            html += "</ul>";
        }
        if (!warnings.isEmpty()) {
            html += "<p style='color:#b26a00;'><b>" + QString(m_ru ? "Предупреждения:" : "Warnings:") + "</b></p><ul>";
            for (const QString &w : warnings) html += "<li style='color:#b26a00;'>" + w.toHtmlEscaped() + "</li>";
            html += "</ul>";
        }
    } else {
        html += "<p style='color:#1f7a3f;'><b>" + QString(m_ru ? "Проверка пройдена" : "Validation passed") + "</b></p>";
    }

    m_summaryBrowser->setHtml(html);
}
