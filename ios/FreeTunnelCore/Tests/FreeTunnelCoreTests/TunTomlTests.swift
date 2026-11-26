import XCTest

@testable import FreeTunnelCore

/// Parity with the desktop's tests/test_configtoml.cpp dotted-tun-key cases. The iOS parser reads
/// dotted keys into nested tables, and the serializer writes every nested table under its own
/// header, so the file's tun keys come back as one [listener.tun] table and never as dotted keys.
final class TunTomlTests: XCTestCase {
    let endpoint = """
        [endpoint]
        hostname = "vpn.example.com"
        addresses = ["1.2.3.4:443"]
        username = "u"
        password = "p"
        """
    func tun(_ config: Configuration) -> [String: Any]? {
        (config.rawTable["listener"] as? [String: Any])?["tun"] as? [String: Any]
    }
    func headers(_ text: String, _ name: String) -> Int {
        text.components(separatedBy: "[" + name + "]").count - 1
    }
    func testRootDottedTunKeysMoveUnderTheTunTable() throws {
        let config = try Configuration(
            """
            loglevel = "info"
            listener.tun.mtu_size = 1400
            listener . "tun" . excluded_routes = [
              "10.9.0.0/16",
            ]
            listener.socks.address = "127.0.0.1:1080"

            \(endpoint)
            """)
        XCTAssertEqual(tun(config)?["mtu_size"] as? Int64, 1400)
        XCTAssertEqual(config.excludedRoutes, ["10.9.0.0/16"])
        let saved = try config.serialized()
        XCTAssertEqual(headers(saved, "\"listener\".\"tun\""), 1)
        XCTAssertFalse(saved.contains("listener.tun."))
        XCTAssertFalse(saved.contains("listener . "))
        let reloaded = try Configuration(saved)
        XCTAssertEqual(tun(reloaded)?["mtu_size"] as? Int64, 1400)
        XCTAssertEqual(reloaded.excludedRoutes, ["10.9.0.0/16"])
        XCTAssertEqual(try reloaded.serialized(), saved)
    }
    func testListenerTableDottedTunKeysMoveUnderTheTunTable() throws {
        let config = try Configuration(
            """
            \(endpoint)

            [listener]
            tun.mtu_size = 1400
            "tun" . excluded_routes = ["10.9.0.0/16"]
            socks.address = "127.0.0.1:1080"
            """)
        XCTAssertEqual(tun(config)?["mtu_size"] as? Int64, 1400)
        let saved = try config.serialized()
        XCTAssertEqual(headers(saved, "\"listener\".\"tun\""), 1)
        XCTAssertFalse(saved.contains("tun.mtu_size"))
        let reloaded = try Configuration(saved)
        XCTAssertEqual(tun(reloaded)?["mtu_size"] as? Int64, 1400)
        XCTAssertEqual(reloaded.excludedRoutes, ["10.9.0.0/16"])
        XCTAssertEqual(try reloaded.serialized(), saved)
    }
    func testTunHeaderAndDottedKeysForTheSameTableAreRefused() throws {
        // TOML forbids reopening a dotted-key table with a header; the core refuses it too.
        let dotted = "[listener]\ntun.mtu_size = 1400\n"
        let table = "[listener.tun]\nmtu_size = 1280\n"
        for text in [
            "listener.tun.mtu_size = 1400\n\(endpoint)\n\(table)",
            "\(endpoint)\n\(dotted)\n\(table)", "\(endpoint)\n\(table)\n\(dotted)"
        ] {
            XCTAssertThrowsError(try Configuration(text))
        }
        // A header on its own is the ordinary form and is kept.
        XCTAssertEqual(tun(try Configuration("\(endpoint)\n\(table)"))?["mtu_size"] as? Int64, 1280)
    }
    /// iOS deliberately owns the tunnel settings: the packet tunnel provider reads the excluded
    /// routes from the generated config, which come from Settings, and the NE tunnel MTU is fixed.
    /// The file's own listener/tun values are replaced, never merged or duplicated.
    func testGeneratedTunnelConfigReplacesFileTunWithOneValidTable() throws {
        for source in [
            "listener.tun.mtu_size = 1400\nlistener.tun.excluded_routes = [\"10.9.0.0/16\"]\n\(endpoint)",
            "\(endpoint)\n[listener]\ntun.mtu_size = 1400\ntun.excluded_routes = [\"10.9.0.0/16\"]",
            "\(endpoint)\n[listener.tun]\nmtu_size = 1400\nexcluded_routes = [\"10.9.0.0/16\"]"
        ] {
            let config = try Configuration(source)
            let text = try config.tunnel(
                routing: Routing(), snapshots: [:], killSwitch: false,
                excludedRoutes: ["192.168.0.0/16"])
            XCTAssertEqual(headers(text, "\"listener\".\"tun\""), 1)
            XCTAssertLessThanOrEqual(headers(text, "\"listener\""), 1)  // the empty parent header is valid TOML
            XCTAssertFalse(text.contains("10.9.0.0/16"))
            let result = try Configuration(text)
            XCTAssertEqual(tun(result)?["mtu_size"] as? Int64, 1280)
            XCTAssertEqual(result.excludedRoutes, ["192.168.0.0/16"])
            XCTAssertEqual(tun(result)?["included_routes"] as? [String], ["0.0.0.0/0", "::/0"])
        }
    }
}
