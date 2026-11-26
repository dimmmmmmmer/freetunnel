import XCTest

@testable import FreeTunnelCore

final class CertificateAndReaderTests: XCTestCase {
    func accepts(_ pem: String, pins: [String]? = nil, name: String = "vpn.test", now: Date = Date()) -> Bool {
        X509Leaf.accepts(
            leaf: TestPKI.der(pem), pins: (pins ?? [pem]).map(TestPKI.der), verifyName: name, now: now)
    }
    func testOnlyAPinnedLeafInItsDatesForTheNameIsAccepted() {
        XCTAssertTrue(accepts(TestPKI.leafPEM))
        XCTAssertTrue(accepts(TestPKI.leafPEM, name: "VPN.TEST"))
        XCTAssertTrue(accepts(TestPKI.leafPEM, name: "127.0.0.1"))
        XCTAssertFalse(accepts(TestPKI.leafPEM, name: "127.0.0.2"))
        XCTAssertFalse(accepts(TestPKI.leafPEM, name: "sub.vpn.test"))
        // A CA that signed it is never enough; neither is another pin.
        XCTAssertFalse(accepts(TestPKI.bycAPEM, pins: [TestPKI.caPEM]))
        XCTAssertFalse(accepts(TestPKI.leafPEM, pins: [TestPKI.wrongPEM, TestPKI.caPEM]))
        XCTAssertFalse(accepts(TestPKI.leafPEM, pins: []))
        XCTAssertTrue(accepts(TestPKI.leafPEM, pins: [TestPKI.wrongPEM, TestPKI.leafPEM]))
        // Dates.
        XCTAssertFalse(accepts(TestPKI.expiredPEM))
        XCTAssertFalse(accepts(TestPKI.futurePEM))
        XCTAssertTrue(accepts(TestPKI.expiredPEM, now: Date(timeIntervalSince1970: 1_560_000_000)))
        XCTAssertFalse(accepts(TestPKI.leafPEM, now: Date(timeIntervalSince1970: 1_500_000_000)))
        // Purpose.
        XCTAssertFalse(accepts(TestPKI.clientPEM))
        XCTAssertTrue(accepts(TestPKI.anyekuPEM))
        XCTAssertTrue(accepts(TestPKI.cnonlyPEM, name: "cn.test"))  // no EKU at all
        XCTAssertFalse(X509Leaf.accepts(leaf: Data([1, 2, 3]), pins: [Data([1, 2, 3])], verifyName: "vpn.test"))
    }
    func testNameRules() throws {
        let wild = try XCTUnwrap(X509Leaf(der: TestPKI.der(TestPKI.wildPEM)))
        XCTAssertTrue(wild.matches(name: "a.wild.test"))
        XCTAssertTrue(wild.matches(name: "A.WILD.test"))
        XCTAssertFalse(wild.matches(name: "wild.test"))
        XCTAssertFalse(wild.matches(name: "a.b.wild.test"))
        XCTAssertTrue(wild.matches(name: "mixed.test"))
        XCTAssertFalse(wild.matches(name: "10.0.0.1"))
        let cn = try XCTUnwrap(X509Leaf(der: TestPKI.der(TestPKI.cnonlyPEM)))
        XCTAssertEqual(cn.commonNames, ["cn.test"])
        XCTAssertTrue(cn.matches(name: "cn.test"))
        let ip = try XCTUnwrap(X509Leaf(der: TestPKI.der(TestPKI.ipv6PEM)))
        XCTAssertTrue(ip.matches(name: "2001:db8::1"))
        XCTAssertTrue(ip.matches(name: "[2001:DB8:0::1]"))
        XCTAssertTrue(ip.matches(name: "10.1.2.3"))
        XCTAssertFalse(ip.matches(name: "2001:db8::2"))
        XCTAssertFalse(ip.matches(name: "vpn.test"))
        // The SAN is read in full, and the dates.
        let leaf = try XCTUnwrap(X509Leaf(der: TestPKI.leafDER))
        XCTAssertEqual(leaf.dnsNames, ["vpn.test"])
        XCTAssertEqual(leaf.ipAddresses, [[127, 0, 0, 1]])
        XCTAssertEqual(leaf.notBefore, Date(timeIntervalSince1970: 1_577_836_800))
        for (candidate, peer, expected) in [
            ("*.com", "a.com", false), ("*.co.uk", "a.co.uk", true), ("*.x.y", "a.x.y", true),
            ("*.y", "a.y", false), ("a*.x.y", "ab.x.y", true), ("a*.x.y", "a.x.y", false),
            ("*b.x.y", "ab.x.y", false), ("*.*.y", "a.b.y", false), ("*.x.y", "xn--a.x.y", true),
            ("xn--*.x.y", "xn--a.x.y", false), ("", "a", false)
        ] {
            XCTAssertEqual(X509Leaf.matches(candidate: candidate, peer: peer), expected, "\(candidate) \(peer)")
        }
    }

    final class Feed: ByteSource {
        var parts: [Data]
        init(_ text: String..., split: Bool = false) {
            parts = split ? text.joined().utf8.map { Data([$0]) } : text.map { Data($0.utf8) }
        }
        func read() async throws -> Data? { parts.isEmpty ? nil : parts.removeFirst() }
    }
    func read(_ text: String, split: Bool = false) async throws -> HTTPReply {
        try await HTTPResponseReader(Feed(text, split: split)).read()
    }
    func testReaderHandlesByteAtATimeInput() async throws {
        let text = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n2;x=y\r\nde\r\n0\r\n\r\n"
        let reply = try await read(text, split: true)
        XCTAssertEqual(reply.body, Data("abcde".utf8))
        let length = try await read("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabcEXTRA", split: true)
        XCTAssertEqual(length.body, Data("abc".utf8))
    }
    func testReaderStatusLine() {
        for (line, status) in [
            ("HTTP/1.1 200 OK", 200), ("HTTP/1.0 404", 404), ("HTTP/1.1 301 ", 301)
        ] { XCTAssertEqual(HTTPResponseReader.status(of: line), status, line) }
        for line in ["HTTP/2 200", "HTTP/1.2 200 OK", "HTTP/1.1 20 OK", "HTTP/1.1 2000", "HTTP/1.1  200", "", "http/1.1 200 OK"] {
            XCTAssertNil(HTTPResponseReader.status(of: line), line)
        }
    }
    func testReaderRefusesAnOverlongHeadOrLine() async throws {
        do {
            _ = try await read("HTTP/1.1 200 OK\r\n" + String(repeating: "a", count: 9000) + ": x\r\n\r\n")
            XCTFail("read")
        } catch { XCTAssertEqual(error as? SubscriptionFetchError, .invalid) }
        do {
            let many = (0..<2000).map { "h\($0): value\r\n" }.joined()
            _ = try await read("HTTP/1.1 200 OK\r\n" + many + "\r\n")
            XCTFail("read")
        } catch { XCTAssertEqual(error as? SubscriptionFetchError, .invalid) }
    }
    func testReaderReadsNoBodyOfAnErrorStatus() async throws {
        let reply = try await read("HTTP/1.1 302 Found\r\nLocation: /next\r\nContent-Length: 5\r\n\r\n")
        XCTAssertEqual(reply.status, 302)
        XCTAssertEqual(reply.location, "/next")
        XCTAssertNil(reply.body)
    }
}
