import XCTest
@testable import FreeTunnelCore

final class RoutingTests: XCTestCase {
    let source = """
    vpn_mode = "general"
    exclusions = ["legacy.example"]
    [endpoint]
    name = "Test"
    hostname = "vpn.example"
    addresses = ["192.0.2.1:443"]
    username = "user"
    password = "secret"
    """
    func testConfigurationRoundTripAndRoutingPriority() throws {
        let config = try Configuration(source)
        XCTAssertEqual(try Configuration(config.serialized()).name, "Test")
        var routing = Routing()
        routing.manualVPN = "manual.example"
        routing.manualBypass = "192.0.2.0/24"
        let list = GeoList(name: "List", source: "https://example.com/list.txt", kind: .sites, direction: .bypass)
        routing.lists = [list]
        let snapshot = try GeoParser.parse(Data("manual.example".utf8), list: list)
        let generated = try config.tunnel(routing: routing, snapshots: [list.id: snapshot], killSwitch: true)
        XCTAssertTrue(generated.contains("@0:manual.example"))
        XCTAssertTrue(generated.contains("@1:192.0.2.0/24"))
        XCTAssertTrue(generated.contains("@3:manual.example"))
        XCTAssertTrue(generated.contains("legacy.example"))
        XCTAssertEqual(try Configuration(generated).hostname, "vpn.example")
    }
    func testMissingOrMismatchedCacheRefusesConnection() throws {
        let config = try Configuration(source)
        var routing = Routing()
        let list = GeoList(name: "List", source: "https://example.com/new.txt", kind: .sites, direction: .vpn)
        routing.lists = [list]
        XCTAssertThrowsError(try config.tunnel(routing: routing, snapshots: [:], killSwitch: true))
        let stale = GeoSnapshot(source: "https://example.com/old.txt", kind: .sites, entries: ["example.com"],
            skipped: 0)
        XCTAssertThrowsError(try config.tunnel(routing: routing, snapshots: [list.id: stale], killSwitch: true))
    }
    func testInvalidSourcesAndRulesAreRejected() {
        for source in ["http://example.com/a", "https://user:secret@example.com/a",
            "https://example.com/a.dat", "https://example.com/a.dat#ru@ads"] {
            XCTAssertThrowsError(try GeoParser.source(source))
        }
        for rule in ["@0:example.com", "example.com/path", "0.0.0.0/33", "::/129", "bad host"] {
            XCTAssertNil(GeoParser.manual(rule), rule)
        }
    }
    func testTextListsAndUnsupportedSyntax() throws {
        let list = GeoList(name: "List", source: "https://example.com/list.txt", kind: .sites, direction: .vpn)
        let result = try GeoParser.parse(Data(
            "# comment\nexample.com\nfull:single.example\nregexp:.*\nexample.com".utf8), list: list)
        XCTAssertEqual(result.entries, ["*.example.com", "example.com", "single.example"])
        XCTAssertEqual(result.skipped, 1)
        XCTAssertThrowsError(try GeoParser.parse(Data("regexp:.*".utf8), list: list))
    }
    func testMalformedProtobufAndMissingCategoryFailClosed() throws {
        let list = GeoList(name: "List", source: "https://example.com/list.dat#ru", kind: .sites, direction: .vpn)
        for bytes: [UInt8] in [[10, 127], [255], [0], [10, 0]] {
            XCTAssertThrowsError(try GeoParser.parse(Data(bytes), list: list))
        }
    }
    func testSecretsAndQuotedStringsSurviveSerialization() throws {
        let password = "pass\"\\\n\tword"
        let text = source.replacingOccurrences(of: "\"secret\"", with: Configuration.quote(password))
        XCTAssertEqual(try Configuration(Configuration(text).serialized()).endpoint["password"] as? String, password)
        XCTAssertThrowsError(try Configuration(source + "\nclient_random_auth_key = \"key\""))
    }
}
