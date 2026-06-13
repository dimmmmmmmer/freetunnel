#pragma once

#include <QWizard>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QTextBrowser;
class QTimer;

/// Multi-step config setup wizard mirroring the TrustTunnel setup_wizard.
///
/// Pages:
///  0 – Welcome / import method  (deep-link, endpoint file, manual)
///  1 – Server settings           (hostname, address, user, pass, cert)
///  2 – Connection options         (protocol, anti-DPI, custom SNI, IPv6)
///  3 – Tunnel settings            (listener type, VPN mode, kill switch, DNS, MTU)
///  4 – Validation & ping          (summary, ping test, file save)
class ConfigWizard : public QWizard {
    Q_OBJECT

public:
    explicit ConfigWizard(const QString &lang, QWidget *parent = nullptr);

    /// Returns the path to the generated config file after accept().
    QString generatedConfigPath() const;

signals:
    void configCreated(const QString &path);
    void pingFinished(const QString &result);

private:
    // ---------- page builders ----------
    QWizardPage *createWelcomePage();
    QWizardPage *createServerPage();
    QWizardPage *createConnectionPage();
    QWizardPage *createTunnelPage();
    QWizardPage *createValidationPage();

    // ---------- helpers ----------
    void importDeeplink(const QString &uri);
    void importEndpointFile(const QString &path);
    void applyImportedToml(const QString &text);
    void runPing();
    QString buildToml() const;
    void refreshSummary();

    bool m_ru = false;

    // ── Page 0: Welcome ──
    QRadioButton *m_methodManual    = nullptr;
    QRadioButton *m_methodDeeplink  = nullptr;
    QRadioButton *m_methodFile      = nullptr;
    QLineEdit    *m_deeplinkEdit    = nullptr;
    QLineEdit    *m_filePathEdit    = nullptr;
    QPushButton  *m_fileBrowseBtn   = nullptr;
    QPushButton  *m_importBtn       = nullptr;
    QLabel       *m_importStatus    = nullptr;

    // ── Page 1: Server ──
    QLineEdit *m_hostEdit      = nullptr;
    QLineEdit *m_addrEdit      = nullptr;
    QLineEdit *m_userEdit      = nullptr;
    QLineEdit *m_passEdit      = nullptr;
    QPlainTextEdit *m_certEdit = nullptr;
    QPushButton *m_certBrowse  = nullptr;
    QCheckBox   *m_skipVerify  = nullptr;

    // ── Page 2: Connection ──
    QComboBox *m_protocolCombo  = nullptr;
    QComboBox *m_fallbackCombo  = nullptr;
    QCheckBox *m_antiDpiCheck   = nullptr;
    QLineEdit *m_customSniEdit  = nullptr;
    QCheckBox *m_ipv6Check      = nullptr;
    QLineEdit *m_clientRandomEdit = nullptr;

    // ── Page 3: Tunnel ──
    QRadioButton *m_tunRadio   = nullptr;
    QRadioButton *m_socksRadio = nullptr;
    QComboBox *m_vpnModeCombo  = nullptr;
    QCheckBox *m_killswitchCheck = nullptr;
    QCheckBox *m_pqCheck       = nullptr;
    QPlainTextEdit *m_dnsEdit  = nullptr;
    QSpinBox  *m_mtuSpin       = nullptr;
    QCheckBox *m_changeDnsCheck = nullptr;
    QPlainTextEdit *m_exclusionsEdit = nullptr;

    // ── Page 4: Validation ──
    QTextBrowser  *m_summaryBrowser = nullptr;
    QPushButton   *m_pingBtn        = nullptr;
    QLabel        *m_pingResult     = nullptr;
    QProgressBar  *m_pingProgress   = nullptr;
    QPushButton   *m_saveBtn        = nullptr;
    QLabel        *m_saveStatus     = nullptr;
    QLineEdit     *m_savePathEdit   = nullptr;
    QPushButton   *m_saveBrowse     = nullptr;

    QString m_savedPath;
};
