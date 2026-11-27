import Foundation

/// The last `capacity` speeds of the tunnel, one a second, for the small graph over Home's speed
/// tiles: what came in and what went out, oldest first. A new reading pushes the oldest out. The
/// graph's scale is the highest speed in it, of either direction, and never less than `floor`, so
/// that a few bytes of keep-alive do not fill it. Mirrors the desktop and Android SpeedHistory.
public struct SpeedHistory: Equatable, Sendable {
    public static let capacity = 60
    /// 64 KB/s: the least the graph's top stands for.
    public static let floor = 64_000.0

    private var downs = [Double](repeating: 0, count: SpeedHistory.capacity)
    private var ups = [Double](repeating: 0, count: SpeedHistory.capacity)
    private var start = 0
    public private(set) var count = 0

    public init() {}

    public mutating func clear() {
        start = 0
        count = 0
    }

    public mutating func add(down: Double, up upload: Double) {
        let slot = (start + count) % SpeedHistory.capacity
        downs[slot] = max(0, down)
        ups[slot] = max(0, upload)
        if count < SpeedHistory.capacity {
            count += 1
        } else {
            start = (start + 1) % SpeedHistory.capacity
        }
    }

    /// The i-th reading, 0 the oldest.
    public func down(_ index: Int) -> Double { downs[(start + index) % SpeedHistory.capacity] }
    public func up(_ index: Int) -> Double { ups[(start + index) % SpeedHistory.capacity] }

    public var scale: Double {
        var top = SpeedHistory.floor
        for index in 0..<count { top = max(top, down(index), up(index)) }
        return top
    }
}
