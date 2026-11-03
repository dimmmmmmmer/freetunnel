import Foundation
import FreeTunnelCore

private final class RedirectPolicy: NSObject, URLSessionTaskDelegate {
    let original: URL
    init(_ original: URL) { self.original = original }
    func urlSession(
        _ session: URLSession, task: URLSessionTask,
        willPerformHTTPRedirection response: HTTPURLResponse, newRequest request: URLRequest,
        completionHandler: @escaping (URLRequest?) -> Void
    ) {
        guard let url = request.url, url.scheme == "https", url.user == nil, url.password == nil
        else {
            completionHandler(nil)
            return
        }
        // Authenticated subscriptions may redirect only within the same origin.
        if task.originalRequest?.value(forHTTPHeaderField: "Authorization") != nil
            && (url.host != original.host || (url.port ?? 443) != (original.port ?? 443)) {
            completionHandler(nil)
            return
        }
        var redirected = request
        if let authorization = task.originalRequest?.value(forHTTPHeaderField: "Authorization") {
            redirected.setValue(authorization, forHTTPHeaderField: "Authorization")
        }
        completionHandler(redirected)
    }
}
enum Downloads {
    static func read(_ request: URLRequest, limit: Int) async throws -> Data {
        guard let url = request.url, url.scheme == "https" else { throw ClientError.invalidSource }
        let configuration = URLSessionConfiguration.ephemeral
        configuration.timeoutIntervalForRequest = 30
        configuration.timeoutIntervalForResource = 120
        configuration.httpCookieStorage = nil
        configuration.urlCredentialStorage = nil
        let session = URLSession(
            configuration: configuration, delegate: RedirectPolicy(url), delegateQueue: nil)
        defer { session.invalidateAndCancel() }
        let (bytes, response) = try await session.bytes(for: request)
        guard let http = response as? HTTPURLResponse, (200...299).contains(http.statusCode) else {
            throw ClientError.networkFailed
        }
        guard response.expectedContentLength <= Int64(limit) else { throw ClientError.tooLarge }
        var body = Data()
        for try await byte in bytes {
            if body.count >= limit { throw ClientError.tooLarge }
            body.append(byte)
        }
        return body
    }
}
