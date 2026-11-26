import XCTest

@testable import FreeTunnelCore

final class GeoDefaultsTests: XCTestCase {
    func testFreshInstallStartsWithRussianListsThatBypassTheVPN() throws {
        let lists = Preferences.freshInstall().splitProfiles[0].routing.lists
        XCTAssertEqual(lists.map(\.name), ["Russian sites", "Russian IP addresses"])
        XCTAssertEqual(lists.map(\.kind), [.sites, .ips])
        XCTAssertEqual(lists.map(\.direction), [.bypass, .bypass])
        XCTAssertTrue(lists.allSatisfy { $0.enabled && UUID(uuidString: $0.id) != nil })
        for list in lists { XCTAssertNoThrow(try GeoParser.source(list.source)) }
        XCTAssertEqual(Preferences.freshInstall().splitProfiles[0].routing.manualBypass, "")
    }
    func testSourcesMatchTheSharedPresetsFile() throws {
        var url = URL(fileURLWithPath: #filePath)
        for _ in 0..<5 { url.deleteLastPathComponent() }
        guard let data = try? Data(contentsOf: url.appendingPathComponent("data/geo/presets.json")),
            let rows = try JSONSerialization.jsonObject(with: data) as? [[String: String]]
        else { return }  // not in a full checkout
        let wanted = ["ru-sites", "ru-ips"].compactMap { id in rows.first { $0["id"] == id } }
        XCTAssertEqual(wanted.map { $0["source"] }, GeoList.freshInstallDefaults().map(\.source))
        XCTAssertEqual(wanted.map { $0["name"] }, GeoList.freshInstallDefaults().map(\.name))
    }
    func testSavedPreferencesAreNeverSeeded() throws {
        // Decoding is how saved preferences load: empty saved lists stay empty.
        let empty = try JSONDecoder().decode(Preferences.self, from: Data("{}".utf8))
        XCTAssertTrue(empty.splitProfiles[0].routing.lists.isEmpty)
        let saved = try JSONDecoder().decode(
            Preferences.self, from: JSONEncoder().encode(Preferences()))
        XCTAssertTrue(saved.splitProfiles[0].routing.lists.isEmpty)
        let roundTrip = try JSONDecoder().decode(
            Preferences.self, from: JSONEncoder().encode(Preferences.freshInstall()))
        XCTAssertEqual(roundTrip.splitProfiles[0].routing.lists.count, 2)
    }
}
