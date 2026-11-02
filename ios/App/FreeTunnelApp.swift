import SwiftUI
import FreeTunnelCore

@main struct FreeTunnelApp: App {
    @StateObject private var state = AppState()
    @Environment(\.scenePhase) private var phase
    var body: some Scene {
        WindowGroup {
            RootView(state: state)
                .preferredColorScheme(
                    state.preferences.theme == "dark" ? .dark : state.preferences.theme == "light" ? .light : nil)
                .environment(\.locale, state.preferences.language == "system" ? .current : Locale(
                    identifier: state.preferences.language))
                .onOpenURL { url in
                    if url.scheme == "tt" { state.importText = url.absoluteString } else if url.isFileURL {
                        state.importFiles([url]) }
                }
                .task { await state.refreshDue() }
                .onChange(of: phase) { _, newValue in
                    if newValue == .active { Task { await state.refreshDue() } }
                }
        }
        .backgroundTask(.appRefresh("org.freetunnel.ios.refresh")) { await state.refreshDue() }
    }
}

struct RootView: View {
    @State private var tab = ProcessInfo.processInfo.environment["FT_PREVIEW_TAB"] ?? "connection"
    @ObservedObject var state: AppState
    var body: some View {
        VStack(spacing: 0) {
            navigation
            Group {
                switch tab {
                case "servers": ServersView(state: state)
                case "routing": RoutingView(state: state)
                case "settings": SettingsView(state: state)
                case "logs": LogsView(vpn: state.vpn)
                default: HomeView(state: state, vpn: state.vpn, showServers: { tab = "servers" })
                }
            }.frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .tint(AppStyle.accent)
        .foregroundStyle(AppStyle.text)
        .font(AppStyle.body)
        .background(AppStyle.background)
        .buttonStyle(AppButtonStyle())
        .modifier(PreviewForms(state: state))
        .alert("FreeTunnel", isPresented: Binding(get: { state.message != nil && state.openDialogs.isEmpty },
            set: { if !$0 {
            state.message = nil } })) {
            Button("OK") { state.message = nil }
        } message: { Text(LocalizedStringKey(state.message ?? "")) }
        .sheet(isPresented: Binding(get: { !state.importText.isEmpty }, set: { if !$0 { state.importText = "" } })) {
            ImportView(state: state, initial: state.importText).modifier(FormAlerts(state: state))
        }
    }
    private var navigation: some View {
        HStack(spacing: 10) {
            tabButton("connection", title: "Home", icon: "")
            tabButton("servers", title: "Servers", icon: "NavConnection")
            tabButton("routing", title: "Routing", icon: "NavNetwork")
            tabButton("settings", title: "Settings", icon: "NavSettings")
            tabButton("logs", title: "Logs", icon: "NavLog")
        }.frame(maxWidth: .infinity).padding(.vertical, 12).background(AppStyle.background)
    }
    private func tabButton(_ id: String, title: LocalizedStringKey, icon: String) -> some View {
        Button { tab = id } label: {
            Group {
                Image(icon.isEmpty ? "TunnelLogo" : icon).resizable().scaledToFit().frame(width: 25, height: 25)
            }.frame(width: 48, height: 48)
                .background(tab == id ? AppStyle.surface : .clear, in: RoundedRectangle(cornerRadius: 10))
        }.buttonStyle(.plain).accessibilityLabel(title)
            .accessibilityAddTraits(tab == id ? [.isSelected] : [])
    }

}

struct HomeView: View {
    @ObservedObject var state: AppState
    @ObservedObject var vpn: VPNController
    var status: LocalizedStringKey {
        switch vpn.status {
        case .connected: "Connected"
        case .connecting: "Connecting"
        case .reasserting: "Reconnecting"
        case .disconnecting: "Disconnecting"
        default: "Disconnected"
        }
    }
    var showServers: () -> Void
    var body: some View {
        VStack(spacing: 22) {
            Spacer()
            Button {
                if state.selected == nil { showServers(); return }
                Task {
                    if vpn.isDisconnected {
                        await state.connect()
                    } else { do { try await vpn.disconnect() } catch { state.report(error) } }
                }
            } label: {
                Image("TunnelLogo").resizable().scaledToFit().frame(width: 132, height: 132)
                    .opacity(vpn.isDisconnected ? 0.5 : 1)
            }.buttonStyle(.plain)
                .disabled(state.isPreview || !state.loaded || vpn.status == .disconnecting)
                .accessibilityLabel(vpn.isDisconnected ? Text("Connect") : Text("Disconnect"))
                .accessibilityIdentifier("connect-button")
            if vpn.status == .connected, let date = vpn.connectedAt {
                Text(date, style: .timer).font(AppStyle.label).foregroundStyle(AppStyle.secondary)
            } else if !vpn.isDisconnected {
                Text(status).font(AppStyle.label).foregroundStyle(AppStyle.secondary)
                    .accessibilityIdentifier("connection-status")
            }
            if state.preferences.profiles.isEmpty {
                Button("Add server", action: showServers).buttonStyle(AppButtonStyle())
            } else {
                Picker("Server", selection: $state.preferences.selectedID) {
                    ForEach(state.preferences.profiles) { profile in Text(profile.name).tag(Optional(profile.id)) }
                }.pickerStyle(.menu).disabled(!vpn.isDisconnected)
            }
            Spacer()
        }.padding(20).frame(maxWidth: .infinity, maxHeight: .infinity).background(AppStyle.background)
    }
}

struct SettingsView: View {
    @ObservedObject var state: AppState
    var body: some View {
        AppForm {
            AppSection("General") {
                AppPicker("Language", selection: $state.preferences.language) {
                    Text("System").tag("system"); Text("English").tag("en"); Text("Русский").tag("ru")
                }
                Divider()
                AppPicker("Theme", selection: $state.preferences.theme) {
                    Text("System").tag("system"); Text("Light").tag("light"); Text("Dark").tag("dark")
                }
            }
            AppSection("Security") {
                Toggle("Kill switch", isOn: $state.preferences.killSwitch)
            }
            HStack(spacing: 8) {
                Link("FreeTunnel 1.2.4", destination: URL(string: "https://github.com/dimmmmmmmer/freetunnel")!)
                Text("·")
                Link("TrustTunnel 1.1.11", destination: URL(
                    string: "https://github.com/TrustTunnel/TrustTunnelClient")!)
            }.font(AppStyle.caption).foregroundStyle(AppStyle.secondary)
                .buttonStyle(.plain).frame(maxWidth: .infinity)
        }
    }
}

struct LogsView: View {
    @ObservedObject var vpn: VPNController
    @State private var copied = false
    var body: some View {
        VStack(spacing: 8) {
            HStack {
                Button("Clear") { Task { await vpn.readLogs(clear: true) } }
                Spacer()
                Button(copied ? "Copied" : "Copy") {
                    UIPasteboard.general.string = vpn.logs
                    copied = true
                }
            }.buttonStyle(.plain).font(AppStyle.label).foregroundStyle(AppStyle.accent).frame(minHeight: 44)
            ScrollView {
                Text(vpn.logs.isEmpty ? String(localized: "No logs yet") : vpn.logs)
                    .font(.system(.caption, design: .monospaced)).textSelection(.enabled)
                    .frame(maxWidth: .infinity, alignment: .leading)
            }.frame(maxHeight: .infinity)
        }.padding(.horizontal, 18).background(AppStyle.background)
            .task {
                while !Task.isCancelled {
                    await vpn.readLogs()
                    do { try await Task.sleep(for: .seconds(2)) } catch { return }
                }
            }
            .task(id: copied) {
                if copied {
                    try? await Task.sleep(for: .seconds(2))
                    copied = false
                }
            }
    }
}
