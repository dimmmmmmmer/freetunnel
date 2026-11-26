import Foundation
import Network
import Security

/// One HTTPS GET to a subscription whose server the config names: TLS that trusts only the
/// config's certificates, to an address given as an IP number while the request still names
/// the server, as the desktop does with Qt (`SubscriptionFetcher`). URLSession cannot do that:
/// its name check and its SNI come from the URL, and its trust from the system. So the TLS
/// is Network.framework's, with a verify block of our own that accepts the server's own
/// certificate when it is a pin (see `X509Leaf.accepts`) and nothing else; and the HTTP/1.1
/// is `HTTPResponseReader`.
enum PinnedHTTPS {
    /// Whether the verify block refused a certificate, so that a handshake that fails is told
    /// from a network's failure.
    final class Verdict: @unchecked Sendable {
        private let lock = NSLock()
        private var value = false
        var refused: Bool { lock.lock(); defer { lock.unlock() }; return value }
        func refuse() { lock.lock(); value = true; lock.unlock() }
    }

    /// A connection that ends at the deadline: whatever is waiting on it is failed at once.
    final class Wire: ByteSource, @unchecked Sendable {
        private let connection: NWConnection
        private let queue = DispatchQueue(label: "org.freetunnel.pinned-https")
        private let lock = NSLock()
        private var waiting: [UUID: (Error) -> Void] = [:]
        private var closed = false
        private var finished = false

        init(connection: NWConnection) { self.connection = connection }

        private func register(_ fail: @escaping (Error) -> Void) -> UUID? {
            lock.lock()
            defer { lock.unlock() }
            if closed { return nil }
            let id = UUID()
            waiting[id] = fail
            return id
        }
        private func unregister(_ id: UUID) -> Bool {
            lock.lock()
            defer { lock.unlock() }
            return waiting.removeValue(forKey: id) != nil
        }

        /// Ends the connection and fails what waits on it.
        func abort(_ error: SubscriptionFetchError) {
            lock.lock()
            closed = true
            let pending = Array(waiting.values)
            waiting = [:]
            lock.unlock()
            connection.cancel()
            for fail in pending { fail(error) }
        }

        private func wait<T>(
            _ start: (@escaping (Result<T, Error>) -> Void) -> Void
        ) async throws -> T {
            try await withCheckedThrowingContinuation { continuation in
                var id: UUID?
                let finish: (Result<T, Error>) -> Void = { [self] result in
                    // Once: whoever removes the registration resumes.
                    if let id, unregister(id) { continuation.resume(with: result) }
                }
                lock.lock()
                let fresh = UUID()
                if closed {
                    lock.unlock()
                    continuation.resume(throwing: SubscriptionFetchError.timeout)
                    return
                }
                waiting[fresh] = { continuation.resume(throwing: $0) }
                lock.unlock()
                id = fresh
                start(finish)
            }
        }

        /// Waits for the handshake to end: ready, or what stopped it.
        func start(verdict: Verdict) async throws {
            try await wait { finish in
                self.connection.stateUpdateHandler = { state in
                    switch state {
                    case .ready:
                        finish(.success(()))
                    case .failed(let error), .waiting(let error):
                        // A refused certificate is told from a network's trouble.
                        finish(.failure(verdict.refused ? SubscriptionFetchError.untrusted : error))
                    case .cancelled:
                        finish(.failure(SubscriptionFetchError.network))
                    default: break
                    }
                }
                self.connection.start(queue: self.queue)
            }
        }

        func send(_ data: Data) async throws {
            try await wait { finish in
                self.connection.send(content: data, completion: .contentProcessed { error in
                    finish(error.map { .failure($0) } ?? .success(()))
                })
            }
        }

        func read() async throws -> Data? {
            if finished { return nil }
            let result: Data? = try await wait { finish in
                self.connection.receive(minimumIncompleteLength: 1, maximumLength: 16 * 1024) {
                    data, _, complete, error in
                    if let data, !data.isEmpty {
                        if complete { self.finished = true }
                        finish(.success(data))
                    } else if complete {
                        self.finished = true
                        finish(.success(nil))
                    } else if let error {
                        finish(.failure(error))
                    } else {
                        finish(.success(nil))
                    }
                }
            }
            return result
        }
    }

    /// Opens the TLS connection for one hop: to `host:port`, naming `verifyName`, trusting `pins`.
    static func open(
        host: String, port: Int, verifyName: String, pins: [Data], verdict: Verdict
    ) -> Wire {
        let tls = NWProtocolTLS.Options()
        let options = tls.securityProtocolOptions
        let name = SubscriptionURL.hostKey(verifyName)
        // No SNI names an IP number (RFC 6066).
        if !SubscriptionURL.isIPLiteral(name) { sec_protocol_options_set_tls_server_name(options, name) }
        sec_protocol_options_set_min_tls_protocol_version(options, .TLSv12)
        sec_protocol_options_add_tls_application_protocol(options, "http/1.1")
        sec_protocol_options_set_verify_block(
            options,
            { _, trust, complete in
                let reference = sec_trust_copy_ref(trust).takeRetainedValue()
                var accepted = false
                if let chain = SecTrustCopyCertificateChain(reference) as? [SecCertificate],
                    let leaf = chain.first
                {
                    let der = SecCertificateCopyData(leaf) as Data
                    accepted = X509Leaf.accepts(leaf: der, pins: pins, verifyName: verifyName)
                }
                if !accepted { verdict.refuse() }
                complete(accepted)
            }, DispatchQueue(label: "org.freetunnel.pinned-https.verify"))
        let parameters = NWParameters(tls: tls, tcp: NWProtocolTCP.Options())
        // No proxy, as the core uses none: a proxy would see the request.
        parameters.preferNoProxies = true
        let endpoint = NWEndpoint.hostPort(
            host: NWEndpoint.Host(host), port: NWEndpoint.Port(rawValue: UInt16(port))!)
        return Wire(connection: NWConnection(to: endpoint, using: parameters))
    }

    /// The request as the desktop's sends it, but for the User-Agent: HTTP/1.1, so that the
    /// Host header names the server while the connection goes to an address; the response is
    /// not compressed; and the connection ends with it. The credentials are a header of ours.
    static func requestBytes(_ url: SubscriptionURL, authorization: String?) -> Data {
        var text = "GET \(url.requestTarget) HTTP/1.1\r\nHost: \(url.authority)\r\n"
        text += "Accept: application/json\r\nAccept-Encoding: identity\r\nConnection: close\r\n"
        if let authorization { text += "Authorization: \(authorization)\r\n" }
        text += "\r\n"
        return Data(text.utf8)
    }
}
