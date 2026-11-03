import Foundation
#if canImport(Darwin)
import Darwin
#else
import Glibc
#endif

public struct IPRoute: Equatable {
    public let address: String
    public let prefix: Int
    public let ipv6: Bool
    public var ipv4Mask: String {
        let mask: UInt32 = prefix == 0 ? 0 : UInt32.max << (32 - prefix)
        return [24, 16, 8, 0].map { String((mask >> $0) & 255) }.joined(separator: ".")
    }
}
public enum IPRoutes {
    public static func parse(_ value: String) throws -> IPRoute {
        let parts = value.split(separator: "/", omittingEmptySubsequences: false)
        guard (1...2).contains(parts.count) else { throw ClientError.invalidList }
        let address = String(parts[0])
        var v4 = in_addr(); var v6 = in6_addr()
        let ipv6: Bool
        if inet_pton(AF_INET, address, &v4) == 1 { ipv6 = false }
        else if inet_pton(AF_INET6, address, &v6) == 1 { ipv6 = true }
        else { throw ClientError.invalidList }
        let maxBits = ipv6 ? 128 : 32
        let prefix = parts.count == 1 ? maxBits : Int(parts[1]) ?? -1
        guard (0...maxBits).contains(prefix) else { throw ClientError.invalidList }
        return IPRoute(address: address, prefix: prefix, ipv6: ipv6)
    }
    public static func isValid(_ value: String) -> Bool { (try? parse(value)) != nil }
}
