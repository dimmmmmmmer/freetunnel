#include "TrafficGraph.h"

#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

static const QColor kRxColor   {0x34, 0xD3, 0x99};        // emerald green
static const QColor kTxColor   {0x5B, 0x6E, 0xF5, 0xB0};  // indigo
static const QColor kRxFill    {0x34, 0xD3, 0x99, 0x28};
static const QColor kTxFill    {0x5B, 0x6E, 0xF5, 0x18};
static const QColor kGridColor {0x33, 0x38, 0x44};

TrafficGraph::TrafficGraph(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(40);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAttribute(Qt::WA_TranslucentBackground);
}

void TrafficGraph::addSample(quint64 rx, quint64 tx) {
    m_samples.push_back({rx, tx});
    if (static_cast<int>(m_samples.size()) > kMaxSamples) {
        m_samples.pop_front();
    }
    // Update peak
    const quint64 localMax = std::max(rx, tx);
    if (localMax > m_peakValue) {
        m_peakValue = localMax;
    }
    // Decay peak slowly so graph scales dynamically
    if (m_samples.size() > 10) {
        quint64 maxInWindow = 1;
        for (const auto &s : m_samples) {
            maxInWindow = std::max(maxInWindow, std::max(s.rx, s.tx));
        }
        m_peakValue = maxInWindow;
    }
    update();
}

void TrafficGraph::reset() {
    m_samples.clear();
    m_peakValue = 1;
    update();
}

void TrafficGraph::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int w = width();
    const int h = height();

    // Grid lines (faint horizontal)
    p.setPen(QPen(kGridColor, 0.5));
    for (int i = 1; i <= 3; ++i) {
        const int y = h * i / 4;
        p.drawLine(0, y, w, y);
    }

    if (m_samples.size() < 2) return;

    const int n = static_cast<int>(m_samples.size());
    const qreal xStep = static_cast<qreal>(w) / (kMaxSamples - 1);
    const qreal xOffset = (kMaxSamples - n) * xStep;
    const qreal peak = static_cast<qreal>(m_peakValue);

    auto buildPath = [&](auto accessor) -> QPainterPath {
        QPainterPath path;
        for (int i = 0; i < n; ++i) {
            const qreal x = xOffset + i * xStep;
            const qreal val = static_cast<qreal>(accessor(m_samples[i]));
            const qreal y = h - (val / peak) * (h - 4) - 2;
            if (i == 0) path.moveTo(x, y);
            else        path.lineTo(x, y);
        }
        return path;
    };

    auto rxAccessor = [](const Sample &s) { return s.rx; };
    auto txAccessor = [](const Sample &s) { return s.tx; };

    QPainterPath rxPath = buildPath(rxAccessor);
    QPainterPath txPath = buildPath(txAccessor);

    // Fill under curves
    auto fillUnder = [&](QPainterPath linePath, const QColor &fillColor) {
        QPainterPath fill = linePath;
        fill.lineTo(xOffset + (n - 1) * xStep, h);
        fill.lineTo(xOffset, h);
        fill.closeSubpath();
        p.fillPath(fill, fillColor);
    };

    fillUnder(rxPath, kRxFill);
    fillUnder(txPath, kTxFill);

    // Draw lines
    p.setPen(QPen(kTxColor, 1.2));
    p.drawPath(txPath);
    p.setPen(QPen(kRxColor, 1.5));
    p.drawPath(rxPath);
}
