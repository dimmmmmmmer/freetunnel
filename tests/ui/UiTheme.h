// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QColor>
#include <QObject>

// Theme palette matching Main.qml dark mode for page smoke tests.
class UiTheme : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool dark READ dark CONSTANT)
    Q_PROPERTY(QColor bg MEMBER m_bg CONSTANT)
    Q_PROPERTY(QColor surface MEMBER m_surface CONSTANT)
    Q_PROPERTY(QColor tile MEMBER m_tile CONSTANT)
    Q_PROPERTY(QColor inputBg MEMBER m_inputBg CONSTANT)
    Q_PROPERTY(QColor inputBorder MEMBER m_inputBorder CONSTANT)
    Q_PROPERTY(QColor text MEMBER m_text CONSTANT)
    Q_PROPERTY(QColor textDim MEMBER m_textDim CONSTANT)
    Q_PROPERTY(QColor textFaint MEMBER m_textFaint CONSTANT)
    Q_PROPERTY(QColor accent MEMBER m_accent CONSTANT)
    Q_PROPERTY(QColor border MEMBER m_border CONSTANT)
    Q_PROPERTY(QColor toggleOff MEMBER m_toggleOff CONSTANT)
    Q_PROPERTY(QColor success MEMBER m_success CONSTANT)
    Q_PROPERTY(QColor warn MEMBER m_warn CONSTANT)
    Q_PROPERTY(QColor danger MEMBER m_danger CONSTANT)
    Q_PROPERTY(QColor infoBg MEMBER m_infoBg CONSTANT)

public:
    explicit UiTheme(QObject *parent = nullptr);

    bool dark() const { return true; }

private:
    QColor m_bg = QColor(QStringLiteral("#181818"));
    QColor m_surface = QColor(QStringLiteral("#262626"));
    QColor m_tile = QColor(QStringLiteral("#202020"));
    QColor m_inputBg = QColor(QStringLiteral("#101010"));
    QColor m_inputBorder = QColor(QStringLiteral("#3a3a3a"));
    QColor m_text = QColor(QStringLiteral("#eaeaea"));
    QColor m_textDim = QColor(QStringLiteral("#9a9a9a"));
    QColor m_textFaint = QColor(QStringLiteral("#6a6a6a"));
    QColor m_accent = QColor(QStringLiteral("#b0b0b0"));
    QColor m_border = QColor(QStringLiteral("#2e2e2e"));
    QColor m_toggleOff = QColor(QStringLiteral("#3a3a3a"));
    QColor m_success = QColor(QStringLiteral("#3fbf93"));
    QColor m_warn = QColor(QStringLiteral("#d99634"));
    QColor m_danger = QColor(QStringLiteral("#e06a6a"));
    QColor m_infoBg = QColor::fromRgbF(0.69, 0.69, 0.69, 0.16);
};
