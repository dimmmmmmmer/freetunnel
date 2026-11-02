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
        TabView(selection: $tab) {
            HomeView(state: state, vpn: state.vpn, showServers: { tab = "servers" }).tabItem { Label("Home",
                systemImage: "power.circle") }.tag("connection")
            ServersView(state: state).tabItem { Label("Servers", systemImage: "server.rack") }.tag("servers")
            RoutingView(state: state).tabItem { Label("Routing",
                systemImage: "arrow.triangle.branch") }.tag("routing")
            SettingsView(state: state).tabItem { Label("Settings", systemImage: "gearshape") }.tag("settings")
            LogsView(vpn: state.vpn).tabItem { Label("Logs", systemImage: "text.alignleft") }.tag("logs")
        }
        .toolbar(.hidden, for: .tabBar)
        .safeAreaInset(edge: .top, spacing: 0) { navigation }
        .tint(AppStyle.accent)
        .foregroundStyle(AppStyle.text)
        .background(AppStyle.background)
        .buttonStyle(AppButtonStyle())
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
            tabButton("servers", title: "Servers", icon: "server.rack")
            tabButton("routing", title: "Routing", icon: "arrow.triangle.branch")
            tabButton("settings", title: "Settings", icon: "gearshape")
            tabButton("logs", title: "Logs", icon: "text.alignleft")
        }.frame(maxWidth: .infinity).padding(.vertical, 12).background(AppStyle.background)
    }
    private func tabButton(_ id: String, title: LocalizedStringKey, icon: String) -> some View {
        Button { tab = id } label: {
            Group {
                if icon.isEmpty { Image("TunnelLogo").resizable().scaledToFit().frame(width: 25, height: 25) }
                else { Image(systemName: icon).font(.system(size: 23, weight: .medium)) }
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
                    if vpn.isDisconnected { await state.connect() }
                    else { do { try await vpn.disconnect() } catch { state.report(error) } }
                }
            } label: {
                Image("TunnelLogo").resizable().scaledToFit().frame(width: 132, height: 132)
                    .opacity(vpn.isDisconnected ? 0.5 : 1)
            }.buttonStyle(.plain)
                .disabled(state.isPreview || !state.loaded || vpn.status == .disconnecting)
                .accessibilityLabel(vpn.isDisconnected ? Text("Connect") : Text("Disconnect"))
                .accessibilityIdentifier("connect-button")
            Text(status).font(.subheadline).foregroundStyle(AppStyle.secondary)
                .accessibilityIdentifier("connection-status")
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
        NavigationStack {
            AppForm {
                AppSection("VPN") {
                    Toggle("Kill switch", isOn: $state.preferences.killSwitch)
                    Toggle("Connect on demand", isOn: $state.preferences.onDemand)
                    Text("VPN settings apply on the next connection.").font(.footnote).foregroundStyle(.secondary)
                }
                AppSection("Appearance") {
                    AppPicker("Theme", selection: $state.preferences.theme) {
                        Text("System").tag("system"); Text("Light").tag("light"); Text("Dark").tag("dark")
                    }
                    AppPicker("Language", selection: $state.preferences.language) {
                        Text("System").tag("system"); Text("English").tag("en"); Text("Русский").tag("ru")
                    }
                }
                AppSection("About") {
                    Text("FreeTunnel for iOS")
                    Text("TrustTunnel 1.1.11").foregroundStyle(.secondary)
                    NavigationLink("Open-source licenses") { LicensesView() }
                    Link("Source code", destination: URL(string: "https://github.com/dimmmmmmmer/freetunnel")!)
                    Link("Help", destination: URL(
                        string: "https://github.com/dimmmmmmmer/freetunnel/blob/main/USER_GUIDE.md")!)
                    Text("Updates are installed through TestFlight or the App Store.").font(.footnote)
                }
            }.navigationTitle("Settings")
        }
    }
}

struct LogsView: View {
    @ObservedObject var vpn: VPNController
    var body: some View {
        NavigationStack {
            ScrollView { Text(vpn.logs.isEmpty ? String(localized: "No logs yet") : vpn.logs)
                    .font(.system(.caption, design: .monospaced)).textSelection(.enabled)
                    .frame(maxWidth: .infinity, alignment: .leading).padding() }
                .background(AppStyle.background)
                .navigationBarTitleDisplayMode(.inline).navigationTitle("Logs")
                .toolbar {
                    ToolbarItemGroup(placement: .topBarTrailing) {
                        Button("Refresh") { Task { await vpn.readLogs() } }
                        Button("Clear", role: .destructive) { Task { await vpn.readLogs(clear: true) } }
                        ShareLink(item: vpn.logs) { Image(systemName: "square.and.arrow.up") }
                    }
                }.task { await vpn.readLogs() }
        }
    }
}
