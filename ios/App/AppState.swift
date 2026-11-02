import Foundation
import SwiftUI
import FreeTunnelCore
import VpnClientFramework

@MainActor final class AppState: ObservableObject {
    @Published var preferences = Preferences() { didSet { persist() } }
    @Published var snapshots: [String: GeoSnapshot] = [:]
    @Published var busy = Set<String>()
    @Published var message: String?
    @Published var loaded = false
    @Published var importText = ""
    let vpn = VPNController()
    private let encoder = JSONEncoder()
    private let decoder = JSONDecoder()
    var selected: Profile? { preferences.profiles.first { $0.id == preferences.selectedID } }
    init() {
        do {
            let path = try Identity.directory().appendingPathComponent("preferences.json")
            if FileManager.default.fileExists(atPath: path.path) {
                preferences = try decoder.decode(Preferences.self, from: Data(contentsOf: path))
            }
            for profile in preferences.profiles {
                for list in profile.routing.lists {
                    if let data = try? Data(contentsOf: cachePath(list.id)),
                       let snapshot = try? decoder.decode(GeoSnapshot.self, from: data),
                       snapshot.source == list.source, snapshot.kind == list.kind {
                        snapshots[list.id] = snapshot
                    }
                }
            }
            loaded = true
        } catch { message = ClientError.storageFailed.localizedDescription }
    }
    func report(_ error: Error) {
        // URL errors often include subscription credentials. Never display raw errors.
        message = (error as? ClientError ?? .networkFailed).localizedDescription
    }
    private func persist() {
        guard loaded else { return }
        do {
            let data = try encoder.encode(preferences)
            try data.write(to: Identity.directory().appendingPathComponent("preferences.json"),
                           options: [.atomic, .completeFileProtectionUntilFirstUserAuthentication])
        } catch { message = ClientError.storageFailed.localizedDescription }
    }
    private func cachePath(_ id: String) throws -> URL {
        guard UUID(uuidString: id) != nil else { throw ClientError.invalidList }
        let directory = try Identity.directory().appendingPathComponent("geo", isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        return directory.appendingPathComponent(id + ".json")
    }
    func secret(_ profile: Profile) throws -> Secret {
        try decoder.decode(Secret.self, from: Vault.read(profile.id.uuidString))
    }
    func update(_ profile: Profile) {
        guard let index = preferences.profiles.firstIndex(where: { $0.id == profile.id }) else { return }
        preferences.profiles[index] = profile
    }
    func add(toml: String, subscription: String? = nil) throws {
        guard loaded else { throw ClientError.storageFailed }
        let config = try Configuration(toml)
        var profile = Profile(name: config.name)
        profile.subscriptionEnabled = subscription != nil
        let record = Secret(toml: try config.serialized(), subscription: subscription)
        try Vault.write(encoder.encode(record), key: profile.id.uuidString)
        preferences.profiles.append(profile)
        if preferences.selectedID == nil { preferences.selectedID = profile.id }
    }
    func importBatch(_ text: String) throws -> Int {
        guard text.utf8.count <= Configuration.maximumBytes else { throw ClientError.tooLarge }
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.hasPrefix("tt://") {
            let links = trimmed.split(whereSeparator: { $0.isWhitespace }).map(String.init)
            guard links.count <= 100 else { throw ClientError.tooLarge }
            // Parse the complete batch before storing any entry.
            let configs = try links.map { link -> String in
                guard link.hasPrefix("tt://") else { throw ClientError.invalidConfig }
                let toml = try TrustTunnelDeepLink.decodeDeeplink(link)
                _ = try Configuration(toml)
                return toml
            }
            for config in configs { try add(toml: config) }
            return configs.count
        }
        try add(toml: trimmed)
        return 1
    }
    func importFiles(_ urls: [URL]) {
        do {
            guard urls.count <= 100 else { throw ClientError.tooLarge }
            for url in urls {
                let access = url.startAccessingSecurityScopedResource()
                defer { if access { url.stopAccessingSecurityScopedResource() } }
                let size = try url.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0
                guard size <= Configuration.maximumBytes else { throw ClientError.tooLarge }
                let data = try Data(contentsOf: url)
                guard data.count <= Configuration.maximumBytes, let text = String(data: data, encoding: .utf8) else {
                    throw ClientError.invalidConfig
                }
                _ = try importBatch(text)
            }
            message = NSLocalizedString("Imported", comment: "")
        } catch { report(error) }
    }
    func remove(_ profile: Profile) throws {
        guard vpn.isDisconnected else { throw ClientError.tunnelFailed }
        try Vault.remove(profile.id.uuidString)
        preferences.profiles.removeAll { $0.id == profile.id }
        if preferences.selectedID == profile.id { preferences.selectedID = preferences.profiles.first?.id }
        for list in profile.routing.lists {
            snapshots.removeValue(forKey: list.id)
            if let path = try? cachePath(list.id) { try? FileManager.default.removeItem(at: path) }
        }
    }
    func refresh(_ list: GeoList) async {
        guard busy.insert(list.id).inserted else { return }
        defer { busy.remove(list.id) }
        do {
            let url = try GeoParser.source(list.source).url
            let data = try await Downloads.read(URLRequest(url: url), limit: GeoParser.maximumBytes)
            let snapshot = try await Task.detached { try GeoParser.parse(data, list: list) }.value
            guard preferences.profiles.contains(where: { $0.routing.lists.contains(list) }) else { return }
            try encoder.encode(snapshot).write(to: cachePath(list.id),
                options: [.atomic, .completeFileProtectionUntilFirstUserAuthentication])
            snapshots[list.id] = snapshot
        } catch { report(error) } // Previous valid snapshot is kept on failure.
    }
    func refreshDue() async {
        for profile in preferences.profiles {
            let interval = Routing.intervals.contains(profile.routing.refreshHours) ? profile.routing.refreshHours : 24
            for list in profile.routing.lists where list.enabled {
                let previous = snapshots[list.id]
                if previous == nil || (interval > 0 && Date().timeIntervalSince(previous!.updated) >= Double(interval * 3600)) {
                    await refresh(list)
                }
            }
        }
    }
    func subscribe(_ url: String) async {
        guard busy.insert("subscription").inserted else { return }
        defer { busy.remove("subscription") }
        do {
            let data = try await Downloads.read(Subscription.request(url), limit: Subscription.maximumBytes)
            try add(toml: Subscription.apply(data), subscription: url)
            message = NSLocalizedString("Imported", comment: "")
        } catch { report(error) }
    }
    func refreshSubscription(_ profile: Profile) async {
        let key = "subscription-" + profile.id.uuidString
        guard busy.insert(key).inserted else { return }
        defer { busy.remove(key) }
        do {
            var record = try secret(profile)
            guard let url = record.subscription else { throw ClientError.invalidSource }
            let data = try await Downloads.read(Subscription.request(url), limit: Subscription.maximumBytes)
            record.toml = try Subscription.apply(data, previous: record.toml)
            guard preferences.profiles.contains(where: { $0.id == profile.id }) else { return }
            try Vault.write(encoder.encode(record), key: profile.id.uuidString)
            var updated = preferences.profiles.first { $0.id == profile.id }!
            updated.subscriptionUpdated = Date()
            update(updated)
        } catch { report(error) }
    }
    func connect() async {
        guard loaded, let profile = selected else { return }
        do {
            let config = try Configuration(secret(profile).toml)
            let text = try config.tunnel(routing: profile.routing, snapshots: snapshots, killSwitch: preferences.killSwitch)
            try await vpn.connect(text: text, name: profile.name, killSwitch: preferences.killSwitch, onDemand: preferences.onDemand)
        } catch { report(error) }
    }
}
