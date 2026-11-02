import Foundation
import CoreFoundation

public enum Subscription {
    public static let maximumBytes = 65_536
    public static func request(_ text: String) throws -> URLRequest {
        guard var parts = URLComponents(string: text), parts.scheme == "https",
              parts.host?.isEmpty == false, parts.fragment == nil,
              let user = parts.user, let password = parts.password,
              !user.contains(":"), !user.isEmpty, !password.isEmpty else { throw ClientError.invalidSource }
        parts.user = nil; parts.password = nil
        guard let url = parts.url else { throw ClientError.invalidSource }
        var request = URLRequest(url: url)
        let credentials = Data((user + ":" + password).utf8).base64EncodedString()
        request.setValue("Basic " + credentials, forHTTPHeaderField: "Authorization")
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        return request
    }
    public static func apply(_ data: Data, previous: String? = nil) throws -> String {
        guard data.count <= maximumBytes else { throw ClientError.tooLarge }
        guard let object = try JSONSerialization.jsonObject(with: data) as? [String: Any],
              let version = object["version"] as? NSNumber, CFGetTypeID(version) != CFBooleanGetTypeID(),
              version.doubleValue == 1,
              object["skip_verification"] as? Bool != true,
              object["client_random_auth_key"] == nil else { throw ClientError.unsupportedFormat }
        var endpoint: [String: Any] = [:]
        for key in ["hostname", "address", "username", "password"] {
            guard let text = object[key] as? String, !text.isEmpty, text.utf8.count <= 8192,
                  !text.unicodeScalars.contains(where: { $0.value < 32 || $0.value == 127 }) else {
                throw ClientError.invalidConfig
            }
            endpoint[key == "address" ? "addresses" : key] = key == "address" ? [text] : text
        }
        if let old = previous { endpoint["name"] = try Configuration(old).name }
        else { endpoint["name"] = object["name"] as? String ?? object["hostname"] }
        for key in ["has_ipv6", "anti_dpi"] {
            if let value = object[key] {
                guard CFGetTypeID(value as CFTypeRef) == CFBooleanGetTypeID() else { throw ClientError.invalidConfig }
                endpoint[key] = value
            }
        }
        if let proto = object["upstream_protocol"] as? String {
            guard ["http2", "http3"].contains(proto) else { throw ClientError.invalidConfig }
            endpoint["upstream_protocol"] = proto
        }
        for key in ["certificate", "custom_sni", "client_random"] {
            if let value = object[key] {
                guard let text = value as? String else { throw ClientError.invalidConfig }
                endpoint[key] = text
            }
        }
        if let dns = object["dns_upstreams"] as? [String], previous == nil { endpoint["dns_upstreams"] = dns }
        // Subscription responses update the endpoint only, preserving local routing/settings.
        var root: [String: Any] = [:]
        if let previous { root = try Configuration(previous).rawTable }
        root["endpoint"] = endpoint
        let result = try Configuration.serialize(root)
        _ = try Configuration(result)
        return result
    }
}
