import SwiftUI
import FreeTunnelCore

@main struct FreeTunnelApp: App {
    @StateObject private var state = AppState()
    @Environment(\.scenePhase) private var phase
    var body: some Scene {
        WindowGroup {
            RootView(state: state)
                .preferredColorScheme(state.preferences.theme == "dark" ? .dark : state.preferences.theme == "light" ? .light : nil)
                .environment(\.locale, state.preferences.language == "system" ? .current : Locale(identifier: state.preferences.language))
                .onOpenURL { url in
                    if url.scheme == "tt" { state.importText = url.absoluteString }
                    else if url.isFileURL { state.importFiles([url]) }
                }
                .task { await state.refreshDue() }
                .onChange(of: phase) { _, newValue in
                    if newValue == .active { Task { await state.refreshDue() } }
                }
        }
    }
}

struct RootView: View {
    @ObservedObject var state: AppState
    var body: some View {
        TabView {
            HomeView(state: state, vpn: state.vpn).tabItem { Label("Connection", systemImage: "power.circle") }
            ServersView(state: state).tabItem { Label("Servers", systemImage: "server.rack") }
            RoutingView(state: state).tabItem { Label("Split tunnelling", systemImage: "arrow.triangle.branch") }
            SettingsView(state: state).tabItem { Label("Settings", systemImage: "gearshape") }
            LogsView(vpn: state.vpn).tabItem { Label("Logs", systemImage: "text.alignleft") }
        }
        .tint(Color(red: 0.61, green: 0.55, blue: 0.77))
        .alert("FreeTunnel", isPresented: Binding(get: { state.message != nil }, set: { if !$0 { state.message = nil } })) {
            Button("OK") { state.message = nil }
        } message: { Text(state.message ?? "") }
        .sheet(isPresented: Binding(get: { !state.importText.isEmpty }, set: { if !$0 { state.importText = "" } })) {
            ImportView(state: state, initial: state.importText)
        }
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
    var body: some View {
        NavigationStack {
            VStack(spacing: 30) {
                Spacer()
                Image("TunnelLogo").resizable().scaledToFit().frame(width: 110, height: 110).accessibilityHidden(true)
                Text("FreeTunnel").font(.largeTitle.bold())
                Text(status).font(.title2).accessibilityIdentifier("connection-status")
                if state.preferences.profiles.isEmpty {
                    Text("Add a server to get connected").foregroundStyle(.secondary)
                } else {
                    Picker("Server", selection: $state.preferences.selectedID) {
                        ForEach(state.preferences.profiles) { profile in Text(profile.name).tag(Optional(profile.id)) }
                    }.pickerStyle(.menu).disabled(!vpn.isDisconnected)
                }
                Button {
                    Task {
                        if vpn.isDisconnected { await state.connect() }
                        else { do { try await vpn.disconnect() } catch { state.report(error) } }
                    }
                } label: {
                    Label(vpn.isDisconnected ? "Connect" : "Disconnect", systemImage: "power")
                        .font(.title3.bold()).frame(maxWidth: 280).padding(.vertical, 14)
                }.buttonStyle(.borderedProminent)
                    .disabled(!state.loaded || state.selected == nil || vpn.status == .disconnecting)
                    .accessibilityIdentifier("connect-button")
                Spacer()
            }.padding().navigationTitle("Connection").navigationBarTitleDisplayMode(.inline)
        }
    }
}

struct SettingsView: View {
    @ObservedObject var state: AppState
    var body: some View {
        NavigationStack {
            Form {
                Section("VPN") {
                    Toggle("Kill switch", isOn: $state.preferences.killSwitch)
                    Toggle("Connect on demand", isOn: $state.preferences.onDemand)
                    Text("VPN settings apply on the next connection.").font(.footnote).foregroundStyle(.secondary)
                }
                Section("Appearance") {
                    Picker("Theme", selection: $state.preferences.theme) {
                        Text("System").tag("system"); Text("Light").tag("light"); Text("Dark").tag("dark")
                    }
                    Picker("Language", selection: $state.preferences.language) {
                        Text("System").tag("system"); Text("English").tag("en"); Text("Русский").tag("ru")
                    }
                }
                Section("About") {
                    Text("FreeTunnel for iOS")
                    Text("TrustTunnel 1.1.11").foregroundStyle(.secondary)
                    Link("Source code", destination: URL(string: "https://github.com/dimmmmmmmer/freetunnel")!)
                    Link("Help", destination: URL(string: "https://github.com/dimmmmmmmer/freetunnel/blob/main/USER_GUIDE.md")!)
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
                .navigationTitle("Logs")
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
