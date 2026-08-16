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
    // Main.qml exposes this read-only (select popup / confirm dialog open); it is
    // writable here so a test can put a sub-screen in that state.
    Q_PROPERTY(bool windowPopupOpen READ windowPopupOpen WRITE setWindowPopupOpen NOTIFY
                       windowPopupOpenChanged)
    // Mirrors Main.qml's monoFont — the fixed-pitch family the log view and the
    // certificate editor ask the shell for. Constant: pages only ever read it.
    Q_PROPERTY(QString monoFont READ monoFont CONSTANT)

public:
    explicit MockShell(QObject *parent = nullptr);

    int currentPage() const { return m_currentPage; }
    void setCurrentPage(int v);
    QString overlay() const { return m_overlay; }
    void setOverlay(const QString &v);
    int editIndex() const { return m_editIndex; }
    void setEditIndex(int v);
    bool windowPopupOpen() const { return m_windowPopupOpen; }
    void setWindowPopupOpen(bool v);
    QString monoFont() const;

    QString lastToast() const { return m_lastToast; }

    Q_INVOKABLE void showToast(const QString &msg);
    Q_INVOKABLE void showConfirm(const QString &, const QString &, const QVariant &) {}
    Q_INVOKABLE void showSelect(QObject *, const QVariant &, const QString &, const QVariant &) {}
    Q_INVOKABLE QString elide(const QString &s, int n) const;
    Q_INVOKABLE QString elideMiddle(const QString &s, int n) const;
    Q_INVOKABLE QString keyGlyphs(const QString &seq) const { return seq; }
    // Mirrors Main.qml's keyName(): a portable QKeySequence name for a key code,
    // or "" when the layout gives a non-Latin character (HotkeyField then falls
    // back to backend.physicalLetterForScanCode).
    Q_INVOKABLE QString keyName(int key, const QString &text) const;
    Q_INVOKABLE void startWindowDrag(QObject *) {}

signals:
    void currentPageChanged();
    void overlayChanged();
    void editIndexChanged();
    void windowPopupOpenChanged();

private:
    int m_currentPage = 0;
    QString m_overlay;
    int m_editIndex = -1;
    bool m_windowPopupOpen = false;
    QString m_lastToast;
};
