import Foundation
import CoreFoundation
import Security

public enum Subscription {
    public static let maximumBytes = 65_536
    public static func request(_ text: String) throws -> URLRequest {
        guard var parts = URLComponents(string: text), parts.scheme == "https",
              parts.host?.isEmpty == false, parts.port != 0,
              !(parts.user ?? "").contains(":"), text.utf8.count <= 2560 else { throw ClientError.invalidSource }
        let user = parts.user ?? ""
        let password = parts.password ?? ""
        guard !(user + password).unicodeScalars.contains(where: { $0.value < 32 || $0.value == 127 }) else { throw ClientError.invalidSource }
        parts.fragment = nil
        parts.user = nil; parts.password = nil
        guard let url = parts.url else { throw ClientError.invalidSource }
        var request = URLRequest(url: url)
        let credentials = Data((user + ":" + password).utf8).base64EncodedString()
        if !user.isEmpty || !password.isEmpty { request.setValue("Basic " + credentials, forHTTPHeaderField: "Authorization") }
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        return request
    }
    public static func apply(_ data: Data, previous: String? = nil) throws -> String {
        guard data.count <= maximumBytes else { throw ClientError.tooLarge }
        guard let object = try JSONSerialization.jsonObject(with: data) as? [String: Any],
              let version = object["version"] as? NSNumber, CFGetTypeID(version) != CFBooleanGetTypeID(),
              version.doubleValue == 1,
              object["skip_verification"] as? Bool != true,
              (object["client_random_auth_key"] as? String ?? "").isEmpty else { throw ClientError.unsupportedFormat }
        for key in ["hostname", "address", "username", "password", "name", "certificate", "custom_sni", "client_random_prefix", "client_random_auth_key", "upstream_protocol"] {
            if let value = object[key], !(value is String) { throw ClientError.invalidConfig }
        }
        if let value = object["skip_verification"], CFGetTypeID(value as CFTypeRef) != CFBooleanGetTypeID() { throw ClientError.invalidConfig }
        if let value = object["dns_upstreams"], !(value is [String]) { throw ClientError.invalidConfig }
        var endpoint: [String: Any] = try previous.map { try Configuration($0).endpoint } ?? [:]
        endpoint["skip_verification"] = false
        endpoint["has_ipv6"] = true
        endpoint["anti_dpi"] = false
        endpoint["upstream_protocol"] = "http2"
        endpoint["custom_sni"] = ""
        endpoint["client_random"] = ""
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
                guard let boolean = value as? NSNumber, CFGetTypeID(boolean) == CFBooleanGetTypeID() else { throw ClientError.invalidConfig }
                endpoint[key] = boolean.boolValue
            }
        }
        if let proto = object["upstream_protocol"] as? String {
            guard ["http2", "http3"].contains(proto) else { throw ClientError.invalidConfig }
            endpoint["upstream_protocol"] = proto
        }
        for key in ["custom_sni"] {
            if let value = object[key] {
                guard let text = value as? String else { throw ClientError.invalidConfig }
                endpoint[key] = text
            }
        }
        if let certificate = object["certificate"] as? String { endpoint["certificate"] = try certificates(certificate) }
        else { endpoint["certificate"] = "" }
        if let prefix = object["client_random_prefix"] as? String {
            let pattern = "^(?:[0-9a-fA-F]{2}){1,32}(?:/(?:[0-9a-fA-F]{2}){1,32})?$"
            guard prefix.isEmpty || prefix.range(of: pattern, options: .regularExpression) != nil else {
                throw ClientError.invalidConfig
            }
            endpoint["client_random"] = prefix.lowercased()
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
    private static func certificates(_ text: String) throws -> String {
        if text.isEmpty { return "" }
        let pattern = "-----BEGIN CERTIFICATE-----([A-Za-z0-9+/=\\s]+)-----END CERTIFICATE-----"
        let regex = try NSRegularExpression(pattern: pattern)
        let matches = regex.matches(in: text, range: NSRange(text.startIndex..., in: text))
        guard !matches.isEmpty, matches.count <= 8 else { throw ClientError.invalidConfig }
        return try matches.map { match in
            guard let bodyRange = Range(match.range(at: 1), in: text), let fullRange = Range(match.range, in: text),
                  let der = Data(base64Encoded: String(text[bodyRange]).filter { !$0.isWhitespace }),
                  SecCertificateCreateWithData(nil, der as CFData) != nil else { throw ClientError.invalidConfig }
            return String(text[fullRange])
        }.joined(separator: "\n")
    }
}
