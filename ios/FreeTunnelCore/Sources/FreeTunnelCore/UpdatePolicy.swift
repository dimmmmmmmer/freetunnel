import Foundation

public enum UpdateChannel: String {
    case development, testFlight = "testflight", appStore = "appstore"
}
public enum UpdatePolicy {
    public struct StoreRelease: Decodable {
        public let bundleId: String
        public let version: String
        public let trackViewUrl: URL
    }
    public static func lookup(bundleID: String) -> URL? {
        var parts = URLComponents(string: "https://itunes.apple.com/lookup")!
        parts.queryItems = [URLQueryItem(name: "bundleId", value: bundleID)]
        return parts.url
    }
    public static func newerRelease(_ data: Data, bundleID: String, current: String) throws -> StoreRelease? {
        struct Response: Decodable { var results: [StoreRelease] }
        let response = try JSONDecoder().decode(Response.self, from: data)
        return response.results.first {
            $0.bundleId == bundleID && $0.version.range(of: "^[0-9]+(?:\\.[0-9]+)*$", options: .regularExpression) != nil
                && $0.version.compare(current, options: .numeric) == .orderedDescending
                && $0.trackViewUrl.scheme == "https" && $0.trackViewUrl.host == "apps.apple.com"
                && $0.trackViewUrl.user == nil && $0.trackViewUrl.password == nil
        }
    }
    public static func testFlightURL(_ text: String) -> URL? {
        guard let url = URL(string: text), url.scheme == "https", url.host == "testflight.apple.com",
            url.path.hasPrefix("/join/"), url.path.count > 6, url.user == nil, url.password == nil else { return nil }
        return url
    }
}
