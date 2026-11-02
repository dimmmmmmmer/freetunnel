import Foundation
#if canImport(Darwin)
import Darwin
#else
import Glibc
#endif

public enum GeoParser {
    public static let maximumBytes = 32 * 1024 * 1024
    public static let maximumEntries = 100_000
    public static func source(_ text: String) throws -> (url: URL, category: String?) {
        guard var parts = URLComponents(string: text), parts.scheme == "https",
              parts.host?.isEmpty == false, parts.user == nil, parts.password == nil else {
            throw ClientError.invalidSource
        }
        let category = parts.fragment
        if let category, category.isEmpty || category.contains("@") { throw ClientError.invalidSource }
        if parts.path.lowercased().hasSuffix(".dat") && category == nil { throw ClientError.invalidSource }
        parts.fragment = nil
        guard let url = parts.url else { throw ClientError.invalidSource }
        return (url, category)
    }
    public static func manual(_ text: String) -> String? {
        ip(text) ?? domain(text)
    }
    public static func domain(_ raw: String) -> String? {
        var text = raw.lowercased().trimmingCharacters(in: .whitespacesAndNewlines)
        var prefix = ""
        if text.hasPrefix("*.") { prefix = "*."; text.removeFirst(2) }
        guard !text.isEmpty, text.utf8.count <= 253, !text.contains(":"), !text.contains("/"),
              !text.contains("@"), !text.contains(" "), !text.contains("?"), !text.contains("#") else { return nil }
        guard let host = URL(string: "https://" + text)?.host, !host.isEmpty else { return nil }
        let labels = host.split(separator: ".", omittingEmptySubsequences: false)
        guard labels.allSatisfy({ label in
            !label.isEmpty && label.count <= 63 && label.first != "-" && label.last != "-" &&
            label.utf8.allSatisfy { (97...122).contains($0) || (48...57).contains($0) || $0 == 45 }
        }) else { return nil }
        return prefix + host
    }
    public static func ip(_ raw: String) -> String? {
        let parts = raw.split(separator: "/", omittingEmptySubsequences: false)
        guard parts.count <= 2, let address = parts.first else { return nil }
        let text = String(address)
        var bytes = [UInt8](repeating: 0, count: 16)
        let family: Int32 = text.contains(":") ? AF_INET6 : AF_INET
        guard inet_pton(family, text, &bytes) == 1 else { return nil }
        if parts.count == 2 {
            guard let length = Int(parts[1]), length >= 0, length <= (family == AF_INET ? 32 : 128) else { return nil }
            return text.lowercased() + "/" + String(length)
        }
        return text.lowercased()
    }
    public static func parse(_ data: Data, list: GeoList) throws -> GeoSnapshot {
        guard data.count <= maximumBytes else { throw ClientError.tooLarge }
        let parsedSource = try source(list.source)
        var entries = Set<String>()
        var skipped = 0
        func add(_ entry: String?) throws {
            if let entry { entries.insert(entry) } else { skipped += 1 }
            if entries.count > maximumEntries { throw ClientError.tooLarge }
        }
        if let category = parsedSource.category {
            var root = ProtoReader(data)
            var found = false
            while let field = try root.next() {
                guard field.number == 1, let blob = field.bytes else { continue }
                var record = ProtoReader(blob)
                var code = ""
                var members: [Data] = []
                while let part = try record.next() {
                    if part.number == 1, let bytes = part.bytes { code = String(data: bytes, encoding: .utf8) ?? "" }
                    if part.number == 2, let bytes = part.bytes { members.append(bytes) }
                    // geoip reverse_match must not silently invert a selected category.
                    if list.kind == .ips && part.number == 3 && (part.integer ?? 0) != 0 { throw ClientError.unsupportedFormat }
                }
                guard code.lowercased() == category.lowercased() else { continue }
                found = true
                for member in members {
                    var item = ProtoReader(member)
                    var type: UInt64 = 0
                    var bytes = Data()
                    while let part = try item.next() {
                        if list.kind == .sites {
                            if part.number == 1 { type = part.integer ?? 0 }
                            if part.number == 2 { bytes = part.bytes ?? Data() }
                        } else {
                            if part.number == 1 { bytes = part.bytes ?? Data() }
                            if part.number == 2 { type = part.integer ?? 0 }
                        }
                    }
                    if list.kind == .sites {
                        guard let text = String(data: bytes, encoding: .utf8), let host = domain(text) else { skipped += 1; continue }
                        switch type {
                        case 2: try add(host); try add("*." + host)
                        case 3: try add(host)
                        default: skipped += 1 // Plain substring/regex are not equivalent to a domain rule.
                        }
                    } else {
                        guard bytes.count == 4 || bytes.count == 16, type <= UInt64(bytes.count * 8) else { throw ClientError.invalidList }
                        var output = [CChar](repeating: 0, count: Int(INET6_ADDRSTRLEN))
                        let converted = bytes.withUnsafeBytes { ptr in
                            inet_ntop(bytes.count == 4 ? AF_INET : AF_INET6, ptr.baseAddress, &output, socklen_t(output.count))
                        }
                        guard converted != nil else { throw ClientError.invalidList }
                        try add(String(cString: output) + "/" + String(type))
                    }
                }
            }
            guard found else { throw ClientError.emptyList }
        } else {
            guard let text = String(data: data, encoding: .utf8), !text.contains("\0") else { throw ClientError.invalidList }
            for line in text.split(whereSeparator: { $0.isNewline }) {
                let value = line.trimmingCharacters(in: .whitespaces)
                if value.isEmpty || value.hasPrefix("#") || value.hasPrefix("!") { continue }
                if list.kind == .ips { try add(ip(value)) }
                else if value.hasPrefix("full:") { try add(domain(String(value.dropFirst(5)))) }
                else {
                    let plain = value.hasPrefix("domain:") ? String(value.dropFirst(7)) : value
                    guard let host = domain(plain) else { skipped += 1; continue }
                    try add(host)
                    if !host.hasPrefix("*.") { try add("*." + host) }
                }
            }
        }
        guard !entries.isEmpty else { throw ClientError.emptyList }
        return GeoSnapshot(source: list.source, kind: list.kind, entries: entries.sorted(), skipped: skipped)
    }
}

private struct ProtoReader {
    struct Field { var number: Int; var integer: UInt64?; var bytes: Data? }
    let data: Data
    var offset = 0
    init(_ data: Data) { self.data = Data(data) }
    mutating func varint() throws -> UInt64 {
        var result: UInt64 = 0
        for shift in stride(from: 0, through: 63, by: 7) {
            guard offset < data.count else { throw ClientError.invalidList }
            let byte = data[offset]; offset += 1
            if shift == 63 && byte > 1 { throw ClientError.invalidList }
            result |= UInt64(byte & 127) << shift
            if byte & 128 == 0 { return result }
        }
        throw ClientError.invalidList
    }
    mutating func next() throws -> Field? {
        if offset == data.count { return nil }
        let tag = try varint()
        guard tag >> 3 > 0 else { throw ClientError.invalidList }
        var field = Field(number: Int(tag >> 3))
        switch tag & 7 {
        case 0: field.integer = try varint()
        case 2:
            let length = try varint()
            guard length <= UInt64(data.count - offset) else { throw ClientError.invalidList }
            field.bytes = data.subdata(in: offset..<(offset + Int(length))); offset += Int(length)
        case 1, 5:
            let length = tag & 7 == 1 ? 8 : 4
            guard length <= data.count - offset else { throw ClientError.invalidList }
            offset += length
        default: throw ClientError.invalidList
        }
        return field
    }
}
