#pragma once

#include <QWidget>
#include <QTimer>
#include <deque>

/// Mini sparkline traffic graph — shows last N seconds of Rx/Tx throughput.
class TrafficGraph : public QWidget {
    Q_OBJECT
public:
    explicit TrafficGraph(QWidget *parent = nullptr);

    /// Push new data point (bytes since last push).
    void addSample(quint64 rx, quint64 tx);

    /// Reset all data.
    void reset();

    QSize sizeHint() const override { return {400, 70}; }
    QSize minimumSizeHint() const override { return {120, 40}; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    struct Sample { quint64 rx = 0; quint64 tx = 0; };
    std::deque<Sample> m_samples;
    static constexpr int kMaxSamples = 80;
    quint64 m_peakValue = 1;
};
