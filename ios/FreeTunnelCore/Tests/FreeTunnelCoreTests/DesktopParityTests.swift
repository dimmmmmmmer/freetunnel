import XCTest

@testable import FreeTunnelCore

final class DesktopParityTests: XCTestCase {
    func testDefaultThemeIsDarkAndSavedChoicesArePreserved() throws {
        XCTAssertEqual(Preferences().theme, "dark")
        XCTAssertEqual(try JSONDecoder().decode(Preferences.self, from: Data("{}".utf8)).theme, "dark")
        for theme in ["system", "light", "dark"] {
            let data = Data("{\"theme\":\"\(theme)\"}".utf8)
            XCTAssertEqual(try JSONDecoder().decode(Preferences.self, from: data).theme, theme)
        }
    }

    let source = """
        dns_upstreams = ["1.1.1.1"]
        custom_root_option = "keep"
        [endpoint]
        name = "Demo"
        hostname = "vpn.example.org"
        addresses = ["192.0.2.1:443"]
        username = "user"
        password = "secret"
        custom_endpoint_option = "keep too"
        """
    func testStructuredEditorPreservesUnknownFieldsAndAdvancedOptions() throws {
        let original = try Configuration(source)
        var fields = EditorFields(original)
        fields.addresses = "192.0.2.2:443, [2001:db8::1]:8443"
        fields.random = "deadbeef/ffffffff"
        fields.sni = "sni.example.org"
        fields.ipv6 = false
        fields.skipVerification = true
        fields.antiDPI = true
        fields.proto = "http3"
        fields.dns = "8.8.8.8, 1.0.0.1"
        fields.password = "a\"b\\c\n"
        let result = try fields.configuration(previous: original)
        XCTAssertEqual(EditorFields(result), fields)
        XCTAssertEqual(result.rawTable["custom_root_option"] as? String, "keep")
        XCTAssertEqual(result.endpoint["custom_endpoint_option"] as? String, "keep too")
        fields.random = "abc"
        XCTAssertThrowsError(try fields.configuration(previous: original))
    }
    func testOldPerServerRoutingMigratesWithoutLosingOppositeRules() throws {
        let id = UUID()
        var first = Profile(id: id, name: "First")
        first.routing.manualVPN = "vpn.example"
        first.routing.manualBypass = "direct.example"
        var second = Profile(name: "Second")
        second.routing.manualBypass = "other.example"
        let encoded = try JSONEncoder().encode([first, second])
        let profilesJSON = try XCTUnwrap(String(data: encoded, encoding: .utf8))
        let old = "{\"profiles\":\(profilesJSON),\"selectedID\":\"\(id)\",\"theme\":\"dark\"}"
        let migrated = try JSONDecoder().decode(Preferences.self, from: Data(old.utf8))
        XCTAssertEqual(migrated.splitProfiles.count, 2)
        XCTAssertEqual(migrated.routing(for: migrated.profiles[0]), first.routing)
        XCTAssertEqual(migrated.routing(for: migrated.profiles[1]), second.routing)
        XCTAssertEqual(migrated.selectedID, id)
        let roundTrip = try JSONDecoder().decode(Preferences.self, from: JSONEncoder().encode(migrated))
        XCTAssertEqual(roundTrip.splitProfiles, migrated.splitProfiles)
        XCTAssertEqual(roundTrip.profiles, migrated.profiles)
    }
    func testExcludedRoutesAndEmptySelectiveProfile() throws {
        let config = try Configuration(source)
        var routing = Routing()
        routing.defaultDirection = .bypass
        let result = try Configuration(
            config.tunnel(
                routing: routing, snapshots: [:], killSwitch: true,
                excludedRoutes: ["10.0.0.0/8", "2001:db8::/32"], verboseLogs: true))
        XCTAssertEqual(result.excludedRoutes, ["10.0.0.0/8", "2001:db8::/32"])
        XCTAssertEqual(result.defaultDirection, .vpn)
        XCTAssertEqual(result.rawTable["loglevel"] as? String, "debug")
        XCTAssertEqual(try IPRoutes.parse("10.0.0.0/8").ipv4Mask, "255.0.0.0")
        XCTAssertEqual(try IPRoutes.parse("0.0.0.0/0").ipv4Mask, "0.0.0.0")
        for route in ["example.org", "10.0.0.0/33", "::/129", "1.2.3.4/-1", "1.2.3.4/"] {
            XCTAssertFalse(IPRoutes.isValid(route))
            XCTAssertThrowsError(
                try config.tunnel(routing: routing, snapshots: [:], killSwitch: true, excludedRoutes: [route])
            )
        }
    }
    func testImportedRulesBecomeEditableAndCanBeRemoved() throws {
        let config = try Configuration("exclusions = [\"example.org\", \"192.0.2.0/24\"]\n" + source)
        var routing = config.importedRouting
        XCTAssertEqual(routing.manualBypass, "example.org\n192.0.2.0/24")
        let stored = try Configuration(config.storingImportedRulesInProfile())
        routing.manualBypass = ""
        let result = try Configuration(stored.tunnel(routing: routing, snapshots: [:], killSwitch: true))
        XCTAssertEqual(result.rawTable["exclusions"] as? [String], [])
    }
    func testCoreLogTimestampAndLevelMatchDesktop() {
        let entry = LogLines.entry(
            "26.06.2026 19:57:38.006946 INFO [4439691] CATEGORY: message", isCore: true)
        XCTAssertEqual(entry, "2026-06-26 19:57:38\tCORE\t[4439691] CATEGORY: message")
        XCTAssertEqual(LogLines.shownText(entry), "19:57:38 CORE [4439691] CATEGORY: message")
        let warning = LogLines.entry("26.06.2026 19:57:38.006946 WARNING retry", isCore: true)
        XCTAssertEqual(LogLines.shownText(warning), "19:57:38 WARN retry")
        XCTAssertFalse(LogLines.entry("line one\nline two\tsecret").contains("\n"))
    }
    func testExportMatchesDesktopV1Vector() throws {
        // Generated by the desktop's encodeDeepLink, including its DNS list encoding.
        var fields = EditorFields(try Configuration(source))
        fields.dns = "1.1.1.1, 8.8.8.8"
        XCTAssertEqual(
            try DeepLinkEncoder.encode(fields.configuration()),
            "tt://?AAEBAQ92cG4uZXhhbXBsZS5vcmcCDTE5Mi4wLjIuMTo0NDMFBHVzZXIGBnNlY3JldAwERGVtbw0QBzEuMS4xLjEHOC44LjguOA"
        )
    }
    func testDeepLinkV1AndExportDoNotCopyPrivateKeys() throws {
        let config = try Configuration(source)
        let link = try DeepLinkEncoder.encode(config)
        XCTAssertTrue(link.hasPrefix("tt://?AAEBAQ"))  // version TLV 00 01 01, hostname tag 01
        var fields = EditorFields(config)
        fields.certificate =
            "-----BEGIN PRIVATE KEY-----\nSECRET\n-----END PRIVATE KEY-----\n"
            + "-----BEGIN CERTIFICATE-----\nMAA=\n-----END CERTIFICATE-----"
        let withCertificate = try fields.configuration(previous: config)
        let text = try DeepLinkEncoder.exportTOML(withCertificate)
        XCTAssertFalse(text.contains("PRIVATE KEY"))
        XCTAssertFalse(text.contains("SECRET"))
        XCTAssertTrue(text.contains("CERTIFICATE"))
        XCTAssertNoThrow(try DeepLinkEncoder.encode(withCertificate))
    }
}
