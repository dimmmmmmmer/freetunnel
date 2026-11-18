import XCTest

@testable import FreeTunnelCore

final class SubscriptionTests: XCTestCase {
    var document: [String: Any] {
        [
            "version": 1, "hostname": "vpn.example", "address": "192.0.2.1:443",
            "username": "user", "password": "secret", "has_ipv6": true
        ]
    }
    func testCredentialsDoNotRemainInRequestURL() throws {
        let request = try Subscription.request("https://user:pass@example.com/sub#label")
        XCTAssertEqual(request.url?.absoluteString, "https://example.com/sub")
        XCTAssertEqual(request.value(forHTTPHeaderField: "Authorization"), "Basic dXNlcjpwYXNz")
        XCTAssertNil(
            try Subscription.request("https://example.com/sub").value(forHTTPHeaderField: "Authorization"))
        XCTAssertThrowsError(try Subscription.request("http://example.com/sub"))
        XCTAssertThrowsError(try Subscription.request("https://user%3Aname:pass@example.com/sub"))
    }
    func testUpdatePreservesLocalDNSAndUsesNewCredentials() throws {
        let data = try JSONSerialization.data(withJSONObject: document)
        let first = try Subscription.apply(data)
        let local = first + "\ndns_upstreams = [\"1.1.1.1\"]\n"
        var changed = document
        changed["password"] = "new secret"
        changed["dns_upstreams"] = ["9.9.9.9"]
        let result = try Configuration(
            Subscription.apply(
                JSONSerialization.data(withJSONObject: changed),
                previous: local))
        XCTAssertEqual(result.endpoint["password"] as? String, "new secret")
        XCTAssertEqual(result.endpoint["dns_upstreams"] as? [String], ["1.1.1.1"])
        XCTAssertEqual(result.endpoint["has_ipv6"] as? Bool, true)
        XCTAssertEqual(result.endpoint["skip_verification"] as? Bool, false)
    }
    func testUntrustedFieldsCannotDisableVerificationOrSelectUnsupportedCore() throws {
        for pair: (String, Any) in [
            ("version", true), ("version", 2), ("skip_verification", true),
            ("skip_verification", "false"), ("certificate", 12),
            ("client_random_auth_key", "key"), ("client_random_auth_key", 123),
            ("has_ipv6", "true"), ("address", "example.com/path")
        ] {
            var value = document
            value[pair.0] = pair.1
            XCTAssertThrowsError(
                try Subscription.apply(JSONSerialization.data(withJSONObject: value)), pair.0)
        }
    }
    func testClientRandomPrefixConversion() throws {
        var value = document
        value["client_random_prefix"] = "AABB"
        let config = try Configuration(Subscription.apply(JSONSerialization.data(withJSONObject: value)))
        XCTAssertEqual(config.endpoint["client_random"] as? String, "aabb")
        value["client_random_prefix"] = "xyz"
        XCTAssertThrowsError(try Subscription.apply(JSONSerialization.data(withJSONObject: value)))
    }
    func testClientRandomMaskUsesCoreHexFormat() throws {
        var value = document
        value["client_random_prefix"] = "AABB/FF00"
        let result = try Configuration(Subscription.apply(JSONSerialization.data(withJSONObject: value)))
        XCTAssertEqual(result.endpoint["client_random"] as? String, "aabb/ff00")
        value["client_random_prefix"] = "0xaabb"
        XCTAssertThrowsError(try Subscription.apply(JSONSerialization.data(withJSONObject: value)))
    }

}
