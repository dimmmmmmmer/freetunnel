import Foundation

/// Fetches a subscription as the desktop's `SubscriptionFetcher` does: a pinned request trusts
/// the config's certificates alone and connects to the config's address (`PinnedHTTPS`); any
/// other goes through the system (`unpinned`). One deadline covers the whole fetch, redirects
/// included, and ends it however the server drips its bytes.
public enum SubscriptionFetcher {
    /// As the desktop's `kFetchTimeoutMs`.
    public static let timeout: TimeInterval = 20
    public static let maximumRedirects = 3

    /// The system's HTTP for an address that is not pinned; it returns the body of a 200,
    /// at most `Subscription.maximumBytes` of it.
    public typealias Unpinned = @Sendable (URLRequest) async throws -> Data

    public static func fetch(
        _ request: SubscriptionRequest, timeout: TimeInterval = SubscriptionFetcher.timeout,
        unpinned: @escaping Unpinned
    ) async throws -> Data {
        guard let url = SubscriptionURL.parse(request.source) else { throw SubscriptionFetchError.badURL }
        // Before anything is dialled: certificates that cannot be read are never a reason to
        // trust the system's instead.
        if request.pinned && request.pins.isEmpty { throw SubscriptionFetchError.badPin }
        let state = Deadline()
        DispatchQueue.global().asyncAfter(deadline: .now() + timeout, execute: state.expire)
        defer { state.finish() }
        do {
            if !request.pinned {
                return try await race(state) {
                    try await unpinned(try Subscription.request(request.source))
                }
            }
            return try await pinned(request, url, state)
        } catch {
            if state.expired { throw SubscriptionFetchError.timeout }
            if let known = error as? SubscriptionFetchError { throw known }
            if error is ClientError { throw error }
            throw SubscriptionFetchError.network
        }
    }

    /// The deadline: what runs at the end of the time is told, once.
    final class Deadline: @unchecked Sendable {
        private let lock = NSLock()
        private(set) var expired = false
        private var done = false
        private var onExpire: [() -> Void] = []
        lazy var expire: DispatchWorkItem = DispatchWorkItem { [self] in
            lock.lock()
            if done { lock.unlock(); return }
            expired = true
            let actions = onExpire
            onExpire = []
            lock.unlock()
            for action in actions { action() }
        }
        func finish() {
            expire.cancel()
            lock.lock()
            done = true
            onExpire = []
            lock.unlock()
        }
        func whenExpired(_ action: @escaping () -> Void) {
            lock.lock()
            if expired { lock.unlock(); action(); return }
            onExpire.append(action)
            lock.unlock()
        }
    }

    // The unpinned fetch, ended by the deadline: the task is cancelled when time is up.
    private static func race(_ state: Deadline, _ work: @escaping @Sendable () async throws -> Data)
        async throws -> Data
    {
        let task = Task { try await work() }
        state.whenExpired { task.cancel() }
        return try await withTaskCancellationHandler {
            try await task.value
        } onCancel: {
            task.cancel()
        }
    }

    private static func pinned(_ request: SubscriptionRequest, _ original: SubscriptionURL, _ state: Deadline)
        async throws -> Data
    {
        let authorization = original.authorization
        var url = original
        var seen = [original.urlString]
        for redirects in 0...maximumRedirects {
            let reply = try await get(url, authorization: authorization, request: request, state: state)
            if [301, 302, 303, 307, 308].contains(reply.status), let location = reply.location {
                guard redirects < maximumRedirects, let next = redirect(from: url, location: location),
                    next.user.isEmpty, next.password.isEmpty,
                    SubscriptionURL.hostKey(next.host) == SubscriptionURL.hostKey(original.host),
                    next.effectivePort == original.effectivePort, !seen.contains(next.urlString)
                else { throw SubscriptionFetchError.invalid }
                seen.append(next.urlString)
                url = next
                continue
            }
            guard reply.status == 200, let body = reply.body else {
                throw SubscriptionFetchError.http(reply.status)
            }
            return body
        }
        throw SubscriptionFetchError.invalid
    }

    /// A Location resolved against the address it came from; an empty path is "/" first,
    /// so a relative one from "https://host" lands on the same host.
    static func redirect(from base: SubscriptionURL, location: String) -> SubscriptionURL? {
        guard let root = URL(string: "https://" + base.authority + (base.encodedPath.isEmpty ? "/" : base.encodedPath)
            + (base.encodedQuery.map { "?" + $0 } ?? "")),
            let resolved = URL(string: location, relativeTo: root)?.absoluteURL
        else { return nil }
        return SubscriptionURL.parse(resolved.absoluteString)
    }

    // One request on a connection of its own: to the config's address when it has one, else
    // to the address's own host and port. A redirect is judged against the address as given,
    // never against where the fetch connected to.
    private static func get(
        _ url: SubscriptionURL, authorization: String?, request: SubscriptionRequest, state: Deadline
    ) async throws -> HTTPReply {
        let verdict = PinnedHTTPS.Verdict()
        let host = request.connectHost ?? url.host
        let port = request.connectPort ?? url.effectivePort
        let wire = PinnedHTTPS.open(
            host: host, port: port, verifyName: request.verifyName, pins: request.pins, verdict: verdict)
        state.whenExpired { wire.abort(.timeout) }
        defer { wire.abort(.network) }
        do {
            try await wire.start(verdict: verdict)
            try await wire.send(PinnedHTTPS.requestBytes(url, authorization: authorization))
            return try await HTTPResponseReader(wire).read()
        } catch {
            if verdict.refused { throw SubscriptionFetchError.untrusted }
            throw error
        }
    }
}
