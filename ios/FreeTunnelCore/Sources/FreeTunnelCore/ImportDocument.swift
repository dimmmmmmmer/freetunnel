import Foundation
import Security
import TOMLDeserializer

/// The desktop's v0–v2 TLV format and the endpoint's flat TOML export.
public enum ImportDocument {
    public struct Document {
        public var toml: String?
        public var subscription: String?
        public init(toml: String?, subscription: String?) { self.toml = toml; self.subscription = subscription }
    }
    public static func isLink(_ text: String) -> Bool {
        text.hasPrefix("tt://") || text.hasPrefix("https://trusttunnel.org/qr.html")
            || text.hasPrefix("https://www.trusttunnel.org/qr.html") || text.hasPrefix("?tt=")
    }
    public static func split(_ text: String) throws -> [String] {
        guard text.utf8.count <= Configuration.maximumBytes else { throw ClientError.tooLarge }
        let clean = text.trimmingCharacters(in: .whitespacesAndNewlines.union(CharacterSet(charactersIn: "\u{feff}")))
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
        let sourceValue = endpoint.removeValue(forKey: "subscription_url")
            ?? (endpoint.removeValue(forKey: "subscription") as? [String: Any])?["url"]
        if let sourceValue, !(sourceValue is String) { throw ClientError.invalidConfig }
        let source = sourceValue as? String
        if let key = endpoint["client_random_auth_key"], !(key is String) || !(key as? String ?? "").isEmpty {
            throw ClientError.unsupportedFormat
        }
        if let source { _ = try Subscription.request(source) }
        if endpoint["addresses"] == nil, let address = endpoint.removeValue(forKey: "address") as? String {
            endpoint["addresses"] = [address]
        }
        if let prefix = endpoint.removeValue(forKey: "client_random_prefix") { endpoint["client_random"] = prefix }
        if let random = endpoint["client_random"] {
            guard let value = random as? String else { throw ClientError.invalidConfig }
            endpoint["client_random"] = try Subscription.clientRandom(value)
        }
        if let certificate = endpoint["certificate"] {
            guard let value = certificate as? String else { throw ClientError.invalidConfig }
            endpoint["certificate"] = try Subscription.certificates(value)
        }
        if root["endpoint"] == nil {
            if endpoint["hostname"] != nil {
                guard let username = endpoint["username"] as? String, !username.isEmpty,
                    let password = endpoint["password"] as? String, !password.isEmpty else { throw ClientError.invalidConfig }
            }
            root = [:]
        }
        root["endpoint"] = endpoint
        if endpoint["hostname"] == nil, source != nil { return Document(toml: nil, subscription: source) }
        return Document(toml: try Configuration(Configuration.serialize(root)).serialized(), subscription: source)
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
    private static func link(_ text: String) throws -> Document {
        guard text.utf8.count <= 131_072 else { throw ClientError.tooLarge }
        let clean = text.filter { !$0.isWhitespace }
        let payload: String
        if clean.hasPrefix("tt://"), let question = clean.firstIndex(of: "?") {
            payload = String(clean[clean.index(after: question)...])
        } else if let range = clean.range(of: "tt=") {
            payload = String(clean[range.upperBound...].prefix(while: { $0 != "&" }))
        } else { throw ClientError.invalidConfig }
        var encoded = payload.replacingOccurrences(of: "-", with: "+").replacingOccurrences(of: "_", with: "/")
        encoded += String(repeating: "=", count: (4 - encoded.count % 4) % 4)
        guard let data = Data(base64Encoded: encoded), !data.isEmpty, data.count <= 65_536 else { throw ClientError.invalidConfig }
        var cursor = Cursor(bytes: Array(data))
        var endpoint: [String: Any] = ["has_ipv6": true, "skip_verification": false, "upstream_protocol": "http2", "anti_dpi": false]
        var addresses: [String] = []
        var seen = Set<Int>()
        var source: String?
        while cursor.index < cursor.bytes.count {
            let tag = try cursor.integer()
            let length = try cursor.integer()
            let bytes = try cursor.take(length)
            guard tag == 2 || tag > 15 || seen.insert(tag).inserted else { throw ClientError.invalidConfig }
            func string() throws -> String {
                guard let value = String(bytes: bytes, encoding: .utf8),
                    !value.unicodeScalars.contains(where: { CharacterSet.controlCharacters.contains($0) })
                else { throw ClientError.invalidConfig }
                return value
            }
            switch tag {
            case 0, 9:
                var value = Cursor(bytes: bytes)
                let number = try value.integer()
                guard value.index == bytes.count else { throw ClientError.invalidConfig }
                if tag == 0 { guard number <= 2 else { throw ClientError.unsupportedFormat } }
                else {
                    guard number == 1 || number == 2 else { throw ClientError.unsupportedFormat }
                    endpoint["upstream_protocol"] = number == 1 ? "http2" : "http3"
                }
            case 1, 3, 5, 6, 11, 12:
                let keys = [1: "hostname", 3: "custom_sni", 5: "username", 6: "password", 11: "client_random", 12: "name"]
                endpoint[keys[tag]!] = try string()
            case 2: addresses.append(try string())
            case 4, 7, 10:
                guard bytes.count == 1, bytes[0] <= 1 else { throw ClientError.invalidConfig }
                endpoint[[4: "has_ipv6", 7: "skip_verification", 10: "anti_dpi"][tag]!] = bytes[0] == 1
            case 8: endpoint["certificate"] = try certificates(bytes)
            case 13:
                var dns = Cursor(bytes: bytes)
                var values: [String] = []
                while dns.index < dns.bytes.count {
                    let size = try dns.integer()
                    guard let value = String(bytes: try dns.take(size), encoding: .utf8), !value.isEmpty else { throw ClientError.invalidConfig }
                    values.append(value)
                }
                endpoint["dns_upstreams"] = values
            case 14:
                source = try string()
                _ = try Subscription.request(source!)
            case 15:
                if !(try string()).isEmpty { throw ClientError.unsupportedFormat }
            default: break
            }
        }
        if addresses.isEmpty, source != nil { return Document(toml: nil, subscription: source) }
        endpoint["addresses"] = addresses
        guard let username = endpoint["username"] as? String, !username.isEmpty,
            let password = endpoint["password"] as? String, !password.isEmpty else { throw ClientError.invalidConfig }
        if let random = endpoint["client_random"] as? String {
            endpoint["client_random"] = try Subscription.clientRandom(random)
        }
        let text = try Configuration.serialize(["endpoint": endpoint])
        return Document(toml: try Configuration(text).serialized(), subscription: source)
    }
    private static func certificates(_ bytes: [UInt8]) throws -> String {
        var offset = 0
        var values: [String] = []
        while offset < bytes.count {
            guard bytes.count - offset >= 2, bytes[offset] == 0x30 else { throw ClientError.invalidConfig }
            var length = Int(bytes[offset + 1])
            var header = 2
            if length & 0x80 != 0 {
                let count = length & 0x7f
                guard count > 0, count <= 4, bytes.count - offset >= 2 + count else { throw ClientError.invalidConfig }
                length = 0
                for index in 0..<count { length = (length << 8) | Int(bytes[offset + 2 + index]) }
                header += count
            }
            guard length <= bytes.count - offset - header else { throw ClientError.invalidConfig }
            let data = Data(bytes[offset..<offset + header + length])
            offset += header + length
            // As on desktop, never retain a private key embedded in the certificate field.
            if SecCertificateCreateWithData(nil, data as CFData) != nil {
                values.append("-----BEGIN CERTIFICATE-----\n" + data.base64EncodedString(options: .lineLength64Characters) + "\n-----END CERTIFICATE-----")
            }
            guard values.count <= 8 else { throw ClientError.invalidConfig }
        }
        guard !values.isEmpty else { throw ClientError.invalidConfig }
        return values.joined(separator: "\n")
    }
}
