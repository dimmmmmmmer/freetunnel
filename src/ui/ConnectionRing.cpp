#include "ConnectionRing.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QConicalGradient>
#include <cmath>

// ─── Colour palette ───
static const QColor kAccent     {0x5B, 0x6E, 0xF5};       // connecting ring (indigo)
static const QColor kGreen      {0x34, 0xD3, 0x99};       // connected ring (emerald)
static const QColor kGreenDim   {0x34, 0xD3, 0x99, 0x30}; // faint glow
static const QColor kGray       {0x64, 0x74, 0x8B};       // disconnected ring (slate)
static const QColor kRed        {0xEF, 0x44, 0x44};       // error ring
static const QColor kTextWhite  {0xEE, 0xEE, 0xEE};
static const QColor kSubText    {0x94, 0xA3, 0xB8};

ConnectionRing::ConnectionRing(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(180, 180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_TranslucentBackground);

    m_text = "Disconnected";

    m_animTimer.setInterval(25);   // ~40 fps
    connect(&m_animTimer, &QTimer::timeout, this, [this]() {
        m_animAngle = std::fmod(m_animAngle + 4.0, 360.0);
        update();
    });
}

void ConnectionRing::setStatus(Status s) {
    m_status = s;
    switch (s) {
    case Disconnected:   m_text = "Disconnected";   break;
    case Connecting:     m_text = "Connecting...";   break;
    case Connected:      m_text = "Connected";       break;
    case Reconnecting:   m_text = "Reconnecting..."; break;
    case Disconnecting:  m_text = "Disconnecting...";break;
    case Error:          m_text = "Error";           break;
    }
    if (s == Connecting || s == Reconnecting || s == Disconnecting) {
        if (!m_animTimer.isActive()) {
            m_elapsed.start();
            m_animTimer.start();
        }
    } else {
        m_animTimer.stop();
    }
    update();
}

void ConnectionRing::setStatusText(const QString &text) {
    m_text = text;
    update();
}

void ConnectionRing::setSubText(const QString &text) {
    m_subText = text;
    update();
}

void ConnectionRing::setTextColor(const QColor &color) {
    m_textColor = color;
    update();
}

void ConnectionRing::setSubTextColor(const QColor &color) {
    m_subTextColor = color;
    update();
}

// ─── painting ───────────────────────────────────────────────

void ConnectionRing::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int side = qMin(width(), height());
    const qreal ringSize = side * 0.82;
    const qreal penWidth = side * 0.018;
    const QPointF centre(width() / 2.0, height() / 2.0);
    const QRectF ringRect(centre.x() - ringSize / 2.0, centre.y() - ringSize / 2.0,
                          ringSize, ringSize);

    // ── outer glow (faint) ──
    if (m_status == Connected) {
        QPen glowPen(kGreenDim, penWidth * 3.5, Qt::SolidLine, Qt::RoundCap);
        p.setPen(glowPen);
        p.drawEllipse(ringRect);
    }

    // ── ring ──
    QColor ringColor;
    switch (m_status) {
    case Connected:      ringColor = kGreen;   break;
    case Error:          ringColor = kRed;     break;
    default:             ringColor = kGray;    break;
    }

    if (m_status == Connecting || m_status == Reconnecting || m_status == Disconnecting) {
        // Draw an animated arc sweep
        QConicalGradient grad(centre, -m_animAngle);
        grad.setColorAt(0.0, kAccent);
        grad.setColorAt(0.35, kAccent.darker(130));
        grad.setColorAt(0.65, QColor(kAccent.red(), kAccent.green(), kAccent.blue(), 40));
        grad.setColorAt(1.0, kAccent);
        QPen arcPen(QBrush(grad), penWidth, Qt::SolidLine, Qt::RoundCap);
        p.setPen(arcPen);
        p.drawEllipse(ringRect);
    } else {
        QPen ringPen(ringColor, penWidth, Qt::SolidLine, Qt::RoundCap);
        p.setPen(ringPen);
        p.drawEllipse(ringRect);
    }

    // ── status text ──
    {
        QFont f = font();
        f.setPixelSize(static_cast<int>(side * 0.09));
        f.setWeight(QFont::DemiBold);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
        p.setFont(f);
        p.setPen(m_textColor);
        p.drawText(ringRect, Qt::AlignCenter, m_text);
    }

    // ── sub text (below main text) ──
    if (!m_subText.isEmpty()) {
        QFont f = font();
        f.setPixelSize(static_cast<int>(side * 0.05));
        p.setFont(f);
        p.setPen(m_subTextColor);
        QRectF subRect = ringRect.adjusted(0, side * 0.15, 0, 0);
        p.drawText(subRect, Qt::AlignHCenter | Qt::AlignCenter, m_subText);
    }
}

void ConnectionRing::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        // Check if click is inside the ring area
        const int side = qMin(width(), height());
        const qreal r = side * 0.41;
        const QPointF centre(width() / 2.0, height() / 2.0);
        const QPointF d = event->position() - centre;
        if (d.x() * d.x() + d.y() * d.y() <= r * r * 1.3) {
            emit clicked();
        }
    }
    QWidget::mousePressEvent(event);
}
