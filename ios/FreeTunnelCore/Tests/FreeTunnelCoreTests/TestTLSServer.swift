import Foundation
import Network
import Security

/// A TLS server on the loopback for the pinned fetch's tests: it presents the identity of a
/// PKCS#12, counts the requests that reach it, and answers each as its handler says.
final class TestTLSServer {
    let listener: NWListener
    private let queue = DispatchQueue(label: "test-server")
    private let lock = NSLock()
    private var heads: [String] = []
    private var connections: [NWConnection] = []
    let handler: (String, NWConnection) -> Void
    private(set) var port: Int = 0

    /// What reached the server, each request's head.
    var requests: [String] { lock.lock(); defer { lock.unlock() }; return heads }

    init(identity p12: String, handler: @escaping (String, NWConnection) -> Void) throws {
        self.handler = handler
        var options: [String: Any] = [kSecImportExportPassphrase as String: "x"]
        #if os(macOS)
            if #available(macOS 15, *) { options[kSecImportToMemoryOnly as String] = true }
        #endif
        var items: CFArray?
        let status = SecPKCS12Import(Data(base64Encoded: p12)! as CFData, options as CFDictionary, &items)
        guard status == errSecSuccess,
            let item = (items as? [[String: Any]])?.first,
            let value = item[kSecImportItemIdentity as String] as AnyObject?,
            CFGetTypeID(value) == SecIdentityGetTypeID()
        else { throw NSError(domain: "TestTLSServer", code: Int(status)) }
        let tls = NWProtocolTLS.Options()
        sec_protocol_options_set_local_identity(
            tls.securityProtocolOptions, sec_identity_create(unsafeDowncast(value, to: SecIdentity.self))!)
        listener = try NWListener(using: NWParameters(tls: tls, tcp: NWProtocolTCP.Options()), on: .any)
        let ready = DispatchSemaphore(value: 0)
        listener.stateUpdateHandler = { state in
            if case .ready = state { ready.signal() }
        }
        listener.newConnectionHandler = { [unowned self] connection in accept(connection) }
        listener.start(queue: queue)
        guard ready.wait(timeout: .now() + 5) == .success, let bound = listener.port else {
            throw NSError(domain: "TestTLSServer", code: -1)
        }
        port = Int(bound.rawValue)
    }

    private func accept(_ connection: NWConnection) {
        lock.lock()
        connections.append(connection)
        lock.unlock()
        connection.start(queue: queue)
        var head = Data()
        func more() {
            connection.receive(minimumIncompleteLength: 1, maximumLength: 8192) { [self] data, _, done, error in
                if let data { head.append(data) }
                if let range = head.range(of: Data("\r\n\r\n".utf8)) {
                    // The requests of these tests are ASCII.
                    let text = String(bytes: head[..<range.lowerBound], encoding: .utf8) ?? ""
                    lock.lock()
                    heads.append(text)
                    lock.unlock()
                    handler(text, connection)
                } else if !done && error == nil {
                    more()
                }
            }
        }
        more()
    }

    func stop() {
        listener.cancel()
        lock.lock()
        let all = connections
        lock.unlock()
        for connection in all { connection.cancel() }
    }

    // MARK: answers

    /// Sends the bytes and ends the connection.
    static func reply(_ connection: NWConnection, _ text: String) {
        connection.send(
            content: Data(text.utf8), contentContext: .finalMessage, isComplete: true,
            completion: .contentProcessed { _ in
                // A server with `Connection: close` hangs up once it has said it all.
                DispatchQueue.global().asyncAfter(deadline: .now() + 0.2) { connection.cancel() }
            })
    }
    static func ok(_ connection: NWConnection, _ body: String) {
        let head = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
        reply(connection, head + "Content-Length: \(body.utf8.count)\r\n\r\n\(body)")
    }
    /// Sends the bytes and keeps the connection open.
    static func send(_ connection: NWConnection, _ data: Data) {
        connection.send(content: data, completion: .contentProcessed { _ in })
    }
}
