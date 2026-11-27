import Network
import XCTest

@testable import FreeTunnelCore

final class PinnedFetchTests: XCTestCase {
    let json = "{\"version\":1}"
    let never: SubscriptionFetcher.Unpinned = { _ in
        XCTFail("the system's HTTP was used")
        return Data()
    }

    func trust(_ port: Int, pin: String = TestPKI.leafPEM, host: String = "vpn.test") -> SubscriptionTrust {
        SubscriptionTrust(hostname: host, addresses: "127.0.0.1:\(port)", certificate: pin)
    }
    func fetch(
        _ source: String, _ trust: SubscriptionTrust, timeout: TimeInterval = 10
    ) async throws -> Data {
        try await SubscriptionFetcher.fetch(
            SubscriptionRequest.make(source, trust: trust), timeout: timeout, unpinned: never)
    }
    func failure(_ work: () async throws -> Data) async -> SubscriptionFetchError? {
        do {
            _ = try await work()
            return nil
        } catch { return error as? SubscriptionFetchError }
    }

    // MARK: trust

    func testAPinnedLeafIsAcceptedAndTheRequestNamesTheServer() async throws {
        let server = try TestTLSServer(identity: TestPKI.leafP12) { _, c in TestTLSServer.ok(c, self.json) }
        defer { server.stop() }
        let body = try await fetch("https://alice:s3cret@vpn.test/sub?x=1", trust(server.port))
        XCTAssertEqual(String(decoding: body, as: UTF8.self), json)
        XCTAssertEqual(server.requests.count, 1)
        let head = server.requests[0]
        XCTAssertTrue(head.hasPrefix("GET /sub?x=1 HTTP/1.1\r\nHost: vpn.test\r\n"), head)
        XCTAssertTrue(head.contains("Authorization: Basic " + Data("alice:s3cret".utf8).base64EncodedString()))
        XCTAssertTrue(head.contains("Connection: close"))
        XCTAssertFalse(head.contains("alice:s3cret@"))
    }
    func testALeafSignedByAPinnedCaIsRefusedAndNoRequestIsSent() async throws {
        let server = try TestTLSServer(identity: TestPKI.bycAP12) { _, c in TestTLSServer.ok(c, self.json) }
        defer { server.stop() }
        let error = await failure { try await fetch("https://vpn.test/sub", trust(server.port, pin: TestPKI.caPEM)) }
        XCTAssertEqual(error, .untrusted)
        XCTAssertEqual(server.requests.count, 0)
        // Pinned itself, the same certificate is fine, though the system trusts neither.
        let body = try await fetch("https://vpn.test/sub", trust(server.port, pin: TestPKI.bycAPEM))
        XCTAssertEqual(body.count, json.utf8.count)
    }
    func testAWrongNameAnExpiredAndAClientOnlyCertificateAreRefused() async throws {
        let rows: [(String, String, String)] = [
            (TestPKI.wrongP12, TestPKI.wrongPEM, "wrong name"),
            (TestPKI.expiredP12, TestPKI.expiredPEM, "expired"),
            (TestPKI.futureP12, TestPKI.futurePEM, "not yet valid"),
            (TestPKI.clientP12, TestPKI.clientPEM, "client only")
        ]
        for row in rows {
            let server = try TestTLSServer(identity: row.0) { _, c in TestTLSServer.ok(c, self.json) }
            defer { server.stop() }
            let error = await failure { try await fetch("https://vpn.test/sub", trust(server.port, pin: row.1)) }
            XCTAssertEqual(error, .untrusted, row.2)
            XCTAssertEqual(server.requests.count, 0, row.2)
        }
    }
    func testAnotherCertificateThanThePinIsRefused() async throws {
        let server = try TestTLSServer(identity: TestPKI.leafP12) { _, c in TestTLSServer.ok(c, self.json) }
        defer { server.stop() }
        let error = await failure { try await fetch("https://vpn.test/sub", trust(server.port, pin: TestPKI.wrongPEM)) }
        XCTAssertEqual(error, .untrusted)
        XCTAssertEqual(server.requests.count, 0)
    }
    func testUnreadablePinsFailAsBadPinBeforeDialling() async throws {
        let server = try TestTLSServer(identity: TestPKI.leafP12) { _, c in TestTLSServer.ok(c, self.json) }
        defer { server.stop() }
        let garbage = "-----BEGIN CERTIFICATE-----\nAAAA\n-----END CERTIFICATE-----"
        let error = await failure { try await fetch("https://vpn.test/sub", trust(server.port, pin: garbage)) }
        XCTAssertEqual(error, .badPin)
        XCTAssertEqual(server.requests.count, 0)
        XCTAssertEqual(SubscriptionFetchError.badPin.clientError, .subscriptionBadPin)
        XCTAssertEqual(SubscriptionFetchError.untrusted.clientError, .subscriptionUntrusted)
    }
    func testAnAddressThatIsNoneIsRefused() async throws {
        let error = await failure { try await fetch("http://vpn.test/sub", trust(1)) }
        XCTAssertEqual(error, .badURL)
    }
    func testAnUnpinnedRequestGoesThroughTheSystemAndNeverDials() async throws {
        let server = try TestTLSServer(identity: TestPKI.leafP12) { _, c in TestTLSServer.ok(c, self.json) }
        defer { server.stop() }
        let seen = LockedBox<[URLRequest]>([])
        let body = try await SubscriptionFetcher.fetch(
            SubscriptionRequest.make("https://sub.vpn.test/x", trust: trust(server.port)), unpinned: { request in
                seen.mutate { $0.append(request) }
                return Data("ok".utf8)
            })
        XCTAssertEqual(body, Data("ok".utf8))
        XCTAssertEqual(seen.value.count, 1)
        XCTAssertEqual(server.requests.count, 0)
    }

