import XCTest

@testable import FreeTunnelCore

final class SubscriptionRequestTests: XCTestCase {
    func trust(_ host: String, _ addresses: String, _ certificate: String = TestPKI.leafPEM)
        -> SubscriptionTrust {
        SubscriptionTrust(hostname: host, addresses: addresses, certificate: certificate)
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
        XCTAssertTrue(SubscriptionRequest.make("https://\(idn.uppercased())/s", trust: trust("xn--e1afmkfd.test", "")).pinned)
        XCTAssertTrue(SubscriptionRequest.make("https://[2001:db8::1]/s", trust: trust("2001:db8::1", "")).pinned)
        XCTAssertTrue(SubscriptionRequest.make("https://[2001:DB8:0::1]/s", trust: trust("2001:db8::1", "")).pinned)
        XCTAssertFalse(SubscriptionRequest.make("https://[2001:db8::2]/s", trust: trust("2001:db8::1", "")).pinned)
        // An address that is none is not pinned and not an error here: the fetch refuses it.
        XCTAssertFalse(SubscriptionRequest.make("not a url", trust: trust("vpn.test", "")).pinned)
        XCTAssertFalse(SubscriptionRequest.make("", trust: trust("vpn.test", "")).pinned)
    }
    func testAPinnedRequestConnectsToTheFirstAddress() {
        let rows: [(String, String?, Int?)] = [
            ("1.2.3.4:443, 5.6.7.8:443", "1.2.3.4", 443),
            ("[2001:db8::1]:8443", "2001:db8::1", 8443),
            (" vpn.example.com:9443 ", "vpn.example.com", 9443),
            ("1.2.3.4", nil, nil), ("", nil, nil), ("1.2.3.4:0", nil, nil),
            ("1.2.3.4:65536", nil, nil), ("[]:443", nil, nil), ("2001:db8::1:443", nil, nil),
            ("a/b:443", nil, nil), ("1.2.3.4:+443", nil, nil),
            ("nonsense, 5.6.7.8:443", nil, nil)
        ]
        for row in rows {
            let request = SubscriptionRequest.make(
                "https://alice:s3cret@vpn.test:1111/sub", trust: trust("vpn.test", row.0))
            XCTAssertTrue(request.pinned, row.0)
            XCTAssertEqual(request.connectHost, row.1, row.0)
            XCTAssertEqual(request.connectPort, row.2, row.0)
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
            let request = SubscriptionRequest.make("https://vpn.test/sub", trust: trust("vpn.test", "127.0.0.1:8443", text))
            XCTAssertTrue(request.pinned)
            XCTAssertTrue(request.pins.isEmpty)
        }
        XCTAssertEqual(
            SubscriptionRequest.make("https://vpn.test/sub", trust: trust("vpn.test", "", shaped + TestPKI.leafPEM)).pins.count, 1)
        XCTAssertEqual(
            SubscriptionRequest.make("https://vpn.test/sub", trust: trust("vpn.test", "", TestPKI.leafPEM + TestPKI.caPEM)).pins.count, 2)
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
