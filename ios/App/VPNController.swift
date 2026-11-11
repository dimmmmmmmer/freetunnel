import Combine
import Foundation
import FreeTunnelCore
import NetworkExtension

@MainActor final class VPNController: ObservableObject {
    @Published private(set) var status: NEVPNStatus = .disconnected
    @Published private(set) var logs = ""
    @Published private(set) var downSpeed = 0.0
    @Published private(set) var upSpeed = 0.0
    private struct TrafficSample {
        let uploaded: UInt64
        let downloaded: UInt64
        let time: TimeInterval
    }
    private var lastStats: TrafficSample?
    private var manager: NETunnelProviderManager?
    private var observer: NSObjectProtocol?
    private var changing = false
    var connectedAt: Date? {
        #if DEBUG
            if let previewConnectedAt { return previewConnectedAt }
        #endif
        return manager?.connection.connectedDate
    }
    var isDisconnected: Bool { status == .disconnected || status == .invalid }
    init() {
        #if DEBUG
            if ProcessInfo.processInfo.arguments.contains("--ui-preview") { return }
        #endif
        Task { try? await load() }
    }
    #if DEBUG
        private var previewConnectedAt: Date?
        func preparePreview(_ scenario: String) {
            guard ProcessInfo.processInfo.arguments.contains("--ui-preview") else { return }
            if scenario == "connected" {
                status = .connected
                previewConnectedAt = Date().addingTimeInterval(-125)
                downSpeed = 2.4
                upSpeed = 0.3
            } else if scenario == "logs-filled" {
                logs =
                    "12:00:00 INFO Configuration loaded\n"
                    + "12:00:01 CORE Connecting to vpn.example.org\n12:00:02 INFO Connected"
            }
        }
    #endif
    func prepare() async { try? await load() }
    private func load() async throws {
        guard !UITestSupport.enabled else { return }
        let managers = try await NETunnelProviderManager.loadAllFromPreferences()
        manager = managers.first {
            ($0.protocolConfiguration as? NETunnelProviderProtocol)?.providerBundleIdentifier
                == Identity.tunnel
        }
        if let observer { NotificationCenter.default.removeObserver(observer) }
        guard let manager else {
            status = .disconnected
            return
        }
        status = manager.connection.status
        observer = NotificationCenter.default.addObserver(
            forName: .NEVPNStatusDidChange,
            object: manager.connection, queue: .main
        ) { [weak self] _ in
            Task { @MainActor in self?.status = self?.manager?.connection.status ?? .disconnected }
        }
    }
    func connect(text: String, name: String, killSwitch: Bool, logging: Bool, verbose: Bool)
        async throws {
        guard !changing else { throw ClientError.tunnelFailed }
        changing = true
        defer { changing = false }
        try await load()
        guard isDisconnected else { throw ClientError.tunnelFailed }
        let manager = manager ?? NETunnelProviderManager()
        let config = NETunnelProviderProtocol()
        config.providerBundleIdentifier = Identity.tunnel
        config.serverAddress = name
        // The system configuration contains a reference, never the VPN credentials.
        let key = "active-tunnel"
        try Vault.write(Data(text.utf8), key: key)
        config.providerConfiguration = [
            "secretKey": key, "loggingEnabled": logging, "verboseLogs": verbose
        ]
        config.includeAllNetworks = killSwitch
        config.excludeLocalNetworks = false
        config.disconnectOnSleep = false
        manager.protocolConfiguration = config
        manager.localizedDescription = "FreeTunnel"
        manager.isEnabled = true
        manager.onDemandRules = []
        manager.isOnDemandEnabled = false
        try await manager.saveToPreferences()
        try await load()
        guard let connection = self.manager?.connection else { throw ClientError.tunnelFailed }
        try connection.startVPNTunnel()
        status = connection.status
    }
    func disconnect() async throws {
        guard !changing else { return }
        changing = true
        defer { changing = false }
        guard let manager else { return }
        manager.isOnDemandEnabled = false
        try await manager.saveToPreferences()
        manager.connection.stopVPNTunnel()
    }
    func disconnectAndWait() async throws {
        try await disconnect()
        for _ in 0..<100 {
            if isDisconnected { return }
            try await Task.sleep(for: .milliseconds(100))
        }
        throw ClientError.tunnelFailed
    }
    func send(_ command: Data) async -> Data? {
        guard let session = manager?.connection as? NETunnelProviderSession, !isDisconnected else {
            return nil
        }
        return await withCheckedContinuation { continuation in
            let reply = ProviderReply(continuation)
            DispatchQueue.global().asyncAfter(deadline: .now() + 5) { reply.finish(nil) }
            do { try session.sendProviderMessage(command) { reply.finish($0) } } catch {
                reply.finish(nil)
            }
        }
    }
    func readLogs(clear: Bool = false) async {
        if clear { logs = "" }
        if let data = await send(Data((clear ? "clear" : "logs").utf8)),
            let text = String(data: data, encoding: .utf8) {
            logs = text
        } else if let url = try? Identity.directory().appendingPathComponent("freetunnel.log") {
            if clear { try? Data().write(to: url, options: .atomic) }
            logs = LogLines.shownText((try? String(contentsOf: url, encoding: .utf8)) ?? "")
        }
    }
    func exportLogs() async throws -> String {
        #if DEBUG
            if ProcessInfo.processInfo.arguments.contains("--ui-preview") { return logs }
        #endif
        if let data = await send(Data("logs-raw".utf8)),
            let text = String(data: data, encoding: .utf8) {
            return text
        }
        let url = try Identity.directory().appendingPathComponent("freetunnel.log")
        guard FileManager.default.fileExists(atPath: url.path) else { return "" }
        return try String(contentsOf: url, encoding: .utf8)
    }
    func configureLogging(enabled: Bool, verbose: Bool) async {
        if let data = try? JSONSerialization.data(withJSONObject: [
            "loggingEnabled": enabled, "verboseLogs": verbose
        ]) {
            _ = await send(data)
        }
    }
    func sampleTraffic() async {
        guard !isDisconnected, let data = await send(Data("stats".utf8)),
            let stats = try? JSONDecoder().decode(TrafficCounters.self, from: data)
        else {
            lastStats = nil
            upSpeed = 0
            downSpeed = 0
            return
        }
        let now = ProcessInfo.processInfo.systemUptime
        if let lastStats, now > lastStats.time, stats.uploaded >= lastStats.uploaded,
            stats.downloaded >= lastStats.downloaded {
            upSpeed =
                Double(stats.uploaded - lastStats.uploaded) / (now - lastStats.time) / 1_000_000
            downSpeed =
                Double(stats.downloaded - lastStats.downloaded) / (now - lastStats.time) / 1_000_000
        }
        lastStats = TrafficSample(uploaded: stats.uploaded, downloaded: stats.downloaded, time: now)
    }
    deinit { if let observer { NotificationCenter.default.removeObserver(observer) } }
}

// A disconnected extension may never answer; resolve its request only once.
private final class ProviderReply: @unchecked Sendable {
    private let lock = NSLock()
    private var continuation: CheckedContinuation<Data?, Never>?
    init(_ continuation: CheckedContinuation<Data?, Never>) { self.continuation = continuation }
    func finish(_ data: Data?) {
        lock.lock()
        let pending = continuation
        continuation = nil
        lock.unlock()
        pending?.resume(returning: data)
    }
}

private struct TrafficCounters: Decodable {
    let uploaded: UInt64
    let downloaded: UInt64
    enum CodingKeys: String, CodingKey {
        case uploaded = "up"
        case downloaded = "down"
    }
}
