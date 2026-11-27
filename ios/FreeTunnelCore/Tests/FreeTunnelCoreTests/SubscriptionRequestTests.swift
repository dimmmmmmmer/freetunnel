import XCTest

@testable import FreeTunnelCore

final class SubscriptionRequestTests: XCTestCase {
    func trust(_ host: String, _ addresses: String, _ certificate: String = TestPKI.leafPEM)
        -> SubscriptionTrust {
        SubscriptionTrust(hostname: host, addresses: addresses, certificate: certificate)
    }
    func request(addresses: String, certificate: String) -> SubscriptionRequest {
        SubscriptionRequest.make("https://vpn.test/sub", trust: trust("vpn.test", addresses, certificate))
    }
    func testAPinnedRequestNeedsTheConfigsHostnameAndACertificate() {
        let url = "https://alice:s3cret@vpn.test/sub"
        let request = SubscriptionRequest.make(url, trust: trust("VPN.Test", "127.0.0.1:8443, 10.0.0.1:443"))
        XCTAssertEqual(request.source, url)
        XCTAssertTrue(request.pinned)
        XCTAssertEqual(request.pins, [TestPKI.leafDER])
        XCTAssertEqual(request.verifyName, "VPN.Test")
        XCTAssertEqual(request.connectHost, "127.0.0.1")
        XCTAssertEqual(request.connectPort, 8443)
        // A subdomain is another host: the system's CAs and DNS.
        let other = SubscriptionRequest.make(
            "https://alice:s3cret@sub.vpn.test/sub", trust: trust("VPN.Test", "127.0.0.1:8443"))
        XCTAssertFalse(other.pinned)
        XCTAssertTrue(other.pins.isEmpty)
        XCTAssertEqual(other.verifyName, "")
        XCTAssertNil(other.connectHost)
        XCTAssertNil(other.connectPort)
        for blank in ["\n  \n", ""] {
            XCTAssertFalse(SubscriptionRequest.make(url, trust: trust("vpn.test", "127.0.0.1:8443", blank)).pinned)
        }
        XCTAssertFalse(SubscriptionRequest.make(url, trust: .none).pinned)
        XCTAssertFalse(SubscriptionRequest.make(url, trust: nil).pinned)
        // An IDN and an IPv6 literal are the same host whichever form either side writes.
        let idn = "\u{43f}\u{440}\u{438}\u{43c}\u{435}\u{440}.test"
        XCTAssertTrue(SubscriptionRequest.make("https://xn--e1afmkfd.test/s", trust: trust(idn, "")).pinned)
        let upperIDN = "https://\(idn.uppercased())/s"
        XCTAssertTrue(SubscriptionRequest.make(upperIDN, trust: trust("xn--e1afmkfd.test", "")).pinned)
        XCTAssertTrue(SubscriptionRequest.make("https://[2001:db8::1]/s", trust: trust("2001:db8::1", "")).pinned)
        XCTAssertTrue(SubscriptionRequest.make("https://[2001:DB8:0::1]/s", trust: trust("2001:db8::1", "")).pinned)
        XCTAssertFalse(SubscriptionRequest.make("https://[2001:db8::2]/s", trust: trust("2001:db8::1", "")).pinned)
        // An address that is none is not pinned and not an error here: the fetch refuses it.
        XCTAssertFalse(SubscriptionRequest.make("not a url", trust: trust("vpn.test", "")).pinned)
        XCTAssertFalse(SubscriptionRequest.make("", trust: trust("vpn.test", "")).pinned)
    }
    func testAPinnedRequestConnectsToTheFirstAddress() {
        struct Row {
            let text: String
            let host: String?
            let port: Int?
            init(_ text: String, _ host: String?, _ port: Int?) {
                self.text = text
                self.host = host
                self.port = port
            }
        }
        let rows = [
            Row("1.2.3.4:443, 5.6.7.8:443", "1.2.3.4", 443),
            Row("[2001:db8::1]:8443", "2001:db8::1", 8443),
            Row(" vpn.example.com:9443 ", "vpn.example.com", 9443),
            Row("1.2.3.4", nil, nil), Row("", nil, nil), Row("1.2.3.4:0", nil, nil),
            Row("1.2.3.4:65536", nil, nil), Row("[]:443", nil, nil), Row("2001:db8::1:443", nil, nil),
            Row("a/b:443", nil, nil), Row("1.2.3.4:+443", nil, nil),
            Row("nonsense, 5.6.7.8:443", nil, nil)
        ]
        for row in rows {
            let request = SubscriptionRequest.make(
                "https://alice:s3cret@vpn.test:1111/sub", trust: trust("vpn.test", row.text))
            XCTAssertTrue(request.pinned, row.text)
            XCTAssertEqual(request.connectHost, row.host, row.text)
            XCTAssertEqual(request.connectPort, row.port, row.text)
        }
    }
    func testCertificatesThatCannotBeReadArePinnedWithNoPins() {
        func pem(_ bytes: [UInt8]) -> String {
            "-----BEGIN CERTIFICATE-----\n" + Data(bytes).base64EncodedString()
                + "\n-----END CERTIFICATE-----\n"
        }
        let garbage = pem([0, 0, 0])
        let shaped = pem([0x30, 0x0b, 0x30, 0x02, 0x05, 0x00, 0x30, 0x02, 0x05, 0x00, 0x03, 0x01, 0x00])
        for text in [garbage, shaped, garbage + shaped, "-----BEGIN CERTIFICATE-----\n!!!\n-----END CERTIFICATE-----"] {
            let request = self.request(addresses: "127.0.0.1:8443", certificate: text)
            XCTAssertTrue(request.pinned)
            XCTAssertTrue(request.pins.isEmpty)
        }
        XCTAssertEqual(request(addresses: "", certificate: shaped + TestPKI.leafPEM).pins.count, 1)
        XCTAssertEqual(request(addresses: "", certificate: TestPKI.leafPEM + TestPKI.caPEM).pins.count, 2)
    }
    func testTrustComesFromAConfiguration() throws {
        let toml = """
            [endpoint]
            hostname = "vpn.test"
            addresses = ["127.0.0.1:8443", "10.0.0.1:443"]
            username = "u"
            password = "p"
            certificate = '''\n\(TestPKI.leafPEM)'''
            """
        let trust = SubscriptionTrust(toml: toml)
        XCTAssertEqual(trust.hostname, "vpn.test")
        XCTAssertEqual(trust.addresses, "127.0.0.1:8443, 10.0.0.1:443")
        XCTAssertTrue(SubscriptionRequest.make("https://vpn.test/s", trust: trust).pinned)
        XCTAssertEqual(SubscriptionTrust(toml: "garbage"), .none)
    }
}