    // MARK: redirects

    func testASameHostRedirectCarriesThePinAndTheCredentials() async throws {
        let server = try TestTLSServer(identity: TestPKI.leafP12) { head, c in
            if head.hasPrefix("GET /sub ") {
                TestTLSServer.reply(c, "HTTP/1.1 302 Found\r\nLocation: next?a=1\r\nContent-Length: 0\r\n\r\n")
            } else {
                TestTLSServer.ok(c, self.json)
            }
        }
        defer { server.stop() }
        let body = try await fetch("https://alice:s3cret@VPN.test/sub", trust(server.port))
        XCTAssertEqual(body.count, json.utf8.count)
        XCTAssertEqual(server.requests.count, 2)
        XCTAssertTrue(server.requests[1].hasPrefix("GET /next?a=1 HTTP/1.1\r\nHost: vpn.test\r\n"), server.requests[1])
        XCTAssertTrue(server.requests[1].contains("Authorization: Basic"))
    }
    func testRedirectsToAnotherHostPortSchemeOrUserinfoAreRefused() async throws {
        for location in [
            "https://other.test/sub", "https://vpn.test:8443/sub", "http://vpn.test/sub",
            "https://u:p@vpn.test/sub", "https://sub.vpn.test/sub"
        ] {
            let server = try TestTLSServer(identity: TestPKI.leafP12) { _, c in
                TestTLSServer.reply(c, "HTTP/1.1 302 Found\r\nLocation: \(location)\r\nContent-Length: 0\r\n\r\n")
            }
            defer { server.stop() }
            let error = await failure { try await fetch("https://vpn.test/sub", trust(server.port)) }
            XCTAssertEqual(error, .invalid, location)
            XCTAssertEqual(server.requests.count, 1, location)
        }
    }
    func testTooManyRedirectsAndLoopsAreRefused() async throws {
        let counter = LockedBox(0)
        let server = try TestTLSServer(identity: TestPKI.leafP12) { _, c in
            let n = counter.mutate { $0 += 1; return $0 }
            TestTLSServer.reply(c, "HTTP/1.1 301 Moved\r\nLocation: /r\(n)\r\nContent-Length: 0\r\n\r\n")
        }
        defer { server.stop() }
        let error = await failure { try await fetch("https://vpn.test/sub", trust(server.port)) }
        XCTAssertEqual(error, .invalid)
        XCTAssertEqual(server.requests.count, 4)
        let loop = try TestTLSServer(identity: TestPKI.leafP12) { _, c in
            TestTLSServer.reply(c, "HTTP/1.1 302 Found\r\nLocation: /sub\r\nContent-Length: 0\r\n\r\n")
        }
        defer { loop.stop() }
        let again = await failure { try await fetch("https://vpn.test/sub", trust(loop.port)) }
        XCTAssertEqual(again, .invalid)
        XCTAssertEqual(loop.requests.count, 1)
    }
    func testRedirectResolutionKeepsTheSlashOfAnEmptyPath() {
        func resolve(_ base: String, _ location: String) -> String? {
            SubscriptionFetcher.redirect(from: SubscriptionURL.parse(base)!, location: location)?.urlString
        }
        XCTAssertEqual(resolve("https://sub.example.com", "v1/sub"), "https://sub.example.com/v1/sub")
        XCTAssertEqual(resolve("https://sub.example.com?x=1", "v1/sub"), "https://sub.example.com/v1/sub")
        XCTAssertEqual(resolve("https://sub.example.com:8443?x=1", "/a?b=2"), "https://sub.example.com:8443/a?b=2")
        XCTAssertEqual(resolve("https://sub.example.com?x=1", "?b=2"), "https://sub.example.com/?b=2")
        XCTAssertEqual(resolve("https://sub.example.com/a/b?x=1", "c"), "https://sub.example.com/a/c")
    }

