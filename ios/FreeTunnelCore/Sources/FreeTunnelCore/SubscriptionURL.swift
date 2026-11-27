import Foundation

/// A subscription address as the desktop's `parseSubscriptionUrl` reads it: at most
/// `maximumBytes` of UTF-8, no whitespace, control or format character anywhere,
/// `https`, a host, a port other than 0, and userinfo that is valid UTF-8 once decoded.
/// Parsed here, not by `URL`, so the host, the request target and the credentials mean
/// the same on every OS version.
public struct SubscriptionURL: Equatable {
    public static let maximumBytes = 2048

    /// The host in ACE form and lower case, with no brackets around an IPv6 address.
    public let host: String
    public let isIPv6: Bool
    /// The port the address names; nil when it names none (443).
    public let port: Int?
    /// The path as sent, percent-encoded; empty when the address has none.
    public let encodedPath: String
    public let encodedQuery: String?
    /// Decoded userinfo.
    public let user: String
    public let password: String

    public var effectivePort: Int { port ?? 443 }
    /// `host[:port]` as a Host header and a URL write it.
    public var authority: String {
        (isIPv6 ? "[" + host + "]" : host) + (port.map { ":" + String($0) } ?? "")
    }
    /// What follows the method in a request line: the path (at least "/") and the query.
    public var requestTarget: String {
        (encodedPath.isEmpty ? "/" : encodedPath) + (encodedQuery.map { "?" + $0 } ?? "")
    }
    /// The address with no userinfo and no fragment.
    public var urlString: String { "https://" + authority + encodedPath + (encodedQuery.map { "?" + $0 } ?? "") }
    /// `Basic …` when the address carries credentials, as the desktop writes it.
    public var authorization: String? {
        guard !user.isEmpty || !password.isEmpty else { return nil }
        return "Basic " + Data((user + ":" + password).utf8).base64EncodedString()
    }

    /// The address in bytes, as a link or a file carries it. Bytes that are not UTF-8
    /// (a lone surrogate, once decoded as UTF-16) are no address.
    public static func parse(utf8 bytes: some Sequence<UInt8>) -> SubscriptionURL? {
        let array = Array(bytes)
        guard array.count <= maximumBytes, let text = String(bytes: array, encoding: .utf8) else { return nil }
        return parse(text)
    }

    public static func parse(_ text: String) -> SubscriptionURL? {
        guard text.utf8.count <= maximumBytes, isWireText(text) else { return nil }
        let bytes = Array(text.utf8)
        let prefix = Array("https://".utf8)
        guard bytes.count > prefix.count,
            zip(bytes, prefix).allSatisfy({ lower($0) == $1 })
        else { return nil }
        var rest = Array(bytes[prefix.count...])
        if let hash = rest.firstIndex(of: UInt8(ascii: "#")) { rest.removeSubrange(hash...) }
        var query: [UInt8]?
        if let mark = rest.firstIndex(of: UInt8(ascii: "?")) {
            query = Array(rest[(mark + 1)...])
            rest.removeSubrange(mark...)
        }
        let slash = rest.firstIndex(of: UInt8(ascii: "/")) ?? rest.count
        let authority = Array(rest[..<slash])
        let path = Array(rest[slash...])
        guard let path = encode(path, allowing: pathAllowed), !authority.isEmpty else { return nil }
        let encodedQuery: String?
        if let query {
            guard let encoded = encode(query, allowing: pathAllowed + [UInt8(ascii: "?")]) else {
                return nil
            }
            encodedQuery = encoded
        } else {
            encodedQuery = nil
        }
        var hostPart = authority
        var user = "", password = ""
        if let atSign = authority.lastIndex(of: UInt8(ascii: "@")) {
            hostPart = Array(authority[(atSign + 1)...])
            guard let credentials = credentials(Array(authority[..<atSign])) else { return nil }
            user = credentials.user
            password = credentials.password
        }
        guard let target = hostAndPort(hostPart) else { return nil }
        return SubscriptionURL(
            host: target.host, isIPv6: target.isIPv6, port: target.port, encodedPath: path,
            encodedQuery: encodedQuery, user: user, password: password)
    }

