#pragma once

#include <QWidget>
#include <QTimer>
#include <QElapsedTimer>

/// Custom widget drawing a large circular ring with status text in the centre.
/// Inspired by modern VPN apps: orange ring when connected, gray when off,
/// animated arc while connecting. Clicking the ring toggles VPN on/off.
class ConnectionRing : public QWidget {
    Q_OBJECT
public:
    enum Status { Disconnected, Connecting, Connected, Reconnecting, Disconnecting, Error };

    explicit ConnectionRing(QWidget *parent = nullptr);

    void setStatus(Status s);
    Status status() const { return m_status; }

    void setStatusText(const QString &text);
    void setSubText(const QString &text);
    void setTextColor(const QColor &color);
    void setSubTextColor(const QColor &color);

    QSize sizeHint() const override { return {260, 260}; }
    QSize minimumSizeHint() const override { return {180, 180}; }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    Status  m_status    = Disconnected;
    QString m_text;
    QString m_subText;
    QColor  m_textColor  {0xEE, 0xEE, 0xEE};
    QColor  m_subTextColor {0x94, 0xA3, 0xB8};
    QTimer  m_animTimer;
    QElapsedTimer m_elapsed;
    qreal   m_animAngle = 0.0;
};
