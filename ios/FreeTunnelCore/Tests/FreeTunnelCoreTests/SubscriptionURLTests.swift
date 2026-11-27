import XCTest

@testable import FreeTunnelCore

final class SubscriptionURLTests: XCTestCase {
    func testHoldsTheDesktopsLimitInBytesNotCharacters() {
        let head = "https://example.org/"
        let exact = head + String(repeating: "a", count: 2048 - head.utf8.count)
        XCTAssertNotNil(SubscriptionURL.parse(exact))
        XCTAssertNil(SubscriptionURL.parse(exact + "a"))
        // 1000 two-byte letters (2020 bytes) pass, 1020 (2060 bytes) do not.
        XCTAssertNotNil(SubscriptionURL.parse(head + String(repeating: "\u{e9}", count: 1000)))
        XCTAssertNil(SubscriptionURL.parse(head + String(repeating: "\u{e9}", count: 1020)))
        XCTAssertThrowsError(try Subscription.request(exact + "a"))
    }
    func testRefusesWhitespaceControlAndFormCharacters() {
        for bad in [
            "https://example.org/a b", "https://example.org/a\tb", "https://example.org/a\nb",
            "https://example.org/a\u{a0}b", "https://example.org/a\u{200b}b",
            "https://example.org/a\u{202e}b", "https://example.org/a\u{85}b",
            "https://example.org/a\u{2028}b", "https://example.org/a\u{7f}b",
            "https://example.org/a\u{feff}b", "https://example.org/\u{ad}", " https://example.org/"
        ] {
            XCTAssertNil(SubscriptionURL.parse(bad), bad.debugDescription)
        }
        // A character outside the BMP is judged as one code point, not as two surrogates.
        XCTAssertNotNil(SubscriptionURL.parse("https://example.org/\u{1F600}"))
        XCTAssertNil(SubscriptionURL.parse("https://example.org/\u{E0001}"))
    }
    func testMalformedBytesAreNoAddress() {
        XCTAssertNotNil(SubscriptionURL.parse(utf8: Array("https://example.org/".utf8)))
        // A lone surrogate (ED A0 80), an overlong form and a truncated sequence.
        for tail: [UInt8] in [[0xED, 0xA0, 0x80], [0xC0, 0xAF], [0xE2, 0x82], [0xFF]] {
            XCTAssertNil(SubscriptionURL.parse(utf8: Array("https://example.org/a".utf8) + tail))
        }
    }
    func testSchemeHostAndPort() {
        XCTAssertNil(SubscriptionURL.parse("http://example.org/"))
        XCTAssertNil(SubscriptionURL.parse("example.org/sub"))
        XCTAssertNil(SubscriptionURL.parse("https://"))
        XCTAssertNil(SubscriptionURL.parse("https:///path"))
        XCTAssertNil(SubscriptionURL.parse("https://example.org:0/"))
        XCTAssertNil(SubscriptionURL.parse("https://example.org:65536/"))
        XCTAssertNil(SubscriptionURL.parse("https://example.org:44x/"))
        XCTAssertNil(SubscriptionURL.parse("https://exa mple.org/"))
        XCTAssertNil(SubscriptionURL.parse("https://exa\\mple.org/"))
        XCTAssertNil(SubscriptionURL.parse("https://[2001:db8::1/"))
        XCTAssertNil(SubscriptionURL.parse("https://[not-v6]/"))
        XCTAssertNil(SubscriptionURL.parse("https://example.org/%zz"))
        let long = String(repeating: "a", count: 64)
        XCTAssertNil(SubscriptionURL.parse("https://\(long).test/"))
        let url = SubscriptionURL.parse("HTTPS://Example.ORG:8443/a/b?x=1#frag")
        XCTAssertEqual(url?.host, "example.org")
        XCTAssertEqual(url?.port, 8443)
        XCTAssertEqual(url?.requestTarget, "/a/b?x=1")
        XCTAssertEqual(url?.urlString, "https://example.org:8443/a/b?x=1")
        let plain = SubscriptionURL.parse("https://example.org")
        XCTAssertEqual(plain?.effectivePort, 443)
        XCTAssertEqual(plain?.requestTarget, "/")
        XCTAssertEqual(SubscriptionURL.parse("https://example.org:/")?.port, nil)
        let ipv6 = SubscriptionURL.parse("https://[2001:DB8:0::1]:444/s")
        XCTAssertEqual(ipv6?.host, "2001:db8::1")
        XCTAssertEqual(ipv6?.authority, "[2001:db8::1]:444")
        XCTAssertEqual(ipv6?.isIPv6, true)
    }
    func testPathAndQueryAreSentEncodedAndKeptEscaped() {
        let url = SubscriptionURL.parse("https://example.org/a%2Fb/\u{e9}?key=a%2Bb&q=\"x\"")
        XCTAssertEqual(url?.encodedPath, "/a%2Fb/%C3%A9")
        XCTAssertEqual(url?.encodedQuery, "key=a%2Bb&q=%22x%22")
    }
    func testCredentials() throws {
        let url = SubscriptionURL.parse("https://al%20ice:p%2Bss%3Aw@example.org/sub")
        XCTAssertEqual(url?.user, "al ice")
        XCTAssertEqual(url?.password, "p+ss:w")
        XCTAssertEqual(url?.authorization, "Basic " + Data("al ice:p+ss:w".utf8).base64EncodedString())
        XCTAssertEqual(url?.urlString, "https://example.org/sub")
        XCTAssertNil(SubscriptionURL.parse("https://example.org/")?.authorization)
        XCTAssertEqual(
            SubscriptionURL.parse("https://user@example.org/")?.authorization,
            "Basic " + Data("user:".utf8).base64EncodedString())
        // A user with a colon cannot be sent; bytes that are not UTF-8 would be replaced;
        // C0 and DEL would be dropped by the config's writer.
        XCTAssertNil(SubscriptionURL.parse("https://us%3Aer:pw@example.org/"))
        XCTAssertNil(SubscriptionURL.parse("https://%FF:pw@example.org/"))
        XCTAssertNil(SubscriptionURL.parse("https://user:p%00w@example.org/"))
        XCTAssertNil(SubscriptionURL.parse("https://user:p%7Fw@example.org/"))
        XCTAssertNil(SubscriptionURL.parse("https://a@b@example.org/"))
        let request = try Subscription.request("https://user:pass@example.com/sub#label")
        XCTAssertEqual(request.url?.absoluteString, "https://example.com/sub")
    }
    func testHostKeyIsAceInLowerCase() {
        XCTAssertEqual(SubscriptionURL.hostKey("VPN.Test"), "vpn.test")
        XCTAssertEqual(SubscriptionURL.hostKey("\u{43f}\u{440}\u{438}\u{43c}\u{435}\u{440}.TEST"), "xn--e1afmkfd.test")
        XCTAssertEqual(SubscriptionURL.hostKey("b\u{fc}cher.example"), "xn--bcher-kva.example")
        XCTAssertEqual(SubscriptionURL.hostKey("2001:DB8::1"), "2001:db8::1")
        XCTAssertEqual(SubscriptionURL.hostKey("[2001:db8:0:0::1]"), "2001:db8::1")
        XCTAssertEqual(SubscriptionURL.hostKey(""), "")
        let idn = SubscriptionURL.parse("https://\u{43f}\u{440}\u{438}\u{43c}\u{435}\u{440}.test/s")
        XCTAssertEqual(idn?.host, "xn--e1afmkfd.test")
    }
}
