import Foundation
import Security
import TOMLDeserializer

/// The desktop's v0–v2 TLV format and the endpoint's flat TOML export.
public enum ImportDocument {
    public struct Document {
        public var toml: String?
        public var subscription: String?
        public init(toml: String?, subscription: String?) {
            self.toml = toml
            self.subscription = subscription
        }
    }
    public static func isLink(_ text: String) -> Bool {
        text.hasPrefix("tt://") || text.hasPrefix("https://trusttunnel.org/qr.html")
            || text.hasPrefix("https://www.trusttunnel.org/qr.html") || text.hasPrefix("?tt=")
    }
    public static func split(_ text: String) throws -> [String] {
        guard text.utf8.count <= Configuration.maximumBytes else { throw ClientError.tooLarge }
        let clean = text.trimmingCharacters(
            in: .whitespacesAndNewlines.union(CharacterSet(charactersIn: "\u{feff}")))
        guard isLink(clean) else { return [clean] }
        let parts = clean.split(whereSeparator: { $0.isWhitespace }).map(String.init)
        if parts.filter(isLink).count <= 1 { return [clean] }
        guard parts.count <= 100 else { throw ClientError.tooLarge }
        return parts
    }
    public static func parse(_ text: String) throws -> Document {
        if isLink(text) { return try link(text) }
        guard text.utf8.count <= Configuration.maximumBytes else { throw ClientError.tooLarge }
        var root = try TOMLDeserializer.tomlTable(with: Data(text.utf8))
        var endpoint = root["endpoint"] as? [String: Any] ?? root
        let source = try subscriptionSource(&endpoint)
        try normalizeEndpoint(&endpoint)
        if root["endpoint"] == nil {
            if endpoint["hostname"] != nil { try requireCredentials(endpoint) }
            root = [:]
        }
        root["endpoint"] = endpoint
        if endpoint["hostname"] == nil, source != nil {
            return Document(toml: nil, subscription: source)
        }
        return Document(
            toml: try Configuration(Configuration.serialize(root)).serialized(),
            subscription: source)
    }
    private static func subscriptionSource(_ endpoint: inout [String: Any]) throws -> String? {
        let value =
            endpoint.removeValue(forKey: "subscription_url")
            ?? (endpoint.removeValue(forKey: "subscription") as? [String: Any])?["url"]
        guard let value else { return nil }
        guard let source = value as? String else { throw ClientError.invalidConfig }
        _ = try Subscription.request(source)
        return source
    }
    private static func normalizeEndpoint(_ endpoint: inout [String: Any]) throws {
        if let key = endpoint["client_random_auth_key"],
            !(key is String) || !(key as? String ?? "").isEmpty {
            throw ClientError.unsupportedFormat
        }
        if endpoint["addresses"] == nil,
            let address = endpoint.removeValue(forKey: "address") as? String {
            endpoint["addresses"] = [address]
        }
        if let prefix = endpoint.removeValue(forKey: "client_random_prefix") {
            endpoint["client_random"] = prefix
        }
        if let random = endpoint["client_random"] {
            guard let value = random as? String else { throw ClientError.invalidConfig }
            endpoint["client_random"] = try Subscription.clientRandom(value)
        }
        if let certificate = endpoint["certificate"] {
            guard let value = certificate as? String else { throw ClientError.invalidConfig }
            endpoint["certificate"] = try Subscription.certificates(value)
        }
    }
    private static func requireCredentials(_ endpoint: [String: Any]) throws {
        guard let username = endpoint["username"] as? String, !username.isEmpty,
            let password = endpoint["password"] as? String, !password.isEmpty
        else { throw ClientError.invalidConfig }
    }
    private struct Cursor {
        var bytes: [UInt8]
        var index = 0
        mutating func integer() throws -> Int {
            guard index < bytes.count else { throw ClientError.invalidConfig }
            let width = 1 << (bytes[index] >> 6)
            guard bytes.count - index >= width else { throw ClientError.invalidConfig }
            var value = UInt64(bytes[index] & 63)
            for offset in 1..<width { value = (value << 8) | UInt64(bytes[index + offset]) }
            index += width
            guard value <= UInt64(Int.max) else { throw ClientError.invalidConfig }
            return Int(value)
        }
        mutating func take(_ count: Int) throws -> [UInt8] {
            guard count >= 0, count <= bytes.count - index else { throw ClientError.invalidConfig }
            defer { index += count }
            return Array(bytes[index..<index + count])
        }
    }
    private static func payload(_ text: String) throws -> [UInt8] {
        guard text.utf8.count <= 131_072 else { throw ClientError.tooLarge }
        let clean = text.filter { !$0.isWhitespace }
        let payload: String
        if clean.hasPrefix("tt://"), let question = clean.firstIndex(of: "?") {
            payload = String(clean[clean.index(after: question)...])
        } else if let range = clean.range(of: "tt=") {
            payload = String(clean[range.upperBound...].prefix(while: { $0 != "&" }))
        } else {
            throw ClientError.invalidConfig
        }
        var encoded = payload.replacingOccurrences(of: "-", with: "+").replacingOccurrences(
            of: "_", with: "/")
        encoded += String(repeating: "=", count: (4 - encoded.count % 4) % 4)
        guard let data = Data(base64Encoded: encoded), !data.isEmpty, data.count <= 65_536 else {
            throw ClientError.invalidConfig
        }
        return Array(data)
    }
    private static func string(_ bytes: [UInt8]) throws -> String {
        guard let value = String(bytes: bytes, encoding: .utf8),
            !value.unicodeScalars.contains(where: { CharacterSet.controlCharacters.contains($0) })
        else {
            throw ClientError.invalidConfig
        }
        return value
    }
    private static func number(_ bytes: [UInt8]) throws -> Int {
        var cursor = Cursor(bytes: bytes)
        let number = try cursor.integer()
        guard cursor.index == bytes.count else { throw ClientError.invalidConfig }
        return number
    }
    private static func dnsServers(_ bytes: [UInt8]) throws -> [String] {
        var cursor = Cursor(bytes: bytes)
        var values: [String] = []
        while cursor.index < cursor.bytes.count {
            let size = try cursor.integer()
            let value = try string(cursor.take(size))
            guard !value.isEmpty else { throw ClientError.invalidConfig }
            values.append(value)
        }
        return values
    }
    private static func fields(_ records: [Int: [UInt8]]) throws -> [String: Any] {
        var endpoint: [String: Any] = [
            "has_ipv6": true, "skip_verification": false, "upstream_protocol": "http2",
            "anti_dpi": false
        ]
        let strings = [
            1: "hostname", 3: "custom_sni", 5: "username", 6: "password", 11: "client_random",
            12: "name"
        ]
        for (tag, key) in strings {
            if let bytes = records[tag] { endpoint[key] = try string(bytes) }
        }
        for (tag, key) in [4: "has_ipv6", 7: "skip_verification", 10: "anti_dpi"] {
            if let bytes = records[tag] {
                guard bytes.count == 1, bytes[0] <= 1 else { throw ClientError.invalidConfig }
                endpoint[key] = bytes[0] == 1
            }
        }
        if let bytes = records[9] {
            let value = try number(bytes)
            guard value == 1 || value == 2 else { throw ClientError.unsupportedFormat }
            endpoint["upstream_protocol"] = value == 1 ? "http2" : "http3"
        }
        if let bytes = records[8] { endpoint["certificate"] = try certificates(bytes) }
        if let bytes = records[13] { endpoint["dns_upstreams"] = try dnsServers(bytes) }
        return endpoint
    }
    private static func link(_ text: String) throws -> Document {
        var cursor = Cursor(bytes: try payload(text))
        var records: [Int: [UInt8]] = [:]
        var addresses: [String] = []
        while cursor.index < cursor.bytes.count {
            let tag = try cursor.integer()
            let length = try cursor.integer()
            let bytes = try cursor.take(length)
            if tag == 2 {
                addresses.append(try string(bytes))
                continue
            }
            guard tag > 15 || records[tag] == nil else { throw ClientError.invalidConfig }
            records[tag] = bytes
        }
        try validateVersionAndAuth(records)
        var endpoint = try fields(records)
        let source = try records[14].map(string)
        if let source { _ = try Subscription.request(source) }
        if addresses.isEmpty, source != nil { return Document(toml: nil, subscription: source) }
        endpoint["addresses"] = addresses
        try requireCredentials(endpoint)
        try normalizeEndpoint(&endpoint)
        let text = try Configuration.serialize(["endpoint": endpoint])
        return Document(toml: try Configuration(text).serialized(), subscription: source)
    }
    private static func validateVersionAndAuth(_ records: [Int: [UInt8]]) throws {
        if let version = records[0], try number(version) > 2 { throw ClientError.unsupportedFormat }
        if let auth = records[15], !(try string(auth)).isEmpty {
            throw ClientError.unsupportedFormat
        }
    }
    private static func certificates(_ bytes: [UInt8]) throws -> String {
        var offset = 0
        var values: [String] = []
        while offset < bytes.count {
            guard bytes.count - offset >= 2, bytes[offset] == 0x30 else {
                throw ClientError.invalidConfig
            }
            var length = Int(bytes[offset + 1])
            var header = 2
            if length & 0x80 != 0 {
                let count = length & 0x7f
                guard count > 0, count <= 4, bytes.count - offset >= 2 + count else {
                    throw ClientError.invalidConfig
                }
                length = 0
                for index in 0..<count { length = (length << 8) | Int(bytes[offset + 2 + index]) }
                header += count
            }
            guard length <= bytes.count - offset - header else { throw ClientError.invalidConfig }
            let data = Data(bytes[offset..<offset + header + length])
            offset += header + length
            // Never retain a private key embedded in the certificate field.
            if SecCertificateCreateWithData(nil, data as CFData) != nil {
                values.append(
                    "-----BEGIN CERTIFICATE-----\n"
                        + data.base64EncodedString(options: .lineLength64Characters)
                        + "\n-----END CERTIFICATE-----")
            }
            guard values.count <= 8 else { throw ClientError.invalidConfig }
        }
        guard !values.isEmpty else { throw ClientError.invalidConfig }
        return values.joined(separator: "\n")
    }
}
