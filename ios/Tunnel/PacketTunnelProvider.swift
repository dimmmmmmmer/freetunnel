import Foundation
import NetworkExtension
import FreeTunnelCore
import VpnClientFramework

final class PacketTunnelProvider: NEPacketTunnelProvider {
    private let queue = DispatchQueue(label: "org.freetunnel.tunnel")
    private var client: VpnClient?
    private var startReply: ((Error?) -> Void)?
    private var timeout: DispatchWorkItem?
    private var lines: [String] = []
    private var stopping = false
    private var loggingEnabled = true
    private var flushPending = false
    private func flushLogs() {
        flushPending = false
        guard let url = try? Identity.directory().appendingPathComponent("freetunnel.log") else { return }
        try? Data(lines.joined(separator: "\n").utf8).write(to: url,
            options: [.atomic, .completeFileProtectionUntilFirstUserAuthentication])
    }
    private func log(_ message: String, level: String = "INFO") {
        guard loggingEnabled else { return }
        let formatter = DateFormatter(); formatter.dateFormat = "HH:mm:ss"
        let stamp = formatter.string(from: Date())
        for line in message.split(whereSeparator: { $0.isNewline }) {
            lines.append("\(stamp) \(level) \(line)")
        }
        if lines.count > 1000 { lines.removeFirst(lines.count - 1000) }
        if !flushPending {
            flushPending = true
            queue.asyncAfter(deadline: .now() + 0.5) { self.flushLogs() }
        }
    }
    override func startTunnel(options: [String: NSObject]? = nil, completionHandler: @escaping (Error?) -> Void) {
        queue.async {
            self.startReply = completionHandler
            self.stopping = false

            do {
                guard let settings = self.protocolConfiguration as? NETunnelProviderProtocol,
                      let key = settings.providerConfiguration?["secretKey"] as? String,
                      key == "active-tunnel",
                      let text = String(data: try Vault.read(key), encoding: .utf8) else {
                          throw ClientError.missingSecret }
                self.loggingEnabled = settings.providerConfiguration?["loggingEnabled"] as? Bool ?? true
                let verbose = settings.providerConfiguration?["verboseLogs"] as? Bool ?? false
                NativeLogger.setLogLevel(verbose ? .debug : .info)
                NativeLogger.setCallback { [weak self] level, message in
                    let tag = ["ERROR", "WARN", "INFO", "DEBUG", "TRACE"][min(4, max(0, level.rawValue))]
                    self?.queue.async { self?.log(message, level: tag) }
                }
                self.log("Connecting")
                let config = try Configuration(text, maximumBytes: 8_388_608)
                let exclusions = try config.excludedRoutes.map(IPRoutes.parse)
                let network = NEPacketTunnelNetworkSettings(tunnelRemoteAddress: "127.0.0.1")
                let ipv4 = NEIPv4Settings(addresses: ["10.0.0.2"], subnetMasks: ["255.255.255.0"])
                ipv4.includedRoutes = [.default()]
                ipv4.excludedRoutes = exclusions.filter { !$0.ipv6 }.map {
                    NEIPv4Route(destinationAddress: $0.address, subnetMask: $0.ipv4Mask)
                }
                let ipv6 = NEIPv6Settings(addresses: ["fd00::1"], networkPrefixLengths: [64])
                ipv6.includedRoutes = [.default()]
                ipv6.excludedRoutes = exclusions.filter { $0.ipv6 }.map {
                    NEIPv6Route(destinationAddress: $0.address, networkPrefixLength: NSNumber(value: $0.prefix))
                }
                network.ipv4Settings = ipv4
                network.ipv6Settings = ipv6
                let dns = config.dnsUpstreams
                // A configured upstream makes the core answer its virtual DNS address.
                let dnsSettings = NEDNSSettings(servers: dns.isEmpty ? ["46.243.231.30",
                    "46.243.231.31"] : ["198.18.53.53"])
                dnsSettings.matchDomains = [""]
                network.dnsSettings = dnsSettings
                network.mtu = 1280
                self.setTunnelNetworkSettings(network) { error in
                    self.queue.async {
                        guard !self.stopping, self.startReply != nil else { return }
                        if error != nil { self.fail(); return }
                        self.startClient(text)
                    }
                }
                let timeout = DispatchWorkItem { [weak self] in
                    guard let self, self.startReply != nil else { return }
                    self.log("Connection timed out")
                    self.fail()
                }
                self.timeout = timeout
                self.queue.asyncAfter(deadline: .now() + 45, execute: timeout)
            } catch { self.fail() }
        }
    }
    private func startClient(_ text: String) {
        client = VpnClient(config: text, connectionInfoHandler: { _ in
            // Connection records may contain browsing history; do not collect them by default.
        }, stateChangeHandler: { [weak self] value in
            self?.queue.async { self?.stateChanged(Int(value)) }
        })
        guard let client, client.start(packetFlow) else { fail(); return }
    }
    private func stateChanged(_ state: Int) {
        guard !stopping else { return }
        switch state {
        case 0:
            log("Disconnected")
            if startReply != nil { fail() } else { cancelTunnelWithError(ClientError.tunnelFailed) }
        case 2:
            log("Connected")
            reasserting = false
            finishStart(nil)
        case 3, 4, 5:
            log("Waiting for network or reconnecting")
            reasserting = true
        default: break
        }
    }
    private func finishStart(_ error: Error?) {
        timeout?.cancel(); timeout = nil
        let reply = startReply; startReply = nil
        reply?(error)
    }
    private func fail() {
        stopping = true
        log("Unable to establish the tunnel")
        client?.stop(); client = nil
        finishStart(ClientError.tunnelFailed)
    }
    override func stopTunnel(with reason: NEProviderStopReason, completionHandler: @escaping () -> Void) {
        queue.async {
            self.stopping = true
            self.finishStart(ClientError.tunnelFailed)
            self.client?.stop(); self.client = nil
            self.log("Stopped")
            self.flushLogs()
            NativeLogger.setCallback(nil)
            completionHandler()
        }
    }
    override func sleep(completionHandler: @escaping () -> Void) {
        queue.async { self.client?.notify_sleep(); completionHandler() }
    }
    override func wake() { queue.async { self.client?.notify_wake() } }
    override func handleAppMessage(_ messageData: Data, completionHandler: ((Data?) -> Void)? = nil) {
        queue.async {
            if let settings = try? JSONSerialization.jsonObject(with: messageData) as? [String: Bool],
               let enabled = settings["loggingEnabled"] {
                self.loggingEnabled = enabled
                NativeLogger.setLogLevel(settings["verboseLogs"] == true ? .debug : .info)
                completionHandler?(Data()); return
            }
            let command = String(data: messageData, encoding: .utf8)
            if command == "stats" {
                let counters: [String: UInt64] = ["up": self.client?.uploadedBytes() ?? 0,
                    "down": self.client?.downloadedBytes() ?? 0]
                completionHandler?(try? JSONEncoder().encode(counters)); return
            }
            if command == "clear" { self.lines.removeAll(); self.flushLogs() }
            guard command == "clear" || command == "logs" else { completionHandler?(nil); return }
            completionHandler?(Data(self.lines.joined(separator: "\n").utf8))
        }
    }
}
