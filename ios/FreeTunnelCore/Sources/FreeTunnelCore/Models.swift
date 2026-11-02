import Foundation

public enum ClientError: String, Error, LocalizedError {
    case invalidConfig, invalidSource, invalidList, tooLarge, emptyList, unsupportedFormat
    case missingSecret, storageFailed, networkFailed, tunnelFailed
    public var errorDescription: String? { NSLocalizedString(rawValue, comment: "") }
}
public enum Direction: String, Codable, CaseIterable, Identifiable {
    case vpn, bypass
    public var id: String { rawValue }
}
public enum ListKind: String, Codable, CaseIterable, Identifiable {
    case sites, ips
    public var id: String { rawValue }
}
public struct GeoList: Codable, Identifiable, Equatable {
    public var id: String
    public var name: String
    public var source: String
    public var kind: ListKind
    public var direction: Direction
    public var enabled: Bool
    public init(id: String = UUID().uuidString, name: String, source: String, kind: ListKind,
                direction: Direction, enabled: Bool = true) {
        self.id = id; self.name = name; self.source = source; self.kind = kind
        self.direction = direction; self.enabled = enabled
    }
}
public struct Routing: Codable, Equatable {
    public var enabled = true
    public var defaultDirection: Direction = .vpn
    public var manualVPN = ""
    public var manualBypass = ""
    public var lists: [GeoList] = []
    public var refreshHours = 24
    public init() {}
    public static let intervals = [0, 1, 2, 5, 24]
}
public struct Profile: Codable, Identifiable, Equatable {
    public var id: UUID
    public var name: String
    public var routing = Routing()
    public var subscriptionEnabled = false
    public var subscriptionUpdated: Date?
    public init(id: UUID = UUID(), name: String) { self.id = id; self.name = name }
}
public struct Preferences: Codable {
    public var profiles: [Profile] = []
    public var selectedID: UUID?
    public var killSwitch = true
    public var onDemand = false
    public var theme = "system"
    public var language = "system"
    public init() {}
}
public struct GeoSnapshot: Codable {
    public let source: String
    public let kind: ListKind
    public let entries: [String]
    public let skipped: Int
    public let updated: Date
    public init(source: String, kind: ListKind, entries: [String], skipped: Int, updated: Date = Date()) {
        self.source = source; self.kind = kind; self.entries = entries
        self.skipped = skipped; self.updated = updated
    }
}
public struct Secret: Codable {
    public var toml: String
    public var subscription: String?
    public init(toml: String, subscription: String? = nil) {
        self.toml = toml; self.subscription = subscription
    }
}
