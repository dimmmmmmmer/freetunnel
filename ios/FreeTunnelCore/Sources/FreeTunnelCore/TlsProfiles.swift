import Foundation

/// The TLS ClientHello names TrustTunnel 1.3.0 accepts, one per connection.
/// Mirrors include/vpn/trusttunnel_tls_profiles.h on the desktop: empty or `chrome` is the core's
/// default, `default` is the deprecated alias of `boringssl`, and the tag 0x10 byte is the index.
public enum TlsProfiles {
    public static let choices = [
        "chrome", "chrome_canary", "safari", "firefox", "okhttp", "openssl", "boringssl"
    ]
    public static let defaultProfile = "chrome"
    /// The name to store (`chrome` for empty), or nil when the core would refuse it.
    public static func canonical(_ name: String) -> String? {
        if name.isEmpty { return defaultProfile }
        if name == "default" { return "boringssl" }
        return choices.contains(name) ? name : nil
    }
    public static func fromByte(_ value: UInt8) -> String? {
        Int(value) < choices.count ? choices[Int(value)] : nil
    }
    /// Tag byte for an explicit profile; nil for chrome, empty and unknown names.
    public static func byte(_ name: String) -> UInt8? {
        guard let stored = canonical(name), stored != defaultProfile else { return nil }
        return choices.firstIndex(of: stored).map { UInt8($0) }
    }
}
