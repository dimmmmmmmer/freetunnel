import Foundation
import SwiftUI
import BackgroundTasks
import FreeTunnelCore
import VpnClientFramework

@MainActor final class AppState: ObservableObject {
    @Published var preferences = Preferences() { didSet { persist() } }
    @Published var snapshots: [String: GeoSnapshot] = [:]
    @Published var busy = Set<String>()
    @Published var message: String?
    @Published var openDialogs = Set<UUID>()
    @Published var loaded = false
    @Published var importText = ""
    let vpn = VPNController()
    var isPreview: Bool {
        #if DEBUG
        return ProcessInfo.processInfo.arguments.contains("--ui-preview")
        #else
        return false
        #endif
    }
    private let encoder = JSONEncoder()
    private let decoder = JSONDecoder()
    var selected: Profile? { preferences.profiles.first { $0.id == preferences.selectedID } }
    init() {
        #if DEBUG
        if isPreview {
            var profile = Profile(name: "FreeTunnel demo")
            profile.routing.manualBypass = "home.example\n192.168.0.0/16"
            profile.routing.manualVPN = "example.org"
            let list = GeoList(name: "Blocked sites in Russia", source: "https://example.com/sites.txt",
                kind: .sites, direction: .vpn)
            profile.routing.lists = [list]
            preferences.profiles = [profile]
            preferences.selectedID = profile.id
            snapshots[list.id] = GeoSnapshot(source: list.source, kind: list.kind, entries: ["example.org",
                "*.example.org"], skipped: 0)
            loaded = true
            return
        }
        #endif
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
        } catch { message = ClientError.storageFailed.rawValue }
    }
    func report(_ error: Error) {
        // URL errors often include subscription credentials. Never display raw errors.
        message = (error as? ClientError ?? .networkFailed).rawValue
    }
    private func persist() {
        guard loaded, !isPreview else { return }
        do {
            let data = try encoder.encode(preferences)
            try data.write(to: Identity.directory().appendingPathComponent("preferences.json"),
                           options: [.atomic, .completeFileProtectionUntilFirstUserAuthentication])
            scheduleRefresh()
        } catch { message = ClientError.storageFailed.rawValue }
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
        profile.routing.defaultDirection = config.defaultDirection
        profile.subscriptionEnabled = subscription != nil
        profile.subscriptionUpdated = subscription == nil ? nil : Date()
        let record = Secret(toml: try config.serialized(), subscription: subscription)
        try Vault.write(encoder.encode(record), key: profile.id.uuidString)
        preferences.profiles.append(profile)
        if preferences.selectedID == nil { preferences.selectedID = profile.id }
    }
    private func parseImport(_ text: String, allowInsecure: Bool) throws -> [String] {
        guard text.utf8.count <= Configuration.maximumBytes else { throw ClientError.tooLarge }
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        let documents: [String]
        if trimmed.hasPrefix("tt://") {
            let links = trimmed.split(whereSeparator: { $0.isWhitespace }).map(String.init)
            guard links.count <= 100 else { throw ClientError.tooLarge }
            documents = try links.map { link in
                guard link.hasPrefix("tt://") else { throw ClientError.invalidConfig }
                return try TrustTunnelDeepLink.decodeDeeplink(link)
            }
        } else {
            documents = [trimmed]
        }
        return try documents.map { document in
            let config = try Configuration(document)
            if config.skipsVerification && !allowInsecure { throw ClientError.insecureConfig }
            return try config.serialized()
        }
    }
    private func storeImport(_ documents: [String]) throws {
        guard loaded else { throw ClientError.storageFailed }
        guard documents.count <= 100 else { throw ClientError.tooLarge }
        var profiles: [Profile] = []
        do {
            for document in documents {
                let config = try Configuration(document)
                var profile = Profile(name: config.name)
                profile.routing.defaultDirection = config.defaultDirection
                let secret = Secret(toml: document)
                try Vault.write(encoder.encode(secret), key: profile.id.uuidString)
                profiles.append(profile)
            }
            var updated = preferences
            updated.profiles.append(contentsOf: profiles)
            if updated.selectedID == nil { updated.selectedID = profiles.first?.id }
            try encoder.encode(updated).write(
                to: Identity.directory().appendingPathComponent("preferences.json"),
                options: [.atomic, .completeFileProtectionUntilFirstUserAuthentication]
            )
            preferences = updated
        } catch {
            for profile in profiles { try? Vault.remove(profile.id.uuidString) }
            throw error
        }
    }
    func importBatch(_ text: String, allowInsecure: Bool = false) throws -> Int {
        let documents = try parseImport(text, allowInsecure: allowInsecure)
        try storeImport(documents)
        return documents.count
    }
    func importFiles(_ urls: [URL]) {
        do {
            guard urls.count <= 100 else { throw ClientError.tooLarge }
            var documents: [String] = []
            for url in urls {
                let access = url.startAccessingSecurityScopedResource()
                defer { if access { url.stopAccessingSecurityScopedResource() } }
                let size = try url.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0
                guard size <= Configuration.maximumBytes else { throw ClientError.tooLarge }
                let data = try Data(contentsOf: url)
                guard data.count <= Configuration.maximumBytes, let text = String(data: data, encoding: .utf8) else {
                    throw ClientError.invalidConfig
                }
                documents += try parseImport(text, allowInsecure: false)
                guard documents.count <= 100 else { throw ClientError.tooLarge }
            }
            try storeImport(documents)
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
        guard !isPreview else { return }
        defer { scheduleRefresh() }
        for profile in preferences.profiles {
            if profile.subscriptionEnabled && Date().timeIntervalSince(
                profile.subscriptionUpdated ?? .distantPast) >= 86400 {
                await refreshSubscription(profile)
            }
            let interval = Routing.intervals.contains(profile.routing.refreshHours) ? profile.routing.refreshHours : 24
            for list in profile.routing.lists where list.enabled {
                let previous = snapshots[list.id]
                if previous == nil || (interval > 0 && Date().timeIntervalSince(
                    previous?.updated ?? .distantPast) >= Double(
                    interval * 3600)) {
                    await refresh(list)
                }
            }
        }
    }
    func scheduleRefresh() {
        guard !isPreview else { return }
        let intervals = preferences.profiles.compactMap { profile -> Int? in
            let hours = profile.routing.refreshHours
            if profile.routing.lists.contains(where: { $0.enabled }) && hours > 0 { return hours }
            return profile.subscriptionEnabled ? 24 : nil
        }
        let id = "org.freetunnel.ios.refresh"
        BGTaskScheduler.shared.cancel(taskRequestWithIdentifier: id)
        guard let hours = intervals.min() else { return }
        let request = BGAppRefreshTaskRequest(identifier: id)
        request.earliestBeginDate = Date().addingTimeInterval(Double(hours * 3600))
        try? BGTaskScheduler.shared.submit(request)
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
            guard var updated = preferences.profiles.first(where: { $0.id == profile.id }) else { return }
            try Vault.write(encoder.encode(record), key: profile.id.uuidString)
            updated.subscriptionUpdated = Date()
            update(updated)
        } catch { report(error) }
    }
    func connect() async {
        guard loaded, let profile = selected else { return }
        do {
            let config = try Configuration(secret(profile).toml)
            let text = try config.tunnel(routing: profile.routing, snapshots: snapshots,
                killSwitch: preferences.killSwitch)
            try await vpn.connect(text: text, name: profile.name, killSwitch: preferences.killSwitch,
                onDemand: false)
        } catch { report(error) }
    }
}
