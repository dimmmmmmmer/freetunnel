import SwiftUI
import Network
import FreeTunnelCore

enum AppDialogRoute: Equatable { case server(UUID?), importText, subscription }
struct AppConfirmation {
    var text: String
    var actionTitle: String
    var action: () -> Void
}
@MainActor extension AppState {
    static let previewConfig = """
    [endpoint]
    name = "FreeTunnel demo"
    hostname = "vpn.example.org"
    addresses = ["192.0.2.1:443"]
    username = "user"
    password = "password"
    """
    func confirm(_ text: String, actionTitle: String = "Delete", action: @escaping () -> Void) {
        confirmation = AppConfirmation(text: text, actionTitle: actionTitle, action: action)
    }
    func assignImportedRouting(_ profile: inout Profile) {
        if profile.routing.defaultDirection != preferences.splitProfiles[0].routing.defaultDirection {
            let name = "Imported " + profile.id.uuidString.prefix(8)
            profile.splitProfile = name
            preferences.splitProfiles.append(SplitProfile(name: name, routing: profile.routing))
        }
    }
    func saveServer(_ fields: EditorFields, original: Configuration?, profile: Profile?, splitProfile: String) throws {
        let text = try fields.configuration(previous: original).serialized()
        if let profile {
            var record = try secret(profile)
            record.toml = text
            if !isPreview { try Vault.write(JSONEncoder().encode(record), key: profile.id.uuidString) }
            guard var latest = preferences.profiles.first(where: { $0.id == profile.id }) else { return }
            latest.name = try Configuration(text).name; latest.splitProfile = splitProfile
            update(latest)
            if selected?.id == profile.id { reapplySelected() }
        } else {
            if isPreview {
                var created = Profile(name: fields.name.isEmpty ? fields.hostname : fields.name)
                created.splitProfile = splitProfile
                preferences.profiles.append(created)
                return
            }
            try add(toml: text)
            if let index = preferences.profiles.indices.last { preferences.profiles[index].splitProfile = splitProfile }
        }
    }
    func select(_ id: UUID) {
        guard preferences.selectedID != id else { return }
        preferences.selectedID = id
        reapplySelected()
    }
    func reapplySelected() {
        guard !isPreview, !vpn.isDisconnected else { return }
        reconnectTask?.cancel()
        reconnectTask = Task {
            do {
                try await Task.sleep(for: .milliseconds(500))
                try await vpn.disconnectAndWait()
                try Task.checkCancellation()
                await connect()
            } catch is CancellationError {} catch { report(error) }
        }
    }
    func setRouting(_ value: Routing, name: String) {
        guard let index = preferences.splitProfiles.firstIndex(where: { $0.name == name }) else { return }
        preferences.splitProfiles[index].routing = value
        let ready = value.lists.filter { $0.enabled }.allSatisfy { list in
            snapshots[list.id]?.source == list.source && snapshots[list.id]?.kind == list.kind
        }
        if selected?.splitProfile == name && ready { reapplySelected() }
    }
    func removeSplitProfile(_ name: String) {
        guard name != "Default" else { return }
        let affected = selected?.splitProfile == name
        preferences.splitProfiles.removeAll { $0.name == name }
        for index in preferences.profiles.indices where preferences.profiles[index].splitProfile == name {
            preferences.profiles[index].splitProfile = "Default"
        }
        if preferences.activeSplitProfile == name { preferences.activeSplitProfile = "Default" }
        if affected { reapplySelected() }
    }
    func addSplitProfile(_ name: String) {
        let name = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !name.isEmpty, name.count <= 16, !preferences.splitProfiles.contains(where: { $0.name == name }) else { return }
        preferences.splitProfiles.append(SplitProfile(name: name))
        preferences.activeSplitProfile = name
    }
    func pingServers() async {
        guard !pinging, !isPreview else { return }
        pinging = true
        defer { pinging = false }
        let targets = preferences.profiles.compactMap { profile -> (UUID, String)? in
            guard let config = try? Configuration(secret(profile).toml),
                  let address = (config.endpoint["addresses"] as? [String])?.first else { return nil }
            return (profile.id, address)
        }
        for (id, _) in targets { pings[id] = "…" }
        // Limit parallel socket probes, including for large imports.
        for start in stride(from: 0, to: targets.count, by: 4) {
            await withTaskGroup(of: (UUID, Int?).self) { group in
                for (id, address) in targets[start..<min(start + 4, targets.count)] {
                    group.addTask { (id, await TCPProbe.measure(address)) }
                }
                for await (id, milliseconds) in group {
                    if preferences.profiles.contains(where: { $0.id == id }) {
                        pings[id] = milliseconds.map { "\($0) ms" } ?? "—"
                    }
                }
            }
        }
    }
    func checkUpdates() async {
        if let updateURL { UIApplication.shared.open(updateURL); return }
        guard !checkingUpdate else { return }
        checkingUpdate = true
        defer { checkingUpdate = false }
        do {
            let url = URL(string: "https://api.github.com/repos/dimmmmmmmer/freetunnel/releases/latest")!
            let data = try await Downloads.read(URLRequest(url: url), limit: 1_048_576)
            struct Release: Decodable { let tag_name: String; let html_url: URL }
            let release = try JSONDecoder().decode(Release.self, from: data)
            let current = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "1.2.4"
            let version = release.tag_name.trimmingCharacters(in: CharacterSet(charactersIn: "v"))
            if version.compare(current, options: .numeric) == .orderedDescending {
                updateStatus = release.tag_name
                updateURL = release.html_url
            } else { updateStatus = "Up to date" }
        } catch { updateStatus = "Check for updates"; report(error) }
    }
}
private final class TCPProbe: @unchecked Sendable {
    private let lock = NSLock()
    private var continuation: CheckedContinuation<Int?, Never>?
    private let connection: NWConnection
    private let started = DispatchTime.now().uptimeNanoseconds
    private init(host: String, port: UInt16, continuation: CheckedContinuation<Int?, Never>) {
        self.continuation = continuation
        connection = NWConnection(host: NWEndpoint.Host(host), port: NWEndpoint.Port(rawValue: port)!, using: .tcp)
    }
    static func measure(_ address: String) async -> Int? {
        guard let parts = URLComponents(string: "https://" + address), let host = parts.host,
              let port = UInt16(exactly: parts.port ?? 443), port > 0 else { return nil }
        return await withCheckedContinuation { continuation in
            let probe = TCPProbe(host: host.trimmingCharacters(in: CharacterSet(charactersIn: "[]")),
                port: port, continuation: continuation)
            probe.connection.stateUpdateHandler = { state in
                switch state {
                case .ready: probe.finish(Int((DispatchTime.now().uptimeNanoseconds - probe.started) / 1_000_000))
                case .failed, .cancelled: probe.finish(nil)
                default: break
                }
            }
            let queue = DispatchQueue(label: "org.freetunnel.ping")
            queue.asyncAfter(deadline: .now() + 4) { probe.finish(nil) }
            probe.connection.start(queue: queue)
        }
    }
    private func finish(_ value: Int?) {
        lock.lock(); let reply = continuation; continuation = nil; lock.unlock()
        guard let reply else { return }
        connection.stateUpdateHandler = nil; connection.cancel(); reply.resume(returning: value)
    }
}
