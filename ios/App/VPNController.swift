import Foundation
import NetworkExtension
import FreeTunnelCore

@MainActor final class VPNController: ObservableObject {
    @Published private(set) var status: NEVPNStatus = .disconnected
    @Published private(set) var logs = ""
    private var manager: NETunnelProviderManager?
    private var observer: NSObjectProtocol?
    private var changing = false
    var isDisconnected: Bool { status == .disconnected || status == .invalid }
    init() { Task { try? await load() } }
    private func load() async throws {
        let managers = try await NETunnelProviderManager.loadAllFromPreferences()
        manager = managers.first { ($0.protocolConfiguration as? NETunnelProviderProtocol)?.providerBundleIdentifier == Identity.tunnel }
        if let observer { NotificationCenter.default.removeObserver(observer) }
        guard let manager else { status = .disconnected; return }
        status = manager.connection.status
        observer = NotificationCenter.default.addObserver(forName: .NEVPNStatusDidChange, object: manager.connection, queue: .main) { [weak self] _ in
            Task { @MainActor in self?.status = self?.manager?.connection.status ?? .disconnected }
        }
    }
    func connect(text: String, name: String, killSwitch: Bool, onDemand: Bool) async throws {
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
        config.providerConfiguration = ["secretKey": key]
        config.includeAllNetworks = killSwitch
        config.excludeLocalNetworks = false
        config.disconnectOnSleep = false
        manager.protocolConfiguration = config
        manager.localizedDescription = "FreeTunnel"
        manager.isEnabled = true
        let rule = NEOnDemandRuleConnect()
        rule.interfaceTypeMatch = .any
        manager.onDemandRules = onDemand ? [rule] : []
        manager.isOnDemandEnabled = onDemand
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
    func readLogs(clear: Bool = false) async {
        guard let session = manager?.connection as? NETunnelProviderSession, !isDisconnected else { return }
        let data: Data? = await withCheckedContinuation { continuation in
            do {
                try session.sendProviderMessage(Data((clear ? "clear" : "logs").utf8)) { continuation.resume(returning: $0) }
            } catch { continuation.resume(returning: nil) }
        }
        if let data, let text = String(data: data, encoding: .utf8) { logs = text }
    }
    deinit { if let observer { NotificationCenter.default.removeObserver(observer) } }
}
