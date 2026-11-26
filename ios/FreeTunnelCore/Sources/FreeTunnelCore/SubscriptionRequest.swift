import Foundation
import Security

/// What a config (or a link) vouches for a subscription fetch with: the server's
/// hostname, its addresses as typed (comma-separated) and its certificate as PEM.
/// The desktop's trust source for `subscriptionRequestFor`.
public struct SubscriptionTrust: Equatable {
    public var hostname: String
    public var addresses: String
    public var certificate: String
    public static let none = SubscriptionTrust(hostname: "", addresses: "", certificate: "")
    public init(hostname: String, addresses: String, certificate: String) {
        self.hostname = hostname
        self.addresses = addresses
        self.certificate = certificate
    }
    /// The server a stored configuration names.
    public init(config: Configuration) {
        let endpoint = config.endpoint
        self.init(
            hostname: endpoint["hostname"] as? String ?? "",
            addresses: (endpoint["addresses"] as? [String] ?? []).joined(separator: ", "),
            certificate: endpoint["certificate"] as? String ?? "")
    }
    /// The server a configuration text names; none when the text is not one.
    public init(toml: String) {
        if let config = try? Configuration(toml) {
            self.init(config: config)
        } else {
            self.init(hostname: "", addresses: "", certificate: "")
        }
    }
}

/// One fetch, decided: the address, whether it trusts the config's certificates alone
/// (pinned) and where it connects. Without pinning, the system's CAs and DNS.
public struct SubscriptionRequest: Equatable {
    public var source: String
    public var pinned: Bool
    /// DER of each certificate the config pins that this platform can read.
    public var pins: [Data]
    /// The name the certificate has to carry, and the SNI, as the config writes it.
    public var verifyName: String
    /// Where to connect instead of the address's own host and port.
    public var connectHost: String?
    public var connectPort: Int?

    /// The desktop's `subscriptionRequestFor`. Pinned iff the config has a certificate
    /// and the address's host is the config's hostname (ACE form, any case). From then
    /// on a failure to read the pins is never a reason to fall back to the system's CAs:
    /// the request stays pinned with no pins and the fetch refuses it (`badPin`).
    public static func make(_ source: String, trust: SubscriptionTrust?) -> SubscriptionRequest {
        let plain = SubscriptionRequest(
            source: source, pinned: false, pins: [], verifyName: "", connectHost: nil,
            connectPort: nil)
        guard let trust, !trust.certificate.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty,
            let url = SubscriptionURL.parse(source),
            SubscriptionURL.hostKey(url.host) == SubscriptionURL.hostKey(trust.hostname),
            !trust.hostname.isEmpty
        else { return plain }
        var request = plain
        request.pinned = true
        request.pins = pins(fromPEM: trust.certificate)
        request.verifyName = trust.hostname
        if let address = firstAddress(trust.addresses) {
            request.connectHost = address.host
            request.connectPort = address.port
        }
        return request
    }

    /// Each certificate of the PEM text that this platform can read, as DER, in order.
    public static func pins(fromPEM text: String) -> [Data] {
        var result: [Data] = []
        var rest = Substring(text)
        while let begin = rest.range(of: "-----BEGIN CERTIFICATE-----"),
            let end = rest.range(of: "-----END CERTIFICATE-----", range: begin.upperBound..<rest.endIndex)
        {
            let body = rest[begin.upperBound..<end.lowerBound].filter { !$0.isWhitespace }
            if let der = Data(base64Encoded: String(body)), !der.isEmpty,
                SecCertificateCreateWithData(nil, der as CFData) != nil
            {
                result.append(der)
            }
            rest = rest[end.upperBound...]
        }
        return result
    }

    /// The config's first address as the core connects to it ("host:port" or
    /// "[v6]:port"); nil when it is not one. Only the first counts.
    static func firstAddress(_ addresses: String) -> (host: String, port: Int)? {
        let first = (addresses.split(separator: ",", maxSplits: 1, omittingEmptySubsequences: false)
            .first.map(String.init) ?? "").trimmingCharacters(in: .whitespaces)
        guard !first.isEmpty, first.utf8.count <= 300, SubscriptionURL.isWireText(first),
            let colon = first.lastIndex(of: ":")
        else { return nil }
        let portText = first[first.index(after: colon)...]
        guard !portText.isEmpty, portText.count <= 5, portText.allSatisfy({ $0.isASCII && $0.isNumber }),
            let port = Int(portText), (1...65535).contains(port)
        else { return nil }
        var host = String(first[..<colon]).trimmingCharacters(in: .whitespaces)
        if host.hasPrefix("[") && host.hasSuffix("]") {
            host = String(host.dropFirst().dropLast())
            guard SubscriptionURL.canonicalIPv6(host) != nil else { return nil }
        } else {
            guard !host.contains(":"), !host.contains(where: { "/@[]?#\\%".contains($0) }) else {
                return nil
            }
        }
        return host.isEmpty ? nil : (host, port)
    }
}