    // MARK: framing and limits

    func testReplyFraming() async throws {
        let body = json
        let chunked = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n5;ext=1\r\n{\"ver\r\n\(body.count - 5)\r\nsion\":1}\r\n0\r\nTrailer: x\r\n\r\n"
        let rows: [(String, String)] = [
            ("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: \(body.count)\r\n\r\n\(body)", "length"),
            ("HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n\r\n\(body)", "until closed"),
            (chunked, "chunked"),
            ("HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nContent-Type: application/json\r\ncontent-length: \(body.count)\r\n\r\n\(body)", "interim"),
            ("HTTP/1.1 200\nContent-Type: application/json\nContent-Length: \(body.count)\n\n\(body)", "bare LF")
        ]
        for row in rows {
            let server = try TestTLSServer(identity: TestPKI.leafP12) { _, c in TestTLSServer.reply(c, row.0) }
            defer { server.stop() }
            let got = try await fetch("https://vpn.test/sub", trust(server.port))
            XCTAssertEqual(String(decoding: got, as: UTF8.self), body, row.1)
        }
    }
    func testStatusesAndMalformedReplies() async throws {
        let rows: [(String, SubscriptionFetchError)] = [
            ("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n", .http(404)),
            ("HTTP/1.1 401 Unauthorized\r\n\r\nbody", .http(401)),
            ("HTTP/1.1 503 Busy\r\n\r\n", .http(503)),
            ("HTTP/1.1 302 Found\r\n\r\n", .http(302)),
            ("garbage\r\n\r\n", .invalid),
            ("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n Folded: x\r\n\r\n", .invalid),
            ("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\nContent-Length: 3\r\n\r\nab", .invalid),
            ("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: +2\r\n\r\nab", .invalid),
            ("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nTransfer-Encoding: gzip, chunked\r\n\r\n0\r\n\r\n", .invalid),
            ("HTTP/1.1 101 Switching\r\n\r\n", .invalid),
            ("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 10\r\n\r\nab", .network)
        ]
        for row in rows {
            let server = try TestTLSServer(identity: TestPKI.leafP12) { _, c in TestTLSServer.reply(c, row.0) }
            defer { server.stop() }
            let error = await failure { try await fetch("https://vpn.test/sub", trust(server.port)) }
            XCTAssertEqual(error, row.1, row.0.debugDescription)
        }
    }
    func testTheBodyCap() async throws {
        let cap = Subscription.maximumBytes
        let exact = String(repeating: "a", count: cap)
        let over = String(repeating: "a", count: cap + 1)
        let chunk = { (s: String) in "\(String(s.count, radix: 16))\r\n\(s)\r\n0\r\n\r\n" }
        let rows: [(String, SubscriptionFetchError?)] = [
            ("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: \(cap)\r\n\r\n\(exact)", nil),
            ("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: \(cap + 1)\r\n\r\n\(over)", .tooLarge),
            ("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 99999999999999999999\r\n\r\n", .tooLarge),
            ("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n\(exact)", nil),
            ("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n\(over)", .tooLarge),
            ("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n\(chunk(exact))", nil),
            ("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n\(chunk(over))", .tooLarge),
            ("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\nFFFFFFFFFF\r\n", .tooLarge)
        ]
        for row in rows {
            let server = try TestTLSServer(identity: TestPKI.leafP12) { _, c in TestTLSServer.reply(c, row.0) }
            defer { server.stop() }
            let error = await failure { try await fetch("https://vpn.test/sub", trust(server.port)) }
            XCTAssertEqual(error, row.1, String(row.0.prefix(70)).debugDescription)
        }
    }

    // MARK: the deadline

    func testASlowDripEndsAtTheDeadline() async throws {
        let server = try TestTLSServer(identity: TestPKI.leafP12) { _, c in
            TestTLSServer.send(c, Data("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 1000\r\n\r\n".utf8))
            DispatchQueue.global().async {
                for _ in 0..<100 {
                    Thread.sleep(forTimeInterval: 0.1)
                    TestTLSServer.send(c, Data("x".utf8))
                }
            }
        }
        defer { server.stop() }
        let start = Date()
        let error = await failure { try await fetch("https://vpn.test/sub", trust(server.port), timeout: 1.5) }
        XCTAssertEqual(error, .timeout)
        XCTAssertLessThan(Date().timeIntervalSince(start), 4)
    }
    func testAServerThatNeverAnswersEndsAtTheDeadline() async throws {
        let server = try TestTLSServer(identity: TestPKI.leafP12) { _, _ in }
        defer { server.stop() }
        let start = Date()
        let error = await failure { try await fetch("https://vpn.test/sub", trust(server.port), timeout: 1) }
        XCTAssertEqual(error, .timeout)
        XCTAssertLessThan(Date().timeIntervalSince(start), 4)
    }
    func testTheDeadlineCoversRedirects() async throws {
        let server = try TestTLSServer(identity: TestPKI.leafP12) { head, c in
            Thread.sleep(forTimeInterval: 0.6)
            TestTLSServer.reply(c, "HTTP/1.1 302 Found\r\nLocation: /n\(head.count)\r\nContent-Length: 0\r\n\r\n")
        }
        defer { server.stop() }
        let error = await failure { try await fetch("https://vpn.test/sub", trust(server.port), timeout: 1.5) }
        XCTAssertEqual(error, .timeout)
    }
    func testAnUnpinnedFetchHasTheDeadlineToo() async throws {
        let error = await failure {
            try await SubscriptionFetcher.fetch(
                SubscriptionRequest.make("https://example.org/s", trust: nil), timeout: 0.5,
                unpinned: { _ in
                    try await Task.sleep(nanoseconds: 30_000_000_000)
                    return Data()
                })
        }
        XCTAssertEqual(error, .timeout)
    }
}

final class LockedBox<T>: @unchecked Sendable {
    private let lock = NSLock()
    private var stored: T
    init(_ value: T) { stored = value }
    var value: T { lock.lock(); defer { lock.unlock() }; return stored }
    @discardableResult func mutate<R>(_ change: (inout T) -> R) -> R {
        lock.lock()
        defer { lock.unlock() }
        return change(&stored)
    }
}
