import Foundation

/// Same v1 TLV format emitted by the desktop (DEEP_LINK.md).
public enum DeepLinkEncoder {
    public static func encode(_ config: Configuration) throws -> String {
        let fields = EditorFields(config)
        var output = Data()
        func entry(_ tag: Int, _ data: Data) { output += varint(tag); output += varint(data.count); output += data }
        func string(_ tag: Int, _ value: String) { entry(tag, Data(value.utf8)) }
        entry(0, varint(1)); string(1, fields.hostname)
        for address in config.endpoint["addresses"] as? [String] ?? [] { string(2, address) }
        string(5, fields.username); string(6, fields.password)
        if !fields.ipv6 { entry(4, Data([0])) }
        if fields.skipVerification { entry(7, Data([1])) }
        if fields.antiDPI { entry(10, Data([1])) }
        if !fields.sni.isEmpty { string(3, fields.sni) }
        let certificate = try certificates(fields.certificate)
        if !certificate.isEmpty { entry(8, certificate) }
        if fields.proto == "http3" { entry(9, varint(2)) }
        if !fields.random.isEmpty { string(11, fields.random) }
        if !fields.name.isEmpty { string(12, fields.name) }
        if !config.dnsUpstreams.isEmpty {
            var data = Data()
            for server in config.dnsUpstreams { let bytes = Data(server.utf8); data += varint(bytes.count); data += bytes }
            entry(13, data)
        }
        guard output.count <= Configuration.maximumBytes else { throw ClientError.tooLarge }
        return "tt://?" + output.base64EncodedString().replacingOccurrences(of: "+", with: "-")
            .replacingOccurrences(of: "/", with: "_").replacingOccurrences(of: "=", with: "")
    }
    private static func varint(_ value: Int) -> Data {
        let width = value < 64 ? 1 : value < 16_384 ? 2 : 4
        var bytes = (0..<width).reversed().map { UInt8((value >> ($0 * 8)) & 255) }
        bytes[0] |= width == 1 ? 0 : width == 2 ? 0x40 : 0x80
        return Data(bytes)
    }
    private static func certificateBlocks(_ pem: String) throws -> [String] {
        let pattern = "-----BEGIN CERTIFICATE-----([A-Za-z0-9+/=\\s]+)-----END CERTIFICATE-----"
        let regex = try NSRegularExpression(pattern: pattern)
        let range = NSRange(pem.startIndex..., in: pem)
        return regex.matches(in: pem, range: range).compactMap { match in
            Range(match.range(at: 1), in: pem).map { String(pem[$0]) }
        }
    }
    private static func certificates(_ pem: String) throws -> Data {
        var data = Data()
        for block in try certificateBlocks(pem) {
            guard let der = Data(base64Encoded: block.filter { !$0.isWhitespace }) else { throw ClientError.invalidConfig }
            data += der
        }
        if !pem.isEmpty && data.isEmpty { throw ClientError.invalidConfig }
        return data
    }
    public static func exportTOML(_ config: Configuration) throws -> String {
        var root = config.rawTable
        var endpoint = config.endpoint
        if let pem = endpoint["certificate"] as? String, !pem.isEmpty {
            let blocks = try certificateBlocks(pem)
            guard !blocks.isEmpty else { throw ClientError.invalidConfig }
            endpoint["certificate"] = blocks.map { "-----BEGIN CERTIFICATE-----\n" + $0.trimmingCharacters(in: .whitespacesAndNewlines) + "\n-----END CERTIFICATE-----" }.joined(separator: "\n")
        }
        root["endpoint"] = endpoint
        return try Configuration.serialize(root)
    }
}