final class ImportTrustTests: XCTestCase {
    func link(_ fields: [(Int, [UInt8])]) -> String {
        var out = [UInt8]()
        for (tag, value) in fields { out += [UInt8(tag), UInt8(value.count)] + value }
        return "tt://?" + Data(out).base64EncodedString().replacingOccurrences(of: "+", with: "-")
            .replacingOccurrences(of: "/", with: "_").replacingOccurrences(of: "=", with: "")
    }
    func testASubscriptionOnlyLinkCarriesTheServerItNames() throws {
        let text = link([
            (0, [2]), (1, Array("vpn.test".utf8)),
            (14, Array("https://vpn.test/sub".utf8))
        ])
        let document = try ImportDocument.parse(text)
        XCTAssertNil(document.toml)
        XCTAssertEqual(document.subscription, "https://vpn.test/sub")
        XCTAssertEqual(document.trust?.hostname, "vpn.test")
        XCTAssertEqual(document.trust?.addresses, "")
        XCTAssertEqual(document.trust?.certificate, "")
    }
    func testALinkWithAServerAndASubscriptionCarriesItsAddresses() throws {
        let text = link([
            (0, [2]), (1, Array("vpn.test".utf8)), (5, Array("u".utf8)), (6, Array("p".utf8)),
            (2, Array("1.2.3.4:443".utf8)), (2, Array("5.6.7.8:443".utf8)),
            (14, Array("https://vpn.test/sub".utf8))
        ])
        let document = try ImportDocument.parse(text)
        XCTAssertNotNil(document.toml)
        XCTAssertEqual(document.trust?.addresses, "1.2.3.4:443, 5.6.7.8:443")
        XCTAssertEqual(document.trust?.hostname, "vpn.test")
    }
    func testATomlSubscriptionComesWithTheServerItNames() throws {
        let toml = """
            [endpoint]
            hostname = "vpn.test"
            addresses = ["127.0.0.1:8443"]
            username = "u"
            password = "p"
            subscription_url = "https://vpn.test/sub"
            certificate = '''\n\(TestPKI.leafPEM)'''
            """
        let document = try ImportDocument.parse(toml)
        XCTAssertEqual(document.subscription, "https://vpn.test/sub")
        XCTAssertEqual(document.trust?.hostname, "vpn.test")
        XCTAssertEqual(document.trust?.addresses, "127.0.0.1:8443")
        XCTAssertTrue(SubscriptionRequest.make("https://vpn.test/sub", trust: document.trust).pinned)
        // Without a subscription there is nothing to trust it with.
        let line = "subscription_url = \"https://vpn.test/sub\"\n"
        let without = try ImportDocument.parse(toml.replacingOccurrences(of: line, with: ""))
        XCTAssertNil(without.trust)
    }
}
