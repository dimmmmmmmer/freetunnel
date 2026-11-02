import XCTest
@testable import FreeTunnelCore

final class GeoDatabaseTests: XCTestCase {
    private func blob(_ number: UInt8, _ bytes: Data) -> Data {
        precondition(bytes.count < 128)
        return Data([number * 8 + 2, UInt8(bytes.count)]) + bytes
    }
    private func site(_ type: UInt8, _ value: String) -> Data {
        Data([8, type]) + blob(2, Data(value.utf8))
    }
    private func category(_ code: String, members: [Data], inverted: Bool = false) -> Data {
        var record = blob(1, Data(code.utf8))
        for member in members { record += blob(2, member) }
        if inverted { record += Data([24, 1]) }
        return blob(1, record)
    }
    func testGeositeSelectsCategoryAndPreservesDomainSemantics() throws {
        let data = category("other", members: [site(3, "other.example")]) + category("RU", members: [
            site(2, "example.com"), site(3, "exact.example"), site(1, ".*"), site(0, "substring")
        ])
        let list = GeoList(name: "Sites", source: "https://example.com/geosite.dat#ru", kind: .sites, direction: .vpn)
        let parsed = try GeoParser.parse(data, list: list)
        XCTAssertEqual(parsed.entries, ["*.example.com", "exact.example", "example.com"])
        XCTAssertEqual(parsed.skipped, 2)
    }
    func testGeoipIgnoresUnselectedReverseCategoryAndRejectsSelectedReverse() throws {
        let member = blob(1, Data([192, 0, 2, 0])) + Data([16, 24])
        let data = category("other", members: [member], inverted: true) + category("ru", members: [member])
        let list = GeoList(name: "IPs", source: "https://example.com/geoip.dat#ru", kind: .ips, direction: .bypass)
        XCTAssertEqual(try GeoParser.parse(data, list: list).entries, ["192.0.2.0/24"])
        XCTAssertThrowsError(try GeoParser.parse(category("ru", members: [member], inverted: true), list: list))
    }
    func testGeoipRejectsOversizedPrefix() {
        let member = blob(1, Data([192, 0, 2, 0])) + Data([16, 33])
        let list = GeoList(name: "IPs", source: "https://example.com/geoip.dat#ru", kind: .ips, direction: .vpn)
        XCTAssertThrowsError(try GeoParser.parse(category("ru", members: [member]), list: list))
    }
}
