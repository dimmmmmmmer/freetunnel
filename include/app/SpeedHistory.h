// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <algorithm>
#include <array>

// The last CAPACITY speeds of the tunnel, one a second, for the small graph over
// Home's speed tiles: what came in and what went out, oldest first. A new reading
// pushes the oldest out. The graph's scale is the highest speed in it, of either
// direction, and never less than FLOOR, so that a few bytes of keep-alive do not
// fill it. No Qt: a plain test can load it.
class SpeedHistory {
public:
    static constexpr int CAPACITY = 60;
    // 64 KB/s: the least the graph's top stands for.
    static constexpr double FLOOR = 64000.0;

    void clear() { m_start = 0; m_size = 0; }

    void add(double downBytesPerSecond, double upBytesPerSecond)
    {
        const int at = (m_start + m_size) % CAPACITY;
        m_down[at] = std::max(0.0, downBytesPerSecond);
        m_up[at] = std::max(0.0, upBytesPerSecond);
        if (m_size < CAPACITY)
            ++m_size;
        else
            m_start = (m_start + 1) % CAPACITY;
    }

    int size() const { return m_size; }
    // The i-th reading, 0 the oldest.
    double down(int i) const { return m_down[(m_start + i) % CAPACITY]; }
    double up(int i) const { return m_up[(m_start + i) % CAPACITY]; }

    double scale() const
    {
        double top = FLOOR;
        for (int i = 0; i < m_size; ++i)
            top = std::max(top, std::max(down(i), up(i)));
        return top;
    }

private:
    std::array<double, CAPACITY> m_down{};
    std::array<double, CAPACITY> m_up{};
    int m_start = 0;
    int m_size = 0;
};
