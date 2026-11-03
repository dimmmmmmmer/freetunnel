import Foundation

public enum ClientError: String, Error, LocalizedError {
    case invalidConfig, invalidSource, invalidList, tooLarge, emptyList, unsupportedFormat
    case missingSecret, storageFailed, networkFailed, tunnelFailed, insecureConfig
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
    public init(
        id: String = UUID().uuidString, name: String, source: String, kind: ListKind,
        direction: Direction, enabled: Bool = true
    ) {
        self.id = id
        self.name = name
        self.source = source
        self.kind = kind
        self.direction = direction
        self.enabled = enabled
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
    public var splitProfile = "Default"
    public var subscriptionEnabled = false
    public var subscriptionUpdated: Date?
    public init(id: UUID = UUID(), name: String) {
        self.id = id
        self.name = name
    }
    enum CodingKeys: String, CodingKey {
        case id, name, routing, splitProfile, subscriptionEnabled, subscriptionUpdated
    }
    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        id = try container.decode(UUID.self, forKey: .id)
        name = try container.decode(String.self, forKey: .name)
        routing = try container.decodeIfPresent(Routing.self, forKey: .routing) ?? Routing()
        splitProfile = try container.decodeIfPresent(String.self, forKey: .splitProfile) ?? "Default"
        subscriptionEnabled = try container.decodeIfPresent(Bool.self, forKey: .subscriptionEnabled) ?? false
        subscriptionUpdated = try container.decodeIfPresent(Date.self, forKey: .subscriptionUpdated)
    }
}
public struct SplitProfile: Codable, Equatable, Identifiable {
    public var name: String
    public var routing: Routing
    public var id: String { name }
    public init(name: String, routing: Routing = Routing()) {
        self.name = name
        self.routing = routing
    }
}
public struct Preferences: Codable {
    public var profiles: [Profile] = []
    public var selectedID: UUID?
    public var killSwitch = true
    public var theme = "system"
    public var language = "system"
    public var splitProfiles = [SplitProfile(name: "Default")]
    public var activeSplitProfile = "Default"
    public var autoConnect = false
    public var loggingEnabled = true
    public var verboseLogs = false
    public var excludedRoutes = Preferences.defaultExcludedRoutes
    public static let defaultExcludedRoutes = [
        "10.0.0.0/8", "100.64.0.0/10", "169.254.0.0/16", "172.16.0.0/12",
        "192.0.0.0/24", "192.168.0.0/16", "255.255.255.255/32"
    ]
    public init() {}
    public func routing(for profile: Profile) -> Routing {
        splitProfiles.first { $0.name == profile.splitProfile }?.routing ?? Routing()
    }
    enum CodingKeys: String, CodingKey {
        case profiles, selectedID, killSwitch, theme, language, splitProfiles, activeSplitProfile,
            autoConnect, loggingEnabled, verboseLogs, excludedRoutes
    }
    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        profiles = try container.decodeIfPresent([Profile].self, forKey: .profiles) ?? []
        selectedID = try container.decodeIfPresent(UUID.self, forKey: .selectedID)
        killSwitch = try container.decodeIfPresent(Bool.self, forKey: .killSwitch) ?? true
        theme = try container.decodeIfPresent(String.self, forKey: .theme) ?? "system"
        language = try container.decodeIfPresent(String.self, forKey: .language) ?? "system"
        autoConnect = try container.decodeIfPresent(Bool.self, forKey: .autoConnect) ?? false
        loggingEnabled = try container.decodeIfPresent(Bool.self, forKey: .loggingEnabled) ?? true
        verboseLogs = try container.decodeIfPresent(Bool.self, forKey: .verboseLogs) ?? false
        excludedRoutes =
            try container.decodeIfPresent([String].self, forKey: .excludedRoutes)
            ?? Self.defaultExcludedRoutes
        activeSplitProfile =
            try container.decodeIfPresent(String.self, forKey: .activeSplitProfile) ?? "Default"
        if let stored = try container.decodeIfPresent([SplitProfile].self, forKey: .splitProfiles) {
            splitProfiles = stored
        } else {
            // Earlier previews kept routing per server. Preserve every distinct set,
            // including both manual directions, when introducing shared profiles.
            splitProfiles = []
            for index in profiles.indices {
                let routing = profiles[index].routing
                if let existing = splitProfiles.first(where: { $0.routing == routing }) {
                    profiles[index].splitProfile = existing.name
                } else {
                    let name = splitProfiles.isEmpty ? "Default" : "Profile \(splitProfiles.count + 1)"
                    splitProfiles.append(SplitProfile(name: name, routing: routing))
                    profiles[index].splitProfile = name
                }
            }
        }
        if !splitProfiles.contains(where: { $0.name == "Default" }) {
            splitProfiles.insert(SplitProfile(name: "Default"), at: 0)
        }
        if !splitProfiles.contains(where: { $0.name == activeSplitProfile }) {
            activeSplitProfile = "Default"
        }
        for index in profiles.indices
        where !splitProfiles.contains(where: { $0.name == profiles[index].splitProfile }) {
            profiles[index].splitProfile = "Default"
        }
    }
}
public struct GeoSnapshot: Codable {
    public let source: String
    public let kind: ListKind
    public let entries: [String]
    public let skipped: Int
    public let updated: Date
    public init(source: String, kind: ListKind, entries: [String], skipped: Int, updated: Date = Date()) {
        self.source = source
        self.kind = kind
        self.entries = entries
        self.skipped = skipped
        self.updated = updated
    }
}
public struct Secret: Codable {
    public var toml: String
    public var subscription: String?
    public init(toml: String, subscription: String? = nil) {
        self.toml = toml
        self.subscription = subscription
    }
}