    // The userinfo before the last "@": a user, optionally a colon and a password.
    private static func credentials(_ info: [UInt8]) -> (user: String, password: String)? {
        guard !info.contains(UInt8(ascii: "@")) else { return nil }
        let colon = info.firstIndex(of: UInt8(ascii: ":"))
        guard let user = decode(colon.map { Array(info[..<$0]) } ?? info),
            let password = decode(colon.map { Array(info[($0 + 1)...]) } ?? [])
        else { return nil }
        // Basic credentials are the user, a colon and the password; and what the
        // server is sent is what the address holds, so no control character is
        // dropped on the way.
        guard !user.contains(":"),
            !(user + password).unicodeScalars.contains(where: { $0.value < 32 || $0.value == 127 })
        else { return nil }
        return (user, password)
    }

    // MARK: pieces

    private static func lower(_ byte: UInt8) -> UInt8 {
        byte >= 65 && byte <= 90 ? byte + 32 : byte
    }

    private static let pathAllowed: [UInt8] = Array(
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~!$&'()*+,;=:@/%".utf8)

    // Percent-encodes what a request line cannot carry (non-ASCII, space-like ASCII,
    // brackets, quotes, …). An existing %XX stays; a % that starts none is no address.
    private static func encode(_ bytes: [UInt8], allowing allowed: [UInt8]) -> String? {
        var out = ""
        var index = 0
        while index < bytes.count {
            let byte = bytes[index]
            if byte == UInt8(ascii: "%") {
                guard index + 2 < bytes.count, hexValue(bytes[index + 1]) != nil,
                    hexValue(bytes[index + 2]) != nil
                else { return nil }
                // "%" and two hex digits: ASCII, so each byte is its own scalar.
                for escaped in bytes[index...index + 2] { out.unicodeScalars.append(Unicode.Scalar(escaped)) }
                index += 3
                continue
            }
            if allowed.contains(byte) {
                out.unicodeScalars.append(Unicode.Scalar(byte))
            } else {
                out += String(format: "%%%02X", byte)
            }
            index += 1
        }
        return out
    }

    private static func hexValue(_ byte: UInt8) -> UInt8? {
        switch byte {
        case 48...57: return byte - 48
        case 65...70: return byte - 55
        case 97...102: return byte - 87
        default: return nil
        }
    }

    // Percent-decoded, and nil when the bytes are not UTF-8: a replacement character
    // would send the server credentials other than the address holds.
    private static func decode(_ bytes: [UInt8]) -> String? {
        var out: [UInt8] = []
        var index = 0
        while index < bytes.count {
            if bytes[index] == UInt8(ascii: "%") {
                guard index + 2 < bytes.count, let high = hexValue(bytes[index + 1]),
                    let low = hexValue(bytes[index + 2])
                else { return nil }
                out.append(high << 4 | low)
                index += 3
            } else {
                out.append(bytes[index])
                index += 1
            }
        }
        return String(bytes: out, encoding: .utf8)
    }

    private struct HostAndPort {
        let host: String
        let isIPv6: Bool
        let port: Int?
    }

    private static func hostAndPort(_ bytes: [UInt8]) -> HostAndPort? {
        var hostBytes = bytes
        var portBytes: [UInt8] = []
        var bracketed = false
        if bytes.first == UInt8(ascii: "[") {
            guard let close = bytes.firstIndex(of: UInt8(ascii: "]")) else { return nil }
            hostBytes = Array(bytes[1..<close])
            let tail = Array(bytes[(close + 1)...])
            if !tail.isEmpty {
                guard tail.first == UInt8(ascii: ":") else { return nil }
                portBytes = Array(tail.dropFirst())
            }
            bracketed = true
        } else if let colon = bytes.lastIndex(of: UInt8(ascii: ":")) {
            hostBytes = Array(bytes[..<colon])
            portBytes = Array(bytes[(colon + 1)...])
        }
        guard let text = String(bytes: hostBytes, encoding: .utf8), let parsed = port(portBytes) else {
            return nil
        }
        if bracketed {
            guard let key = canonicalIPv6(text) else { return nil }
            return HostAndPort(host: key, isIPv6: true, port: parsed.value)
        }
        guard let host = asciiHost(text) else { return nil }
        return HostAndPort(host: host, isIPv6: false, port: parsed.value)
    }

    // An empty port is none; anything but 1...65535 in digits is no port.
    private static func port(_ bytes: [UInt8]) -> (value: Int?, Void)? {
        if bytes.isEmpty { return (nil, ()) }
        guard bytes.count <= 8, bytes.allSatisfy({ $0 >= 48 && $0 <= 57 }),
            // Only ASCII digits got here, so the bytes are always valid UTF-8.
            let value = String(bytes: bytes, encoding: .utf8).flatMap({ Int($0) }), value >= 1, value <= 65535
        else { return nil }
        return (value, ())
    }

    // A host name in ACE form and lower case; nil when it is no name (empty, a label
    // empty or over 63 bytes, a character a host never holds). An IPv4 address is one.
    private static func asciiHost(_ text: String) -> String? {
        let key = hostKey(text)
        guard !key.isEmpty, key.utf8.count <= 253 else { return nil }
        let labels = key.split(separator: ".", omittingEmptySubsequences: false)
        for (index, label) in labels.enumerated() {
            if label.isEmpty { if index == labels.count - 1 && index > 0 { continue } else { return nil } }
            guard label.utf8.count <= 63,
                label.utf8.allSatisfy({
                    ($0 >= 97 && $0 <= 122) || ($0 >= 48 && $0 <= 57) || $0 == UInt8(ascii: "-")
                        || $0 == UInt8(ascii: "_")
                })
            else { return nil }
        }
        return key
    }

    // MARK: text rules

    /// No whitespace, control or format character, by scalar: what the desktop's
    /// `isWireText` refuses in an address, a hostname and an SNI. (A lone surrogate
    /// cannot be in a `String`; see `parse(utf8:)`.)
    public static func isWireText(_ text: String) -> Bool {
        !text.unicodeScalars.contains { scalar in
            let properties = scalar.properties
            return properties.isWhitespace || properties.generalCategory == .control
                || properties.generalCategory == .format
                || properties.generalCategory == .lineSeparator
                || properties.generalCategory == .paragraphSeparator
                || properties.generalCategory == .spaceSeparator
        }
    }
}

extension SubscriptionURL {
    // MARK: host names

