import XCTest

@testable import FreeTunnelCore

final class SpeedHistoryTests: XCTestCase {
    func testKeepsTheLastMinuteOldestFirst() {
        var history = SpeedHistory()
        for index in 0..<(SpeedHistory.capacity + 5) { history.add(down: Double(index), up: Double(-index)) }
        XCTAssertEqual(history.count, SpeedHistory.capacity)
        XCTAssertEqual(history.down(0), 5)
        XCTAssertEqual(history.down(SpeedHistory.capacity - 1), Double(SpeedHistory.capacity + 4))
        XCTAssertEqual(history.up(3), 0, "A negative speed is 0")
    }

    func testTheScaleIsTheHighestSpeedAndNeverBelowTheFloor() {
        var history = SpeedHistory()
        XCTAssertEqual(history.scale, SpeedHistory.floor)
        history.add(down: 100, up: 200)
        XCTAssertEqual(history.scale, SpeedHistory.floor)
        history.add(down: 10, up: 2_000_000)
        history.add(down: 1_000_000, up: 0)
        XCTAssertEqual(history.scale, 2_000_000)
    }

    func testClearStartsAfresh() {
        var history = SpeedHistory()
        history.add(down: 1, up: 1)
        history.clear()
        XCTAssertEqual(history.count, 0)
        history.add(down: 7, up: 8)
        XCTAssertEqual(history.down(0), 7)
        XCTAssertEqual(history.up(0), 8)
    }
}
