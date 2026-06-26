// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QObject>
#include <QString>
#include <QVariant>

// Minimal shell object passed to QML pages (Window API subset).
class MockShell : public QObject {
    Q_OBJECT
    Q_PROPERTY(int currentPage READ currentPage WRITE setCurrentPage NOTIFY currentPageChanged)
    Q_PROPERTY(QString overlay READ overlay WRITE setOverlay NOTIFY overlayChanged)
    Q_PROPERTY(int editIndex READ editIndex WRITE setEditIndex NOTIFY editIndexChanged)

public:
    explicit MockShell(QObject *parent = nullptr);

    int currentPage() const { return m_currentPage; }
    void setCurrentPage(int v);
    QString overlay() const { return m_overlay; }
    void setOverlay(const QString &v);
    int editIndex() const { return m_editIndex; }
    void setEditIndex(int v);

    QString lastToast() const { return m_lastToast; }

    Q_INVOKABLE void showToast(const QString &msg);
    Q_INVOKABLE void showConfirm(const QString &, const QString &, const QVariant &) {}
    Q_INVOKABLE void showSelect(QObject *, const QVariant &, const QString &, const QVariant &) {}
    Q_INVOKABLE QString elide(const QString &s, int n) const;
    Q_INVOKABLE QString elideMiddle(const QString &s, int n) const;
    Q_INVOKABLE QString keyGlyphs(const QString &seq) const { return seq; }
    Q_INVOKABLE void startWindowDrag(QObject *) {}

signals:
    void currentPageChanged();
    void overlayChanged();
    void editIndexChanged();

private:
    int m_currentPage = 0;
    QString m_overlay;
    int m_editIndex = -1;
    QString m_lastToast;
};
