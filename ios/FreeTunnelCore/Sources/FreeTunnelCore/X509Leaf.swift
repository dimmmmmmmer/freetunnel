import Foundation

/// The few fields of an X.509 certificate that deciding whether a pinned server
/// certificate is acceptable takes, read from its DER. Done here, not by the
/// platform, because `SecCertificateCopyValues` does not exist on iOS and the
/// answer has to be the same on every platform.
struct X509Leaf {
    var notBefore: Date
    var notAfter: Date
    /// Extended key usage OIDs, dotted; nil when the certificate has no such extension.
    var extendedKeyUsage: [String]?
    var dnsNames: [String] = []
    /// Raw 4 or 16 byte addresses.
    var ipAddresses: [[UInt8]] = []
    var commonNames: [String] = []

    static let serverAuth = "1.3.6.1.5.5.7.3.1"
    static let anyUsage = "2.5.29.37.0"

    struct Element {
        var tag: UInt8
        var content: ArraySlice<UInt8>
    }

    // One TLV at `index`; advances it.
    private static func next(_ bytes: ArraySlice<UInt8>, _ index: inout Int) -> Element? {
        guard index + 2 <= bytes.endIndex else { return nil }
        let tag = bytes[index]
        guard tag & 0x1f != 0x1f else { return nil }
        var length = Int(bytes[index + 1])
        var position = index + 2
        if length & 0x80 != 0 {
            let count = length & 0x7f
            guard count >= 1, count <= 4, position + count <= bytes.endIndex else { return nil }
            length = 0
            for offset in 0..<count { length = length << 8 | Int(bytes[position + offset]) }
            position += count
        }
        guard length >= 0, position + length <= bytes.endIndex else { return nil }
        index = position + length
        return Element(tag: tag, content: bytes[position..<position + length])
    }

    private static func children(_ element: Element) -> [Element]? {
        var out: [Element] = []
        var index = element.content.startIndex
        while index < element.content.endIndex {
            guard let child = next(element.content, &index) else { return nil }
            out.append(child)
        }
        return out
    }

    private static func oid(_ element: Element) -> String? {
        guard element.tag == 0x06, let first = element.content.first else { return nil }
        var parts = [Int(first) / 40, Int(first) % 40]
        var value = 0
        for byte in element.content.dropFirst() {
            value = value << 7 | Int(byte & 0x7f)
            if byte & 0x80 == 0 {
                parts.append(value)
                value = 0
            }
        }
        return parts.map(String.init).joined(separator: ".")
    }

    static func time(_ element: Element) -> Date? {
        guard let text = String(bytes: Array(element.content), encoding: .ascii), text.hasSuffix("Z")
        else { return nil }
        let digits = text.dropLast()
        guard digits.allSatisfy({ $0.isASCII && $0.isNumber }) else { return nil }
        var year: Int
        var rest: Substring
        switch element.tag {
        case 0x17:
            guard digits.count == 12 || digits.count == 10, let yy = Int(digits.prefix(2)) else { return nil }
            year = yy >= 50 ? 1900 + yy : 2000 + yy
            rest = digits.dropFirst(2)
        case 0x18:
            guard digits.count == 14 || digits.count == 12, let yyyy = Int(digits.prefix(4)) else { return nil }
            year = yyyy
            rest = digits.dropFirst(4)
        default: return nil
        }
        let numbers = stride(from: 0, to: rest.count, by: 2).compactMap {
            Int(rest.dropFirst($0).prefix(2))
        }
        guard numbers.count >= 4 else { return nil }
        var components = DateComponents()
        components.year = year
        components.month = numbers[0]
        components.day = numbers[1]
        components.hour = numbers[2]
        components.minute = numbers[3]
        components.second = numbers.count > 4 ? numbers[4] : 0
        var calendar = Calendar(identifier: .gregorian)
        calendar.timeZone = TimeZone(identifier: "UTC")!
        return calendar.date(from: components)
    }

    private static func text(_ element: Element) -> String? {
        let bytes = Array(element.content)
        switch element.tag {
        case 0x0c, 0x13, 0x16: return String(bytes: bytes, encoding: .utf8)
        case 0x14: return String(bytes: bytes, encoding: .isoLatin1)
        case 0x1e: return String(bytes: bytes, encoding: .utf16BigEndian)
        case 0x1c: return String(bytes: bytes, encoding: .utf32BigEndian)
        default: return nil
        }
    }

