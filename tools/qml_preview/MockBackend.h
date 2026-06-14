#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// Stand-in for the real Backend so the QML UI can be built/previewed without the
// C++ core. Mirrors the Backend property/method surface the QML binds to.
class MockBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(QString sessionTime READ sessionTime NOTIFY changed)
    Q_PROPERTY(QString downSpeed READ downSpeed NOTIFY changed)
    Q_PROPERTY(QString upSpeed READ upSpeed NOTIFY changed)
    Q_PROPERTY(QString activeConfig READ activeConfig NOTIFY changed)
    Q_PROPERTY(QStringList configs READ configs NOTIFY changed)
    Q_PROPERTY(int activeIndex READ activeIndex NOTIFY changed)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY changed)
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode NOTIFY changed)
    Q_PROPERTY(bool autoConnect READ autoConnect WRITE setAutoConnect NOTIFY changed)
    Q_PROPERTY(bool killSwitch READ killSwitch WRITE setKillSwitch NOTIFY changed)
    Q_PROPERTY(QVariantList logEntries READ logEntries NOTIFY changed)
public:
    QVariantList logEntries() const {
        auto e = [](const char *t, const char *l, const char *m) {
            QVariantMap v; v["time"] = t; v["level"] = l; v["msg"] = m; return QVariant(v);
        };
        return m_logCleared ? QVariantList{}
                            : QVariantList{
                                  e("12:04:32", "INFO", "connecting frankfurt.example.com:443 (HTTP/2)"),
                                  e("12:04:33", "WARN", "DNS upstream 1.1.1.1 slow, retrying"),
                                  e("12:04:34", "INFO", "tunnel established (utun5)"),
                                  e("12:06:11", "ERROR", "connection reset, reconnecting (1)"),
                                  e("12:06:13", "INFO", "tunnel re-established") };
    }
    Q_INVOKABLE void clearLogs() { m_logCleared = true; emit changed(); }
    Q_INVOKABLE void openLogFolder() {}
    bool connected() const { return m_on; }
    QString sessionTime() const { return QStringLiteral("01:24:36"); }
    QString downSpeed() const { return QStringLiteral("12.4"); }
    QString upSpeed() const { return QStringLiteral("1.2"); }
    QStringList configs() const { return m_configs; }
    int activeIndex() const { return m_active; }
    QString activeConfig() const {
        return m_active >= 0 && m_active < m_configs.size() ? m_configs.at(m_active)
                                                            : QStringLiteral("Нет конфига");
    }
    QString language() const { return m_lang; }
    QString themeMode() const { return m_theme; }
    bool autoConnect() const { return m_autoConnect; }
    bool killSwitch() const { return m_kill; }
    void setLanguage(const QString &v) { m_lang = v; emit changed(); }
    void setThemeMode(const QString &v) { m_theme = v; emit changed(); }
    void setAutoConnect(bool v) { m_autoConnect = v; emit changed(); }
    void setKillSwitch(bool v) { m_kill = v; emit changed(); }

    Q_INVOKABLE void toggle() { m_on = !m_on; emit changed(); }
    Q_INVOKABLE void selectConfig(int i) { m_active = i; emit changed(); }
    Q_INVOKABLE void removeConfig(int i) {
        if (i >= 0 && i < m_configs.size()) { m_configs.removeAt(i); if (m_active >= m_configs.size()) m_active = m_configs.size() - 1; }
        emit changed();
    }
    Q_INVOKABLE bool importDeepLink(const QString &) { return true; }
    Q_INVOKABLE bool importFile(const QString &) { return true; }
    Q_INVOKABLE bool createConfig(const QVariantMap &) { return true; }
signals:
    void changed();
private:
    bool m_on = true;
    QStringList m_configs{QStringLiteral("Германия · Франкфурт"),
                          QStringLiteral("Нидерланды · Амстердам"),
                          QStringLiteral("США · Нью-Йорк")};
    int m_active = 0;
    QString m_lang = QStringLiteral("en");
    QString m_theme = QStringLiteral("system");
    bool m_autoConnect = false;
    bool m_kill = true;
    bool m_logCleared = false;
};
