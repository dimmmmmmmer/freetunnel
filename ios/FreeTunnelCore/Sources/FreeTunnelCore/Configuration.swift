import Foundation
import TOMLDeserializer

public struct Configuration {
    public static let maximumBytes = 1_048_576
    private var table: [String: Any]
    var rawTable: [String: Any] { table }
    public var name: String { endpoint["name"] as? String ?? hostname }
    public var defaultDirection: Direction { (table["vpn_mode"] as? String) == "selective" ? .bypass : .vpn }
    public var dnsUpstreams: [String] {
        endpoint["dns_upstreams"] as? [String] ?? table["dns_upstreams"] as? [String] ?? [] }
    public var excludedRoutes: [String] {
        ((table["listener"] as? [String: Any])?["tun"] as? [String: Any])?["excluded_routes"] as? [String] ?? []
    }
    public var hostname: String { endpoint["hostname"] as? String ?? "" }
    public var endpoint: [String: Any] { table["endpoint"] as? [String: Any] ?? [:] }
    public var skipsVerification: Bool { endpoint["skip_verification"] as? Bool ?? false }
    public init(_ text: String, maximumBytes: Int = 1_048_576) throws {
        guard text.utf8.count <= maximumBytes else { throw ClientError.tooLarge }
        do { table = try TOMLDeserializer.tomlTable(with: Data(text.utf8)) } catch { throw ClientError.invalidConfig }
        guard let server = table["endpoint"] as? [String: Any],
              let hostname = server["hostname"] as? String, !hostname.isEmpty,
              let addresses = server["addresses"] as? [String], !addresses.isEmpty,
              addresses.allSatisfy(Self.validAddress),
              server["username"] is String, server["password"] is String else {
            throw ClientError.invalidConfig
        }
        guard !hostname.hasPrefix("*."), GeoParser.manual(hostname) != nil else { throw ClientError.invalidConfig }
        if let proto = server["upstream_protocol"] {
            guard let text = proto as? String, ["http2", "http3"].contains(text) else {
                throw ClientError.invalidConfig }
        }
        if let key = server["client_random_auth_key"] as? String, !key.isEmpty {
            throw ClientError.unsupportedFormat // Stable core; do not silently omit an auth key.
        }
    }
    private static func validAddress(_ text: String) -> Bool {
        guard !text.isEmpty, !text.contains(where: { $0.isWhitespace }),
              let parts = URLComponents(string: "https://" + text),
              parts.host?.isEmpty == false, parts.user == nil, parts.password == nil,
              parts.path.isEmpty, parts.query == nil, parts.fragment == nil, parts.port != 0 else { return false }
        return true
    }
    public var importedRouting: Routing {
        var routing = Routing()
        routing.defaultDirection = defaultDirection
        let rules = (table["exclusions"] as? [String] ?? []).compactMap(GeoParser.manual)
        if defaultDirection == .vpn { routing.manualBypass = rules.joined(separator: "\n") }
        else { routing.manualVPN = rules.joined(separator: "\n") }
        return routing
    }
    public func storingImportedRulesInProfile() throws -> String {
        var result = table
        if let rules = table["exclusions"] as? [String] {
            result["exclusions"] = rules.filter { GeoParser.manual($0) == nil }
        }
        return try Self.serialize(result)
    }
    public func serialized() throws -> String { try Self.serialize(table) }
    public func tunnel(routing: Routing, snapshots: [String: GeoSnapshot], killSwitch: Bool,
                       excludedRoutes: [String] = [], verboseLogs: Bool = false) throws -> String {
        for route in excludedRoutes { _ = try IPRoutes.parse(route) }
        var result = table
        result["killswitch_enabled"] = killSwitch
        result["loglevel"] = verboseLogs ? "debug" : "info"
        result["listener"] = ["tun": ["mtu_size": 1280,
            "included_routes": ["0.0.0.0/0", "::/0"], "excluded_routes": excludedRoutes]]
        if routing.enabled {
            result["vpn_mode"] = routing.defaultDirection == .vpn ? "general" : "selective"
            var rules = result["exclusions"] as? [String] ?? []
            for (text, tier) in [(routing.manualVPN, 0), (routing.manualBypass, 1)] {
                for line in text.split(whereSeparator: { $0.isNewline }) {
                    let raw = line.trimmingCharacters(in: .whitespaces)
                    if raw.isEmpty { continue }
                    guard let entry = GeoParser.manual(raw) else { throw ClientError.invalidList }
                    rules.append("@\(tier):\(entry)")
                }
            }
            for list in routing.lists where list.enabled {
                guard let snapshot = snapshots[list.id], snapshot.source == list.source,
                      snapshot.kind == list.kind else { throw ClientError.emptyList }
                rules += snapshot.entries.map { "@\(list.direction == .vpn ? 2 : 3):\($0)" }
            }
            guard rules.count <= 100_000 else { throw ClientError.tooLarge }
            result["exclusions"] = Array(Set(rules)).sorted()
            // An empty selective profile must not silently send all traffic direct.
            if routing.defaultDirection == .bypass && rules.isEmpty { result["vpn_mode"] = "general" }
        } else {
            result["vpn_mode"] = "general"
            result["exclusions"] = [String]()
        }
        let text = try Self.serialize(result)
        guard text.utf8.count <= 8_388_608 else { throw ClientError.tooLarge }
        return text
    }
    public static func quote(_ value: String) -> String {
        var out = "\""
        for scalar in value.unicodeScalars {
            switch scalar.value {
            case 34: out += "\\\""
            case 92: out += "\\\\"
            case 10: out += "\\n"
            case 13: out += "\\r"
            case 9: out += "\\t"
            case 0...31, 127: out += String(format: "\\u%04X", scalar.value)
            default: out.unicodeScalars.append(scalar)
            }
        }
        return out + "\""
    }
    public static func serialize(_ object: [String: Any], path: [String] = []) throws -> String {
        var output = path.isEmpty ? "" : "[" + path.map(quote).joined(separator: ".") + "]\n"
        for key in object.keys.sorted() where !(object[key] is [String: Any]) {
            output += quote(key) + " = " + (try value(object[key]!)) + "\n"
        }
        for key in object.keys.sorted() {
            if let child = object[key] as? [String: Any] {
                output += "\n" + (try serialize(child, path: path + [key]))
            }
        }
        return output
    }
    private static func value(_ input: Any) throws -> String {
        if let text = input as? String { return quote(text) }
        if type(of: input) == Bool.self, let boolean = input as? Bool { return boolean ? "true" : "false" }
        if let integer = input as? Int64 { return String(integer) }
        if let integer = input as? Int { return String(integer) }
        if let number = input as? Double, number.isFinite { return String(number) }
        if let array = input as? [Any] { return "[" + (try array.map(value)).joined(separator: ", ") + "]" }
        throw ClientError.unsupportedFormat
    }
}
