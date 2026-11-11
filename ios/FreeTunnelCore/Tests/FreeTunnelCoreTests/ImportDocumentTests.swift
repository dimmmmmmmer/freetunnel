import XCTest
@testable import FreeTunnelCore

final class ImportDocumentTests: XCTestCase {
    let source = """
        [endpoint]
        hostname = "vpn.example.com"
        addresses = ["192.0.2.1:443"]
        username = "alice"
        password = "secret"
        """
    func testDesktopV2VectorPreservesOptionsAndSubscription() throws {
        let link = "tt://?AAECAQ92cG4uZXhhbXBsZS5jb20FBWFsaWNlBgZzM2NyZXQCCzEuMi4zLjQ6NDQzAhFbMjAwMTpkYjg6OjFdOjQ0MwsNYWFiYmNjL2ZmMDBmZgMLZXhhbXBsZS5vcmcEAQAKAQEJAQIMB0V4YW1wbGUNHgcxLjEuMS4xFXRsczovL2Rucy5leGFtcGxlLmNvbQ4xaHR0cHM6Ly9hbGljZTpzM2NyZXRAdnBuLmV4YW1wbGUuY29tL3N1YnNjcmlwdGlvbg"
        let document = try ImportDocument.parse(link)
        let config = try Configuration(XCTUnwrap(document.toml))
        XCTAssertEqual(config.name, "Example")
        XCTAssertEqual(config.endpoint["client_random"] as? String, "aabbcc/ff00ff")
        XCTAssertEqual(config.endpoint["upstream_protocol"] as? String, "http3")
        XCTAssertEqual(config.endpoint["has_ipv6"] as? Bool, false)
        XCTAssertEqual(config.dnsUpstreams, ["1.1.1.1", "tls://dns.example.com"])
        XCTAssertEqual(document.subscription, "https://alice:s3cret@vpn.example.com/subscription")
    }
    func testFlatExportAndSubscriptionOnly() throws {
        let flat = source.replacingOccurrences(of: "[endpoint]\n", with: "")
            .replacingOccurrences(of: "addresses = [\"192.0.2.1:443\"]", with: "address = \"192.0.2.1:443\"")
        let document = try ImportDocument.parse(flat + "\nclient_random_prefix = \"AABB\"")
        XCTAssertEqual(try Configuration(XCTUnwrap(document.toml)).endpoint["client_random"] as? String, "aabb")
        let subscription = try ImportDocument.parse("subscription_url = \"https://example.com/sub\"")
        XCTAssertNil(subscription.toml)
        XCTAssertEqual(subscription.subscription, "https://example.com/sub")
        XCTAssertThrowsError(try ImportDocument.parse("subscription_url = \"http://example.com/sub\""))
        XCTAssertThrowsError(try ImportDocument.parse("subscription_url = \"https://example.com/sub\"\nclient_random_auth_key = \"aa\""))
    }
    func testBatchAndWrappedLink() throws {
        let link = try DeepLinkEncoder.encode(Configuration(source))
        let cut = link.index(link.startIndex, offsetBy: 20)
        let wrapped = String(link[..<cut]) + "\n" + String(link[cut...])
        XCTAssertEqual(try ImportDocument.split(wrapped).count, 1)
        XCTAssertEqual(try Configuration(XCTUnwrap(ImportDocument.parse(wrapped).toml)).hostname, "vpn.example.com")
        XCTAssertEqual(try ImportDocument.split(link + "\n" + link).count, 2)
        XCTAssertThrowsError(try ImportDocument.split(Array(repeating: link, count: 101).joined(separator: "\n")))
    }
    func testInvalidAndUnsupportedFieldsAreNotSilentlyLost() throws {
        XCTAssertThrowsError(try ImportDocument.parse("tt://?AAECAQ92cG4uZXhhbXBsZS5jb20FBWFsaWNlBgZzZWNyZXQCCzEuMi4zLjQ6NDQzDwhhYWJiY2NkZA"))
        XCTAssertThrowsError(try ImportDocument.parse(source + "\nclient_random_prefix = \"zz\""))
        XCTAssertThrowsError(try ImportDocument.parse(source + "\ncertificate = \"PRIVATE KEY\""))
        XCTAssertThrowsError(try ImportDocument.parse("tt://?%%%"))
    }
}
