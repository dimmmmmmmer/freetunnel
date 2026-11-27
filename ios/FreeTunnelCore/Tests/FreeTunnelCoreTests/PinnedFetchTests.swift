import Network
import XCTest

@testable import FreeTunnelCore

final class PinnedFetchTests: XCTestCase {
    let json = "{\"version\":1}"
    let okHead = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
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
    func okServer(_ identity: String) throws -> TestTLSServer {
        try TestTLSServer(identity: identity) { _, connection in TestTLSServer.ok(connection, self.json) }
    }
    func replyServer(_ text: String) throws -> TestTLSServer {
        try TestTLSServer(identity: TestPKI.leafP12) { _, connection in TestTLSServer.reply(connection, text) }
    }
    func failure(_ work: () async throws -> Data) async -> SubscriptionFetchError? {
        do {
            _ = try await work()
            return nil
        } catch { return error as? SubscriptionFetchError }
    }

    // MARK: trust

    func testAPinnedLeafIsAcceptedAndTheRequestNamesTheServer() async throws {
        let server = try okServer(TestPKI.leafP12)
        defer { server.stop() }
        let body = try await fetch("https://alice:s3cret@vpn.test/sub?x=1", trust(server.port))
        XCTAssertEqual(String(bytes: body, encoding: .utf8), json)
        XCTAssertEqual(server.requests.count, 1)
        let head = server.requests[0]
        XCTAssertTrue(head.hasPrefix("GET /sub?x=1 HTTP/1.1\r\nHost: vpn.test\r\n"), head)
        XCTAssertTrue(head.contains("Authorization: Basic " + Data("alice:s3cret".utf8).base64EncodedString()))
        XCTAssertTrue(head.contains("Connection: close"))
        XCTAssertFalse(head.contains("alice:s3cret@"))
    }
    func testALeafSignedByAPinnedCaIsRefusedAndNoRequestIsSent() async throws {
        let server = try okServer(TestPKI.bycAP12)
        defer { server.stop() }
        let error = await failure { try await fetch("https://vpn.test/sub", trust(server.port, pin: TestPKI.caPEM)) }
        XCTAssertEqual(error, .untrusted)
        XCTAssertEqual(server.requests.count, 0)
        // Pinned itself, the same certificate is fine, though the system trusts neither.
        let body = try await fetch("https://vpn.test/sub", trust(server.port, pin: TestPKI.bycAPEM))
        XCTAssertEqual(body.count, json.utf8.count)
    }
    func testAWrongNameAnExpiredAndAClientOnlyCertificateAreRefused() async throws {
        struct Row {
            let identity: String
            let pin: String
            let label: String
        }
        let rows = [
            Row(identity: TestPKI.wrongP12, pin: TestPKI.wrongPEM, label: "wrong name"),
            Row(identity: TestPKI.expiredP12, pin: TestPKI.expiredPEM, label: "expired"),
            Row(identity: TestPKI.futureP12, pin: TestPKI.futurePEM, label: "not yet valid"),
            Row(identity: TestPKI.clientP12, pin: TestPKI.clientPEM, label: "client only")
        ]
        for row in rows {
            let server = try TestTLSServer(identity: row.identity) { _, connection in
                TestTLSServer.ok(connection, self.json)
            }
            defer { server.stop() }
            let error = await failure { try await fetch("https://vpn.test/sub", trust(server.port, pin: row.pin)) }
            XCTAssertEqual(error, .untrusted, row.label)
            XCTAssertEqual(server.requests.count, 0, row.label)
        }
    }
    func testAnotherCertificateThanThePinIsRefused() async throws {
        let server = try okServer(TestPKI.leafP12)
        defer { server.stop() }
        let error = await failure { try await fetch("https://vpn.test/sub", trust(server.port, pin: TestPKI.wrongPEM)) }
        XCTAssertEqual(error, .untrusted)
        XCTAssertEqual(server.requests.count, 0)
    }
    func testUnreadablePinsFailAsBadPinBeforeDialling() async throws {
        let server = try okServer(TestPKI.leafP12)
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
        let server = try okServer(TestPKI.leafP12)
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
        let server = try TestTLSServer(identity: TestPKI.leafP12) { head, connection in
            if head.hasPrefix("GET /sub ") {
                TestTLSServer.reply(connection, "HTTP/1.1 302 Found\r\nLocation: next?a=1\r\nContent-Length: 0\r\n\r\n")
            } else {
                TestTLSServer.ok(connection, self.json)
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
            let server = try replyServer("HTTP/1.1 302 Found\r\nLocation: \(location)\r\nContent-Length: 0\r\n\r\n")
            defer { server.stop() }
            let error = await failure { try await fetch("https://vpn.test/sub", trust(server.port)) }
            XCTAssertEqual(error, .invalid, location)
            XCTAssertEqual(server.requests.count, 1, location)
        }
    }
    func testTooManyRedirectsAndLoopsAreRefused() async throws {
        let counter = LockedBox(0)
        let server = try TestTLSServer(identity: TestPKI.leafP12) { _, connection in
            let hop = counter.mutate { $0 += 1; return $0 }
            TestTLSServer.reply(connection, "HTTP/1.1 301 Moved\r\nLocation: /r\(hop)\r\nContent-Length: 0\r\n\r\n")
        }
        defer { server.stop() }
        let error = await failure { try await fetch("https://vpn.test/sub", trust(server.port)) }
        XCTAssertEqual(error, .invalid)
        XCTAssertEqual(server.requests.count, 4)
        let loop = try TestTLSServer(identity: TestPKI.leafP12) { _, connection in
            TestTLSServer.reply(connection, "HTTP/1.1 302 Found\r\nLocation: /sub\r\nContent-Length: 0\r\n\r\n")
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
        let chunked = okHead + "Transfer-Encoding: chunked\r\n\r\n"
            + "5;ext=1\r\n{\"ver\r\n\(body.count - 5)\r\nsion\":1}\r\n0\r\nTrailer: x\r\n\r\n"
        let interim = "HTTP/1.1 100 Continue\r\n\r\n" + okHead
            + "content-length: \(body.count)\r\n\r\n\(body)"
        let rows: [(String, String)] = [
            (okHead + "Content-Length: \(body.count)\r\n\r\n\(body)", "length"),
            ("HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n\r\n\(body)", "until closed"),
            (chunked, "chunked"),
            (interim, "interim"),
            ("HTTP/1.1 200\nContent-Type: application/json\nContent-Length: \(body.count)\n\n\(body)", "bare LF")
        ]
        for row in rows {
            let server = try replyServer(row.0)
            defer { server.stop() }
            let got = try await fetch("https://vpn.test/sub", trust(server.port))
            XCTAssertEqual(String(bytes: got, encoding: .utf8), body, row.1)
        }
    }
    func testStatusesAndMalformedReplies() async throws {
        let rows: [(String, SubscriptionFetchError)] = [
            ("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n", .http(404)),
            ("HTTP/1.1 401 Unauthorized\r\n\r\nbody", .http(401)),
            ("HTTP/1.1 503 Busy\r\n\r\n", .http(503)),
            ("HTTP/1.1 302 Found\r\n\r\n", .http(302)),
            ("garbage\r\n\r\n", .invalid),
            (okHead + " Folded: x\r\n\r\n", .invalid),
            (okHead + "Content-Length: 2\r\nContent-Length: 3\r\n\r\nab", .invalid),
            (okHead + "Content-Length: +2\r\n\r\nab", .invalid),
            (okHead + "Transfer-Encoding: gzip, chunked\r\n\r\n0\r\n\r\n", .invalid),
            ("HTTP/1.1 101 Switching\r\n\r\n", .invalid),
            (okHead + "Content-Length: 10\r\n\r\nab", .network)
        ]
        for row in rows {
            let server = try replyServer(row.0)
            defer { server.stop() }
            let error = await failure { try await fetch("https://vpn.test/sub", trust(server.port)) }
            XCTAssertEqual(error, row.1, row.0.debugDescription)
        }
    }
    func testTheBodyCap() async throws {
        let cap = Subscription.maximumBytes
        let exact = String(repeating: "a", count: cap)
        let over = String(repeating: "a", count: cap + 1)
        let chunk = { (text: String) in "\(String(text.count, radix: 16))\r\n\(text)\r\n0\r\n\r\n" }
        let chunkedHead = okHead + "Transfer-Encoding: chunked\r\n\r\n"
        let rows: [(String, SubscriptionFetchError?)] = [
            (okHead + "Content-Length: \(cap)\r\n\r\n\(exact)", nil),
            (okHead + "Content-Length: \(cap + 1)\r\n\r\n\(over)", .tooLarge),
            (okHead + "Content-Length: 99999999999999999999\r\n\r\n", .tooLarge),
            (okHead + "\r\n\(exact)", nil),
            (okHead + "\r\n\(over)", .tooLarge),
            (chunkedHead + chunk(exact), nil),
            (chunkedHead + chunk(over), .tooLarge),
            (chunkedHead + "FFFFFFFFFF\r\n", .tooLarge)
        ]
        for row in rows {
            let server = try replyServer(row.0)
            defer { server.stop() }
            let error = await failure { try await fetch("https://vpn.test/sub", trust(server.port)) }
            XCTAssertEqual(error, row.1, String(row.0.prefix(70)).debugDescription)
        }
    }
}

extension PinnedFetchTests {
    // MARK: the deadline

    func testASlowDripEndsAtTheDeadline() async throws {
        let server = try TestTLSServer(identity: TestPKI.leafP12) { _, connection in
            TestTLSServer.send(connection, Data((self.okHead + "Content-Length: 1000\r\n\r\n").utf8))
            DispatchQueue.global().async {
                for _ in 0..<100 {
                    Thread.sleep(forTimeInterval: 0.1)
                    TestTLSServer.send(connection, Data("x".utf8))
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
        let server = try TestTLSServer(identity: TestPKI.leafP12) { head, connection in
            Thread.sleep(forTimeInterval: 0.6)
            let target = "/n\(head.count)"
            TestTLSServer.reply(connection, "HTTP/1.1 302 Found\r\nLocation: \(target)\r\nContent-Length: 0\r\n\r\n")
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
