import XCTest

@testable import FreeTunnelCore

final class SessionTimeTests: XCTestCase {
    func testFormatsHoursMinutesAndSecondsWithLeadingZeros() {
        XCTAssertEqual(SessionTime.format(seconds: 0), "00:00:00")
        XCTAssertEqual(SessionTime.format(seconds: 125), "00:02:05")
        XCTAssertEqual(SessionTime.format(seconds: 3_661), "01:01:01")
        XCTAssertEqual(SessionTime.format(seconds: 100 * 3_600), "100:00:00")
        XCTAssertEqual(SessionTime.format(seconds: -3), "00:00:00")
    }
}
