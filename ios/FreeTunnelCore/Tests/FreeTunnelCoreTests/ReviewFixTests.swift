import Network
import XCTest

@testable import FreeTunnelCore

final class ReviewFixTests: XCTestCase {
    func testPunycodeOverflowIsRefusedNotATrap() {
        // A long ASCII run and a high scalar overflowed the later `delta += 1`.
        let input = [UInt32](repeating: 97, count: 3900) + [0x10CD00]
        // Whatever it returns, it does not trap (the old code did, here).
        _ = SubscriptionURL.punycode(input)
        _ = SubscriptionURL.punycode([UInt32](repeating: 97, count: 5000) + [0x10FFFF, 0x10FFFE])
        XCTAssertNil(SubscriptionURL.punycode([0x110000]))
        let label = String(repeating: "a", count: 3900) + "\u{10CD00}"
        XCTAssertNil(SubscriptionURL.ace(label + ".test"))
        XCTAssertEqual(SubscriptionURL.hostKey(label), "")
    }
    func testNamesBeyondDnsLimitsHaveNoAceForm() {
        XCTAssertNil(SubscriptionURL.ace(String(repeating: "a", count: 64) + ".test"))
        XCTAssertNil(SubscriptionURL.ace(String(repeating: "\u{e9}", count: 40) + ".test"))
        XCTAssertNil(SubscriptionURL.ace(Array(repeating: "abcdefghi", count: 30).joined(separator: ".")))
        XCTAssertEqual(SubscriptionURL.ace("b\u{fc}cher.example"), "xn--bcher-kva.example")
    }
    func testAnInvalidConfigHostnameIsNeverPinnedAndNeverCrashes() {
        let pem = TestPKI.leafPEM
        for bad in [
            String(repeating: "a", count: 3900) + "\u{10CD}\u{10CD00}", String(repeating: "\u{e9}", count: 70),
            String(repeating: "a", count: 64) + ".test"
        ] {
            let trust = SubscriptionTrust(hostname: bad, addresses: "1.2.3.4:443", certificate: pem)
            XCTAssertFalse(SubscriptionRequest.make("https://vpn.test/s", trust: trust).pinned)
            // Two different bad names do not match each other either.
            XCTAssertFalse(SubscriptionRequest.make("https://xn--.test/s", trust: trust).pinned)
        }
    }

    func testContentTypeMustBeJsonAsOnDesktop() async throws {
        func read(_ type: String?) async throws -> HTTPReply {
            let head = "HTTP/1.1 200 OK\r\n" + (type.map { "Content-Type: \($0)\r\n" } ?? "")
            return try await HTTPResponseReader(CertificateAndReaderTests.Feed(head + "Content-Length: 2\r\n\r\n{}")).read()
        }
        for good in ["application/json", "Application/JSON; charset=utf-8", " application/json ;x=y"] {
            let reply = try await read(good)
            XCTAssertEqual(reply.body, Data("{}".utf8), good)
        }
        for bad in [nil, "text/plain", "application/jsonp", "application/json-seq", "text/html; application/json"] {
            do {
                _ = try await read(bad)
                XCTFail("accepted \(bad ?? "none")")
            } catch { XCTAssertEqual(error as? SubscriptionFetchError, .invalid) }
        }
    }

    func testStatusesMapAsOnDesktop() {
        XCTAssertEqual(SubscriptionFetchError.http(401).clientError, .subscriptionDenied)
        for status in [403, 404, 405, 407] {
            XCTAssertEqual(SubscriptionFetchError.http(status).clientError, .subscriptionNoUpdates)
        }
        for status in [400, 410, 500, 503, 302] {
            XCTAssertEqual(SubscriptionFetchError.http(status).clientError, .networkFailed)
        }
    }

    // A plain TCP listener that keeps the first bytes a TLS client sends, its ClientHello.
    func hello(connectHost: String, verifyName: String) async throws -> Data {
        let listener = try NWListener(using: .tcp, on: .any)
        let got = LockedBox(Data())
        listener.newConnectionHandler = { connection in
            connection.start(queue: .global())
            connection.receive(minimumIncompleteLength: 1, maximumLength: 4096) { data, _, _, _ in
                got.mutate { $0 = data ?? Data() }
                connection.cancel()
            }
        }
        let ready = DispatchSemaphore(value: 0)
        listener.stateUpdateHandler = { if case .ready = $0 { ready.signal() } }
        listener.start(queue: .global())
        XCTAssertEqual(ready.wait(timeout: .now() + 5), .success)
        defer { listener.cancel() }
        let trust = SubscriptionTrust(
            hostname: verifyName, addresses: "\(connectHost):\(listener.port!.rawValue)", certificate: TestPKI.leafPEM)
        _ = try? await SubscriptionFetcher.fetch(
            SubscriptionRequest.make("https://\(verifyName)/s", trust: trust), timeout: 3, unpinned: { _ in Data() })
        return got.value
    }
    func testNoSniIsSentForAnIpNameEvenWhenConnectingToAHostName() async throws {
        let bytes = try await hello(connectHost: "localhost", verifyName: "127.0.0.1")
        XCTAssertGreaterThan(bytes.count, 50)
        XCTAssertNil(bytes.range(of: Data("localhost".utf8)))
        XCTAssertNil(bytes.range(of: Data("127.0.0.1".utf8)))
        // The control: a DNS verify name is the SNI, whatever is connected to.
        let named = try await hello(connectHost: "127.0.0.1", verifyName: "vpn.test")
        XCTAssertNotNil(named.range(of: Data("vpn.test".utf8)))
    }
}