    /// A host as it names a server, as the desktop's `hostKey`: in ACE form and in lower
    /// case, so that however each side writes it they compare equal. An IP address is
    /// written in its canonical form; brackets around an IPv6 one are not part of it.
    public static func hostKey(_ host: String) -> String { ace(host) ?? "" }

    /// `hostKey`, or nil for a name that has no ACE form (a label over 63 bytes, a name over
    /// 253, a label punycode cannot encode): such a name is never equal to any host.
    public static func ace(_ host: String) -> String? {
        var name = host
        if name.hasPrefix("[") && name.hasSuffix("]") && name.count >= 2 {
            name = String(name.dropFirst().dropLast())
        }
        guard name.utf8.count <= 253 else { return nil }
        if let ipv6 = canonicalIPv6(name) { return ipv6 }
        let separators: Set<Character> = [".", "\u{3002}", "\u{FF0E}", "\u{FF61}"]
        let labels = name.split(omittingEmptySubsequences: false, whereSeparator: { separators.contains($0) })
        var out: [String] = []
        for label in labels {
            let text = String(label)
            guard text.utf8.count <= 63 else { return nil }
            if text.unicodeScalars.allSatisfy({ $0.isASCII }) {
                out.append(text.lowercased())
                continue
            }
            let mapped = text.precomposedStringWithCompatibilityMapping.lowercased()
            if mapped.unicodeScalars.allSatisfy({ $0.isASCII }) {
                out.append(mapped)
            } else {
                guard mapped.utf8.count <= 63,
                    let encoded = punycode(mapped.unicodeScalars.map { $0.value }),
                    encoded.utf8.count + 4 <= 63
                else { return nil }
                out.append("xn--" + encoded)
            }
        }
        return out.joined(separator: ".")
    }

    static func canonicalIPv6(_ text: String) -> String? {
        guard text.contains(":") else { return nil }
        var address = in6_addr()
        guard inet_pton(AF_INET6, text, &address) == 1 else { return nil }
        var buffer = [CChar](repeating: 0, count: Int(INET6_ADDRSTRLEN))
        guard inet_ntop(AF_INET6, &address, &buffer, socklen_t(buffer.count)) != nil else { return nil }
        return String(cString: buffer)
    }

    /// Whether the text is an IPv4 or IPv6 address (no brackets).
    public static func isIPLiteral(_ text: String) -> Bool {
        var ipv4 = in_addr()
        return canonicalIPv6(text) != nil || inet_pton(AF_INET, text, &ipv4) == 1
    }

    // RFC 3492, with every step of the arithmetic checked: nil on overflow.
    static func punycode(_ input: [UInt32]) -> String? { PunycodeEncoder.encode(input) }
}
