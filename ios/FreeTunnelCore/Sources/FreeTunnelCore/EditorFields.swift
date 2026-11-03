import Foundation

/// The structured editor overlays only its own fields, preserving imported extensions.
public struct EditorFields: Equatable {
    public var name = ""
    public var hostname = ""
    public var addresses = ""
    public var username = ""
    public var password = ""
    public var proto = "http2"
    public var dns = ""
    public var sni = ""
    public var random = ""
    public var ipv6 = true
    public var skipVerification = false
    public var antiDPI = false
    public var certificate = ""
    public init() {}
    public init(_ config: Configuration) {
        let endpoint = config.endpoint
        name = config.name; hostname = config.hostname
        addresses = (endpoint["addresses"] as? [String] ?? []).joined(separator: ", ")
        username = endpoint["username"] as? String ?? ""
        password = endpoint["password"] as? String ?? ""
        proto = endpoint["upstream_protocol"] as? String ?? "http2"
        dns = config.dnsUpstreams.joined(separator: ", ")
        sni = endpoint["custom_sni"] as? String ?? ""
        random = endpoint["client_random"] as? String ?? ""
        ipv6 = endpoint["has_ipv6"] as? Bool ?? true
        skipVerification = config.skipsVerification
        antiDPI = endpoint["anti_dpi"] as? Bool ?? false
        certificate = endpoint["certificate"] as? String ?? ""
    }
    public func configuration(previous: Configuration? = nil) throws -> Configuration {
        let trimmedRandom = random.trimmingCharacters(in: .whitespacesAndNewlines)
        let parts = trimmedRandom.split(separator: "/", omittingEmptySubsequences: false)
        guard trimmedRandom.isEmpty || (parts.count <= 2 && parts.allSatisfy { part in
            !part.isEmpty && part.count.isMultiple(of: 2) && part.allSatisfy { $0.isHexDigit }
        }) else { throw ClientError.invalidConfig }
        let host = hostname.trimmingCharacters(in: .whitespacesAndNewlines)
        var root = previous?.rawTable ?? [:]
        var endpoint = previous?.endpoint ?? [:]
        endpoint["name"] = name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty ? host : name
        endpoint["hostname"] = host
        endpoint["addresses"] = addresses.split(separator: ",").map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
        endpoint["username"] = username; endpoint["password"] = password
        endpoint["upstream_protocol"] = proto
        endpoint["custom_sni"] = sni.trimmingCharacters(in: .whitespacesAndNewlines)
        endpoint["client_random"] = trimmedRandom
        endpoint["has_ipv6"] = ipv6; endpoint["skip_verification"] = skipVerification
        endpoint["anti_dpi"] = antiDPI
        endpoint["certificate"] = certificate
        // Canonical location used by the desktop and core. Remove the legacy alias.
        endpoint.removeValue(forKey: "dns_upstreams")
        root["dns_upstreams"] = dns.split(whereSeparator: { $0 == "," || $0.isWhitespace }).map(String.init)
        root["endpoint"] = endpoint
        return try Configuration(Configuration.serialize(root))
    }
}