    init?(der: Data) {
        let bytes = Array(der)[...]
        var index = bytes.startIndex
        guard let certificate = Self.next(bytes, &index), certificate.tag == 0x30,
            index == bytes.endIndex,
            let parts = Self.children(certificate), let tbsElement = parts.first, tbsElement.tag == 0x30,
            var fields = Self.children(tbsElement)
        else { return nil }
        if fields.first?.tag == 0xa0 { fields.removeFirst() }
        // serial, signature, issuer, validity, subject, subjectPublicKeyInfo, then optional parts
        guard fields.count >= 6, let validity = Self.children(fields[3]), validity.count == 2,
            let start = Self.time(validity[0]), let end = Self.time(validity[1])
        else { return nil }
        notBefore = start
        notAfter = end
        guard let subject = Self.children(fields[4]) else { return nil }
        for set in subject {
            for pair in Self.children(set) ?? [] {
                guard let items = Self.children(pair), items.count == 2,
                    Self.oid(items[0]) == "2.5.4.3", let name = Self.text(items[1])
                else { continue }
                commonNames.append(name)
            }
        }
        for field in fields.dropFirst(6) where field.tag == 0xa3 {
            guard let wrapper = Self.children(field).flatMap({ $0.first }),
                let extensions = Self.children(wrapper)
            else { return nil }
            for item in extensions {
                guard let parts = Self.children(item), parts.count >= 2,
                    let id = Self.oid(parts[0]), let value = parts.last, value.tag == 0x04
                else { continue }
                guard id == "2.5.29.17" || id == "2.5.29.37" else { continue }
                var inner = value.content.startIndex
                guard let body = Self.next(value.content, &inner), body.tag == 0x30,
                    let entries = Self.children(body)
                else { return nil }
                switch id {
                case "2.5.29.17":
                    for entry in entries {
                        if entry.tag == 0x82, let name = String(bytes: Array(entry.content), encoding: .utf8) {
                            dnsNames.append(name)
                        } else if entry.tag == 0x87, entry.content.count == 4 || entry.content.count == 16 {
                            ipAddresses.append(Array(entry.content))
                        }
                    }
                case "2.5.29.37":
                    extendedKeyUsage = entries.compactMap(Self.oid)
                default: break
                }
            }
        }
    }

    // MARK: acceptance

    /// What a pinned fetch accepts of a server's certificate (the desktop's `isPinned`, plus
    /// the checks its TLS backend makes of a name and a purpose): the certificate itself is,
    /// byte for byte, one of the pins; it is in its dates; it may serve TLS; and it is for the
    /// name. A CA that signed it is never enough, and a chain is not built at all.
    static func accepts(leaf der: Data, pins: [Data], verifyName: String, now: Date = Date()) -> Bool {
        guard pins.contains(der), let leaf = X509Leaf(der: der), leaf.notBefore <= now, now <= leaf.notAfter
        else { return false }
        if let usage = leaf.extendedKeyUsage, !usage.contains(serverAuth), !usage.contains(anyUsage) {
            return false
        }
        return leaf.matches(name: verifyName)
    }

    /// As Qt reads a name: an IP address is matched by the certificate's IP addresses alone
    /// and by no wildcard; a host name, in ACE form and lower case, by every DNS name of the
    /// certificate and every common name of its subject (Qt reads the common name even when
    /// there are DNS names). A wildcard is one `*` at the end of the first label of a name of
    /// three labels or more, so `*.com` is no name; it stands for one or more characters of
    /// one label and is not read in an internationalized (xn--) label.
    func matches(name: String) -> Bool {
        let peer = SubscriptionURL.hostKey(name)
        guard !peer.isEmpty else { return false }
        if SubscriptionURL.isIPLiteral(peer) {
            guard let wanted = Self.addressBytes(peer) else { return false }
            return ipAddresses.contains(wanted)
        }
        return (dnsNames + commonNames).contains { Self.matches(candidate: $0, peer: peer) }
    }

    private static func addressBytes(_ text: String) -> [UInt8]? {
        var v4 = in_addr()
        if inet_pton(AF_INET, text, &v4) == 1 { return withUnsafeBytes(of: &v4) { Array($0) } }
        var v6 = in6_addr()
        if inet_pton(AF_INET6, text, &v6) == 1 { return withUnsafeBytes(of: &v6) { Array($0) } }
        return nil
    }

    static func matches(candidate: String, peer: String) -> Bool {
        guard !candidate.isEmpty else { return false }
        let name = SubscriptionURL.hostKey(candidate)
        guard name.contains("*") else { return name == peer }
        let chars = Array(name)
        guard let star = chars.firstIndex(of: "*"), star == chars.lastIndex(of: "*"),
            let dot = chars.firstIndex(of: "."), dot >= 1, star == dot - 1,
            !name.hasPrefix("xn--"), name.split(separator: ".", omittingEmptySubsequences: false).count >= 3,
            let peerDot = peer.firstIndex(of: "."), peerDot != peer.startIndex
        else { return false }
        let prefix = String(chars[..<star])
        let peerLabel = peer[..<peerDot]
        return peerLabel.count >= prefix.count + 1 && peer.hasPrefix(prefix)
            && peer[peer.index(after: peerDot)...] == String(chars[(dot + 1)...])
    }
}
